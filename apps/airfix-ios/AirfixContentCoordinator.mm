#import "AirfixContentCoordinator.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "airfix/package/AfPackInstaller.hpp"
#include "airfix/package/AfPackRecovery.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
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

namespace {

constexpr std::uint64_t kMaximumImportedPackBytes = 512U * 1024U * 1024U;
constexpr std::size_t kCopyBufferBytes = 64U * 1024U;

class NativeCopyCancelled final : public std::runtime_error {
public:
    NativeCopyCancelled() : std::runtime_error("native content copy cancelled") {}
};

void closeFile(const int descriptor) noexcept {
    if (descriptor >= 0) {
        (void)::close(descriptor);
    }
}

void removeFile(const std::filesystem::path& path) noexcept {
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

std::filesystem::path fileSystemPath(NSURL* url) {
    const char* representation = url.fileSystemRepresentation;
    if (representation == nullptr || representation[0] == '\0') {
        throw std::runtime_error("file URL has no filesystem representation");
    }
    return std::filesystem::path(representation);
}

void requirePrivateDirectory(const std::filesystem::path& path) {
    struct stat info {};
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

bool isOwnedTransactionName(
    const std::string_view name,
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
        }
        else if (!((uuid[index] >= '0' && uuid[index] <= '9') ||
                   (uuid[index] >= 'a' && uuid[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

void cleanupOwnedFiles(
    const std::filesystem::path& directory,
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
        if (isOwnedTransactionName(
                candidate.filename().string(), prefix, suffix)) {
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
            throw std::runtime_error("private import directory changed during cleanup");
        }
    }
}

class ScopedSecurityAccess final {
public:
    explicit ScopedSecurityAccess(NSURL* url)
        : url_(url), accessed_([url startAccessingSecurityScopedResource]) {}

    ~ScopedSecurityAccess() {
        if (accessed_) {
            [url_ stopAccessingSecurityScopedResource];
        }
    }

    ScopedSecurityAccess(const ScopedSecurityAccess&) = delete;
    ScopedSecurityAccess& operator=(const ScopedSecurityAccess&) = delete;

private:
    __strong NSURL* url_;
    BOOL accessed_;
};

using CopyProgress = std::function<void(std::uint64_t, std::uint64_t)>;

void copyRegularFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::stop_token stopToken,
    const CopyProgress& progress) {
    checkStop(stopToken);
    int sourceFile = ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (sourceFile < 0) {
        throw std::runtime_error("selected content cannot be opened");
    }

    int destinationFile = -1;
    try {
        struct stat sourceInfo {};
        if (::fstat(sourceFile, &sourceInfo) != 0 || !S_ISREG(sourceInfo.st_mode) ||
            sourceInfo.st_size < 0) {
            throw std::runtime_error("selected content is not a regular file");
        }
        const auto expectedBytes = static_cast<std::uint64_t>(sourceInfo.st_size);
        if (expectedBytes == 0U || expectedBytes > kMaximumImportedPackBytes) {
            throw std::runtime_error("selected content size is outside limits");
        }

        destinationFile = ::open(
            destination.c_str(),
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
                const ssize_t written = ::write(
                    destinationFile,
                    buffer.data() + writtenBytes,
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
    }
    catch (...) {
        closeFile(destinationFile);
        closeFile(sourceFile);
        removeFile(destination);
        throw;
    }
}

NSString* canonicalTransactionIdentifier(void) {
    return NSUUID.UUID.UUIDString.lowercaseString;
}

} // namespace

@interface AirfixContentCoordinator () <UIDocumentPickerDelegate> {
    dispatch_queue_t _workQueue;
    std::stop_source _operationStop;
    std::optional<airfix::afpack::ActiveContentInspection> _inspection;
}
@property(nonatomic, weak) UIViewController* presentingViewController;
@property(nonatomic, strong, readwrite) UIView* controlsView;
@property(nonatomic, strong) UILabel* statusLabel;
@property(nonatomic, strong) UIProgressView* progressView;
@property(nonatomic, strong) UIButton* importButton;
@property(nonatomic, strong) UIButton* rollbackButton;
@property(nonatomic, readwrite) AirfixContentReadiness readiness;
@property(nonatomic) BOOL busy;
@property(nonatomic) BOOL rollbackEligible;
@property(nonatomic) BOOL started;
@property(nonatomic) BOOL pickerPresented;
@property(nonatomic) BOOL inspectWhenIdle;
@end

@implementation AirfixContentCoordinator

- (instancetype)initWithPresentingViewController:(UIViewController*)viewController {
    self = [super init];
    if (self == nil) {
        return nil;
    }
    _presentingViewController = viewController;
    _readiness = AirfixContentReadinessMissing;
    _workQueue = dispatch_queue_create(
        "com.tryk016.airfixdogfighter.content", DISPATCH_QUEUE_SERIAL);
    [self buildControls];
    return self;
}

- (void)buildControls {
    UIView* container = [[UIView alloc] initWithFrame:CGRectZero];
    container.translatesAutoresizingMaskIntoConstraints = NO;

    UILabel* status = [[UILabel alloc] initWithFrame:CGRectZero];
    status.numberOfLines = 0;
    status.textAlignment = NSTextAlignmentCenter;
    status.textColor = UIColor.whiteColor;
    status.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    status.adjustsFontForContentSizeCategory = YES;
    status.text = @"Checking private game content...";
    status.accessibilityTraits |= UIAccessibilityTraitUpdatesFrequently;
    self.statusLabel = status;

    UIProgressView* progress = [[UIProgressView alloc]
        initWithProgressViewStyle:UIProgressViewStyleDefault];
    progress.progress = 0.0F;
    progress.hidden = YES;
    progress.accessibilityLabel = @"Content operation progress";
    self.progressView = progress;

    UIButton* import = [UIButton buttonWithType:UIButtonTypeSystem];
    [import setTitle:@"Import AFPACK" forState:UIControlStateNormal];
    import.titleLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
    import.titleLabel.adjustsFontForContentSizeCategory = YES;
    import.titleLabel.numberOfLines = 0;
    import.titleLabel.lineBreakMode = NSLineBreakByWordWrapping;
    import.titleLabel.textAlignment = NSTextAlignmentCenter;
    import.accessibilityHint = @"Choose a private game content package";
    [import addTarget:self
               action:@selector(importPressed:)
     forControlEvents:UIControlEventTouchUpInside];
    self.importButton = import;

    UIButton* rollback = [UIButton buttonWithType:UIButtonTypeSystem];
    [rollback setTitle:@"Restore Previous Package" forState:UIControlStateNormal];
    rollback.titleLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
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

    UIStackView* buttons = [[UIStackView alloc]
        initWithArrangedSubviews:@[ import, rollback ]];
    buttons.axis = UILayoutConstraintAxisVertical;
    buttons.spacing = 10.0;
    buttons.alignment = UIStackViewAlignmentFill;
    buttons.distribution = UIStackViewDistributionFillProportionally;

    UIStackView* stack = [[UIStackView alloc]
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
        [rollback.heightAnchor constraintGreaterThanOrEqualToConstant:44.0],
    ]];
    self.controlsView = container;
}

- (void)start {
    NSAssert(NSThread.isMainThread, @"Content coordinator must be called on the main thread");
    if (self.started) {
        return;
    }
    self.started = YES;
    [self beginInspection];
}

- (void)applicationWillResignActive {
    _operationStop.request_stop();
}

- (void)applicationDidEnterBackground {
    _operationStop.request_stop();
}

- (void)applicationWillEnterForeground {
    // The active record may have changed at a commit boundary while the app was
    // leaving the foreground. Inspection is intentionally restarted only once
    // the application is active again.
}

- (void)applicationDidBecomeActive {
    if (!self.started || self.pickerPresented) {
        return;
    }
    if (self.busy) {
        self.inspectWhenIdle = YES;
    }
    else {
        self.inspectWhenIdle = NO;
        [self beginInspection];
    }
}

- (void)importPressed:(UIButton*)sender {
    (void)sender;
    if (self.busy || self.presentingViewController == nil ||
        self.presentingViewController.presentedViewController != nil) {
        return;
    }
    self.pickerPresented = YES;
    self.importButton.enabled = NO;
    self.rollbackButton.hidden = YES;
    self.rollbackButton.enabled = NO;
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ]
                                                                   asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;
    [self.presentingViewController presentViewController:picker
                                                animated:YES
                                              completion:nil];
}

- (void)rollbackPressed:(UIButton*)sender {
    (void)sender;
    if (self.busy || !self.rollbackEligible) {
        return;
    }
    [self beginRollback];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller
didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)controller;
    NSURL* selected = urls.firstObject;
    self.pickerPresented = NO;
    if (selected == nil || self.busy) {
        self.importButton.enabled = !self.busy;
        self.rollbackButton.hidden = !self.rollbackEligible;
        self.rollbackButton.enabled = self.rollbackEligible && !self.busy;
        return;
    }
    [self beginInstallFromURL:selected];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    (void)controller;
    self.pickerPresented = NO;
    self.importButton.enabled = !self.busy;
    self.rollbackButton.hidden = !self.rollbackEligible;
    self.rollbackButton.enabled = self.rollbackEligible && !self.busy;
}

- (nullable NSURL*)prepareContentRoot {
    NSFileManager* manager = NSFileManager.defaultManager;
    NSError* supportError = nil;
    NSURL* support = [manager URLForDirectory:NSApplicationSupportDirectory
                                      inDomain:NSUserDomainMask
                             appropriateForURL:nil
                                        create:YES
                                         error:&supportError];
    if (support == nil || supportError != nil) {
        return nil;
    }
    NSURL* parent = [support URLByAppendingPathComponent:@"AirfixDogfighter"
                                             isDirectory:YES];
    NSDictionary* attributes = @{
        NSFilePosixPermissions : @(0700),
        NSFileProtectionKey : NSFileProtectionCompleteUntilFirstUserAuthentication,
    };
    NSError* createError = nil;
    if (![manager createDirectoryAtURL:parent
           withIntermediateDirectories:YES
                            attributes:attributes
                                 error:&createError] || createError != nil) {
        return nil;
    }
    try {
        const std::filesystem::path parentPath = fileSystemPath(parent);
        requirePrivateDirectory(parentPath);
        cleanupOwnedFiles(parentPath / "incoming", "import-", ".afpack");
        const auto contentPath = parentPath / "content";
        requirePrivateDirectory(contentPath);
        cleanupOwnedFiles(
            contentPath / "staging", "import-", ".afpack.partial");
        cleanupOwnedFiles(contentPath, "active-", ".afac.partial");
    }
    catch (...) {
        return nil;
    }
    return [parent URLByAppendingPathComponent:@"content" isDirectory:YES];
}

- (void)beginOperationWithText:(NSString*)text {
    self.busy = YES;
    _operationStop = std::stop_source{};
    self.statusLabel.text = text;
    self.progressView.progress = 0.0F;
    self.progressView.hidden = YES;
    self.importButton.enabled = NO;
    self.rollbackButton.hidden = YES;
    self.rollbackButton.enabled = NO;
    [self setReadinessAndNotify:AirfixContentReadinessValidating];
}

- (void)finishOperationWithStoredInspection {
    if (!_inspection.has_value()) {
        [self finishOperationWithErrorText:
            @"Content could not be checked. Try again in the foreground."];
        return;
    }
    self.inspectWhenIdle = NO;
    self.busy = NO;
    self.progressView.hidden = YES;
    self.importButton.enabled = YES;

    const auto status = _inspection->status();
    self.rollbackEligible = status == airfix::afpack::ActiveContentStatus::rollbackAvailable;
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
        break;
    case airfix::afpack::ActiveContentStatus::rollbackAvailable:
        self.statusLabel.text =
            @"The active package is unusable. A verified previous package can be restored.";
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

- (void)finishOperationWithErrorText:(NSString*)text {
    _inspection.reset();
    self.busy = NO;
    self.rollbackEligible = NO;
    self.statusLabel.text = text;
    self.progressView.hidden = YES;
    self.importButton.enabled = YES;
    self.rollbackButton.hidden = YES;
    self.rollbackButton.enabled = NO;
    [self setReadinessAndNotify:AirfixContentReadinessRejected];
    if (self.inspectWhenIdle &&
        UIApplication.sharedApplication.applicationState == UIApplicationStateActive) {
        self.inspectWhenIdle = NO;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self beginInspection];
        });
    }
}

- (void)setReadinessAndNotify:(AirfixContentReadiness)readiness {
    self.readiness = readiness;
    [self.delegate contentCoordinator:self didChangeReadiness:readiness];
}

- (void)publishCompleted:(std::uint64_t)completed
                   total:(std::uint64_t)total
                    text:(NSString*)text {
    const std::uint64_t boundedCompleted = std::min(completed, total);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!self.busy) {
            return;
        }
        self.statusLabel.text = text;
        self.progressView.hidden = total == 0U;
        self.progressView.progress = total == 0U ? 0.0F :
            static_cast<float>(boundedCompleted) / static_cast<float>(total);
    });
}

