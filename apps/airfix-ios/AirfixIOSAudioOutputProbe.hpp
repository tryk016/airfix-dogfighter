#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace airfix::ios {

enum class AirfixIOSAudioOutputProbeStatus : std::uint8_t {
  ready,
  playing,
  stopped,
  outputUnavailable,
  registrationFailed,
  submissionFailed,
};

// Main-thread-only, public diagnostic output path. This owns a backend that is
// deliberately isolated from the mission audio backend and private clip IDs.
class AirfixIOSAudioOutputProbe final {
public:
  using StatusHandler = std::function<void(AirfixIOSAudioOutputProbeStatus)>;

  AirfixIOSAudioOutputProbe();
  ~AirfixIOSAudioOutputProbe();

  AirfixIOSAudioOutputProbe(const AirfixIOSAudioOutputProbe &) = delete;
  AirfixIOSAudioOutputProbe &
  operator=(const AirfixIOSAudioOutputProbe &) = delete;
  AirfixIOSAudioOutputProbe(AirfixIOSAudioOutputProbe &&) = delete;
  AirfixIOSAudioOutputProbe &operator=(AirfixIOSAudioOutputProbe &&) = delete;

  [[nodiscard]] AirfixIOSAudioOutputProbeStatus status() const noexcept;
  [[nodiscard]] AirfixIOSAudioOutputProbeStatus start();
  [[nodiscard]] AirfixIOSAudioOutputProbeStatus stop();
  void setStatusHandler(StatusHandler handler);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace airfix::ios
