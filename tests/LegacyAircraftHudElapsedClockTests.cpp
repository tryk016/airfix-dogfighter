#include "airfix/render/LegacyAircraftHudElapsedClock.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace airfix::render;
using namespace recovered_legacy_aircraft_hud_elapsed_clock;

static_assert(rollingDigitConsumerCount == 6U);
static_assert(noexcept(LegacyAircraftHudElapsedClockState{}));

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] constexpr LegacyAircraftHudElapsedClockGates
acceptedGates() noexcept {
  return {
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
  };
}

void testConstructionAndMultipleRefreshes() {
  LegacyAircraftHudElapsedClockState clock;
  require(std::bit_cast<std::uint32_t>(clock.accumulatedElapsedSeconds()) ==
                  0U &&
              !clock.stageInProgress(),
          "construction did not establish exact positive zero");

  const auto first = clock.advance(0.012F);
  const auto second = clock.advance(0.012F);
  const auto third = clock.advance(0.006F);
  const float expected = static_cast<float>(
      static_cast<double>(static_cast<float>(static_cast<double>(0.012F) +
                                             static_cast<double>(0.012F))) +
      static_cast<double>(0.006F));
  require(first.advanced() && second.advanced() && third.advanced() &&
              clock.accumulatedElapsedSeconds() == expected &&
              third.accumulatedElapsedSeconds == expected,
          "zero/one/many refresh accumulation changed");
}

void testAllNativeGateRejectionsRetainTime() {
  const auto expectRejected =
      [](LegacyAircraftHudElapsedClockGates gates,
         const LegacyAircraftHudElapsedClockBeginStatus expectedStatus,
         const std::string_view message) {
        LegacyAircraftHudElapsedClockState clock;
        require(clock.advance(0.125F).advanced(),
                "gate test could not establish accumulated time");
        const auto result = clock.beginHudStage(gates);
        require(!result.ready() && !result.snapshot.has_value() &&
                    result.status == expectedStatus &&
                    clock.accumulatedElapsedSeconds() == 0.125F &&
                    !clock.stageInProgress(),
                message);
      };

  auto gates = acceptedGates();
  gates.activeWindowPresent = true;
  expectRejected(gates,
                 LegacyAircraftHudElapsedClockBeginStatus::activeWindowPresent,
                 "active-window gate consumed elapsed time");

  gates = acceptedGates();
  gates.cameraAttachedAtEntry = false;
  expectRejected(
      gates, LegacyAircraftHudElapsedClockBeginStatus::cameraNotAttachedAtEntry,
      "entry-camera gate consumed elapsed time");

  gates = acceptedGates();
  gates.typeHudEnabled = false;
  expectRejected(gates,
                 LegacyAircraftHudElapsedClockBeginStatus::typeHudDisabled,
                 "type-HUD gate consumed elapsed time");

  gates = acceptedGates();
  gates.cameraAttachedAfterLayout = false;
  expectRejected(
      gates, LegacyAircraftHudElapsedClockBeginStatus::cameraDetachedBeforeDraw,
      "post-layout camera gate consumed elapsed time");

  LegacyAircraftHudElapsedClockState precedenceClock;
  require(precedenceClock.advance(0.125F).advanced(),
          "precedence test could not establish accumulated time");
  gates = {
      .activeWindowPresent = true,
      .cameraAttachedAtEntry = false,
      .typeHudEnabled = false,
      .cameraAttachedAfterLayout = false,
  };
  require(precedenceClock.beginHudStage(gates).status ==
              LegacyAircraftHudElapsedClockBeginStatus::activeWindowPresent,
          "active-window gate lost first precedence");
  gates.activeWindowPresent = false;
  require(
      precedenceClock.beginHudStage(gates).status ==
          LegacyAircraftHudElapsedClockBeginStatus::cameraNotAttachedAtEntry,
      "entry-camera gate lost second precedence");
  gates.cameraAttachedAtEntry = true;
  require(precedenceClock.beginHudStage(gates).status ==
              LegacyAircraftHudElapsedClockBeginStatus::typeHudDisabled,
          "type-HUD gate lost third precedence");
  gates.typeHudEnabled = true;
  require(precedenceClock.beginHudStage(gates).status ==
                  LegacyAircraftHudElapsedClockBeginStatus::
                      cameraDetachedBeforeDraw &&
              precedenceClock.accumulatedElapsedSeconds() == 0.125F,
          "post-layout gate lost fourth precedence or consumed time");
}

