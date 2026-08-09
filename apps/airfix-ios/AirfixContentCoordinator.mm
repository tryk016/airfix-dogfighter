#import "AirfixContentCoordinator.h"
#import "AirfixMissionWorldRoomSnapshot+Private.hpp"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "airfix/content/LegacyAircraftAudioClipSet.hpp"
#include "airfix/content/LegacyAircraftHealthGaugeTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudIdentityStatusTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelTextureSet.hpp"
#include "airfix/content/MissionLoadManifest.hpp"
#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/content/WorldRoomPublicationGate.hpp"
#include "airfix/package/AfPackInstaller.hpp"
#include "airfix/package/AfPackRecovery.hpp"
#include "airfix/texture/TextureModeState.hpp"
#include "airfix/texture/TexturePackInstallation.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {

constexpr std::uint64_t kMaximumImportedPackBytes = 512U * 1024U * 1024U;
constexpr std::size_t kCopyBufferBytes = 64U * 1024U;
constexpr NSUInteger kMaximumMissionLogicalPathBytes = 4096U;
char kContentWorkQueueSpecificKey;

struct RememberedMissionRequest final {
  std::string setupLogicalPath;
  std::string levelLogicalPath;
  std::optional<std::string> playerObjectLogicalPath;
  std::uint32_t requestedStartIndex{};
};

struct StoredInspectionOutcome final {
  airfix::afpack::ActiveContentStatus status{
      airfix::afpack::ActiveContentStatus::noContent};
  std::optional<airfix::content::ContentRevision> activeRevision;
};

typedef NS_ENUM(NSInteger, AirfixDocumentPickerPurpose) {
  AirfixDocumentPickerPurposeNone,
  AirfixDocumentPickerPurposeContentPackage,
  AirfixDocumentPickerPurposeTexturePackage,
};

[[nodiscard]] AirfixTexturePackageAvailability publicTextureAvailability(
    const airfix::texture::TexturePackageAvailability availability) noexcept {
  switch (availability) {
  case airfix::texture::TexturePackageAvailability::notConfigured:
    return AirfixTexturePackageAvailabilityNotConfigured;
  case airfix::texture::TexturePackageAvailability::validating:
    return AirfixTexturePackageAvailabilityValidating;
  case airfix::texture::TexturePackageAvailability::ready:
    return AirfixTexturePackageAvailabilityReady;
  case airfix::texture::TexturePackageAvailability::unavailable:
    return AirfixTexturePackageAvailabilityUnavailable;
  }
  return AirfixTexturePackageAvailabilityUnavailable;
}

[[nodiscard]] airfix::texture::TexturePackageAvailability
textureAvailabilityForInspection(
    const airfix::texture::InstalledTexturePackInspection &inspection) noexcept {
  switch (inspection.status) {
  case airfix::texture::InstalledTexturePackStatus::ready:
    return airfix::texture::TexturePackageAvailability::ready;
  case airfix::texture::InstalledTexturePackStatus::notConfigured:
    return airfix::texture::TexturePackageAvailability::notConfigured;
  case airfix::texture::InstalledTexturePackStatus::invalidConfiguration:
  case airfix::texture::InstalledTexturePackStatus::persistenceBlocked:
  case airfix::texture::InstalledTexturePackStatus::packageUnavailable:
  case airfix::texture::InstalledTexturePackStatus::allocationFailure:
  case airfix::texture::InstalledTexturePackStatus::internalFailure:
    return airfix::texture::TexturePackageAvailability::unavailable;
  }
  return airfix::texture::TexturePackageAvailability::unavailable;
}

[[nodiscard]] airfix::texture::TextureMode textureMode(
    const AirfixMissionTextureMode mode) noexcept {
  return mode == AirfixMissionTextureModeEnhanced
             ? airfix::texture::TextureMode::enhanced
             : airfix::texture::TextureMode::classic;
}

[[nodiscard]] StoredInspectionOutcome storeInspectedContent(
    std::optional<airfix::afpack::ActiveContentInspection> &inspectionSlot,
    std::optional<airfix::content::VerifiedContentSession> &sessionSlot,
    airfix::afpack::ActiveContentInspection &&inspected) {
  inspectionSlot.reset();
  sessionSlot.reset();

  StoredInspectionOutcome outcome{
      .status = inspected.status(),
      .activeRevision = std::nullopt,
  };
  if (outcome.status == airfix::afpack::ActiveContentStatus::ready) {
    auto lease = std::move(inspected).takeReadyLease();
    auto session =
        airfix::content::VerifiedContentSession::adopt(std::move(lease));
    outcome.activeRevision = session.revision();
    sessionSlot.emplace(std::move(session));
  } else if (outcome.status ==
             airfix::afpack::ActiveContentStatus::rollbackAvailable) {
    inspectionSlot.emplace(std::move(inspected));
  }
  return outcome;
}

void clearWorkerContent(
    std::optional<airfix::afpack::ActiveContentInspection> &inspectionSlot,
    std::optional<airfix::content::VerifiedContentSession>
        &sessionSlot) noexcept {
  sessionSlot.reset();
  inspectionSlot.reset();
}

[[nodiscard]] std::optional<std::string>
copyPrivateLogicalPath(NSString *const logicalPath) {
  if (logicalPath == nil) {
    return std::nullopt;
  }
  NSData *const encoded = [logicalPath dataUsingEncoding:NSUTF8StringEncoding
                                    allowLossyConversion:NO];
  if (encoded == nil || encoded.length == 0U ||
      encoded.length > kMaximumMissionLogicalPathBytes ||
      std::memchr(encoded.bytes, 0, encoded.length) != nullptr) {
    return std::nullopt;
  }
  const auto *const bytes = static_cast<const char *>(encoded.bytes);
  return std::string(bytes, static_cast<std::size_t>(encoded.length));
}

class NativeCopyCancelled final : public std::runtime_error {
public:
  NativeCopyCancelled() : std::runtime_error("native content copy cancelled") {}
};

void closeFile(const int descriptor) noexcept {
  if (descriptor >= 0) {
    (void)::close(descriptor);
  }
}

void removeFile(const std::filesystem::path &path) noexcept {
  if (!path.empty()) {
    std::error_code ignored;
    (void)std::filesystem::remove(path, ignored);
  }
}

void checkStop(const std::stop_token stopToken) {
  if (stopToken.stop_requested()) {
    throw NativeCopyCancelled();
  }
}

std::filesystem::path fileSystemPath(NSURL *url) {
  const char *representation = url.fileSystemRepresentation;
  if (representation == nullptr || representation[0] == '\0') {
    throw std::runtime_error("file URL has no filesystem representation");
  }
  return std::filesystem::path(representation);
}

void requirePrivateDirectory(const std::filesystem::path &path) {
  struct stat info{};
  if (::lstat(path.c_str(), &info) == 0) {
    if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
      throw std::runtime_error("private content location has an invalid type");
    }
    if (::chmod(path.c_str(), S_IRWXU) != 0) {
      throw std::runtime_error("private content permissions cannot be secured");
    }
    return;
  }
  if (errno != ENOENT || ::mkdir(path.c_str(), S_IRWXU) != 0) {
    throw std::runtime_error("private content location is unavailable");
  }
}

bool isOwnedTransactionName(const std::string_view name,
                            const std::string_view prefix,
                            const std::string_view suffix) {
  constexpr std::size_t uuidSize = 36U;
  if (!name.starts_with(prefix) || !name.ends_with(suffix) ||
      name.size() != prefix.size() + uuidSize + suffix.size()) {
    return false;
  }
  const auto uuid = name.substr(prefix.size(), uuidSize);
  for (std::size_t index = 0U; index < uuid.size(); ++index) {
    const bool isSeparator =
        index == 8U || index == 13U || index == 18U || index == 23U;
    if (isSeparator) {
      if (uuid[index] != '-') {
        return false;
      }
    } else if (!((uuid[index] >= '0' && uuid[index] <= '9') ||
                 (uuid[index] >= 'a' && uuid[index] <= 'f'))) {
      return false;
    }
  }
  return true;
}

void cleanupOwnedFiles(const std::filesystem::path &directory,
                       const std::string_view prefix,
                       const std::string_view suffix) {
  requirePrivateDirectory(directory);
  std::error_code error;
  std::filesystem::directory_iterator iterator(directory, error);
  const std::filesystem::directory_iterator end;
  if (error) {
    throw std::runtime_error("private import directory cannot be inspected");
  }
  while (iterator != end) {
    const auto candidate = iterator->path();
    if (isOwnedTransactionName(candidate.filename().string(), prefix, suffix)) {
      const auto status = std::filesystem::symlink_status(candidate, error);
      if (error) {
        throw std::runtime_error("private import entry cannot be inspected");
      }
      // Never follow or remove links. Crash recovery owns only exact,
      // regular transaction files inside these private directories.
      if (std::filesystem::is_regular_file(status)) {
        (void)std::filesystem::remove(candidate, error);
        if (error) {
          throw std::runtime_error("private import entry cannot be removed");
        }
      }
    }
    iterator.increment(error);
    if (error) {
      throw std::runtime_error(
          "private import directory changed during cleanup");
    }
  }
}

class ScopedSecurityAccess final {
public:
  explicit ScopedSecurityAccess(NSURL *url)
      : url_(url), accessed_([url startAccessingSecurityScopedResource]) {}

