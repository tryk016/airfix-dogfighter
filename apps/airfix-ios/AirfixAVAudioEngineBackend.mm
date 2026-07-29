#include "AirfixAVAudioEngineBackend.hpp"

#import <AVFAudio/AVFAudio.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

@interface AirfixIOSRegisteredAudioClip : NSObject {
@public
  std::uint32_t clipId;
  AVAudioFormat *format;
  AVAudioPCMBuffer *buffer;
  std::size_t byteCount;
}
@end

@implementation AirfixIOSRegisteredAudioClip
@end

@interface AirfixIOSAudioVoice : NSObject {
@public
  std::uint32_t voiceId;
  std::uint32_t clipId;
  std::uint64_t token;
  float gain;
  float pitch;
  BOOL looping;
  AVAudioPlayerNode *player;
  AVAudioUnitVarispeed *varispeed;
}
@end

@implementation AirfixIOSAudioVoice
@end

namespace airfix::ios {
namespace {

inline constexpr std::size_t maximumRegisteredClips = 128U;
inline constexpr std::size_t maximumActiveVoices = 64U;
inline constexpr std::size_t maximumRegisteredPcmBytes = 256U * 1024U * 1024U;

inline constexpr std::int64_t wrongThreadError = -10'001;
inline constexpr std::int64_t invalidArgumentError = -10'002;
inline constexpr std::int64_t missingClipError = -10'003;
inline constexpr std::int64_t voiceCapacityError = -10'004;
inline constexpr std::int64_t graphCreationError = -10'005;
inline constexpr std::int64_t voiceGenerationError = -10'006;

[[nodiscard]] bool isMainThread() noexcept { return NSThread.isMainThread; }

[[nodiscard]] NSNumber *identifierKey(const std::uint32_t value) {
  return [NSNumber numberWithUnsignedInt:value];
}

[[nodiscard]] std::int64_t errorCode(NSError *error,
                                     const std::int64_t fallback) noexcept {
  return error == nil ? fallback : static_cast<std::int64_t>(error.code);
}

struct NotificationOwner final {
  std::atomic<void *> owner{};
};

} // namespace

class AirfixAVAudioEngineBackend::Impl final {
public:
  Impl()
      : session_([AVAudioSession sharedInstance]),
        clips_([[NSMutableDictionary alloc]
            initWithCapacity:maximumRegisteredClips]),
        voices_(
            [[NSMutableDictionary alloc] initWithCapacity:maximumActiveVoices]),
        notificationOwner_(std::make_shared<NotificationOwner>()) {
    if (!isMainThread()) {
      outputState_ = AirfixIOSAudioOutputState::initializationFailed;
      lastErrorCode_ = wrongThreadError;
      return;
    }

    notificationOwner_->owner.store(this, std::memory_order_release);
    installNotificationObservers();
    if (!configureSession()) {
      outputState_ = AirfixIOSAudioOutputState::initializationFailed;
      return;
    }
    if (!createEmptyGraph()) {
      outputState_ = AirfixIOSAudioOutputState::initializationFailed;
      return;
    }
    outputState_ = AirfixIOSAudioOutputState::inactive;
    lastErrorCode_ = 0;
  }

  ~Impl() {
    notificationOwner_->owner.store(nullptr, std::memory_order_release);
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    for (id observer in notificationObservers_) {
      [center removeObserver:observer];
    }
    notificationObservers_ = nil;

    if (isMainThread()) {
      desiredActive_ = false;
      discardOneShotVoices();
      pauseEngine();
      deactivateSession();
      destroyGraph(false);
    }
  }

  [[nodiscard]] AirfixIOSAudioOutputState outputState() const noexcept {
    return outputState_;
  }

  [[nodiscard]] std::int64_t lastErrorCode() const noexcept {
    return lastErrorCode_;
  }

  [[nodiscard]] std::uint64_t lastAcceptedSequence() const noexcept {
    return lastAcceptedSequence_;
  }

