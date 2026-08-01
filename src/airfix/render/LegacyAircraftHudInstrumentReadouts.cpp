#include "airfix/render/LegacyAircraftHudInstrumentReadouts.hpp"

#include <cmath>

namespace airfix::render {
namespace {

using namespace recovered_legacy_aircraft_hud_instrument_readouts;

[[nodiscard]] LegacyAircraftHudInstrumentReadoutsPlan
failure(const LegacyAircraftHudInstrumentReadoutsPlanStatus status,
        const LegacyAircraftHudInstrumentReadoutsInput &input) noexcept {
  return {
      .status = status,
      .rightDigitState = input.rightDigitState,
      .leftDigitState = input.leftDigitState,
  };
}

[[nodiscard]] bool append(LegacyAircraftHudInstrumentReadoutsPlan &plan,
                          const LegacyAircraftHudInstrumentReadoutSide side,
                          const LegacyAircraftHudRollingDigitsState &state,
                          const float originX, const float originY,
                          const std::uint32_t tintArgb) noexcept {
  if (plan.readoutCount >= plan.orderedReadouts.size()) {
    return false;
  }
  const auto digits = buildLegacyAircraftHudRollingDigitsPlan(
      state, originX, originY, tintArgb, true);
  if (!digits.ready()) {
    return false;
  }
  plan.orderedReadouts[plan.readoutCount++] = {
      .side = side,
      .digits = digits,
  };
  return true;
}

} // namespace

const LegacyAircraftHudInstrumentReadoutPlan *
LegacyAircraftHudInstrumentReadoutsPlan::readout(
    const std::size_t index) const noexcept {
  if (index >= readoutCount || index >= orderedReadouts.size()) {
    return nullptr;
  }
  return &orderedReadouts[index];
}

LegacyAircraftHudInstrumentReadoutsPlan
buildLegacyAircraftHudInstrumentReadoutsPlan(
    const LegacyAircraftHudInstrumentReadoutsInput &input) noexcept {
  if (input.activeWindowPresent) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::activeWindowPresent,
        input);
  }
  if (!input.cameraAttachedAtEntry) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::cameraNotAttachedAtEntry,
        input);
  }
  if (!input.typeHudEnabled) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::typeHudDisabled, input);
  }
  if (!input.cameraAttachedAfterLayout) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::cameraDetachedBeforeDraw,
        input);
  }
  if (input.screenWidth == 0U || input.screenHeight == 0U) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::invalidScreenExtent,
        input);
  }
  if (!std::isfinite(input.accumulatedElapsedSeconds)) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::elapsedSecondsNotFinite,
        input);
  }
  if (input.accumulatedElapsedSeconds < 0.0F) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::elapsedSecondsNegative,
        input);
  }

  auto rightState = input.rightDigitState;
  auto leftState = input.leftDigitState;
  if (input.rightDigitStateAvailable) {
    const auto advanced = advanceLegacyAircraftHudRollingDigits(
        rightState, input.quantizedVectorMagnitudeTimesHundred,
        input.accumulatedElapsedSeconds);
    if (!advanced.ready()) {
      return failure(
          LegacyAircraftHudInstrumentReadoutsPlanStatus::digitAdvanceFailed,
          input);
    }
    rightState = advanced.state;
  }
  if (input.leftDigitStateAvailable) {
    const auto advanced = advanceLegacyAircraftHudRollingDigits(
        leftState, input.quantizedRemainingRatioPercent,
        input.accumulatedElapsedSeconds);
    if (!advanced.ready()) {
      return failure(
          LegacyAircraftHudInstrumentReadoutsPlanStatus::digitAdvanceFailed,
          input);
    }
    leftState = advanced.state;
  }

  const float width = static_cast<float>(input.screenWidth);
  const float height = static_cast<float>(input.screenHeight);
  const float centreY = height - bottomInset;
  const float halfWidth = width * 0.5F;
  const float rightOriginX =
      halfWidth + horizontalCentreOffset + digitsHorizontalOffset;
  const float leftOriginX =
      halfWidth - horizontalCentreOffset + digitsHorizontalOffset;
  const float originY = centreY + digitsVerticalOffset;
  if (!std::isfinite(width) || !std::isfinite(height) ||
      !std::isfinite(rightOriginX) || !std::isfinite(leftOriginX) ||
      !std::isfinite(originY)) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::derivedGeometryNotFinite,
        input);
  }

  LegacyAircraftHudInstrumentReadoutsPlan plan{
      .status = LegacyAircraftHudInstrumentReadoutsPlanStatus::ready,
      .sourceScreenWidth = input.screenWidth,
      .sourceScreenHeight = input.screenHeight,
      .rightDigitState = rightState,
      .leftDigitState = leftState,
  };
  if (input.rollingDigitAtlasAvailable &&
      ((input.rightDigitStateAvailable &&
        !append(plan, LegacyAircraftHudInstrumentReadoutSide::right,
                plan.rightDigitState, rightOriginX, originY, rightTintArgb)) ||
       (input.leftDigitStateAvailable &&
        !append(plan, LegacyAircraftHudInstrumentReadoutSide::left,
                plan.leftDigitState, leftOriginX, originY,
                input.leftTintArgb)))) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsPlanStatus::digitPlanFailed, input);
  }
  return plan;
}

} // namespace airfix::render