  ~ScopedSecurityAccess() {
    if (accessed_) {
      [url_ stopAccessingSecurityScopedResource];
    }
  }

  ScopedSecurityAccess(const ScopedSecurityAccess &) = delete;
  ScopedSecurityAccess &operator=(const ScopedSecurityAccess &) = delete;

private:
  __strong NSURL *url_;
  BOOL accessed_;
};

using CopyProgress = std::function<void(std::uint64_t, std::uint64_t)>;

void copyRegularFile(const std::filesystem::path &source,
                     const std::filesystem::path &destination,
                     const std::stop_token stopToken,
                     const CopyProgress &progress) {
  checkStop(stopToken);
  int sourceFile = ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (sourceFile < 0) {
    throw std::runtime_error("selected content cannot be opened");
  }

  int destinationFile = -1;
  try {
    struct stat sourceInfo{};
    if (::fstat(sourceFile, &sourceInfo) != 0 || !S_ISREG(sourceInfo.st_mode) ||
        sourceInfo.st_size < 0) {
      throw std::runtime_error("selected content is not a regular file");
    }
    const auto expectedBytes = static_cast<std::uint64_t>(sourceInfo.st_size);
    if (expectedBytes == 0U || expectedBytes > kMaximumImportedPackBytes) {
      throw std::runtime_error("selected content size is outside limits");
    }

    destinationFile =
        ::open(destination.c_str(),
               O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
               S_IRUSR | S_IWUSR);
    if (destinationFile < 0) {
      throw std::runtime_error("private import file cannot be created");
    }

    std::array<std::uint8_t, kCopyBufferBytes> buffer{};
    std::uint64_t copiedBytes = 0U;
    if (progress) {
      progress(0U, expectedBytes);
    }
    for (;;) {
      checkStop(stopToken);
      const ssize_t received = ::read(sourceFile, buffer.data(), buffer.size());
      if (received < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error("selected content cannot be read");
      }
      if (received == 0) {
        break;
      }
      const auto receivedBytes = static_cast<std::uint64_t>(received);
      if (receivedBytes > kMaximumImportedPackBytes - copiedBytes) {
        throw std::runtime_error("selected content grew beyond limits");
      }

      std::size_t writtenBytes = 0U;
      while (writtenBytes < static_cast<std::size_t>(received)) {
        checkStop(stopToken);
        const ssize_t written =
            ::write(destinationFile, buffer.data() + writtenBytes,
                    static_cast<std::size_t>(received) - writtenBytes);
        if (written < 0) {
          if (errno == EINTR) {
            continue;
          }
          throw std::runtime_error("private import file cannot be written");
        }
        if (written == 0) {
          throw std::runtime_error("private import write made no progress");
        }
        writtenBytes += static_cast<std::size_t>(written);
      }
      copiedBytes += receivedBytes;
      if (progress) {
        progress(std::min(copiedBytes, expectedBytes), expectedBytes);
      }
    }
    if (copiedBytes != expectedBytes) {
      throw std::runtime_error("selected content changed during copy");
    }
    if (::fsync(destinationFile) != 0) {
      throw std::runtime_error("private import file cannot be synchronized");
    }
    closeFile(destinationFile);
    destinationFile = -1;
    closeFile(sourceFile);
    sourceFile = -1;
  } catch (...) {
    closeFile(destinationFile);
    closeFile(sourceFile);
    removeFile(destination);
    throw;
  }
}

NSString *canonicalTransactionIdentifier(void) {
  return NSUUID.UUID.UUIDString.lowercaseString;
}

} // namespace

@interface AirfixContentCoordinator () <UIDocumentPickerDelegate> {
  dispatch_queue_t _workQueue;
  std::stop_source _operationStop;
  // Access only from _workQueue. They own every authenticated package
  // handle; neither object is ever moved to the main/render threads.
  std::optional<airfix::afpack::ActiveContentInspection> _inspection;
  std::optional<airfix::content::VerifiedContentSession> _verifiedSession;
  std::unique_ptr<airfix::texture::TexturePackSession> _texturePackSession;

  // Access only from the main thread. stop_source cancellation itself is
  // thread-safe and its token is copied into the serialized worker.
  std::stop_source _loadStop;
  airfix::content::WorldRoomPublicationGate _roomPublicationGate;
  std::optional<RememberedMissionRequest> _rememberedMissionRequest;
  airfix::texture::TextureMode _requestedTextureMode;
  airfix::texture::TexturePackageAvailability _textureAvailability;
  std::optional<airfix::texture::ActiveMissionTextureState>
      _activeMissionTextureState;
  std::optional<airfix::texture::ActiveMissionTextureState>
      _loadingMissionTextureState;

  // Opaque identities are confined to the main thread. Blocks retain the
  // identities they started with, so pointer equality cannot wrap or alias a
  // later operation/lifecycle/request.
  __strong NSObject *_operationIdentity;
  __strong NSObject *_lifecycleIdentity;
  __strong NSObject *_missionRequestIdentity;
}
@property(nonatomic, weak) UIViewController *presentingViewController;
@property(nonatomic, strong, readwrite) UIView *controlsView;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UIProgressView *progressView;
@property(nonatomic, strong) UIButton *importButton;
@property(nonatomic, strong) UIButton *textureImportButton;
@property(nonatomic, strong) UIButton *rollbackButton;
@property(nonatomic, readwrite) AirfixContentReadiness readiness;
@property(nonatomic, readwrite)
    AirfixTexturePackageAvailability texturePackageAvailability;
@property(nonatomic) BOOL busy;
@property(nonatomic) BOOL rollbackEligible;
@property(nonatomic) BOOL started;
@property(nonatomic) BOOL pickerPresented;
@property(nonatomic) AirfixDocumentPickerPurpose pickerPurpose;
@property(nonatomic) BOOL inspectWhenIdle;

- (void)cancelMissionLoadClearingRevision:(BOOL)clearRevision;
- (void)invalidateContentOperationLifecycle;
- (void)startRememberedMissionLoadIfPossible;
- (void)publishTextureAvailability:
    (airfix::texture::TexturePackageAvailability)availability;
- (void)reloadMissionForTextureStateIfNeeded;
- (void)beginTextureInstallFromURL:(NSURL *)selectedURL;
- (void)beginTextureOperationWithText:(NSString *)text;
- (void)completeInspectionWithErrorText:(NSString *)text
                      operationIdentity:(NSObject *)operationIdentity
                      lifecycleIdentity:(NSObject *)lifecycleIdentity;
- (void)finishTextureOperationWithAvailability:
            (airfix::texture::TexturePackageAvailability)availability
                                         text:(NSString *)text;
@end

@implementation AirfixContentCoordinator

- (instancetype)initWithPresentingViewController:
    (UIViewController *)viewController {
  self = [super init];
  if (self == nil) {
    return nil;
  }
  _presentingViewController = viewController;
  _readiness = AirfixContentReadinessMissing;
  _texturePackageAvailability =
      AirfixTexturePackageAvailabilityNotConfigured;
  _requestedTextureMode = airfix::texture::TextureMode::classic;
  _textureAvailability =
      airfix::texture::TexturePackageAvailability::notConfigured;
  _lifecycleIdentity = [NSObject new];
  _missionRequestIdentity = [NSObject new];
  _workQueue = dispatch_queue_create("com.tryk016.airfixdogfighter.content",
                                     DISPATCH_QUEUE_SERIAL);
  dispatch_queue_set_specific(_workQueue, &kContentWorkQueueSpecificKey,
                              &kContentWorkQueueSpecificKey, nullptr);
  [self buildControls];
  return self;
}

- (void)dealloc {
  // C++ ivars are normally destroyed on whichever thread releases the last
  // Objective-C owner. Explicitly empty handle-owning state on its serialized
  // queue first; the later automatic C++ ivar destructors then see empties.
  auto *const inspection = &_inspection;
  auto *const session = &_verifiedSession;
  auto *const textureSession = &_texturePackSession;
  if (dispatch_get_specific(&kContentWorkQueueSpecificKey) ==
      &kContentWorkQueueSpecificKey) {
    clearWorkerContent(*inspection, *session);
    textureSession->reset();
  } else {
    dispatch_sync(_workQueue, ^{
      clearWorkerContent(*inspection, *session);
      textureSession->reset();
    });
  }
}