  [[nodiscard]] AirfixIOSAudioClipRegistrationResult
  registerPcm16Clip(const audio::Pcm16ClipView &clip) {
    if (!isMainThread()) {
      lastErrorCode_ = wrongThreadError;
      return AirfixIOSAudioClipRegistrationResult::wrongThread;
    }
    if (!audio::validPcm16Clip(clip)) {
      lastErrorCode_ = invalidArgumentError;
      return AirfixIOSAudioClipRegistrationResult::invalidClip;
    }
    NSNumber *key = identifierKey(clip.id.value);
    if ([clips_ objectForKey:key] != nil) {
      lastErrorCode_ = invalidArgumentError;
      return AirfixIOSAudioClipRegistrationResult::duplicateId;
    }
    if (clips_.count >= maximumRegisteredClips) {
      lastErrorCode_ = voiceCapacityError;
      return AirfixIOSAudioClipRegistrationResult::capacityExceeded;
    }

    const std::size_t clipBytes =
        clip.interleavedSamples.size() * sizeof(std::int16_t);
    if (clipBytes > maximumRegisteredPcmBytes - registeredPcmBytes_) {
      lastErrorCode_ = voiceCapacityError;
      return AirfixIOSAudioClipRegistrationResult::capacityExceeded;
    }
    const std::size_t frameCount =
        clip.interleavedSamples.size() / clip.channelCount;
    if (frameCount > static_cast<std::size_t>(
                         std::numeric_limits<AVAudioFrameCount>::max())) {
      lastErrorCode_ = invalidArgumentError;
      return AirfixIOSAudioClipRegistrationResult::invalidClip;
    }

    AVAudioFormat *format = [[AVAudioFormat alloc]
        initWithCommonFormat:AVAudioPCMFormatInt16
                  sampleRate:static_cast<double>(clip.sampleRate)
                    channels:clip.channelCount
                 interleaved:YES];
    if (format == nil) {
      lastErrorCode_ = graphCreationError;
      return AirfixIOSAudioClipRegistrationResult::allocationFailed;
    }

    AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:format
            frameCapacity:static_cast<AVAudioFrameCount>(frameCount)];
    if (buffer == nil || buffer.int16ChannelData == nullptr ||
        buffer.int16ChannelData[0] == nullptr) {
      lastErrorCode_ = graphCreationError;
      return AirfixIOSAudioClipRegistrationResult::allocationFailed;
    }
    buffer.frameLength = static_cast<AVAudioFrameCount>(frameCount);
    std::memcpy(buffer.int16ChannelData[0], clip.interleavedSamples.data(),
                clipBytes);

    AirfixIOSRegisteredAudioClip *registered =
        [[AirfixIOSRegisteredAudioClip alloc] init];
    if (registered == nil) {
      lastErrorCode_ = graphCreationError;
      return AirfixIOSAudioClipRegistrationResult::allocationFailed;
    }
    registered->clipId = clip.id.value;
    registered->format = format;
    registered->buffer = buffer;
    registered->byteCount = clipBytes;
    [clips_ setObject:registered forKey:key];
    registeredPcmBytes_ += clipBytes;
    lastErrorCode_ = 0;
    return AirfixIOSAudioClipRegistrationResult::registered;
  }

  [[nodiscard]] AirfixIOSAudioSubmissionResult
  submit(const audio::AudioCommandBatch &batch) {
    if (!isMainThread()) {
      lastErrorCode_ = wrongThreadError;
      return {.errorCode = lastErrorCode_};
    }
    if (!audio::validAudioCommandBatch(batch) ||
        batch.sequence <= lastAcceptedSequence_) {
      lastErrorCode_ = invalidArgumentError;
      return {.errorCode = lastErrorCode_};
    }

    const std::int64_t preflightError = preflight(batch);
    if (preflightError != 0) {
      lastErrorCode_ = preflightError;
      return {.errorCode = lastErrorCode_};
    }

    bool graphFailed = false;
    for (std::size_t index = 0U; index < batch.commandCount; ++index) {
      const audio::AudioCommand &command = batch.commands[index];
      switch (command.kind) {
      case audio::AudioCommandKind::startVoice:
        if (!startVoice(command)) {
          graphFailed = true;
        }
        break;
      case audio::AudioCommandKind::stopVoice:
        removeVoice(command.voice.value);
        break;
      case audio::AudioCommandKind::setVoiceGain:
        setVoiceGain(command.voice.value, command.gain);
        break;
      case audio::AudioCommandKind::setVoicePitch:
        setVoicePitch(command.voice.value, command.pitch);
        break;
      case audio::AudioCommandKind::stopAllVoices:
        removeAllVoices();
        break;
      }
    }

    lastAcceptedSequence_ = batch.sequence;
    if (graphFailed) {
      destroyGraph(true);
      discardOneShotVoices();
      outputState_ = AirfixIOSAudioOutputState::outputUnavailable;
      if (lastErrorCode_ == 0) {
        lastErrorCode_ = graphCreationError;
      }
    } else if (!desiredActive_) {
      // Simulation is not expected to publish effects while paused.
      // Consume any such one-shot command instead of replaying it later.
      discardOneShotVoices();
    }

    return {
        .accepted = true,
        .outputAvailable = outputState_ == AirfixIOSAudioOutputState::ready,
        .appliedCommandCount = batch.commandCount,
        .errorCode = lastErrorCode_,
    };
  }

