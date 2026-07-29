#pragma once

#include "airfix/audio/AudioCommand.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace airfix::ios {

enum class AirfixIOSAudioOutputState : std::uint8_t {
  inactive,
  ready,
  interrupted,
  outputUnavailable,
  initializationFailed,
};

enum class AirfixIOSAudioPauseReason : std::uint8_t {
  interruption,
  outputRouteLost,
  mediaServicesReset,
};

enum class AirfixIOSAudioClipRegistrationResult : std::uint8_t {
  registered,
  invalidClip,
  duplicateId,
  capacityExceeded,
  allocationFailed,
  wrongThread,
};

struct AirfixIOSAudioSubmissionResult final {
  bool accepted{};
  bool outputAvailable{};
  std::size_t appliedCommandCount{};
  std::int64_t errorCode{};
};

class AirfixAVAudioEngineBackend final {
public:
  using ForcedPauseHandler = std::function<void(AirfixIOSAudioPauseReason)>;

  // Construction and every public method are main-thread operations. Audio
  // framework notifications are marshalled to main before touching state.
  AirfixAVAudioEngineBackend();
  ~AirfixAVAudioEngineBackend();

  AirfixAVAudioEngineBackend(const AirfixAVAudioEngineBackend &) = delete;
  AirfixAVAudioEngineBackend &
  operator=(const AirfixAVAudioEngineBackend &) = delete;
  AirfixAVAudioEngineBackend(AirfixAVAudioEngineBackend &&) = delete;
  AirfixAVAudioEngineBackend &operator=(AirfixAVAudioEngineBackend &&) = delete;

  [[nodiscard]] AirfixIOSAudioOutputState outputState() const noexcept;
  [[nodiscard]] std::int64_t lastErrorCode() const noexcept;
  [[nodiscard]] std::uint64_t lastAcceptedSequence() const noexcept;

  [[nodiscard]] AirfixIOSAudioClipRegistrationResult
  registerPcm16Clip(const audio::Pcm16ClipView &clip);

  [[nodiscard]] AirfixIOSAudioSubmissionResult
  submit(const audio::AudioCommandBatch &batch);

  // Deactivation drops one-shot voices and retains desired looping voices.
  // Activation never occurs automatically after an interruption.
  void setActive(bool active);

  [[nodiscard]] AirfixIOSAudioOutputState recover();

  void setForcedPauseHandler(ForcedPauseHandler handler);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace airfix::ios