- (void)buildControls {
  UIView *container = [[UIView alloc] initWithFrame:CGRectZero];
  container.translatesAutoresizingMaskIntoConstraints = NO;

  UILabel *status = [[UILabel alloc] initWithFrame:CGRectZero];
  status.numberOfLines = 0;
  status.textAlignment = NSTextAlignmentCenter;
  status.textColor = UIColor.whiteColor;
  status.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
  status.adjustsFontForContentSizeCategory = YES;
  status.text = @"Checking private game content...";
  status.accessibilityTraits |= UIAccessibilityTraitUpdatesFrequently;
  self.statusLabel = status;

  UIProgressView *progress = [[UIProgressView alloc]
      initWithProgressViewStyle:UIProgressViewStyleDefault];
  progress.progress = 0.0F;
  progress.hidden = YES;
  progress.accessibilityLabel = @"Content operation progress";
  self.progressView = progress;

  UIButton *import = [UIButton buttonWithType:UIButtonTypeSystem];
  [import setTitle:@"Import AFPACK" forState:UIControlStateNormal];
  import.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  import.titleLabel.adjustsFontForContentSizeCategory = YES;
  import.titleLabel.numberOfLines = 0;
  import.titleLabel.lineBreakMode = NSLineBreakByWordWrapping;
  import.titleLabel.textAlignment = NSTextAlignmentCenter;
  import.accessibilityHint = @"Choose a private game content package";
  [import addTarget:self
                action:@selector(importPressed:)
      forControlEvents:UIControlEventTouchUpInside];
  self.importButton = import;

  UIButton *textureImport = [UIButton buttonWithType:UIButtonTypeSystem];
  [textureImport setTitle:@"Import HD Textures"
                 forState:UIControlStateNormal];
  textureImport.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  textureImport.titleLabel.adjustsFontForContentSizeCategory = YES;
  textureImport.titleLabel.numberOfLines = 0;
  textureImport.titleLabel.lineBreakMode = NSLineBreakByWordWrapping;
  textureImport.titleLabel.textAlignment = NSTextAlignmentCenter;
  textureImport.accessibilityHint =
      @"Choose a private reviewed HD texture package folder";
  [textureImport addTarget:self
                    action:@selector(textureImportPressed:)
          forControlEvents:UIControlEventTouchUpInside];
  self.textureImportButton = textureImport;

  UIButton *rollback = [UIButton buttonWithType:UIButtonTypeSystem];
  [rollback setTitle:@"Restore Previous Package" forState:UIControlStateNormal];
  rollback.titleLabel.font =
      [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  rollback.titleLabel.adjustsFontForContentSizeCategory = YES;
  rollback.titleLabel.numberOfLines = 0;
  rollback.titleLabel.lineBreakMode = NSLineBreakByWordWrapping;
  rollback.titleLabel.textAlignment = NSTextAlignmentCenter;
  rollback.accessibilityHint = @"Activate the last verified package";
  rollback.hidden = YES;
  [rollback addTarget:self
                action:@selector(rollbackPressed:)
      forControlEvents:UIControlEventTouchUpInside];
  self.rollbackButton = rollback;

  UIStackView *buttons =
      [[UIStackView alloc]
          initWithArrangedSubviews:@[ import, textureImport, rollback ]];
  buttons.axis = UILayoutConstraintAxisVertical;
  buttons.spacing = 10.0;
  buttons.alignment = UIStackViewAlignmentFill;
  buttons.distribution = UIStackViewDistributionFillProportionally;

  UIStackView *stack = [[UIStackView alloc]
      initWithArrangedSubviews:@[ status, progress, buttons ]];
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 14.0;
  stack.alignment = UIStackViewAlignmentFill;
  [container addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [stack.topAnchor constraintEqualToAnchor:container.topAnchor],
    [stack.bottomAnchor constraintEqualToAnchor:container.bottomAnchor],
    [stack.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
    [stack.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
    [progress.widthAnchor constraintGreaterThanOrEqualToConstant:220.0],
    [import.heightAnchor constraintGreaterThanOrEqualToConstant:44.0],
    [textureImport.heightAnchor constraintGreaterThanOrEqualToConstant:44.0],
    [rollback.heightAnchor constraintGreaterThanOrEqualToConstant:44.0],
  ]];
  self.controlsView = container;
}

- (void)start {
  NSAssert(NSThread.isMainThread,
           @"Content coordinator must be called on the main thread");
  if (self.started) {
    return;
  }
  self.started = YES;
  [self beginInspection];
}

- (void)cancelMissionLoadClearingRevision:(BOOL)clearRevision {
  NSAssert(NSThread.isMainThread,
           @"Mission room publication gate is main-thread confined");
  _loadStop.request_stop();
  _loadStop = std::stop_source{};
  _loadingMissionTextureState.reset();
  if (clearRevision) {
    _roomPublicationGate.clearActiveRevision();
    _activeMissionTextureState.reset();
  } else {
    _roomPublicationGate.invalidate();
  }
}

- (void)requestMissionTextureMode:(AirfixMissionTextureMode)requestedMode {
  NSAssert(NSThread.isMainThread,
           @"Mission texture policy is main-thread confined");
  const auto selected = textureMode(requestedMode);
  if (_requestedTextureMode == selected) {
    return;
  }
  _requestedTextureMode = selected;
  [self reloadMissionForTextureStateIfNeeded];
}

- (void)publishTextureAvailability:
    (const airfix::texture::TexturePackageAvailability)availability {
  NSAssert(NSThread.isMainThread,
           @"Texture package availability is main-thread confined");
  _textureAvailability = availability;
  self.texturePackageAvailability = publicTextureAvailability(availability);
  id<AirfixContentCoordinatorDelegate> delegate = self.delegate;
  if ([delegate respondsToSelector:
                    @selector(contentCoordinator:
                        didChangeTexturePackageAvailability:)]) {
    [delegate contentCoordinator:self
        didChangeTexturePackageAvailability:self.texturePackageAvailability];
  }
  [self reloadMissionForTextureStateIfNeeded];
}

- (void)reloadMissionForTextureStateIfNeeded {
  NSAssert(NSThread.isMainThread,
           @"Mission texture reload decisions are main-thread confined");
  const auto resolved = airfix::texture::resolveTextureModeState(
      _requestedTextureMode, _textureAvailability,
      _activeMissionTextureState);
  if (!resolved.complete()) {
    return;
  }
  const airfix::texture::ActiveMissionTextureState target{
      .requestedMode = resolved.state->requestedMode,
      .effectiveMode = resolved.state->effectiveMode,
  };
  const bool loadingDifferent =
      _loadingMissionTextureState.has_value() &&
      *_loadingMissionTextureState != target;
  if (!resolved.state->missionReloadRequired && !loadingDifferent) {
    return;
  }
  [self cancelMissionLoadClearingRevision:NO];
  [self startRememberedMissionLoadIfPossible];
}

- (void)invalidateContentOperationLifecycle {
  NSAssert(NSThread.isMainThread, @"Content lifecycle is main-thread confined");
  _operationStop.request_stop();
  _lifecycleIdentity = [NSObject new];
}

- (void)applicationWillResignActive {
  [self invalidateContentOperationLifecycle];
  [self cancelMissionLoadClearingRevision:YES];
}

- (void)applicationDidEnterBackground {
  [self invalidateContentOperationLifecycle];
  [self cancelMissionLoadClearingRevision:YES];
}

- (void)applicationWillEnterForeground {
  [self cancelMissionLoadClearingRevision:YES];
  // The active record may have changed at a commit boundary while the app was
  // leaving the foreground. Inspection is intentionally restarted only once
  // the application is active again.
}

- (void)applicationDidBecomeActive {
  [self cancelMissionLoadClearingRevision:YES];
  if (!self.started || self.pickerPresented) {
    return;
  }
  if (self.busy) {
    self.inspectWhenIdle = YES;
  } else {
    self.inspectWhenIdle = NO;
    [self beginInspection];
  }
}

- (void)importPressed:(UIButton *)sender {
  (void)sender;
  if (self.busy || self.presentingViewController == nil ||
      self.presentingViewController.presentedViewController != nil) {
    return;
  }
  self.pickerPresented = YES;
  self.pickerPurpose = AirfixDocumentPickerPurposeContentPackage;
  self.importButton.enabled = NO;
  self.textureImportButton.enabled = NO;
  self.rollbackButton.hidden = YES;
  self.rollbackButton.enabled = NO;
  UIDocumentPickerViewController *picker =
      [[UIDocumentPickerViewController alloc]
          initForOpeningContentTypes:@[ UTTypeData ]
                              asCopy:NO];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.modalPresentationStyle = UIModalPresentationFormSheet;
  [self.presentingViewController presentViewController:picker
                                              animated:YES
                                            completion:nil];
}

- (void)textureImportPressed:(UIButton *)sender {
  (void)sender;
  if (self.busy || self.presentingViewController == nil ||
      self.presentingViewController.presentedViewController != nil) {
    return;
  }
  self.pickerPresented = YES;
  self.pickerPurpose = AirfixDocumentPickerPurposeTexturePackage;
  self.importButton.enabled = NO;
  self.textureImportButton.enabled = NO;
  self.rollbackButton.hidden = YES;
  self.rollbackButton.enabled = NO;
  UIDocumentPickerViewController *picker =
      [[UIDocumentPickerViewController alloc]
          initForOpeningContentTypes:@[ UTTypeFolder ]
                              asCopy:YES];
  picker.delegate = self;
  picker.allowsMultipleSelection = NO;
  picker.modalPresentationStyle = UIModalPresentationFormSheet;
  [self.presentingViewController presentViewController:picker
                                              animated:YES
                                            completion:nil];
}

- (void)rollbackPressed:(UIButton *)sender {
  (void)sender;
  if (self.busy || !self.rollbackEligible) {
    return;
  }
  [self beginRollback];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  (void)controller;
  NSURL *selected = urls.firstObject;
  const AirfixDocumentPickerPurpose purpose = self.pickerPurpose;
  self.pickerPresented = NO;
  self.pickerPurpose = AirfixDocumentPickerPurposeNone;
  if (selected == nil || self.busy) {
    self.importButton.enabled = !self.busy;
    self.textureImportButton.enabled = !self.busy;
    self.rollbackButton.hidden = !self.rollbackEligible;
    self.rollbackButton.enabled = self.rollbackEligible && !self.busy;
    return;
  }
  if (purpose == AirfixDocumentPickerPurposeTexturePackage) {
    [self beginTextureInstallFromURL:selected];
  } else if (purpose == AirfixDocumentPickerPurposeContentPackage) {
    [self beginInstallFromURL:selected];
  } else {
    self.importButton.enabled = YES;
    self.textureImportButton.enabled = YES;
  }
}

- (void)documentPickerWasCancelled:
    (UIDocumentPickerViewController *)controller {
  (void)controller;
  self.pickerPresented = NO;
  self.pickerPurpose = AirfixDocumentPickerPurposeNone;
  self.importButton.enabled = !self.busy;
  self.textureImportButton.enabled = !self.busy;
  self.rollbackButton.hidden = !self.rollbackEligible;
  self.rollbackButton.enabled = self.rollbackEligible && !self.busy;
}

- (nullable NSURL *)prepareContentRoot {
  NSFileManager *manager = NSFileManager.defaultManager;
  NSError *supportError = nil;
  NSURL *support = [manager URLForDirectory:NSApplicationSupportDirectory
                                   inDomain:NSUserDomainMask
                          appropriateForURL:nil
                                     create:YES
                                      error:&supportError];
  if (support == nil || supportError != nil) {
    return nil;
  }
  NSURL *parent = [support URLByAppendingPathComponent:@"AirfixDogfighter"
                                           isDirectory:YES];
  NSDictionary *attributes = @{
    NSFilePosixPermissions : @(0700),
    NSFileProtectionKey : NSFileProtectionCompleteUntilFirstUserAuthentication,
  };
  NSError *createError = nil;
  if (![manager createDirectoryAtURL:parent
          withIntermediateDirectories:YES
                           attributes:attributes
                                error:&createError] ||
      createError != nil) {
    return nil;
  }
  try {
    const std::filesystem::path parentPath = fileSystemPath(parent);
    requirePrivateDirectory(parentPath);
    cleanupOwnedFiles(parentPath / "incoming", "import-", ".afpack");
    const auto contentPath = parentPath / "content";
    requirePrivateDirectory(contentPath);
    cleanupOwnedFiles(contentPath / "staging", "import-", ".afpack.partial");
    cleanupOwnedFiles(contentPath, "active-", ".afac.partial");
    requirePrivateDirectory(parentPath / "texture-packs");
  } catch (...) {
    return nil;
  }
  return [parent URLByAppendingPathComponent:@"content" isDirectory:YES];
}

- (void)beginOperationWithText:(NSString *)text {
  NSAssert(NSThread.isMainThread,
           @"Content operations are main-thread confined");
  self.busy = YES;
  _operationStop = std::stop_source{};
  _operationIdentity = [NSObject new];
  self.statusLabel.text = text;
  self.progressView.progress = 0.0F;
  self.progressView.hidden = YES;
  self.importButton.enabled = NO;
  self.textureImportButton.enabled = NO;
  self.rollbackButton.hidden = YES;
  self.rollbackButton.enabled = NO;
  [self setReadinessAndNotify:AirfixContentReadinessValidating];
}

- (void)beginTextureOperationWithText:(NSString *)text {
  NSAssert(NSThread.isMainThread,
           @"Texture package operations are main-thread confined");
  self.busy = YES;
  _operationStop = std::stop_source{};
  _operationIdentity = [NSObject new];
  self.statusLabel.text = text;
  self.progressView.progress = 0.0F;
  self.progressView.hidden = YES;
  self.importButton.enabled = NO;
  self.textureImportButton.enabled = NO;
  self.rollbackButton.hidden = YES;
  self.rollbackButton.enabled = NO;
  _textureAvailability =
      airfix::texture::TexturePackageAvailability::validating;
  self.texturePackageAvailability =
      AirfixTexturePackageAvailabilityValidating;
  id<AirfixContentCoordinatorDelegate> delegate = self.delegate;
  if ([delegate respondsToSelector:
                    @selector(contentCoordinator:
                        didChangeTexturePackageAvailability:)]) {
    [delegate contentCoordinator:self
        didChangeTexturePackageAvailability:
            AirfixTexturePackageAvailabilityValidating];
  }
}

- (BOOL)consumeTerminalOperationIdentity:(NSObject *)operationIdentity
                       lifecycleIdentity:(NSObject *)lifecycleIdentity {
  NSAssert(NSThread.isMainThread,
           @"Content completion is main-thread confined");
  if (_operationIdentity != operationIdentity) {
    return NO;
  }

  // Consume before publishing any result. Queued progress and duplicate
  // terminal callbacks for this operation are stale from this point onward.
  _operationIdentity = nil;
  if (_lifecycleIdentity == lifecycleIdentity) {
    return YES;
  }

  // The worker completed after a lifecycle cancellation. Its authenticated
  // state remains worker-confined and will be cleared by the next serialized
  // inspection, but it must not drive readiness/revision/room publication.
  self.busy = NO;
  self.inspectWhenIdle = YES;
  self.rollbackEligible = NO;
  self.progressView.hidden = YES;
  self.rollbackButton.hidden = YES;
  self.rollbackButton.enabled = NO;
  self.statusLabel.text =
      @"Content activity was paused. Active content will be checked again.";

  const BOOL canInspectNow = self.started && !self.pickerPresented &&
                             UIApplication.sharedApplication.applicationState ==
                                 UIApplicationStateActive;
  self.importButton.enabled = !canInspectNow;
  self.textureImportButton.enabled = !canInspectNow;
  if (canInspectNow) {
    NSObject *const currentLifecycleIdentity = _lifecycleIdentity;
    __weak AirfixContentCoordinator *weakSelf = self;
    dispatch_async(dispatch_get_main_queue(), ^{
      AirfixContentCoordinator *coordinator = weakSelf;
      if (coordinator == nil ||
          coordinator->_lifecycleIdentity != currentLifecycleIdentity ||
          coordinator->_operationIdentity != nil || coordinator.busy ||
          !coordinator.inspectWhenIdle || coordinator.pickerPresented ||
          UIApplication.sharedApplication.applicationState !=
              UIApplicationStateActive) {
        return;
      }
      coordinator.inspectWhenIdle = NO;
      [coordinator beginInspection];
    });
  }
  return NO;
}

- (void)completeOperationWithErrorText:(NSString *)text
                     operationIdentity:(NSObject *)operationIdentity
                     lifecycleIdentity:(NSObject *)lifecycleIdentity
                    inspectAfterFinish:(BOOL)inspectAfterFinish {
  if (![self consumeTerminalOperationIdentity:operationIdentity
                            lifecycleIdentity:lifecycleIdentity]) {
    return;
  }
  self.inspectWhenIdle = inspectAfterFinish;
  [self finishOperationWithErrorText:text];
}

- (void)completeInspectionWithErrorText:(NSString *)text
                      operationIdentity:(NSObject *)operationIdentity
                      lifecycleIdentity:(NSObject *)lifecycleIdentity {
  if (![self consumeTerminalOperationIdentity:operationIdentity
                            lifecycleIdentity:lifecycleIdentity]) {
    return;
  }
  [self publishTextureAvailability:
            airfix::texture::TexturePackageAvailability::unavailable];
  self.inspectWhenIdle = NO;
  [self finishOperationWithErrorText:text];
}

- (void)finishOperationWithInspectionStatus:
    (airfix::afpack::ActiveContentStatus)status {
  NSAssert(NSThread.isMainThread,
           @"Inspection presentation is main-thread confined");
  self.inspectWhenIdle = NO;
  self.busy = NO;
  self.progressView.hidden = YES;
  self.importButton.enabled = YES;
  self.textureImportButton.enabled = YES;

  self.rollbackEligible =
      status == airfix::afpack::ActiveContentStatus::rollbackAvailable;
  self.rollbackButton.hidden = !self.rollbackEligible;
  self.rollbackButton.enabled = self.rollbackEligible;
  switch (status) {
  case airfix::afpack::ActiveContentStatus::noContent:
    self.statusLabel.text = @"No private game content is installed.";
    [self setReadinessAndNotify:AirfixContentReadinessMissing];
    break;
  case airfix::afpack::ActiveContentStatus::ready:
    self.statusLabel.text = @"Private game content is ready.";
    [self setReadinessAndNotify:AirfixContentReadinessReady];
    [self startRememberedMissionLoadIfPossible];
    break;
  case airfix::afpack::ActiveContentStatus::rollbackAvailable:
    self.statusLabel.text = @"The active package is unusable. A verified "
                            @"previous package can be restored.";
    [self setReadinessAndNotify:AirfixContentReadinessRejected];
    break;
  case airfix::afpack::ActiveContentStatus::unusable:
  case airfix::afpack::ActiveContentStatus::malformedActive:
  case airfix::afpack::ActiveContentStatus::unavailable:
    self.statusLabel.text =
        @"Installed content is unavailable or invalid. Import a valid AFPACK.";
    [self setReadinessAndNotify:AirfixContentReadinessRejected];
    break;
  }
}

- (void)finishOperationWithErrorText:(NSString *)text {
  NSAssert(NSThread.isMainThread,
           @"Content UI completion is main-thread confined");
  self.busy = NO;
  self.rollbackEligible = NO;
  self.statusLabel.text = text;
  self.progressView.hidden = YES;
  self.importButton.enabled = YES;
  self.textureImportButton.enabled = YES;
  self.rollbackButton.hidden = YES;
  self.rollbackButton.enabled = NO;
  [self setReadinessAndNotify:AirfixContentReadinessRejected];
  if (self.inspectWhenIdle &&
      UIApplication.sharedApplication.applicationState ==
          UIApplicationStateActive) {
    self.inspectWhenIdle = NO;
    dispatch_async(dispatch_get_main_queue(), ^{
      [self beginInspection];
    });
  }
}

- (void)finishTextureOperationWithAvailability:
            (const airfix::texture::TexturePackageAvailability)availability
                                         text:(NSString *)text {
  NSAssert(NSThread.isMainThread,
           @"Texture package completion is main-thread confined");
  self.busy = NO;
  self.statusLabel.text = text;
  self.progressView.hidden = YES;
  self.importButton.enabled = YES;
  self.textureImportButton.enabled = YES;
  self.rollbackButton.hidden = !self.rollbackEligible;
  self.rollbackButton.enabled = self.rollbackEligible;
  if (_activeMissionTextureState.has_value()) {
    [self cancelMissionLoadClearingRevision:NO];
  }
  [self publishTextureAvailability:availability];
  [self startRememberedMissionLoadIfPossible];
}

- (void)setReadinessAndNotify:(AirfixContentReadiness)readiness {
  self.readiness = readiness;
  [self.delegate contentCoordinator:self didChangeReadiness:readiness];
}

- (void)publishCompleted:(std::uint64_t)completed
                   total:(std::uint64_t)total
                    text:(NSString *)text
       operationIdentity:(NSObject *)operationIdentity
       lifecycleIdentity:(NSObject *)lifecycleIdentity {
  const std::uint64_t boundedCompleted = std::min(completed, total);
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!self.busy || self->_operationIdentity != operationIdentity ||
        self->_lifecycleIdentity != lifecycleIdentity) {
      return;
    }
    self.statusLabel.text = text;
    self.progressView.hidden = total == 0U;
    self.progressView.progress =
        total == 0U
            ? 0.0F
            : static_cast<float>(boundedCompleted) / static_cast<float>(total);
  });
}

