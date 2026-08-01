#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::render {

namespace recovered_legacy_aircraft_hud_elapsed_clock {

// Five static calls to the rolling-digit advance helper exist in the native
// HUD stage. The weapon-panel call is inside a two-slot loop, so six retained
// displays consume the same AirCraft+0x54C snapshot.
inline constexpr std::size_t rollingDigitConsumerCount = 6U;

} // namespace recovered_legacy_aircraft_hud_elapsed_clock

struct LegacyAircraftHudElapsedClockGates final {
  bool activeWindowPresent{};
  bool cameraAttachedAtEntry{};
  bool typeHudEnabled{};
  bool cameraAttachedAfterLayout{};
};

struct LegacyAircraftHudElapsedClockStageSnapshot final {
  std::uint64_t transactionToken{};
  float accumulatedElapsedSeconds{};

  [[nodiscard]] constexpr std::optional<float>
  elapsedSecondsForRollingDigitConsumer(
      const std::size_t consumerIndex) const noexcept {
    if (consumerIndex >= recovered_legacy_aircraft_hud_elapsed_clock::
                             rollingDigitConsumerCount) {
      return std::nullopt;
    }
    return accumulatedElapsedSeconds;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const LegacyAircraftHudElapsedClockStageSnapshot &,
      const LegacyAircraftHudElapsedClockStageSnapshot &) noexcept = default;
};

enum class LegacyAircraftHudElapsedClockAdvanceStatus : std::uint8_t {
  advanced,
  stageInProgress,
  refreshDeltaNotFinite,
  refreshDeltaNegative,
  accumulatedElapsedNotFinite,
};

struct LegacyAircraftHudElapsedClockAdvanceResult final {
  LegacyAircraftHudElapsedClockAdvanceStatus status{
      LegacyAircraftHudElapsedClockAdvanceStatus::advanced};
  float accumulatedElapsedSeconds{};

  [[nodiscard]] constexpr bool advanced() const noexcept {
    return status == LegacyAircraftHudElapsedClockAdvanceStatus::advanced;
  }
};

enum class LegacyAircraftHudElapsedClockBeginStatus : std::uint8_t {
  ready,
  stageAlreadyInProgress,
  activeWindowPresent,
  cameraNotAttachedAtEntry,
  typeHudDisabled,
  cameraDetachedBeforeDraw,
  transactionTokenExhausted,
};

struct LegacyAircraftHudElapsedClockBeginResult final {
  LegacyAircraftHudElapsedClockBeginStatus status{
      LegacyAircraftHudElapsedClockBeginStatus::activeWindowPresent};
  std::optional<LegacyAircraftHudElapsedClockStageSnapshot> snapshot{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHudElapsedClockBeginStatus::ready &&
           snapshot.has_value();
  }
};

enum class LegacyAircraftHudElapsedClockEndStatus : std::uint8_t {
  committed,
  aborted,
  noStageInProgress,
  stageMismatch,
};

// Single-thread-confined lifecycle owner for the AirCraft+0x54C HUD elapsed
// accumulator. Construction establishes exact +0. Each accepted vehicle
// refresh adds one already-scheduled binary32 delta. An accepted HUD stage
// takes one immutable snapshot for all six rolling-number consumers and
// clears the accumulator only when that complete stage commits.
//
// This dormant boundary owns no scheduler, render-event publication, live
// gates, counter producers, or backend calls. Double staging models the
// startup-compatible x87 PC53/RNE FADD followed by the native binary32 spill;
// it is not a claim about an unobserved process-wide runtime control word.
class LegacyAircraftHudElapsedClockState final {
public:
  constexpr LegacyAircraftHudElapsedClockState() noexcept = default;

  [[nodiscard]] constexpr float accumulatedElapsedSeconds() const noexcept {
    return accumulatedElapsedSeconds_;
  }

  [[nodiscard]] constexpr bool stageInProgress() const noexcept {
    return activeTransactionToken_ != 0U;
  }

  [[nodiscard]] LegacyAircraftHudElapsedClockAdvanceResult
  advance(float refreshDeltaSeconds) noexcept;

  [[nodiscard]] LegacyAircraftHudElapsedClockBeginResult
  beginHudStage(const LegacyAircraftHudElapsedClockGates &gates) noexcept;

  [[nodiscard]] LegacyAircraftHudElapsedClockEndStatus commitHudStage(
      const LegacyAircraftHudElapsedClockStageSnapshot &snapshot) noexcept;

  [[nodiscard]] LegacyAircraftHudElapsedClockEndStatus abortHudStage(
      const LegacyAircraftHudElapsedClockStageSnapshot &snapshot) noexcept;

private:
  [[nodiscard]] bool
  matchesActiveStage(const LegacyAircraftHudElapsedClockStageSnapshot &snapshot)
      const noexcept;

  float accumulatedElapsedSeconds_{};
  std::uint64_t lastIssuedTransactionToken_{};
  std::uint64_t activeTransactionToken_{};
  float activeSnapshotElapsedSeconds_{};
};

} // namespace airfix::render
