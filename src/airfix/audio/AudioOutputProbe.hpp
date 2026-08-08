#pragma once

#include "airfix/audio/AudioCommand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::audio {

inline constexpr std::uint32_t audioOutputProbeSampleRate = 24'000U;
inline constexpr std::size_t audioOutputProbeSampleCount = 4'800U;
inline constexpr AudioClipId audioOutputProbeClipId{0xA1F1'0001U};
inline constexpr AudioVoiceId audioOutputProbeVoiceId{0xA1F1'0001U};

using AudioOutputProbeSamples =
    std::array<std::int16_t, audioOutputProbeSampleCount>;

// Produces a bounded 200 ms mono triangle tone using integer arithmetic only.
// The signal is public diagnostic data and is unrelated to original content.
[[nodiscard]] AudioOutputProbeSamples makeAudioOutputProbeSamples() noexcept;

enum class AudioOutputProbeState : std::uint8_t {
  ready,
  awaitingStart,
  playing,
  awaitingStop,
  stopped,
  failed,
};

class AudioOutputProbePlan final {
public:
  [[nodiscard]] AudioOutputProbeState state() const noexcept;
  [[nodiscard]] std::uint64_t lastSequence() const noexcept;

  [[nodiscard]] std::optional<AudioCommandBatch> beginStart() noexcept;
  [[nodiscard]] std::optional<AudioCommandBatch> beginStop() noexcept;

  // A submission commits only when the backend accepted exactly the one
  // command prepared by beginStart/beginStop. Any mismatch fails closed.
  [[nodiscard]] bool
  completeSubmission(std::uint64_t sequence, bool accepted,
                     std::size_t appliedCommandCount) noexcept;

  // Lifecycle cancellation never replays a diagnostic tone. It retains the
  // monotonic sequence so a later explicit request cannot alias an old one.
  void cancel() noexcept;

private:
  AudioOutputProbeState state_{AudioOutputProbeState::ready};
  std::uint64_t lastSequence_{};
};

} // namespace airfix::audio
