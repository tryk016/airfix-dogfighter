#include "airfix/render/LegacyAircraftHudRollingDigits.hpp"

#include <cmath>
#include <cstdio>

namespace airfix::render {
namespace {

using namespace recovered_legacy_aircraft_hud_rolling_digits;

[[nodiscard]] std::array<int, digitCount>
formatNativeDigits(const std::int32_t value) noexcept {
  static_assert(sizeof(int) == sizeof(std::int32_t));
  std::array<char, 12U> text{};
  const int written =
      std::snprintf(text.data(), text.size(), "%04d", static_cast<int>(value));
  if (written < static_cast<int>(digitCount)) {
    return {};
  }

  std::array<int, digitCount> digits{};
  for (std::size_t index = 0U; index < digits.size(); ++index) {
    digits[index] = static_cast<int>(text[index]) - static_cast<int>('0');
  }
  return digits;
}

[[nodiscard]] float advanceDigit(const float current, const int rawTarget,
                                 const float retention) noexcept {
  int target = rawTarget;
  const double distance =
      std::fabs(static_cast<double>(current) - static_cast<double>(target));
  if (distance > shortestRouteThreshold) {
    target += target < 5 ? 10 : -10;
  }

  // Native arithmetic loads binary32 state and the cached binary32 factor
  // into x87, performs the two products and addition, then spills once to
  // binary32. Double staging retains that operation order under the portable
  // PC53 policy without claiming live-process x87 bit identity.
  const double retentionWide = static_cast<double>(retention);
  const double targetWeight = 1.0 - retentionWide;
  const double targetTerm = targetWeight * static_cast<double>(target);
  const double currentTerm = retentionWide * static_cast<double>(current);
  float result = static_cast<float>(currentTerm + targetTerm);

  if (result < 0.0F) {
    result = static_cast<float>(static_cast<double>(result) + digitCycle);
  } else if (result >= static_cast<float>(digitCycle)) {
    result = static_cast<float>(static_cast<double>(result) - digitCycle);
  }
  if (!(result >= 0.0F && result < static_cast<float>(digitCycle))) {
    result = 0.0F;
  }
  return result;
}

[[nodiscard]] bool finite(const LegacyCanvasRect &rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height);
}

[[nodiscard]] bool
finite(const LegacyAircraftHudRollingDigitSourceRect &rect) noexcept {
  return std::isfinite(rect.left) && std::isfinite(rect.top) &&
         std::isfinite(rect.right) && std::isfinite(rect.bottom);
}

} // namespace

const LegacyAircraftHudRollingDigitCommand *
LegacyAircraftHudRollingDigitsPlan::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

LegacyAircraftHudRollingDigitsState makeLegacyAircraftHudRollingDigitsState(
    const std::int32_t initialValue) noexcept {
  const auto digits = formatNativeDigits(initialValue);
  LegacyAircraftHudRollingDigitsState state;
  for (std::size_t index = 0U; index < state.digitPositions.size(); ++index) {
    state.digitPositions[index] = static_cast<float>(digits[index]);
  }
  return state;
}

LegacyAircraftHudRollingDigitsAdvanceResult
advanceLegacyAircraftHudRollingDigits(
    LegacyAircraftHudRollingDigitsState current,
    const std::int32_t quantizedValue,
    const float accumulatedElapsedSeconds) noexcept {
  LegacyAircraftHudRollingDigitsAdvanceResult result{.state = current};
  if (!std::isfinite(accumulatedElapsedSeconds)) {
    result.status =
        LegacyAircraftHudRollingDigitsAdvanceStatus::elapsedSecondsNotFinite;
    return result;
  }
  if (accumulatedElapsedSeconds < 0.0F) {
    result.status =
        LegacyAircraftHudRollingDigitsAdvanceStatus::elapsedSecondsNegative;
    return result;
  }

  if (!result.state.retentionFactorValid ||
      accumulatedElapsedSeconds != result.state.cachedElapsedSeconds) {
    const double retention =
        std::pow(retentionBase, static_cast<double>(accumulatedElapsedSeconds));
    const float spilledRetention = static_cast<float>(retention);
    if (!std::isfinite(spilledRetention)) {
      result.status =
          LegacyAircraftHudRollingDigitsAdvanceStatus::retentionFactorNotFinite;
      return result;
    }
    result.state.cachedElapsedSeconds = accumulatedElapsedSeconds;
    result.state.retentionFactor = spilledRetention;
    result.state.retentionFactorValid = true;
  }

  if (!std::isfinite(result.state.retentionFactor)) {
    result.status =
        LegacyAircraftHudRollingDigitsAdvanceStatus::retentionFactorNotFinite;
    return result;
  }

  const auto targets = formatNativeDigits(quantizedValue);
  for (std::size_t index = 0U; index < result.state.digitPositions.size();
       ++index) {
    result.state.digitPositions[index] =
        advanceDigit(result.state.digitPositions[index], targets[index],
                     result.state.retentionFactor);
  }
  return result;
}

LegacyAircraftHudRollingDigitsPlan buildLegacyAircraftHudRollingDigitsPlan(
    const LegacyAircraftHudRollingDigitsState &state, const float originX,
    const float originY, const std::uint32_t colourArgb,
    const bool atlasAvailable) noexcept {
  if (!atlasAvailable) {
    return {.status =
                LegacyAircraftHudRollingDigitsPlanStatus::atlasUnavailable};
  }
  if (!std::isfinite(originX) || !std::isfinite(originY)) {
    return {.status =
                LegacyAircraftHudRollingDigitsPlanStatus::originNotFinite};
  }

  LegacyAircraftHudRollingDigitsPlan plan{
      .status = LegacyAircraftHudRollingDigitsPlanStatus::ready};
  for (std::size_t index = 0U; index < state.digitPositions.size(); ++index) {
    const float position = state.digitPositions[index];
    if (!(position >= 0.0F && position < static_cast<float>(digitCycle))) {
      continue;
    }

    const float sourceTop = position * glyphPitchY + glyphTopInset;
    const LegacyAircraftHudRollingDigitCommand command{
        .slotIndex = static_cast<std::uint8_t>(index),
        .destinationRect =
            {
                .x = originX + static_cast<float>(index) * destinationPitchX,
                .y = originY + destinationTopInset,
                .width = glyphWidth,
                .height = glyphHeight,
            },
        .sourceRect =
            {
                .left = 0.0F,
                .top = sourceTop,
                .right = glyphWidth,
                .bottom = sourceTop + glyphHeight,
            },
        .colourArgb = colourArgb,
    };
    if (!finite(command.destinationRect) || !finite(command.sourceRect)) {
      return {.status = LegacyAircraftHudRollingDigitsPlanStatus::
                  derivedGeometryNotFinite};
    }
    plan.orderedCommands[plan.commandCount++] = command;
  }
  return plan;
}

} // namespace airfix::render
