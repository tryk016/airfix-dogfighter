#import "AirfixDiagnosticsCoordinator.h"

#import <Foundation/Foundation.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr char kImportsDirectory[] = "Imports";
constexpr char kDiagnosticsDirectory[] = "Diagnostics";
constexpr char kCurrentLogName[] = "latest.jsonl";
constexpr char kPreviousLogName[] = "previous.jsonl";
constexpr char kReadmeName[] = "README.txt";
constexpr std::size_t kMaximumLogBytes = 1024U * 1024U;
constexpr std::size_t kMaximumRecordBytes = 8U * 1024U;
constexpr std::size_t kMaximumLogRecords = 4096U;
constexpr NSTimeInterval kInputSampleIntervalSeconds = 5.0;

constexpr char kReadme[] =
    "Airfix Dogfighter owner-local files\n\n"
    "Imports: place .afpack and .afmission files here, then select them "
    "with the matching button in the app. After a successful import, the app "
    "uses its validated private copy and the staging file may be removed.\n"
    "Diagnostics: latest.jsonl is the current bounded diagnostic journal; "
    "previous.jsonl is the preceding session or rotation.\n\n"
    "Diagnostic journals contain controlled runtime states and counters. "
    "They do not contain original assets, logical game paths, checksums, or "
    "device-local paths.\n";

