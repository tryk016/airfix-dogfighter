#include "airfix/render/LegacyAircraftHudElapsedClock.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace airfix::render {

LegacyAircraftHudElapsedClockAdvanceResult
LegacyAircraftHudElapsedClockState::advance(
    const float refreshDeltaSeconds) noexcept {
  if (stageInProgress()) {
    return {
        .status = LegacyAircraftHudElapsedClockAdvanceStatus::stageInProgress,
        .accumulatedElapsedSeconds = accumulatedElapsedSeconds_,
    };
  }
  if (!std::isfinite(refreshDeltaSeconds)) {
    return {
        .status =
            LegacyAircraftHudElapsedClockAdvanceStatus::refreshDeltaNotFinite,
        .accumulatedElapsedSeconds = accumulatedElapsedSeconds_,
    };
  }
  if (refreshDeltaSeconds < 0.0F) {
    return {
        .status =
            LegacyAircraftHudElapsedClockAdvanceStatus::refreshDeltaNegative,
        .accumulatedElapsedSeconds = accumulatedElapsedSeconds_,
    };
  }

  const double accumulatedWide =
      static_cast<double>(accumulatedElapsedSeconds_);
  const double deltaWide = static_cast<double>(refreshDeltaSeconds);
  const double sumWide = accumulatedWide + deltaWide;
  if (!std::isfinite(sumWide) ||
      sumWide > static_cast<double>(std::numeric_limits<float>::max())) {
    return {
        .status = LegacyAircraftHudElapsedClockAdvanceStatus::
            accumulatedElapsedNotFinite,
        .accumulatedElapsedSeconds = accumulatedElapsedSeconds_,
    };
  }

  const float next = static_cast<float>(sumWide);
  accumulatedElapsedSeconds_ = next;
  return {
      .status = LegacyAircraftHudElapsedClockAdvanceStatus::advanced,
      .accumulatedElapsedSeconds = accumulatedElapsedSeconds_,
  };
}

LegacyAircraftHudElapsedClockBeginResult
LegacyAircraftHudElapsedClockState::beginHudStage(
    const LegacyAircraftHudElapsedClockGates &gates) noexcept {
  if (stageInProgress()) {
    return {
        .status =
            LegacyAircraftHudElapsedClockBeginStatus::stageAlreadyInProgress};
  }
  if (gates.activeWindowPresent) {
    return {.status =
                LegacyAircraftHudElapsedClockBeginStatus::activeWindowPresent};
  }
  if (!gates.cameraAttachedAtEntry) {
    return {
        .status =
            LegacyAircraftHudElapsedClockBeginStatus::cameraNotAttachedAtEntry};
  }
  if (!gates.typeHudEnabled) {
    return {.status =
                LegacyAircraftHudElapsedClockBeginStatus::typeHudDisabled};
  }
  if (!gates.cameraAttachedAfterLayout) {
    return {
        .status =
            LegacyAircraftHudElapsedClockBeginStatus::cameraDetachedBeforeDraw};
  }
  if (lastIssuedTransactionToken_ ==
      std::numeric_limits<std::uint64_t>::max()) {
    return {.status = LegacyAircraftHudElapsedClockBeginStatus::
                transactionTokenExhausted};
  }

  ++lastIssuedTransactionToken_;
  activeTransactionToken_ = lastIssuedTransactionToken_;
  activeSnapshotElapsedSeconds_ = accumulatedElapsedSeconds_;
  return {
      .status = LegacyAircraftHudElapsedClockBeginStatus::ready,
      .snapshot =
          LegacyAircraftHudElapsedClockStageSnapshot{
              .transactionToken = activeTransactionToken_,
              .accumulatedElapsedSeconds = activeSnapshotElapsedSeconds_,
          },
  };
}

LegacyAircraftHudElapsedClockEndStatus
LegacyAircraftHudElapsedClockState::commitHudStage(
    const LegacyAircraftHudElapsedClockStageSnapshot &snapshot) noexcept {
  if (!stageInProgress()) {
    return LegacyAircraftHudElapsedClockEndStatus::noStageInProgress;
  }
  if (!matchesActiveStage(snapshot)) {
    return LegacyAircraftHudElapsedClockEndStatus::stageMismatch;
  }

  accumulatedElapsedSeconds_ = 0.0F;
  activeTransactionToken_ = 0U;
  activeSnapshotElapsedSeconds_ = 0.0F;
  return LegacyAircraftHudElapsedClockEndStatus::committed;
}

LegacyAircraftHudElapsedClockEndStatus
LegacyAircraftHudElapsedClockState::abortHudStage(
    const LegacyAircraftHudElapsedClockStageSnapshot &snapshot) noexcept {
  if (!stageInProgress()) {
    return LegacyAircraftHudElapsedClockEndStatus::noStageInProgress;
  }
  if (!matchesActiveStage(snapshot)) {
    return LegacyAircraftHudElapsedClockEndStatus::stageMismatch;
  }

  activeTransactionToken_ = 0U;
  activeSnapshotElapsedSeconds_ = 0.0F;
  return LegacyAircraftHudElapsedClockEndStatus::aborted;
}

bool LegacyAircraftHudElapsedClockState::matchesActiveStage(
    const LegacyAircraftHudElapsedClockStageSnapshot &snapshot) const noexcept {
  return snapshot.transactionToken == activeTransactionToken_ &&
         std::bit_cast<std::uint32_t>(snapshot.accumulatedElapsedSeconds) ==
             std::bit_cast<std::uint32_t>(activeSnapshotElapsedSeconds_);
}

} // namespace airfix::render