  void setActive(const bool active) {
    if (!isMainThread()) {
      lastErrorCode_ = wrongThreadError;
      return;
    }
    if (!active) {
      desiredActive_ = false;
      pauseEngine();
      discardOneShotVoices();
      deactivateSession();
      if (outputState_ != AirfixIOSAudioOutputState::initializationFailed) {
        outputState_ = AirfixIOSAudioOutputState::inactive;
      }
      return;
    }

    desiredActive_ = true;
    if (!configureSession() || !ensureGraph() || !activateSession() ||
        !startEngineAndVoices()) {
      outputState_ = AirfixIOSAudioOutputState::outputUnavailable;
      return;
    }
    outputState_ = AirfixIOSAudioOutputState::ready;
    lastErrorCode_ = 0;
  }

  [[nodiscard]] AirfixIOSAudioOutputState recover() {
    if (!isMainThread()) {
      lastErrorCode_ = wrongThreadError;
      return outputState_;
    }

    if (!configureSession()) {
      outputState_ = AirfixIOSAudioOutputState::initializationFailed;
      return outputState_;
    }
    if (!ensureGraph()) {
      outputState_ = AirfixIOSAudioOutputState::outputUnavailable;
      return outputState_;
    }
    if (!desiredActive_) {
      outputState_ = AirfixIOSAudioOutputState::inactive;
      lastErrorCode_ = 0;
      return outputState_;
    }
    if (!activateSession() || !startEngineAndVoices()) {
      outputState_ = AirfixIOSAudioOutputState::outputUnavailable;
      return outputState_;
    }
    outputState_ = AirfixIOSAudioOutputState::ready;
    lastErrorCode_ = 0;
    return outputState_;
  }

  void setForcedPauseHandler(ForcedPauseHandler handler) {
    if (!isMainThread()) {
      lastErrorCode_ = wrongThreadError;
      return;
    }
    forcedPauseHandler_ = std::move(handler);
  }

private:
  using VoiceIdentifiers = std::array<std::uint32_t, maximumActiveVoices>;