- (void)scheduleMissionRequestFailure:(NSObject *)requestIdentity {
  NSAssert(NSThread.isMainThread, @"Mission requests are main-thread confined");
  __weak AirfixContentCoordinator *weakSelf = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    AirfixContentCoordinator *coordinator = weakSelf;
    if (coordinator == nil ||
        coordinator->_missionRequestIdentity != requestIdentity) {
      return;
    }
    coordinator.statusLabel.text = @"The private mission selection is invalid.";
    id<AirfixContentCoordinatorDelegate> delegate = coordinator.delegate;
    if ([delegate
            respondsToSelector:@selector(
                                   contentCoordinatorDidFailLoadingMission:)]) {
      [delegate contentCoordinatorDidFailLoadingMission:coordinator];
    }
  });
}

- (void)requestMissionWithSetupLogicalPath:(NSString *)setupLogicalPath
                          levelLogicalPath:(NSString *)levelLogicalPath
                       requestedStartIndex:(uint32_t)requestedStartIndex {
  [self requestMissionWithSetupLogicalPath:setupLogicalPath
                          levelLogicalPath:levelLogicalPath
                   playerObjectLogicalPath:nil
                       requestedStartIndex:requestedStartIndex];
}

- (void)requestMissionWithSetupLogicalPath:(NSString *)setupLogicalPath
                          levelLogicalPath:(NSString *)levelLogicalPath
                   playerObjectLogicalPath:
                       (NSString *_Nullable)playerObjectLogicalPath
                       requestedStartIndex:(uint32_t)requestedStartIndex {
  NSAssert(NSThread.isMainThread,
           @"Mission requests must start on the main thread");
  NSObject *const requestIdentity = [NSObject new];
  _missionRequestIdentity = requestIdentity;
  [self cancelMissionLoadClearingRevision:NO];

  try {
    auto setup = copyPrivateLogicalPath(setupLogicalPath);
    auto level = copyPrivateLogicalPath(levelLogicalPath);
    if (!setup.has_value() || !level.has_value()) {
      _rememberedMissionRequest.reset();
      [self scheduleMissionRequestFailure:requestIdentity];
      return;
    }
    std::optional<std::string> playerObject;
    if (playerObjectLogicalPath != nil) {
      playerObject = copyPrivateLogicalPath(playerObjectLogicalPath);
      if (!playerObject.has_value()) {
        _rememberedMissionRequest.reset();
        [self scheduleMissionRequestFailure:requestIdentity];
        return;
      }
    }
    _rememberedMissionRequest = RememberedMissionRequest{
        .setupLogicalPath = std::move(*setup),
        .levelLogicalPath = std::move(*level),
        .playerObjectLogicalPath = std::move(playerObject),
        .requestedStartIndex = requestedStartIndex,
    };
  } catch (...) {
    _rememberedMissionRequest.reset();
    [self scheduleMissionRequestFailure:requestIdentity];
    return;
  }
  [self startRememberedMissionLoadIfPossible];
}