- (void)beginInspection {
    if (self.busy) {
        return;
    }
    [self beginOperationWithText:@"Checking private game content..."];
    const std::stop_token stopToken = _operationStop.get_token();
    __weak AirfixContentCoordinator* weakSelf = self;
    dispatch_async(_workQueue, ^{
        AirfixContentCoordinator* strongSelf = weakSelf;
        if (strongSelf == nil) {
            return;
        }
        try {
            NSURL* rootURL = [strongSelf prepareContentRoot];
            if (rootURL == nil) {
                throw std::runtime_error("application support is unavailable");
            }
            auto inspection = std::make_shared<airfix::afpack::ActiveContentInspection>(
                airfix::afpack::inspectActiveContent(
                    fileSystemPath(rootURL), {}, stopToken,
                    [weakSelf](const airfix::afpack::RecoveryProgress& progress) {
                    AirfixContentCoordinator* coordinator = weakSelf;
                    [coordinator publishCompleted:progress.completedBytes
                                             total:progress.totalBytes
                                              text:@"Checking private game content..."];
                    }));
            dispatch_async(dispatch_get_main_queue(), ^{
                AirfixContentCoordinator* coordinator = weakSelf;
                if (coordinator != nil) {
                    coordinator->_inspection = std::move(*inspection);
                    [coordinator finishOperationWithStoredInspection];
                }
            });
        }
        catch (const airfix::afpack::RecoveryCancelled&) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf finishOperationWithErrorText:
                    @"Content check was paused. It will retry in the foreground."];
            });
        }
        catch (const std::exception&) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf finishOperationWithErrorText:
                    @"Content could not be checked. Try again in the foreground."];
            });
        }
        catch (...) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf finishOperationWithErrorText:
                    @"Content could not be checked. Try again in the foreground."];
            });
        }
    });
}