  void installNotificationObservers() {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    NSMutableArray *observers = [[NSMutableArray alloc] initWithCapacity:4U];
    const auto owner = notificationOwner_;

    [observers
        addObject:[center
                      addObserverForName:AVAudioSessionInterruptionNotification
                                  object:session_
                                   queue:NSOperationQueue.mainQueue
                              usingBlock:^(NSNotification *notification) {
                                auto *implementation =
                                    static_cast<Impl *>(owner->owner.load(
                                        std::memory_order_acquire));
                                if (implementation != nullptr) {
                                  implementation->handleInterruption(
                                      notification);
                                }
                              }]];
    [observers
        addObject:[center
                      addObserverForName:AVAudioSessionRouteChangeNotification
                                  object:session_
                                   queue:NSOperationQueue.mainQueue
                              usingBlock:^(NSNotification *notification) {
                                auto *implementation =
                                    static_cast<Impl *>(owner->owner.load(
                                        std::memory_order_acquire));
                                if (implementation != nullptr) {
                                  implementation->handleRouteChange(
                                      notification);
                                }
                              }]];
    [observers
        addObject:[center
                      addObserverForName:
                          AVAudioSessionMediaServicesWereResetNotification
                                  object:session_
                                   queue:NSOperationQueue.mainQueue
                              usingBlock:^(NSNotification *notification) {
                                (void)notification;
                                auto *implementation =
                                    static_cast<Impl *>(owner->owner.load(
                                        std::memory_order_acquire));
                                if (implementation != nullptr) {
                                  implementation->handleMediaServicesReset();
                                }
                              }]];
    [observers
        addObject:
            [center
                addObserverForName:AVAudioEngineConfigurationChangeNotification
                            object:nil
                             queue:NSOperationQueue.mainQueue
                        usingBlock:^(NSNotification *notification) {
                          auto *implementation = static_cast<Impl *>(
                              owner->owner.load(std::memory_order_acquire));
                          if (implementation != nullptr &&
                              notification.object == implementation->engine_) {
                            implementation->handleConfigurationChange();
                          }
                        }]];
    notificationObservers_ = observers;
  }

  [[nodiscard]] bool configureSession() {
    NSError *error = nil;
    const BOOL configured = [session_ setCategory:AVAudioSessionCategoryAmbient
                                             mode:AVAudioSessionModeDefault
                                          options:0
                                            error:&error];
    if (!configured) {
      lastErrorCode_ = errorCode(error, graphCreationError);
      return false;
    }
    return true;
  }

  [[nodiscard]] bool activateSession() {
    NSError *error = nil;
    if (![session_ setActive:YES error:&error]) {
      lastErrorCode_ = errorCode(error, graphCreationError);
      return false;
    }
    return true;
  }

  void deactivateSession() noexcept {
    NSError *error = nil;
    if (![session_
              setActive:NO
            withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                  error:&error] &&
        error != nil) {
      lastErrorCode_ = errorCode(error, graphCreationError);
    }
  }

  [[nodiscard]] bool createEmptyGraph() {
    @try {
      engine_ = [[AVAudioEngine alloc] init];
      if (engine_ == nil || engine_.mainMixerNode == nil) {
        lastErrorCode_ = graphCreationError;
        return false;
      }
      [engine_ prepare];
      return true;
    } @catch (NSException *exception) {
      (void)exception;
      engine_ = nil;
      lastErrorCode_ = graphCreationError;
      return false;
    }
  }