- (BOOL)isMissionWorldRoomSnapshotCurrent:
    (AirfixMissionWorldRoomSnapshot *)snapshot {
  NSAssert(NSThread.isMainThread,
           @"Mission room publication checks are main-thread confined");
  try {
    return _roomPublicationGate.accepts(
               airfix::ios::missionWorldRoomPublicationTicket(snapshot),
               airfix::ios::missionWorldRoomResultRevision(snapshot))
               ? YES
               : NO;
  } catch (...) {
    return NO;
  }
}

- (BOOL)consumeMissionWorldRoomSnapshot:
    (AirfixMissionWorldRoomSnapshot *)snapshot {
  NSAssert(NSThread.isMainThread,
           @"Mission room publication commits are main-thread confined");
  try {
    if (!_roomPublicationGate.consume(
            airfix::ios::missionWorldRoomPublicationTicket(snapshot),
            airfix::ios::missionWorldRoomResultRevision(snapshot))) {
      return NO;
    }
    _activeMissionTextureState =
        airfix::ios::missionWorldRoomTextureState(snapshot);
    _loadingMissionTextureState.reset();
    return YES;
  } catch (...) {
    return NO;
  }
}

- (BOOL)abandonMissionWorldRoomSnapshot:
    (AirfixMissionWorldRoomSnapshot *)snapshot {
  NSAssert(NSThread.isMainThread,
           @"Mission room publication failures are main-thread confined");
  try {
    if (!_roomPublicationGate.abandon(
            airfix::ios::missionWorldRoomPublicationTicket(snapshot),
            airfix::ios::missionWorldRoomResultRevision(snapshot))) {
      return NO;
    }
    _loadingMissionTextureState.reset();
    return YES;
  } catch (...) {
    return NO;
  }
}