- (void)beginInstallFromURL:(NSURL*)selectedURL {
    if (self.busy) {
        return;
    }
    [self beginOperationWithText:@"Preparing private import..."];
    const std::stop_token stopToken = _operationStop.get_token();
    NSString* transaction = canonicalTransactionIdentifier();
    __weak AirfixContentCoordinator* weakSelf = self;
    dispatch_async(_workQueue, ^{
        AirfixContentCoordinator* strongSelf = weakSelf;
        if (strongSelf == nil) {
            return;
        }
        std::filesystem::path privateCopy;
        bool installReturned = false;
        try {
            NSURL* rootURL = [strongSelf prepareContentRoot];
            if (rootURL == nil) {
                throw std::runtime_error("application support is unavailable");
            }
            const std::filesystem::path root = fileSystemPath(rootURL);
            const std::filesystem::path incoming = root.parent_path() / "incoming";
            requirePrivateDirectory(incoming);
            privateCopy = incoming /
                (std::string("import-") + transaction.UTF8String + ".afpack");

            {
                ScopedSecurityAccess scopedAccess(selectedURL);
                NSFileCoordinator* fileCoordinator =
                    [[NSFileCoordinator alloc] initWithFilePresenter:nil];
                __block std::exception_ptr copyFailure;
                NSError* coordinationError = nil;
                [fileCoordinator coordinateReadingItemAtURL:selectedURL
                                                    options:0
                                                      error:&coordinationError
                                                 byAccessor:^(NSURL* coordinatedURL) {
                    try {
                        copyRegularFile(
                            fileSystemPath(coordinatedURL), privateCopy, stopToken,
                            [weakSelf](const std::uint64_t completed,
                                       const std::uint64_t total) {
                                [weakSelf publishCompleted:completed
                                                     total:total
                                                      text:@"Copying private package..."];
                            });
                    }
                    catch (...) {
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
                [weakSelf](const airfix::afpack::InstallProgress& progress) {
                    [weakSelf publishCompleted:progress.completedBytes
                                         total:progress.totalBytes
                                          text:@"Validating and activating package..."];
                });
            installReturned = true;
            removeFile(privateCopy);
            privateCopy.clear();
            auto inspection = std::make_shared<airfix::afpack::ActiveContentInspection>(
                airfix::afpack::inspectActiveContent(
                    root, {}, stopToken,
                    [weakSelf](const airfix::afpack::RecoveryProgress& progress) {
                    [weakSelf publishCompleted:progress.completedBytes
                                         total:progress.totalBytes
                                          text:@"Confirming active content..."];
                    }));
            dispatch_async(dispatch_get_main_queue(), ^{
                AirfixContentCoordinator* coordinator = weakSelf;
                if (coordinator != nil) {
                    coordinator->_inspection = std::move(*inspection);
                    [coordinator finishOperationWithStoredInspection];
                }
            });
        }
        catch (const NativeCopyCancelled&) {
            removeFile(privateCopy);
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.inspectWhenIdle = YES;
                [weakSelf finishOperationWithErrorText:
                    @"Import was paused. It can be started again in the foreground."];
            });
        }
        catch (const airfix::afpack::InstallCancelled&) {
            removeFile(privateCopy);
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.inspectWhenIdle = YES;
                [weakSelf finishOperationWithErrorText:
                    @"Import was paused. It can be started again in the foreground."];
            });
        }
        catch (const airfix::afpack::RecoveryCancelled&) {
            removeFile(privateCopy);
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.inspectWhenIdle = YES;
                [weakSelf finishOperationWithErrorText:
                    @"The package was processed. Active content will be checked in the foreground."];
            });
        }
        catch (const airfix::afpack::InstallCommitUnknown&) {
            removeFile(privateCopy);
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.inspectWhenIdle = YES;
                [weakSelf finishOperationWithErrorText:
                    @"Package activation could not be confirmed. Content will be checked again."];
            });
        }
        catch (const std::exception&) {
            removeFile(privateCopy);
            if (installReturned) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    weakSelf.inspectWhenIdle = YES;
                    [weakSelf finishOperationWithErrorText:
                        @"The package was processed. Active content will be checked again."];
                });
            }
            else {
                dispatch_async(dispatch_get_main_queue(), ^{
                    weakSelf.inspectWhenIdle = YES;
                    [weakSelf finishOperationWithErrorText:
                        @"The package could not be imported. Choose a valid AFPACK and try again."];
                });
            }
        }
        catch (...) {
            removeFile(privateCopy);
            if (installReturned) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    weakSelf.inspectWhenIdle = YES;
                    [weakSelf finishOperationWithErrorText:
                        @"The package was processed. Active content will be checked again."];
                });
            }
            else {
                dispatch_async(dispatch_get_main_queue(), ^{
                    weakSelf.inspectWhenIdle = YES;
                    [weakSelf finishOperationWithErrorText:
                        @"The package could not be imported. Choose a valid AFPACK and try again."];
                });
            }
        }
    });
}

