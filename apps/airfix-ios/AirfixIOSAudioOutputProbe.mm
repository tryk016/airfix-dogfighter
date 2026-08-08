#include "AirfixIOSAudioOutputProbe.hpp"

#include "AirfixAVAudioEngineBackend.hpp"
#include "airfix/audio/AudioOutputProbe.hpp"

#import <Foundation/Foundation.h>

#include <utility>

namespace airfix::ios {

class AirfixIOSAudioOutputProbe::Impl final {
public:
  Impl() : samples_(audio::makeAudioOutputProbeSamples()) {
    NSCAssert(NSThread.isMainThread,
              @"The audio output probe must be created on main");
    const audio::Pcm16ClipView clip{
        .id = audio::audioOutputProbeClipId,
        .sampleRate = audio::audioOutputProbeSampleRate,
        .channelCount = 1U,
        .interleavedSamples = samples_,
    };
    if (backend_.registerPcm16Clip(clip) !=
        AirfixIOSAudioClipRegistrationResult::registered) {
      setStatus(AirfixIOSAudioOutputProbeStatus::registrationFailed);
      return;
    }
    backend_.setForcedPauseHandler([this](AirfixIOSAudioPauseReason) {
      plan_.cancel();
      setStatus(AirfixIOSAudioOutputProbeStatus::outputUnavailable);
    });
  }

  ~Impl() {
    NSCAssert(NSThread.isMainThread,
              @"The audio output probe must be destroyed on main");
    backend_.setForcedPauseHandler({});
    backend_.setActive(false);
  }

  [[nodiscard]] AirfixIOSAudioOutputProbeStatus status() const noexcept {
    return status_;
  }

  [[nodiscard]] AirfixIOSAudioOutputProbeStatus start() {
    NSCAssert(NSThread.isMainThread,
              @"Audio output probe start belongs to main");
    if (status_ == AirfixIOSAudioOutputProbeStatus::registrationFailed ||
        status_ == AirfixIOSAudioOutputProbeStatus::submissionFailed) {
      return status_;
    }
    if (plan_.state() == audio::AudioOutputProbeState::playing) {
      return status_;
    }

    const auto batch = plan_.beginStart();
    if (!batch.has_value()) {
      setStatus(AirfixIOSAudioOutputProbeStatus::submissionFailed);
      return status_;
    }
    backend_.setActive(true);
    const auto result = backend_.submit(*batch);
    if (!plan_.completeSubmission(batch->sequence, result.accepted,
                                  result.appliedCommandCount)) {
      backend_.setActive(false);
      setStatus(AirfixIOSAudioOutputProbeStatus::submissionFailed);
      return status_;
    }
    if (!result.outputAvailable) {
      backend_.setActive(false);
      plan_.cancel();
      setStatus(AirfixIOSAudioOutputProbeStatus::outputUnavailable);
      return status_;
    }
    setStatus(AirfixIOSAudioOutputProbeStatus::playing);
    return status_;
  }

  [[nodiscard]] AirfixIOSAudioOutputProbeStatus stop() {
    NSCAssert(NSThread.isMainThread,
              @"Audio output probe stop belongs to main");
    if (status_ == AirfixIOSAudioOutputProbeStatus::registrationFailed ||
        status_ == AirfixIOSAudioOutputProbeStatus::submissionFailed) {
      backend_.setActive(false);
      return status_;
    }
    if (plan_.state() == audio::AudioOutputProbeState::playing) {
      const auto batch = plan_.beginStop();
      if (!batch.has_value()) {
        backend_.setActive(false);
        setStatus(AirfixIOSAudioOutputProbeStatus::submissionFailed);
        return status_;
      }
      const auto result = backend_.submit(*batch);
      const bool stopped = plan_.completeSubmission(
          batch->sequence, result.accepted, result.appliedCommandCount);
      backend_.setActive(false);
      if (!stopped) {
        setStatus(AirfixIOSAudioOutputProbeStatus::submissionFailed);
        return status_;
      }
    } else {
      backend_.setActive(false);
      plan_.cancel();
    }
    setStatus(AirfixIOSAudioOutputProbeStatus::stopped);
    return status_;
  }

  void setStatusHandler(StatusHandler handler) {
    NSCAssert(NSThread.isMainThread,
              @"Audio output probe callbacks belong to main");
    handler_ = std::move(handler);
  }

private:
  void setStatus(const AirfixIOSAudioOutputProbeStatus status) {
    status_ = status;
    if (handler_) {
      handler_(status_);
    }
  }

  audio::AudioOutputProbeSamples samples_;
  audio::AudioOutputProbePlan plan_;
  AirfixAVAudioEngineBackend backend_;
  AirfixIOSAudioOutputProbeStatus status_{
      AirfixIOSAudioOutputProbeStatus::ready};
  StatusHandler handler_;
};

AirfixIOSAudioOutputProbe::AirfixIOSAudioOutputProbe()
    : impl_(std::make_unique<Impl>()) {}

AirfixIOSAudioOutputProbe::~AirfixIOSAudioOutputProbe() = default;

AirfixIOSAudioOutputProbeStatus
AirfixIOSAudioOutputProbe::status() const noexcept {
  return impl_->status();
}

AirfixIOSAudioOutputProbeStatus AirfixIOSAudioOutputProbe::start() {
  return impl_->start();
}

AirfixIOSAudioOutputProbeStatus AirfixIOSAudioOutputProbe::stop() {
  return impl_->stop();
}

void AirfixIOSAudioOutputProbe::setStatusHandler(StatusHandler handler) {
  impl_->setStatusHandler(std::move(handler));
}

} // namespace airfix::ios