- (void)startRememberedMissionLoadIfPossible {
  NSAssert(NSThread.isMainThread,
           @"Mission room publication gate is main-thread confined");
  if (self.busy || self.readiness != AirfixContentReadinessReady ||
      UIApplication.sharedApplication.applicationState !=
          UIApplicationStateActive ||
      !_rememberedMissionRequest.has_value() ||
      _roomPublicationGate.hasOutstandingTicket() ||
      !_roomPublicationGate.activeRevision().has_value()) {
    return;
  }

  const auto request = *_rememberedMissionRequest;
  const auto textureModeResolution =
      airfix::texture::resolveTextureModeState(
          _requestedTextureMode, _textureAvailability,
          _activeMissionTextureState);
  if (!textureModeResolution.complete()) {
    self.statusLabel.text =
        @"The mission texture policy could not be resolved.";
    return;
  }
  const airfix::texture::ActiveMissionTextureState missionTextureState{
      .requestedMode = textureModeResolution.state->requestedMode,
      .effectiveMode = textureModeResolution.state->effectiveMode,
  };
  const auto expectedRevision = *_roomPublicationGate.activeRevision();
  const auto ticket = _roomPublicationGate.begin(expectedRevision);
  if (!ticket.has_value()) {
    _roomPublicationGate.invalidate();
    self.statusLabel.text = @"The requested mission could not be started.";
    id<AirfixContentCoordinatorDelegate> delegate = self.delegate;
    if ([delegate
            respondsToSelector:@selector(
                                   contentCoordinatorDidFailLoadingMission:)]) {
      [delegate contentCoordinatorDidFailLoadingMission:self];
    }
    return;
  }
  _loadingMissionTextureState = missionTextureState;

  _loadStop = std::stop_source{};
  const auto stopToken = _loadStop.get_token();
  self.statusLabel.text = @"Loading authenticated private mission...";
  self.progressView.progress = 0.0F;
  self.progressView.hidden = YES;
  id<AirfixContentCoordinatorDelegate> delegate = self.delegate;
  if ([delegate
          respondsToSelector:@selector(
                                 contentCoordinatorDidBeginLoadingMission:)]) {
    [delegate contentCoordinatorDidBeginLoadingMission:self];
  }

  __weak AirfixContentCoordinator *weakSelf = self;
  dispatch_async(_workQueue, ^{
    AirfixContentCoordinator *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }

    [&] {
      // Materialize ownership inside the C++ IIFE. Objective-C blocks queued
      // after it returns must not capture outer ARC storage by reference.
      AirfixContentCoordinator *const completionCoordinator = strongSelf;
      void (^publishFailure)(void) = ^{
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          if (coordinator == nil || !coordinator->_roomPublicationGate.abandon(
                                        *ticket, ticket->expectedRevision)) {
            return;
          }
          coordinator->_loadingMissionTextureState.reset();
          coordinator.progressView.hidden = YES;
          coordinator.statusLabel.text =
              @"The requested mission could not be prepared.";
          id<AirfixContentCoordinatorDelegate> currentDelegate =
              coordinator.delegate;
          if ([currentDelegate
                  respondsToSelector:
                      @selector(contentCoordinatorDidFailLoadingMission:)]) {
            [currentDelegate
                contentCoordinatorDidFailLoadingMission:coordinator];
          }
        });
      };
      try {
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() !=
                ticket->expectedRevision) {
          publishFailure();
          return;
        }

        const auto revisionBeforeLoad =
            strongSelf->_verifiedSession->revision();
        const airfix::content::MissionLoadManifestRequest manifestRequest{
            .levelLogicalPath = request.levelLogicalPath,
            .setupLogicalPath = request.setupLogicalPath,
            .playerObjectLogicalPath = request.playerObjectLogicalPath,
        };
        auto manifestResult = airfix::content::buildMissionLoadManifest(
            *strongSelf->_verifiedSession, manifestRequest, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            revisionBeforeLoad != ticket->expectedRevision ||
            !manifestResult.success() || !manifestResult.manifest.has_value() ||
            manifestResult.manifest->revision() != ticket->expectedRevision) {
          publishFailure();
          return;
        }

        const airfix::content::MissionWorldRoomLoadRequest loadRequest{
            .initialRootName = {},
            .requestedStartIndex = request.requestedStartIndex,
            .basis = {},
            .uvPolicy = airfix::render::UvPolicy::preserveRaw,
        };
        airfix::content::MissionWorldRoomTextureReplacementContext
            textureReplacement;
        if (missionTextureState.effectiveMode ==
            airfix::texture::TextureMode::enhanced) {
          if (strongSelf->_texturePackSession == nullptr) {
            publishFailure();
            return;
          }
          textureReplacement = {
              .requestedMode = airfix::texture::TextureMode::enhanced,
              .resolver = &strongSelf->_texturePackSession->resolver(),
              .files = &strongSelf->_texturePackSession->files(),
          };
        }
        auto result = airfix::content::loadMissionWorldRoom(
            *strongSelf->_verifiedSession, *manifestResult.manifest,
            loadRequest, {}, stopToken, {}, textureReplacement);

        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            revisionBeforeLoad != ticket->expectedRevision ||
            !result.success() || !result.room.has_value() ||
            result.room->revision != ticket->expectedRevision) {
          publishFailure();
          return;
        }

        auto audioResult = airfix::content::loadLegacyAircraftAudioClips(
            *strongSelf->_verifiedSession, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            !audioResult.success() || !audioResult.clips.has_value() ||
            !audioResult.clips->belongsTo(*strongSelf->_verifiedSession) ||
            audioResult.clips->revision != ticket->expectedRevision) {
          publishFailure();
          return;
        }

        auto crosshairResult =
            airfix::content::loadLegacyWeaponCrosshairTextures(
                *strongSelf->_verifiedSession, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            !crosshairResult.success() ||
            !crosshairResult.textures.has_value() ||
            !crosshairResult.textures->belongsTo(
                *strongSelf->_verifiedSession) ||
            crosshairResult.textures->revision != ticket->expectedRevision) {
          publishFailure();
          return;
        }

        auto healthGaugeResult =
            airfix::content::loadLegacyAircraftHealthGaugeTextures(
                *strongSelf->_verifiedSession, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            !healthGaugeResult.success() ||
            !healthGaugeResult.textures.has_value() ||
            !healthGaugeResult.textures->belongsTo(
                *strongSelf->_verifiedSession) ||
            healthGaugeResult.textures->revision != ticket->expectedRevision) {
          publishFailure();
          return;
        }

        auto rollingDigitsResult =
            airfix::content::loadLegacyAircraftHudRollingDigitsTexture(
                *strongSelf->_verifiedSession, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            !rollingDigitsResult.success() ||
            !rollingDigitsResult.textures.has_value() ||
            !rollingDigitsResult.textures->belongsTo(
                *strongSelf->_verifiedSession) ||
            rollingDigitsResult.textures->revision !=
                ticket->expectedRevision) {
          publishFailure();
          return;
        }

        auto hudInstrumentsResult =
            airfix::content::loadLegacyAircraftHudInstrumentTextures(
                *strongSelf->_verifiedSession, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            !hudInstrumentsResult.success() ||
            !hudInstrumentsResult.textures.has_value() ||
            !hudInstrumentsResult.textures->belongsTo(
                *strongSelf->_verifiedSession) ||
            hudInstrumentsResult.textures->revision !=
                ticket->expectedRevision) {
          publishFailure();
          return;
        }

        auto hudWeaponPanelsResult =
            airfix::content::loadLegacyAircraftHudWeaponPanelTextures(
                *strongSelf->_verifiedSession, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            !hudWeaponPanelsResult.success() ||
            !hudWeaponPanelsResult.textures.has_value() ||
            !hudWeaponPanelsResult.textures->belongsTo(
                *strongSelf->_verifiedSession) ||
            hudWeaponPanelsResult.textures->revision !=
                ticket->expectedRevision) {
          publishFailure();
          return;
        }

        auto hudIdentityStatusResult =
            airfix::content::loadLegacyAircraftHudIdentityStatusTextures(
                *strongSelf->_verifiedSession, {}, stopToken);
        if (!strongSelf->_verifiedSession.has_value() ||
            strongSelf->_verifiedSession->revision() != revisionBeforeLoad ||
            !hudIdentityStatusResult.success() ||
            !hudIdentityStatusResult.textures.has_value() ||
            !hudIdentityStatusResult.textures->belongsTo(
                *strongSelf->_verifiedSession) ||
            hudIdentityStatusResult.textures->revision !=
                ticket->expectedRevision) {
          publishFailure();
          return;
        }

        const auto resultRevision = result.room->revision;
        AirfixMissionWorldRoomSnapshot *const snapshot =
            airfix::ios::makeMissionWorldRoomSnapshot(
                *ticket, std::move(*result.room), std::move(*audioResult.clips),
                std::move(*crosshairResult.textures),
                std::move(*healthGaugeResult.textures),
                std::move(*rollingDigitsResult.textures),
                std::move(*hudInstrumentsResult.textures),
                std::move(*hudWeaponPanelsResult.textures),
                std::move(*hudIdentityStatusResult.textures),
                missionTextureState);
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          if (coordinator == nil || !coordinator->_roomPublicationGate.accepts(
                                        *ticket, resultRevision)) {
            return;
          }
          coordinator.progressView.hidden = YES;
          coordinator.statusLabel.text =
              @"Mission data is ready for rendering.";
          id<AirfixContentCoordinatorDelegate> currentDelegate =
              coordinator.delegate;
          if (![currentDelegate
                  respondsToSelector:@selector(
                                         contentCoordinator:
                                         didLoadMissionWorldRoomSnapshot:)]) {
            if (coordinator->_roomPublicationGate.abandon(*ticket,
                                                          resultRevision)) {
              coordinator->_loadingMissionTextureState.reset();
            }
            coordinator.statusLabel.text =
                @"The mission renderer is unavailable.";
            if ([currentDelegate
                    respondsToSelector:
                        @selector(contentCoordinatorDidFailLoadingMission:)]) {
              [currentDelegate
                  contentCoordinatorDidFailLoadingMission:coordinator];
            }
            return;
          }
          [currentDelegate contentCoordinator:coordinator
              didLoadMissionWorldRoomSnapshot:snapshot];
        });
      } catch (...) {
        // No C++ exception may cross a GCD/Objective-C boundary.
        publishFailure();
      }
    }();

    // Ensure the worker's retain cannot be the final release. UIKit ivars
    // and coordinator deallocation are handed back to the main queue.
    AirfixContentCoordinator *const releaseOnMain = strongSelf;
    dispatch_async(dispatch_get_main_queue(), ^{
      (void)releaseOnMain;
    });
  });
}