  [[nodiscard]] bool ensureGraph() {
    if (engine_ == nil && !createEmptyGraph()) {
      return false;
    }
    for (AirfixIOSAudioVoice *voice in voices_.allValues) {
      if (voice->player == nil && !materializeVoice(voice)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool startEngineAndVoices() {
    NSError *error = nil;
    if (!engine_.isRunning && ![engine_ startAndReturnError:&error]) {
      lastErrorCode_ = errorCode(error, graphCreationError);
      return false;
    }
    for (AirfixIOSAudioVoice *voice in voices_.allValues) {
      if (voice->player != nil && !voice->player.isPlaying) {
        [voice->player play];
      }
    }
    return true;
  }

  void pauseEngine() noexcept {
    if (engine_.isRunning) {
      [engine_ pause];
    }
  }

  void destroyGraph(const bool preserveLoopingVoices) {
    NSArray<AirfixIOSAudioVoice *> *snapshot = voices_.allValues;
    for (AirfixIOSAudioVoice *voice in snapshot) {
      if (voice->player != nil) {
        [voice->player stop];
      }
      voice->player = nil;
      voice->varispeed = nil;
      if (!preserveLoopingVoices || !voice->looping) {
        [voices_ removeObjectForKey:identifierKey(voice->voiceId)];
      }
    }
    if (engine_ != nil) {
      [engine_ stop];
      [engine_ reset];
      engine_ = nil;
    }
  }

  [[nodiscard]] bool materializeVoice(AirfixIOSAudioVoice *voice) {
    AirfixIOSRegisteredAudioClip *clip =
        [clips_ objectForKey:identifierKey(voice->clipId)];
    if (engine_ == nil || clip == nil) {
      lastErrorCode_ = missingClipError;
      return false;
    }

    AVAudioPlayerNode *player = [[AVAudioPlayerNode alloc] init];
    AVAudioUnitVarispeed *varispeed = [[AVAudioUnitVarispeed alloc] init];
    if (player == nil || varispeed == nil) {
      lastErrorCode_ = graphCreationError;
      return false;
    }
    player.volume = voice->gain;
    varispeed.rate = voice->pitch;

    @try {
      [engine_ attachNode:player];
      [engine_ attachNode:varispeed];
      [engine_ connect:player to:varispeed format:clip->format];
      [engine_ connect:varispeed to:engine_.mainMixerNode format:clip->format];

      const auto owner = notificationOwner_;
      const std::uint32_t completedVoiceId = voice->voiceId;
      const std::uint64_t completedToken = voice->token;
      const AVAudioPlayerNodeBufferOptions options =
          voice->looping ? AVAudioPlayerNodeBufferLoops : 0;
      [player scheduleBuffer:clip->buffer
                          atTime:nil
                         options:options
          completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack
               completionHandler:^(
                   AVAudioPlayerNodeCompletionCallbackType callbackType) {
                 (void)callbackType;
                 dispatch_async(dispatch_get_main_queue(), ^{
                   auto *implementation = static_cast<Impl *>(
                       owner->owner.load(std::memory_order_acquire));
                   if (implementation != nullptr) {
                     implementation->completeVoice(completedVoiceId,
                                                   completedToken);
                   }
                 });
               }];
    } @catch (NSException *exception) {
      (void)exception;
      @try {
        [engine_ disconnectNodeOutput:player];
        [engine_ disconnectNodeOutput:varispeed];
        [engine_ detachNode:player];
        [engine_ detachNode:varispeed];
      } @catch (NSException *cleanupException) {
        (void)cleanupException;
      }
      lastErrorCode_ = graphCreationError;
      return false;
    }

    voice->player = player;
    voice->varispeed = varispeed;
    return true;
  }

  [[nodiscard]] std::int64_t
  preflight(const audio::AudioCommandBatch &batch) const {
    VoiceIdentifiers identifiers{};
    std::size_t identifierCount = 0U;
    std::uint64_t tokenCursor = nextVoiceToken_;
    for (AirfixIOSAudioVoice *voice in voices_.allValues) {
      identifiers[identifierCount++] = voice->voiceId;
    }

    const auto findIdentifier = [&](const std::uint32_t value) {
      return std::find(identifiers.begin(),
                       identifiers.begin() +
                           static_cast<std::ptrdiff_t>(identifierCount),
                       value);
    };
    for (std::size_t index = 0U; index < batch.commandCount; ++index) {
      const audio::AudioCommand &command = batch.commands[index];
      switch (command.kind) {
      case audio::AudioCommandKind::startVoice: {
        if ([clips_ objectForKey:identifierKey(command.clip.value)] == nil) {
          return missingClipError;
        }
        if (tokenCursor == std::numeric_limits<std::uint64_t>::max()) {
          return voiceGenerationError;
        }
        ++tokenCursor;
        if (findIdentifier(command.voice.value) ==
            identifiers.begin() +
                static_cast<std::ptrdiff_t>(identifierCount)) {
          if (identifierCount >= maximumActiveVoices) {
            return voiceCapacityError;
          }
          identifiers[identifierCount++] = command.voice.value;
        }
        break;
      }
      case audio::AudioCommandKind::stopVoice: {
        const auto found = findIdentifier(command.voice.value);
        if (found != identifiers.begin() +
                         static_cast<std::ptrdiff_t>(identifierCount)) {
          *found = identifiers[--identifierCount];
        }
        break;
      }
      case audio::AudioCommandKind::stopAllVoices:
        identifierCount = 0U;
        break;
      case audio::AudioCommandKind::setVoiceGain:
      case audio::AudioCommandKind::setVoicePitch:
        break;
      }
    }
    return 0;
  }

  [[nodiscard]] bool startVoice(const audio::AudioCommand &command) {
    removeVoice(command.voice.value);

    AirfixIOSAudioVoice *voice = [[AirfixIOSAudioVoice alloc] init];
    if (voice == nil) {
      lastErrorCode_ = graphCreationError;
      return false;
    }
    voice->voiceId = command.voice.value;
    voice->clipId = command.clip.value;
    voice->token = ++nextVoiceToken_;
    voice->gain = command.gain;
    voice->pitch = command.pitch;
    voice->looping = command.looping;
    [voices_ setObject:voice forKey:identifierKey(command.voice.value)];

    if (!desiredActive_ || outputState_ != AirfixIOSAudioOutputState::ready) {
      return true;
    }
    if (!materializeVoice(voice)) {
      return false;
    }
    [voice->player play];
    return true;
  }

  void setVoiceGain(const std::uint32_t identifier,
                    const float value) noexcept {
    AirfixIOSAudioVoice *voice =
        [voices_ objectForKey:identifierKey(identifier)];
    if (voice == nil) {
      return;
    }
    voice->gain = value;
    if (voice->player != nil) {
      voice->player.volume = value;
    }
  }

  void setVoicePitch(const std::uint32_t identifier,
                     const float value) noexcept {
    AirfixIOSAudioVoice *voice =
        [voices_ objectForKey:identifierKey(identifier)];
    if (voice == nil) {
      return;
    }
    voice->pitch = value;
    if (voice->varispeed != nil) {
      voice->varispeed.rate = value;
    }
  }

  void removeVoice(const std::uint32_t identifier) noexcept {
    NSNumber *key = identifierKey(identifier);
    AirfixIOSAudioVoice *voice = [voices_ objectForKey:key];
    if (voice == nil) {
      return;
    }
    [voices_ removeObjectForKey:key];
    if (voice->player == nil || engine_ == nil) {
      return;
    }

    [voice->player stop];
    @try {
      [engine_ disconnectNodeOutput:voice->player];
      [engine_ disconnectNodeOutput:voice->varispeed];
      [engine_ detachNode:voice->player];
      [engine_ detachNode:voice->varispeed];
    } @catch (NSException *exception) {
      (void)exception;
    }
    voice->player = nil;
    voice->varispeed = nil;
  }

  void removeAllVoices() noexcept {
    NSArray<NSNumber *> *keys = voices_.allKeys;
    for (NSNumber *key in keys) {
      removeVoice(key.unsignedIntValue);
    }
  }

  void discardOneShotVoices() noexcept {
    NSArray<AirfixIOSAudioVoice *> *snapshot = voices_.allValues;
    for (AirfixIOSAudioVoice *voice in snapshot) {
      if (!voice->looping) {
        removeVoice(voice->voiceId);
      }
    }
  }

  void completeVoice(const std::uint32_t identifier,
                     const std::uint64_t token) noexcept {
    if (!isMainThread()) {
      return;
    }
    AirfixIOSAudioVoice *voice =
        [voices_ objectForKey:identifierKey(identifier)];
    if (voice == nil || voice->token != token || voice->looping) {
      return;
    }
    removeVoice(identifier);
  }

  void forcePause(const AirfixIOSAudioPauseReason reason) {
    const bool notify = desiredActive_;
    desiredActive_ = false;
    pauseEngine();
    discardOneShotVoices();
    deactivateSession();
    outputState_ = AirfixIOSAudioOutputState::interrupted;
    if (notify && forcedPauseHandler_) {
      const ForcedPauseHandler handler = forcedPauseHandler_;
      handler(reason);
    }
  }

  void handleInterruption(NSNotification *notification) {
    NSNumber *typeValue =
        notification.userInfo[AVAudioSessionInterruptionTypeKey];
    if (typeValue == nil) {
      return;
    }
    const auto type = static_cast<AVAudioSessionInterruptionType>(
        typeValue.unsignedIntegerValue);
    if (type == AVAudioSessionInterruptionTypeBegan) {
      forcePause(AirfixIOSAudioPauseReason::interruption);
    } else if (outputState_ == AirfixIOSAudioOutputState::interrupted) {
      // Ended is only an availability signal. Deliberate user input is
      // still required before setActive(true).
      outputState_ = AirfixIOSAudioOutputState::inactive;
    }
  }

  void handleRouteChange(NSNotification *notification) {
    NSNumber *reasonValue =
        notification.userInfo[AVAudioSessionRouteChangeReasonKey];
    if (reasonValue == nil) {
      return;
    }
    const auto reason = static_cast<AVAudioSessionRouteChangeReason>(
        reasonValue.unsignedIntegerValue);
    if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable ||
        reason == AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory) {
      forcePause(AirfixIOSAudioPauseReason::outputRouteLost);
      return;
    }
    if (desiredActive_ && !engine_.isRunning) {
      (void)recover();
    }
  }

  void handleMediaServicesReset() {
    const bool wasActive = desiredActive_;
    forcePause(AirfixIOSAudioPauseReason::mediaServicesReset);
    destroyGraph(true);
    (void)configureSession();
    (void)createEmptyGraph();
    if (!wasActive) {
      outputState_ = AirfixIOSAudioOutputState::inactive;
    }
  }

  void handleConfigurationChange() {
    if (handlingConfigurationChange_) {
      return;
    }
    handlingConfigurationChange_ = true;
    const bool wasActive = desiredActive_;
    destroyGraph(true);
    if (!createEmptyGraph() || !ensureGraph()) {
      outputState_ = AirfixIOSAudioOutputState::outputUnavailable;
      handlingConfigurationChange_ = false;
      return;
    }
    if (wasActive && (!activateSession() || !startEngineAndVoices())) {
      outputState_ = AirfixIOSAudioOutputState::outputUnavailable;
    } else {
      outputState_ = wasActive ? AirfixIOSAudioOutputState::ready
                               : AirfixIOSAudioOutputState::inactive;
      lastErrorCode_ = 0;
    }
    handlingConfigurationChange_ = false;
  }

  AVAudioSession *session_;
  AVAudioEngine *engine_;
  NSMutableDictionary<NSNumber *, AirfixIOSRegisteredAudioClip *> *clips_;
  NSMutableDictionary<NSNumber *, AirfixIOSAudioVoice *> *voices_;
  NSArray *notificationObservers_;
  std::shared_ptr<NotificationOwner> notificationOwner_;
  ForcedPauseHandler forcedPauseHandler_;
  AirfixIOSAudioOutputState outputState_{
      AirfixIOSAudioOutputState::initializationFailed};
  std::int64_t lastErrorCode_{};
  std::uint64_t lastAcceptedSequence_{};
  std::uint64_t nextVoiceToken_{};
  std::size_t registeredPcmBytes_{};
  bool desiredActive_{};
  bool handlingConfigurationChange_{};
};

AirfixAVAudioEngineBackend::AirfixAVAudioEngineBackend()
    : impl_(std::make_unique<Impl>()) {}

AirfixAVAudioEngineBackend::~AirfixAVAudioEngineBackend() = default;

AirfixIOSAudioOutputState
AirfixAVAudioEngineBackend::outputState() const noexcept {
  return impl_->outputState();
}

std::int64_t AirfixAVAudioEngineBackend::lastErrorCode() const noexcept {
  return impl_->lastErrorCode();
}

std::uint64_t
AirfixAVAudioEngineBackend::lastAcceptedSequence() const noexcept {
  return impl_->lastAcceptedSequence();
}

AirfixIOSAudioClipRegistrationResult
AirfixAVAudioEngineBackend::registerPcm16Clip(
    const audio::Pcm16ClipView &clip) {
  return impl_->registerPcm16Clip(clip);
}

AirfixIOSAudioSubmissionResult
AirfixAVAudioEngineBackend::submit(const audio::AudioCommandBatch &batch) {
  return impl_->submit(batch);
}

void AirfixAVAudioEngineBackend::setActive(const bool active) {
  impl_->setActive(active);
}

AirfixIOSAudioOutputState AirfixAVAudioEngineBackend::recover() {
  return impl_->recover();
}

void AirfixAVAudioEngineBackend::setForcedPauseHandler(
    ForcedPauseHandler handler) {
  impl_->setForcedPauseHandler(std::move(handler));
}

} // namespace airfix::ios