- (void)beginRollback {
    if (self.busy || !self.rollbackEligible || !_inspection.has_value()) {
        return;
    }
    [self beginOperationWithText:@"Restoring the verified package..."];
    const std::stop_token stopToken = _operationStop.get_token();
    NSString* transaction = canonicalTransactionIdentifier();
    __weak AirfixContentCoordinator* weakSelf = self;
    dispatch_async(_workQueue, ^{
        AirfixContentCoordinator* strongSelf = weakSelf;
        if (strongSelf == nil) {
            return;
        }
        try {
            if (!strongSelf->_inspection.has_value() ||
                strongSelf->_inspection->status() !=
                    airfix::afpack::ActiveContentStatus::rollbackAvailable) {
                throw std::runtime_error("verified rollback inspection is unavailable");
            }
            const std::filesystem::path contentRoot =
                strongSelf->_inspection->contentRoot();
            (void)airfix::afpack::commitRollback(
                *strongSelf->_inspection, transaction.UTF8String, {}, stopToken,
                [weakSelf](const airfix::afpack::RecoveryProgress& progress) {
                    [weakSelf publishCompleted:progress.completedBytes
                                         total:progress.totalBytes
                                          text:@"Restoring the verified package..."];
                });
            strongSelf->_inspection.reset();
            auto inspection = std::make_shared<airfix::afpack::ActiveContentInspection>(
                airfix::afpack::inspectActiveContent(
                    contentRoot, {}, stopToken,
                    [weakSelf](const airfix::afpack::RecoveryProgress& progress) {
                    [weakSelf publishCompleted:progress.completedBytes
                                         total:progress.totalBytes
                                          text:@"Confirming restored content..."];
                    }));
            dispatch_async(dispatch_get_main_queue(), ^{
                AirfixContentCoordinator* coordinator = weakSelf;
                if (coordinator != nil) {
                    coordinator->_inspection = std::move(*inspection);
                    [coordinator finishOperationWithStoredInspection];
                }
            });
        }
        catch (const airfix::afpack::RecoveryCancelled&) {
            strongSelf->_inspection.reset();
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.inspectWhenIdle = YES;
                [weakSelf finishOperationWithErrorText:
                    @"Restore was paused. Active content will be checked in the foreground."];
            });
        }
        catch (const std::exception&) {
            strongSelf->_inspection.reset();
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.inspectWhenIdle = YES;
                [weakSelf finishOperationWithErrorText:
                    @"The previous package could not be restored. Content will be checked again."];
            });
        }
        catch (...) {
            strongSelf->_inspection.reset();
            dispatch_async(dispatch_get_main_queue(), ^{
                weakSelf.inspectWhenIdle = YES;
                [weakSelf finishOperationWithErrorText:
                    @"The previous package could not be restored. Content will be checked again."];
            });
        }
    });
}

@end