- (void)beginInspection {
  if (self.busy) {
    return;
  }
  [self cancelMissionLoadClearingRevision:YES];
  [self publishTextureAvailability:
            airfix::texture::TexturePackageAvailability::validating];
  [self beginOperationWithText:@"Checking private game content..."];
  const std::stop_token stopToken = _operationStop.get_token();
  NSObject *const operationIdentity = _operationIdentity;
  NSObject *const lifecycleIdentity = _lifecycleIdentity;
  __weak AirfixContentCoordinator *weakSelf = self;
  dispatch_async(_workQueue, ^{
    AirfixContentCoordinator *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    [&] {
      // Materialize ownership inside the C++ IIFE. Objective-C blocks queued
      // after it returns must not capture outer ARC storage by reference.
      AirfixContentCoordinator *const completionCoordinator = strongSelf;
      clearWorkerContent(strongSelf->_inspection, strongSelf->_verifiedSession);
      strongSelf->_texturePackSession.reset();
      try {
        NSURL *rootURL = [strongSelf prepareContentRoot];
        if (rootURL == nil) {
          throw std::runtime_error("application support is unavailable");
        }
        const auto contentRoot = fileSystemPath(rootURL);
        auto textureInspection =
            airfix::texture::inspectInstalledTexturePack(
                contentRoot.parent_path() / "texture-packs");
        const auto textureAvailability =
            textureAvailabilityForInspection(textureInspection);
        strongSelf->_texturePackSession =
            std::move(textureInspection.session);
        auto inspected = airfix::afpack::inspectActiveContent(
            contentRoot, {}, stopToken,
            [weakSelf, operationIdentity, lifecycleIdentity](
                const airfix::afpack::RecoveryProgress &progress) {
              AirfixContentCoordinator *coordinator = weakSelf;
              [coordinator publishCompleted:progress.completedBytes
                                      total:progress.totalBytes
                                       text:@"Checking private game content..."
                          operationIdentity:operationIdentity
                          lifecycleIdentity:lifecycleIdentity];
            });
        const auto outcome = storeInspectedContent(strongSelf->_inspection,
                                                   strongSelf->_verifiedSession,
                                                   std::move(inspected));
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          if (coordinator == nil ||
              ![coordinator
                  consumeTerminalOperationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity]) {
            return;
          }
          if (outcome.activeRevision.has_value()) {
            coordinator->_roomPublicationGate.setActiveRevision(
                *outcome.activeRevision);
          } else {
            coordinator->_roomPublicationGate.clearActiveRevision();
          }
          [coordinator publishTextureAvailability:textureAvailability];
          [coordinator finishOperationWithInspectionStatus:outcome.status];
        });
      } catch (const airfix::afpack::RecoveryCancelled &) {
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          [coordinator
              completeInspectionWithErrorText:
                  @"Content check was paused. It will retry in the foreground."
                           operationIdentity:operationIdentity
                           lifecycleIdentity:lifecycleIdentity];
        });
      } catch (const std::exception &) {
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          [coordinator
              completeInspectionWithErrorText:
                  @"Content could not be checked. Try again in the foreground."
                           operationIdentity:operationIdentity
                           lifecycleIdentity:lifecycleIdentity];
        });
      } catch (...) {
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          [coordinator
              completeInspectionWithErrorText:
                  @"Content could not be checked. Try again in the foreground."
                           operationIdentity:operationIdentity
                           lifecycleIdentity:lifecycleIdentity];
        });
      }
    }();

    AirfixContentCoordinator *const releaseOnMain = strongSelf;
    dispatch_async(dispatch_get_main_queue(), ^{
      (void)releaseOnMain;
    });
  });
}

- (void)beginInstallFromURL:(NSURL *)selectedURL {
  if (self.busy) {
    return;
  }
  [self cancelMissionLoadClearingRevision:YES];
  [self beginOperationWithText:@"Preparing private import..."];
  const std::stop_token stopToken = _operationStop.get_token();
  NSObject *const operationIdentity = _operationIdentity;
  NSObject *const lifecycleIdentity = _lifecycleIdentity;
  NSString *transaction = canonicalTransactionIdentifier();
  __weak AirfixContentCoordinator *weakSelf = self;
  dispatch_async(_workQueue, ^{
    AirfixContentCoordinator *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    [&] {
      // Materialize ownership inside the C++ IIFE. Objective-C blocks queued
      // after it returns must not capture outer ARC storage by reference.
      AirfixContentCoordinator *const completionCoordinator = strongSelf;
      std::filesystem::path privateCopy;
      bool installReturned = false;
      // Close every authenticated reader before the serialized writer starts.
      clearWorkerContent(strongSelf->_inspection, strongSelf->_verifiedSession);
      try {
        NSURL *rootURL = [strongSelf prepareContentRoot];
        if (rootURL == nil) {
          throw std::runtime_error("application support is unavailable");
        }
        const std::filesystem::path root = fileSystemPath(rootURL);
        const std::filesystem::path incoming = root.parent_path() / "incoming";
        requirePrivateDirectory(incoming);
        privateCopy = incoming / (std::string("import-") +
                                  transaction.UTF8String + ".afpack");

        {
          ScopedSecurityAccess scopedAccess(selectedURL);
          NSFileCoordinator *fileCoordinator =
              [[NSFileCoordinator alloc] initWithFilePresenter:nil];
          __block std::exception_ptr copyFailure;
          NSError *coordinationError = nil;
          [fileCoordinator
              coordinateReadingItemAtURL:selectedURL
                                 options:0
                                   error:&coordinationError
                              byAccessor:^(NSURL *coordinatedURL) {
                                try {
                                  copyRegularFile(
                                      fileSystemPath(coordinatedURL),
                                      privateCopy, stopToken,
                                      [weakSelf, operationIdentity,
                                       lifecycleIdentity](
                                          const std::uint64_t completed,
                                          const std::uint64_t total) {
                                        [weakSelf
                                             publishCompleted:completed
                                                        total:total
                                                         text:
                                                             @"Copying private "
                                                             @"package..."
                                            operationIdentity:operationIdentity
                                            lifecycleIdentity:
                                                lifecycleIdentity];
                                      });
                                } catch (...) {
                                  copyFailure = std::current_exception();
                                }
                              }];
          if (copyFailure != nullptr) {
            std::rethrow_exception(copyFailure);
          }
          if (coordinationError != nil) {
            throw std::runtime_error("document coordination failed");
          }
        }

        (void)airfix::afpack::installPack(
            privateCopy, root, transaction.UTF8String, {}, stopToken,
            [weakSelf, operationIdentity, lifecycleIdentity](
                const airfix::afpack::InstallProgress &progress) {
              [weakSelf publishCompleted:progress.completedBytes
                                   total:progress.totalBytes
                                    text:@"Validating and activating package..."
                       operationIdentity:operationIdentity
                       lifecycleIdentity:lifecycleIdentity];
            });
        installReturned = true;
        removeFile(privateCopy);
        privateCopy.clear();
        auto inspected = airfix::afpack::inspectActiveContent(
            root, {}, stopToken,
            [weakSelf, operationIdentity, lifecycleIdentity](
                const airfix::afpack::RecoveryProgress &progress) {
              [weakSelf publishCompleted:progress.completedBytes
                                   total:progress.totalBytes
                                    text:@"Confirming active content..."
                       operationIdentity:operationIdentity
                       lifecycleIdentity:lifecycleIdentity];
            });
        const auto outcome = storeInspectedContent(strongSelf->_inspection,
                                                   strongSelf->_verifiedSession,
                                                   std::move(inspected));
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          if (coordinator == nil ||
              ![coordinator
                  consumeTerminalOperationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity]) {
            return;
          }
          if (outcome.activeRevision.has_value()) {
            coordinator->_roomPublicationGate.setActiveRevision(
                *outcome.activeRevision);
          } else {
            coordinator->_roomPublicationGate.clearActiveRevision();
          }
          [coordinator finishOperationWithInspectionStatus:outcome.status];
        });
      } catch (const NativeCopyCancelled &) {
        removeFile(privateCopy);
        dispatch_async(dispatch_get_main_queue(), ^{
          [completionCoordinator
              completeOperationWithErrorText:@"Import was paused. It can be "
                                             @"started again in the foreground."
                           operationIdentity:operationIdentity
                           lifecycleIdentity:lifecycleIdentity
                          inspectAfterFinish:YES];
        });
      } catch (const airfix::afpack::InstallCancelled &) {
        removeFile(privateCopy);
        dispatch_async(dispatch_get_main_queue(), ^{
          [completionCoordinator
              completeOperationWithErrorText:@"Import was paused. It can be "
                                             @"started again in the foreground."
                           operationIdentity:operationIdentity
                           lifecycleIdentity:lifecycleIdentity
                          inspectAfterFinish:YES];
        });
      } catch (const airfix::afpack::RecoveryCancelled &) {
        removeFile(privateCopy);
        dispatch_async(dispatch_get_main_queue(), ^{
          [completionCoordinator completeOperationWithErrorText:
                        @"The package was processed. Active content will be "
                        @"checked in the foreground."
                                 operationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity
                                inspectAfterFinish:YES];
        });
      } catch (const airfix::afpack::InstallCommitUnknown &) {
        removeFile(privateCopy);
        dispatch_async(dispatch_get_main_queue(), ^{
          [completionCoordinator completeOperationWithErrorText:
                        @"Package activation could not be confirmed. Content "
                        @"will be checked again."
                                 operationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity
                                inspectAfterFinish:YES];
        });
      } catch (const std::exception &) {
        removeFile(privateCopy);
        if (installReturned) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [completionCoordinator completeOperationWithErrorText:
                          @"The package was processed. Active content will be "
                          @"checked again."
                                   operationIdentity:operationIdentity
                                   lifecycleIdentity:lifecycleIdentity
                                  inspectAfterFinish:YES];
          });
        } else {
          dispatch_async(dispatch_get_main_queue(), ^{
            [completionCoordinator completeOperationWithErrorText:
                          @"The package could not be imported. Choose a valid "
                          @"AFPACK and try again."
                                   operationIdentity:operationIdentity
                                   lifecycleIdentity:lifecycleIdentity
                                  inspectAfterFinish:YES];
          });
        }
      } catch (...) {
        removeFile(privateCopy);
        if (installReturned) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [completionCoordinator completeOperationWithErrorText:
                          @"The package was processed. Active content will be "
                          @"checked again."
                                   operationIdentity:operationIdentity
                                   lifecycleIdentity:lifecycleIdentity
                                  inspectAfterFinish:YES];
          });
        } else {
          dispatch_async(dispatch_get_main_queue(), ^{
            [completionCoordinator completeOperationWithErrorText:
                          @"The package could not be imported. Choose a valid "
                          @"AFPACK and try again."
                                   operationIdentity:operationIdentity
                                   lifecycleIdentity:lifecycleIdentity
                                  inspectAfterFinish:YES];
          });
        }
      }
    }();

    AirfixContentCoordinator *const releaseOnMain = strongSelf;
    dispatch_async(dispatch_get_main_queue(), ^{
      (void)releaseOnMain;
    });
  });
}