[[nodiscard]] bool writeAll(const int descriptor, const std::uint8_t *bytes,
                            const std::size_t size) noexcept {
  std::size_t written = 0U;
  while (written < size) {
    const auto remaining = size - written;
    constexpr auto maximumWrite =
        static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
    const auto chunk = remaining > maximumWrite ? maximumWrite : remaining;
    const ssize_t result = ::write(descriptor, bytes + written, chunk);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (result == 0) {
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  return true;
}

[[nodiscard]] int openDirectoryAt(const int parent, const char *name) noexcept {
  if (::mkdirat(parent, name, 0700) != 0 && errno != EEXIST) {
    return -1;
  }
  const int descriptor =
      ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (descriptor < 0) {
    return -1;
  }
  struct stat metadata{};
  if (::fstat(descriptor, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
    ::close(descriptor);
    return -1;
  }
  return descriptor;
}

[[nodiscard]] bool removeReplaceableEntry(const int directory,
                                          const char *name) noexcept {
  struct stat metadata{};
  if (::fstatat(directory, name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
    return errno == ENOENT;
  }
  if (S_ISDIR(metadata.st_mode)) {
    return false;
  }
  return ::unlinkat(directory, name, 0) == 0;
}

[[nodiscard]] bool sanitizeBoundedLogEntry(const int directory,
                                           const char *name) noexcept {
  struct stat metadata{};
  if (::fstatat(directory, name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
    return errno == ENOENT;
  }
  if (S_ISREG(metadata.st_mode) && metadata.st_size >= 0 &&
      static_cast<std::uint64_t>(metadata.st_size) <= kMaximumLogBytes) {
    return true;
  }
  return removeReplaceableEntry(directory, name);
}

[[nodiscard]] bool writeReadmeIfMissing(const int documents) noexcept {
  const int descriptor =
      ::openat(documents, kReadmeName,
               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (descriptor < 0) {
    if (errno != EEXIST) {
      return false;
    }
    struct stat existing{};
    return ::fstatat(documents, kReadmeName, &existing, AT_SYMLINK_NOFOLLOW) ==
               0 &&
           S_ISREG(existing.st_mode);
  }
  const bool written =
      writeAll(descriptor, reinterpret_cast<const std::uint8_t *>(kReadme),
               sizeof(kReadme) - 1U);
  const bool synced = written && ::fsync(descriptor) == 0;
  const bool closed = ::close(descriptor) == 0;
  if (!synced || !closed) {
    (void)::unlinkat(documents, kReadmeName, 0);
    return false;
  }
  return true;
}

[[nodiscard]] NSString *
contentStateName(const AirfixDiagnosticContentState state) noexcept {
  switch (state) {
  case AirfixDiagnosticContentStateMissing:
    return @"missing";
  case AirfixDiagnosticContentStateValidating:
    return @"validating";
  case AirfixDiagnosticContentStateReady:
    return @"ready";
  case AirfixDiagnosticContentStateRejected:
    return @"rejected";
  }
  return @"invalid";
}

[[nodiscard]] NSString *missionFailureStageName(
    const AirfixDiagnosticMissionFailureStage stage) noexcept {
  switch (stage) {
  case AirfixDiagnosticMissionFailureStageContent:
    return @"content";
  case AirfixDiagnosticMissionFailureStageHandoff:
    return @"handoff";
  case AirfixDiagnosticMissionFailureStageMetalPreparation:
    return @"metal-preparation";
  case AirfixDiagnosticMissionFailureStagePlayerSpawn:
    return @"player-spawn";
  case AirfixDiagnosticMissionFailureStagePublication:
    return @"publication";
  case AirfixDiagnosticMissionFailureStagePlayerPose:
    return @"player-pose";
  case AirfixDiagnosticMissionFailureStageCamera:
    return @"camera";
  case AirfixDiagnosticMissionFailureStageAudio:
    return @"audio";
  }
  return @"invalid";
}

[[nodiscard]] NSString *
lifecycleEventName(const AirfixDiagnosticLifecycleEvent event) noexcept {
  switch (event) {
  case AirfixDiagnosticLifecycleEventResignActive:
    return @"resign-active";
  case AirfixDiagnosticLifecycleEventBackground:
    return @"background";
  case AirfixDiagnosticLifecycleEventForeground:
    return @"foreground";
  case AirfixDiagnosticLifecycleEventActive:
    return @"active";
  }
  return @"invalid";
}

[[nodiscard]] NSString *
pauseReasonName(const AirfixDiagnosticPauseReason reason) noexcept {
  switch (reason) {
  case AirfixDiagnosticPauseReasonUser:
    return @"user";
  case AirfixDiagnosticPauseReasonSettings:
    return @"settings";
  case AirfixDiagnosticPauseReasonLifecycle:
    return @"lifecycle";
  case AirfixDiagnosticPauseReasonControllerDisconnected:
    return @"controller-disconnected";
  case AirfixDiagnosticPauseReasonInputOverflow:
    return @"input-overflow";
  case AirfixDiagnosticPauseReasonInputFailure:
    return @"input-failure";
  case AirfixDiagnosticPauseReasonAudioInterruption:
    return @"audio-interruption";
  case AirfixDiagnosticPauseReasonAudioRoute:
    return @"audio-route";
  case AirfixDiagnosticPauseReasonAudioServices:
    return @"audio-services";
  }
  return @"invalid";
}

[[nodiscard]] NSString *
inputSourceName(const AirfixDiagnosticInputSource source) noexcept {
  switch (source) {
  case AirfixDiagnosticInputSourceNone:
    return @"none";
  case AirfixDiagnosticInputSourceTouch:
    return @"touch";
  case AirfixDiagnosticInputSourceController:
    return @"controller";
  }
  return @"invalid";
}

} // namespace

@interface AirfixDiagnosticsCoordinator () {
  int _diagnosticsDirectory;
  int _logDescriptor;
  std::size_t _bytesWritten;
  std::size_t _recordsWritten;
  std::uint64_t _sequence;
  NSTimeInterval _sessionStartedAt;
  NSTimeInterval _nextInputSampleAt;
}
@property(nonatomic, readwrite, getter=isReady) BOOL ready;
- (BOOL)openFreshLogRotatingCurrent:(BOOL)rotateCurrent;
- (void)recordEvent:(NSString *)event
             fields:(NSDictionary<NSString *, id> *)fields;
- (void)recordEvent:(NSString *)event
             fields:(NSDictionary<NSString *, id> *)fields
              flush:(BOOL)flush;
@end

@implementation AirfixDiagnosticsCoordinator

- (instancetype)init {
  self = [super init];
  if (self == nil) {
    return nil;
  }
  _diagnosticsDirectory = -1;
  _logDescriptor = -1;
  _sessionStartedAt = NSProcessInfo.processInfo.systemUptime;
  _nextInputSampleAt = _sessionStartedAt;

  NSURL *const documents =
      [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                           inDomains:NSUserDomainMask]
          .firstObject;
  if (documents == nil || !documents.isFileURL) {
    return self;
  }
  const char *const documentsPath = documents.fileSystemRepresentation;
  if (documentsPath == nullptr) {
    return self;
  }

  const int documentsDirectory =
      ::open(documentsPath, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (documentsDirectory < 0) {
    return self;
  }
  const int importsDirectory =
      openDirectoryAt(documentsDirectory, kImportsDirectory);
  _diagnosticsDirectory =
      openDirectoryAt(documentsDirectory, kDiagnosticsDirectory);
  const bool readmeReady = writeReadmeIfMissing(documentsDirectory);
  if (importsDirectory >= 0) {
    (void)::close(importsDirectory);
  }
  (void)::close(documentsDirectory);
  if (importsDirectory < 0 || _diagnosticsDirectory < 0 || !readmeReady ||
      ![self openFreshLogRotatingCurrent:YES]) {
    if (_diagnosticsDirectory >= 0) {
      (void)::close(_diagnosticsDirectory);
      _diagnosticsDirectory = -1;
    }
    return self;
  }

  self.ready = YES;
  [self recordEvent:@"session.started" fields:@{} flush:YES];
  return self;
}

- (void)dealloc {
  if (_logDescriptor >= 0) {
    (void)::fsync(_logDescriptor);
    (void)::close(_logDescriptor);
  }
  if (_diagnosticsDirectory >= 0) {
    (void)::close(_diagnosticsDirectory);
  }
}

- (BOOL)openFreshLogRotatingCurrent:(BOOL)rotateCurrent {
  if (_diagnosticsDirectory < 0) {
    return NO;
  }
  if (rotateCurrent) {
    if (!sanitizeBoundedLogEntry(_diagnosticsDirectory, kPreviousLogName) ||
        !sanitizeBoundedLogEntry(_diagnosticsDirectory, kCurrentLogName)) {
      return NO;
    }
    struct stat currentMetadata{};
    if (::fstatat(_diagnosticsDirectory, kCurrentLogName, &currentMetadata,
                  AT_SYMLINK_NOFOLLOW) == 0) {
      if (!removeReplaceableEntry(_diagnosticsDirectory, kPreviousLogName) ||
          ::renameat(_diagnosticsDirectory, kCurrentLogName,
                     _diagnosticsDirectory, kPreviousLogName) != 0) {
        return NO;
      }
    } else if (errno != ENOENT) {
      return NO;
    }
  }

  _logDescriptor =
      ::openat(_diagnosticsDirectory, kCurrentLogName,
               O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  _bytesWritten = 0U;
  _recordsWritten = 0U;
  return _logDescriptor >= 0;
}

- (void)recordEvent:(NSString *)event
             fields:(NSDictionary<NSString *, id> *)fields {
  [self recordEvent:event fields:fields flush:YES];
}

- (void)recordEvent:(NSString *)event
             fields:(NSDictionary<NSString *, id> *)fields
              flush:(BOOL)flush {
  NSAssert(NSThread.isMainThread,
           @"Diagnostic journal writes belong to the main thread");
  if (!self.ready || _logDescriptor < 0 || event.length == 0U) {
    return;
  }

  // Keep enough space for any valid record. A rotated current journal starts
  // with its own session marker so latest.jsonl remains self-contained.
  if (_bytesWritten > kMaximumLogBytes - kMaximumRecordBytes ||
      _recordsWritten >= kMaximumLogRecords) {
    (void)::fsync(_logDescriptor);
    (void)::close(_logDescriptor);
    _logDescriptor = -1;
    if (![self openFreshLogRotatingCurrent:YES]) {
      self.ready = NO;
      return;
    }
    [self recordEvent:@"session.continued" fields:@{} flush:YES];
    if (!self.ready) {
      return;
    }
  }

  const NSTimeInterval elapsed =
      MAX(0.0, NSProcessInfo.processInfo.systemUptime - _sessionStartedAt);
  NSDictionary<NSString *, id> *const record = @{
    @"elapsedMs" : @(static_cast<unsigned long long>(elapsed * 1000.0)),
    @"event" : event,
    @"fields" : fields,
    @"schema" : @"airfix.ios-diagnostics-v1",
    @"sequence" : @(++_sequence),
  };
  if (![NSJSONSerialization isValidJSONObject:record]) {
    return;
  }
  NSError *serializationError = nil;
  NSData *const json =
      [NSJSONSerialization dataWithJSONObject:record
                                      options:NSJSONWritingSortedKeys
                                        error:&serializationError];
  if (json == nil || serializationError != nil || json.length == 0U ||
      json.length + 1U > kMaximumRecordBytes) {
    return;
  }

  const bool jsonWritten =
      writeAll(_logDescriptor, static_cast<const std::uint8_t *>(json.bytes),
               json.length);
  constexpr std::uint8_t newline = '\n';
  const bool newlineWritten =
      jsonWritten && writeAll(_logDescriptor, &newline, 1U);
  const bool synchronized = !flush || ::fsync(_logDescriptor) == 0;
  if (!newlineWritten || !synchronized) {
    self.ready = NO;
    (void)::close(_logDescriptor);
    _logDescriptor = -1;
    return;
  }
  _bytesWritten += json.length + 1U;
  ++_recordsWritten;
}

- (void)recordRendererInitializationSucceeded:(BOOL)succeeded {
  [self recordEvent:@"renderer.initialized"
             fields:@{@"succeeded" : @(succeeded)}];
}

- (void)recordContentState:(AirfixDiagnosticContentState)state {
  [self recordEvent:@"content.state"
             fields:@{@"state" : contentStateName(state)}];
}

- (void)recordMissionLoadStarted {
  [self recordEvent:@"mission.load.started" fields:@{}];
}

- (void)recordMissionReadyWithMeshCount:(NSUInteger)meshCount
                           textureCount:(NSUInteger)textureCount
                          drawCallCount:(NSUInteger)drawCallCount {
  [self recordEvent:@"mission.load.ready"
             fields:@{
               @"drawCalls" : @(drawCallCount),
               @"meshes" : @(meshCount),
               @"textures" : @(textureCount),
             }];
}

- (void)recordMissionLoadFailedAtStage:
    (AirfixDiagnosticMissionFailureStage)stage {
  [self recordEvent:@"mission.load.failed"
             fields:@{@"stage" : missionFailureStageName(stage)}];
}

- (void)recordGameplayResumed {
  [self recordEvent:@"gameplay.state" fields:@{@"state" : @"running"}];
}

- (void)recordGameplayPausedForReason:(AirfixDiagnosticPauseReason)reason {
  [self recordEvent:@"gameplay.state"
             fields:@{
               @"reason" : pauseReasonName(reason),
               @"state" : @"paused",
             }];
}

- (void)recordLifecycleEvent:(AirfixDiagnosticLifecycleEvent)event {
  [self recordEvent:@"lifecycle.transition"
             fields:@{@"state" : lifecycleEventName(event)}];
}

- (void)recordControllerConnected:(BOOL)connected {
  [self recordEvent:@"controller.connection"
             fields:@{@"connected" : @(connected)}];
}

- (void)recordInputSampleWithTick:(uint64_t)tick
                             bank:(int16_t)bank
                            pitch:(int16_t)pitch
                         fireHeld:(BOOL)fireHeld
              controllerConnected:(BOOL)controllerConnected
                           source:(AirfixDiagnosticInputSource)source
                   simulationStep:(uint64_t)simulationStep
                   simulationHash:(uint64_t)simulationHash {
  NSAssert(NSThread.isMainThread,
           @"Diagnostic input samples belong to the main thread");
  const NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
  if (now < _nextInputSampleAt) {
    return;
  }
  _nextInputSampleAt = now + kInputSampleIntervalSeconds;
  NSString *const hash = [NSString
      stringWithFormat:@"%016llX",
                       static_cast<unsigned long long>(simulationHash)];
  [self recordEvent:@"input.sample"
             fields:@{
               @"bank" : @(bank),
               @"controllerConnected" : @(controllerConnected),
               @"fireHeld" : @(fireHeld),
               @"pitch" : @(pitch),
               @"simulationHash" : hash,
               @"simulationStep" : @(simulationStep),
               @"source" : inputSourceName(source),
               @"tick" : @(tick),
             }
              flush:NO];
}

- (void)recordMemoryWarning {
  [self recordEvent:@"memory.warning" fields:@{}];
}

@end
