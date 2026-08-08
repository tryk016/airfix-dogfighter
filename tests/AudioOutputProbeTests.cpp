#include "airfix/audio/AudioOutputProbe.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using airfix::audio::AudioCommandKind;
using airfix::audio::AudioOutputProbePlan;
using airfix::audio::AudioOutputProbeState;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testPublicToneIsBoundedAndDeterministic() {
  const auto first = airfix::audio::makeAudioOutputProbeSamples();
  const auto second = airfix::audio::makeAudioOutputProbeSamples();
  require(first == second, "probe samples are not deterministic");
  require(first.size() == airfix::audio::audioOutputProbeSampleCount,
          "probe sample count changed");
  require(first.size() * sizeof(first.front()) < 16U * 1024U,
          "probe exceeded its public PCM budget");
  const auto [minimum, maximum] =
      std::minmax_element(first.begin(), first.end());
  require(*minimum == -6'000 && *maximum == 6'000, "probe amplitude changed");
  require(std::any_of(first.begin(), first.end(),
                      [](const auto sample) { return sample != 0; }),
          "probe is silent");

  const airfix::audio::Pcm16ClipView clip{
      .id = airfix::audio::audioOutputProbeClipId,
      .sampleRate = airfix::audio::audioOutputProbeSampleRate,
      .channelCount = 1U,
      .interleavedSamples = first,
  };
  require(airfix::audio::validPcm16Clip(clip),
          "probe did not produce valid PCM16");
}

void testExplicitStartStopUsesMonotonicOneCommandBatches() {
  AudioOutputProbePlan plan;
  const auto start = plan.beginStart();
  require(start.has_value() && start->sequence == 1U &&
              start->commandCount == 1U,
          "start batch is invalid");
  require(start->commands[0].kind == AudioCommandKind::startVoice &&
              !start->commands[0].looping,
          "probe start must be a one-shot");
  require(!plan.beginStart().has_value(), "concurrent start was accepted");
  require(plan.completeSubmission(start->sequence, true, 1U),
          "accepted start did not commit");

  const auto stop = plan.beginStop();
  require(stop.has_value() && stop->sequence == 2U &&
              stop->commandCount == 1U &&
              stop->commands[0].kind == AudioCommandKind::stopVoice,
          "stop batch is invalid");
  require(plan.completeSubmission(stop->sequence, true, 1U),
          "accepted stop did not commit");

  const auto restart = plan.beginStart();
  require(restart.has_value() && restart->sequence == 3U,
          "explicit restart did not retain sequence monotonicity");
}

void testSubmissionMismatchFailsClosed() {
  AudioOutputProbePlan plan;
  const auto start = plan.beginStart();
  require(start.has_value(), "start could not be prepared");
  require(!plan.completeSubmission(start->sequence + 1U, true, 1U),
          "wrong sequence was accepted");
  require(plan.state() == AudioOutputProbeState::failed,
          "wrong sequence did not fail terminally");
  require(!plan.beginStart().has_value(), "failed plan restarted");

  AudioOutputProbePlan rejected;
  const auto rejectedStart = rejected.beginStart();
  require(rejectedStart.has_value(), "rejected start could not be prepared");
  require(!rejected.completeSubmission(rejectedStart->sequence, false, 0U),
          "backend rejection was accepted");
  require(rejected.state() == AudioOutputProbeState::failed,
          "backend rejection did not fail terminally");
}

void testLifecycleCancellationNeverReplays() {
  AudioOutputProbePlan plan;
  const auto start = plan.beginStart();
  require(start.has_value() &&
              plan.completeSubmission(start->sequence, true, 1U),
          "start setup failed");
  plan.cancel();
  require(plan.state() == AudioOutputProbeState::stopped,
          "cancellation did not stop the probe");
  require(!plan.beginStop().has_value(),
          "cancelled probe emitted a stale stop");
  const auto explicitRestart = plan.beginStart();
  require(explicitRestart.has_value() && explicitRestart->sequence == 2U,
          "cancellation replayed or reset the sequence");
}

} // namespace

int main() {
  try {
    testPublicToneIsBoundedAndDeterministic();
    testExplicitStartStopUsesMonotonicOneCommandBatches();
    testSubmissionMismatchFailsClosed();
    testLifecycleCancellationNeverReplays();
    std::cout << "Audio output probe tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Audio output probe tests failed: " << error.what() << '\n';
    return 1;
  }
}