- (void)beginTextureInstallFromURL:(NSURL *)selectedURL {
  if (self.busy || selectedURL == nil) {
    return;
  }
  [self cancelMissionLoadClearingRevision:NO];
  [self beginTextureOperationWithText:@"Validating private HD textures..."];
  NSObject *const operationIdentity = _operationIdentity;
  NSObject *const lifecycleIdentity = _lifecycleIdentity;
  NSString *const transaction = canonicalTransactionIdentifier();
  const std::string packageDirectoryName =
      std::string("pack-") + transaction.UTF8String;
  __weak AirfixContentCoordinator *weakSelf = self;
  dispatch_async(_workQueue, ^{
    AirfixContentCoordinator *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    [&] {
      // Materialize ownership inside the C++ IIFE. Objective-C blocks queued
      // after it returns must not capture outer ARC storage by reference.
      AirfixContentCoordinator *const completionCoordinator = strongSelf;
      std::filesystem::path textureRoot;
      bool installed = false;
      auto availability =
          airfix::texture::TexturePackageAvailability::unavailable;
      strongSelf->_texturePackSession.reset();
      try {
        NSURL *contentRootURL = [strongSelf prepareContentRoot];
        if (contentRootURL == nil) {
          throw std::runtime_error("application support is unavailable");
        }
        textureRoot =
            fileSystemPath(contentRootURL).parent_path() / "texture-packs";
        airfix::texture::TexturePackInstallResult installResult;
        auto *const installResultSlot = &installResult;
        {
          ScopedSecurityAccess scopedAccess(selectedURL);
          NSFileCoordinator *fileCoordinator =
              [[NSFileCoordinator alloc] initWithFilePresenter:nil];
          __block std::exception_ptr installFailure;
          NSError *coordinationError = nil;
          [fileCoordinator
              coordinateWritingItemAtURL:selectedURL
                                  options:NSFileCoordinatorWritingForMoving
                                    error:&coordinationError
                               byAccessor:^(NSURL *coordinatedURL) {
                                 try {
                                   *installResultSlot = airfix::texture::
                                       installImportedTexturePack(
                                           textureRoot,
                                           fileSystemPath(coordinatedURL),
                                           packageDirectoryName);
                                 } catch (...) {
                                   installFailure = std::current_exception();
                                 }
                               }];
          if (installFailure != nullptr) {
            std::rethrow_exception(installFailure);
          }
          if (coordinationError != nil) {
            throw std::runtime_error("folder coordination failed");
          }
        }
        installed = installResult.success();
        if (installed) {
          strongSelf->_texturePackSession =
              std::move(installResult.session);
          availability = airfix::texture::TexturePackageAvailability::ready;
        }
      } catch (...) {
        // Fixed public status below; no private path, manifest name, checksum,
        // or exception string crosses back to UIKit.
      }

      if (!installed && !textureRoot.empty()) {
        auto recovered =
            airfix::texture::inspectInstalledTexturePack(textureRoot);
        availability = textureAvailabilityForInspection(recovered);
        strongSelf->_texturePackSession = std::move(recovered.session);
      }

      dispatch_async(dispatch_get_main_queue(), ^{
        AirfixContentCoordinator *coordinator = completionCoordinator;
        if (coordinator == nil ||
            ![coordinator
                consumeTerminalOperationIdentity:operationIdentity
                               lifecycleIdentity:lifecycleIdentity]) {
          return;
        }
        NSString *message = nil;
        if (installed) {
          message = @"Private HD textures are validated and ready.";
        } else if (availability ==
                   airfix::texture::TexturePackageAvailability::ready) {
          message = @"The selected HD package was rejected. The previous "
                    @"validated package remains ready.";
        } else {
          message = @"The selected HD texture package is unavailable or "
                    @"invalid. Classic GTI textures remain available.";
        }
        [coordinator finishTextureOperationWithAvailability:availability
                                                       text:message];
      });
    }();

    AirfixContentCoordinator *const releaseOnMain = strongSelf;
    dispatch_async(dispatch_get_main_queue(), ^{
      (void)releaseOnMain;
    });
  });
}

- (void)beginRollback {
  if (self.busy || !self.rollbackEligible) {
    return;
  }
  [self cancelMissionLoadClearingRevision:YES];
  [self beginOperationWithText:@"Restoring the verified package..."];
  const std::stop_token stopToken = _operationStop.get_token();
  NSObject *const operationIdentity = _operationIdentity;
  NSObject *const lifecycleIdentity = _lifecycleIdentity;
  NSString *transaction = canonicalTransactionIdentifier();
  __weak AirfixContentCoordinator *weakSelf = self;
  dispatch_async(_workQueue, ^{
    AirfixContentCoordinator *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    [&] {
      // Materialize ownership inside the C++ IIFE. Objective-C blocks queued
      // after it returns must not capture outer ARC storage by reference.
      AirfixContentCoordinator *const completionCoordinator = strongSelf;
      try {
        // A ready session and rollback inspection are mutually exclusive,
        // but close a session defensively before entering the writer.
        strongSelf->_verifiedSession.reset();
        if (!strongSelf->_inspection.has_value() ||
            strongSelf->_inspection->status() !=
                airfix::afpack::ActiveContentStatus::rollbackAvailable) {
          throw std::runtime_error(
              "verified rollback inspection is unavailable");
        }
        const std::filesystem::path contentRoot =
            strongSelf->_inspection->contentRoot();
        (void)airfix::afpack::commitRollback(
            *strongSelf->_inspection, transaction.UTF8String, {}, stopToken,
            [weakSelf, operationIdentity, lifecycleIdentity](
                const airfix::afpack::RecoveryProgress &progress) {
              [weakSelf publishCompleted:progress.completedBytes
                                   total:progress.totalBytes
                                    text:@"Restoring the verified package..."
                       operationIdentity:operationIdentity
                       lifecycleIdentity:lifecycleIdentity];
            });
        strongSelf->_inspection.reset();
        auto inspected = airfix::afpack::inspectActiveContent(
            contentRoot, {}, stopToken,
            [weakSelf, operationIdentity, lifecycleIdentity](
                const airfix::afpack::RecoveryProgress &progress) {
              [weakSelf publishCompleted:progress.completedBytes
                                   total:progress.totalBytes
                                    text:@"Confirming restored content..."
                       operationIdentity:operationIdentity
                       lifecycleIdentity:lifecycleIdentity];
            });
        const auto outcome = storeInspectedContent(strongSelf->_inspection,
                                                   strongSelf->_verifiedSession,
                                                   std::move(inspected));
        dispatch_async(dispatch_get_main_queue(), ^{
          AirfixContentCoordinator *coordinator = completionCoordinator;
          if (coordinator == nil ||
              ![coordinator
                  consumeTerminalOperationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity]) {
            return;
          }
          if (outcome.activeRevision.has_value()) {
            coordinator->_roomPublicationGate.setActiveRevision(
                *outcome.activeRevision);
          } else {
            coordinator->_roomPublicationGate.clearActiveRevision();
          }
          [coordinator finishOperationWithInspectionStatus:outcome.status];
        });
      } catch (const airfix::afpack::RecoveryCancelled &) {
        clearWorkerContent(strongSelf->_inspection,
                           strongSelf->_verifiedSession);
        dispatch_async(dispatch_get_main_queue(), ^{
          [completionCoordinator completeOperationWithErrorText:
                        @"Restore was paused. Active content will be checked "
                        @"in the foreground."
                                 operationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity
                                inspectAfterFinish:YES];
        });
      } catch (const std::exception &) {
        clearWorkerContent(strongSelf->_inspection,
                           strongSelf->_verifiedSession);
        dispatch_async(dispatch_get_main_queue(), ^{
          [completionCoordinator completeOperationWithErrorText:
                        @"The previous package could not be restored. Content "
                        @"will be checked again."
                                 operationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity
                                inspectAfterFinish:YES];
        });
      } catch (...) {
        clearWorkerContent(strongSelf->_inspection,
                           strongSelf->_verifiedSession);
        dispatch_async(dispatch_get_main_queue(), ^{
          [completionCoordinator completeOperationWithErrorText:
                        @"The previous package could not be restored. Content "
                        @"will be checked again."
                                 operationIdentity:operationIdentity
                                 lifecycleIdentity:lifecycleIdentity
                                inspectAfterFinish:YES];
        });
      }
    }();

    AirfixContentCoordinator *const releaseOnMain = strongSelf;
    dispatch_async(dispatch_get_main_queue(), ^{
      (void)releaseOnMain;
    });
  });
}

@end