void testOneSnapshotFeedsSixConsumersAndCommitsOnce() {
  LegacyAircraftHudElapsedClockState clock;
  require(clock.advance(0.25F).advanced(),
          "accepted stage could not establish time");
  const auto begun = clock.beginHudStage(acceptedGates());
  require(begun.ready() && begun.snapshot.has_value() &&
              clock.stageInProgress(),
          "accepted gates did not begin a stage");

  const auto snapshot = *begun.snapshot;
  const auto nested = clock.beginHudStage(acceptedGates());
  require(!nested.ready() &&
              nested.status == LegacyAircraftHudElapsedClockBeginStatus::
                                   stageAlreadyInProgress &&
              !nested.snapshot.has_value(),
          "nested HUD stage was accepted");
  for (std::size_t index = 0U; index < rollingDigitConsumerCount; ++index) {
    const auto elapsed = snapshot.elapsedSecondsForRollingDigitConsumer(index);
    require(elapsed.has_value() && *elapsed == 0.25F,
            "rolling-digit consumers did not share one elapsed snapshot");
  }
  require(
      !snapshot.elapsedSecondsForRollingDigitConsumer(rollingDigitConsumerCount)
           .has_value(),
      "out-of-range rolling-digit consumer was accepted");
  require(!clock.advance(0.01F).advanced() &&
              clock.accumulatedElapsedSeconds() == 0.25F,
          "refresh mutated the clock during an active HUD stage");

  require(clock.commitHudStage(snapshot) ==
                  LegacyAircraftHudElapsedClockEndStatus::committed &&
              std::bit_cast<std::uint32_t>(clock.accumulatedElapsedSeconds()) ==
                  0U &&
              !clock.stageInProgress(),
          "accepted complete stage did not reset exact positive zero");
  require(clock.commitHudStage(snapshot) ==
              LegacyAircraftHudElapsedClockEndStatus::noStageInProgress,
          "same stage committed more than once");
}

void testAbortAndMismatchedStageNeverPartiallyCommit() {
  LegacyAircraftHudElapsedClockState clock;
  require(clock.advance(0.5F).advanced(),
          "abort test could not establish time");
  const auto first = clock.beginHudStage(acceptedGates());
  require(first.ready(), "abort test could not begin first stage");

  auto forged = *first.snapshot;
  ++forged.transactionToken;
  require(clock.commitHudStage(forged) ==
                  LegacyAircraftHudElapsedClockEndStatus::stageMismatch &&
              clock.stageInProgress() &&
              clock.accumulatedElapsedSeconds() == 0.5F,
          "mismatched token partially committed the clock");
  forged = *first.snapshot;
  forged.accumulatedElapsedSeconds = 0.25F;
  require(clock.abortHudStage(forged) ==
                  LegacyAircraftHudElapsedClockEndStatus::stageMismatch &&
              clock.stageInProgress() &&
              clock.accumulatedElapsedSeconds() == 0.5F,
          "mismatched snapshot partially aborted the clock");

  require(clock.abortHudStage(*first.snapshot) ==
                  LegacyAircraftHudElapsedClockEndStatus::aborted &&
              !clock.stageInProgress() &&
              clock.accumulatedElapsedSeconds() == 0.5F,
          "valid abort consumed accumulated time");
  const auto second = clock.beginHudStage(acceptedGates());
  require(second.ready() &&
              second.snapshot->transactionToken !=
                  first.snapshot->transactionToken &&
              second.snapshot->accumulatedElapsedSeconds == 0.5F,
          "retry after abort reused a token or lost elapsed time");
  require(clock.abortHudStage(*second.snapshot) ==
              LegacyAircraftHudElapsedClockEndStatus::aborted,
          "retry stage could not close cleanly");
}

void testInvalidRefreshDeltasFailClosed() {
  LegacyAircraftHudElapsedClockState clock;
  require(clock.advance(0.25F).advanced(),
          "invalid-input test could not establish time");

  const auto expectRejected =
      [&](const float delta,
          const LegacyAircraftHudElapsedClockAdvanceStatus expectedStatus,
          const std::string_view message) {
        const auto result = clock.advance(delta);
        require(!result.advanced() && result.status == expectedStatus &&
                    result.accumulatedElapsedSeconds == 0.25F &&
                    clock.accumulatedElapsedSeconds() == 0.25F,
                message);
      };

  expectRejected(
      std::numeric_limits<float>::quiet_NaN(),
      LegacyAircraftHudElapsedClockAdvanceStatus::refreshDeltaNotFinite,
      "NaN refresh delta mutated the clock");
  expectRejected(
      std::numeric_limits<float>::infinity(),
      LegacyAircraftHudElapsedClockAdvanceStatus::refreshDeltaNotFinite,
      "infinite refresh delta mutated the clock");
  expectRejected(
      -0.001F, LegacyAircraftHudElapsedClockAdvanceStatus::refreshDeltaNegative,
      "negative refresh delta mutated the clock");

  LegacyAircraftHudElapsedClockState overflow;
  require(overflow.advance(std::numeric_limits<float>::max()).advanced(),
          "maximum finite baseline was rejected");
  const auto overflowResult =
      overflow.advance(std::numeric_limits<float>::max());
  require(!overflowResult.advanced() &&
              overflowResult.status ==
                  LegacyAircraftHudElapsedClockAdvanceStatus::
                      accumulatedElapsedNotFinite &&
              overflow.accumulatedElapsedSeconds() ==
                  std::numeric_limits<float>::max(),
          "overflow did not roll back completely");
}

} // namespace

int main() {
  try {
    testConstructionAndMultipleRefreshes();
    testAllNativeGateRejectionsRetainTime();
    testOneSnapshotFeedsSixConsumersAndCommitsOnce();
    testAbortAndMismatchedStageNeverPartiallyCommit();
    testInvalidRefreshDeltasFailClosed();
    std::cout << "Legacy aircraft HUD elapsed-clock tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft HUD elapsed-clock test failure: "
              << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
