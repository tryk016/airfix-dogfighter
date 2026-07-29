#pragma once

#include "airfix/audio/AudioCommand.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace airfix::windows {

enum class AirfixXAudio2OutputState : std::uint8_t {
  ready,
  outputUnavailable,
  initializationFailed,
};

enum class AirfixAudioClipRegistrationResult : std::uint8_t {
  registered,
  invalidClip,
  duplicateId,
  capacityExceeded,
};

struct AirfixAudioSubmissionResult final {
  bool accepted{};
  bool outputAvailable{};
  std::size_t appliedCommandCount{};
  std::uint32_t errorCode{};
};

class AirfixXAudio2Backend final {
public:
  // Registration, submission, lifecycle, and recovery are game-thread calls.
  // Only the internal XAudio2 engine callback crosses threads.
  AirfixXAudio2Backend();
  ~AirfixXAudio2Backend();

  AirfixXAudio2Backend(const AirfixXAudio2Backend &) = delete;
  AirfixXAudio2Backend &operator=(const AirfixXAudio2Backend &) = delete;
  AirfixXAudio2Backend(AirfixXAudio2Backend &&) = delete;
  AirfixXAudio2Backend &operator=(AirfixXAudio2Backend &&) = delete;

  [[nodiscard]] AirfixXAudio2OutputState outputState() const noexcept;
  [[nodiscard]] std::uint32_t lastErrorCode() const noexcept;

  [[nodiscard]] AirfixAudioClipRegistrationResult
  registerPcm16Clip(const audio::Pcm16ClipView &clip);

  [[nodiscard]] AirfixAudioSubmissionResult
  submit(const audio::AudioCommandBatch &batch);

  void setActive(bool active) noexcept;

  [[nodiscard]] AirfixXAudio2OutputState recover() noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace airfix::windows
