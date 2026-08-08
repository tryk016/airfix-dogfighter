#include "airfix/audio/AudioOutputProbe.hpp"

#include <limits>

namespace airfix::audio {

AudioOutputProbeSamples makeAudioOutputProbeSamples() noexcept {
  AudioOutputProbeSamples samples{};
  constexpr std::int32_t amplitude = 6'000;
  constexpr std::size_t period = 120U;
  constexpr std::size_t halfPeriod = period / 2U;

  for (std::size_t index = 0U; index < samples.size(); ++index) {
    const auto phase = index % period;
    const auto ramp = phase < halfPeriod ? phase : period - phase;
    const auto centered = static_cast<std::int32_t>(ramp * 2U) -
                          static_cast<std::int32_t>(halfPeriod);
    samples[index] = static_cast<std::int16_t>(
        (centered * amplitude) / static_cast<std::int32_t>(halfPeriod));
  }
  return samples;
}

AudioOutputProbeState AudioOutputProbePlan::state() const noexcept {
  return state_;
}

std::uint64_t AudioOutputProbePlan::lastSequence() const noexcept {
  return lastSequence_;
}

std::optional<AudioCommandBatch> AudioOutputProbePlan::beginStart() noexcept {
  if ((state_ != AudioOutputProbeState::ready &&
       state_ != AudioOutputProbeState::stopped) ||
      lastSequence_ == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }

  AudioCommandBatch batch{.sequence = lastSequence_ + 1U};
  if (!appendAudioCommand(batch, {
                                     .kind = AudioCommandKind::startVoice,
                                     .voice = audioOutputProbeVoiceId,
                                     .clip = audioOutputProbeClipId,
                                     .gain = 0.35F,
                                     .pitch = 1.0F,
                                     .looping = false,
                                 })) {
    state_ = AudioOutputProbeState::failed;
    return std::nullopt;
  }
  lastSequence_ = batch.sequence;
  state_ = AudioOutputProbeState::awaitingStart;
  return batch;
}

std::optional<AudioCommandBatch> AudioOutputProbePlan::beginStop() noexcept {
  if (state_ != AudioOutputProbeState::playing ||
      lastSequence_ == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }

  AudioCommandBatch batch{.sequence = lastSequence_ + 1U};
  if (!appendAudioCommand(batch, {
                                     .kind = AudioCommandKind::stopVoice,
                                     .voice = audioOutputProbeVoiceId,
                                 })) {
    state_ = AudioOutputProbeState::failed;
    return std::nullopt;
  }
  lastSequence_ = batch.sequence;
  state_ = AudioOutputProbeState::awaitingStop;
  return batch;
}

bool AudioOutputProbePlan::completeSubmission(
    const std::uint64_t sequence, const bool accepted,
    const std::size_t appliedCommandCount) noexcept {
  if ((state_ != AudioOutputProbeState::awaitingStart &&
       state_ != AudioOutputProbeState::awaitingStop) ||
      sequence != lastSequence_ || !accepted || appliedCommandCount != 1U) {
    state_ = AudioOutputProbeState::failed;
    return false;
  }

  state_ = state_ == AudioOutputProbeState::awaitingStart
               ? AudioOutputProbeState::playing
               : AudioOutputProbeState::stopped;
  return true;
}

void AudioOutputProbePlan::cancel() noexcept {
  if (state_ != AudioOutputProbeState::failed) {
    state_ = AudioOutputProbeState::stopped;
  }
}

} // namespace airfix::audio
