#include "airfix/render/LegacyAircraftHudIdentityStatus.hpp"

#include <cmath>

namespace airfix::render {
namespace {

using namespace recovered_legacy_aircraft_hud_identity_status;

[[nodiscard]] LegacyAircraftHudIdentityStatusPlan
failure(const LegacyAircraftHudIdentityStatusPlanStatus status,
        const LegacyAircraftHudIdentityStatusInput &input) noexcept {
  return {
      .status = status,
      .healthDigitState = input.healthDigitState,
      .technologyDigitState = input.technologyDigitState,
  };
}

[[nodiscard]] bool finiteRect(const LegacyCanvasRect &rectangle) noexcept {
  return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
         std::isfinite(rectangle.width) && std::isfinite(rectangle.height) &&
         rectangle.width > 0.0F && rectangle.height > 0.0F;
}

[[nodiscard]] bool
append(LegacyAircraftHudIdentityStatusPlan &plan,
       const LegacyAircraftHudIdentityStatusCommand &command) noexcept {
  if (!finiteRect(command.destinationRect) || !finiteRect(command.sourceRect) ||
      plan.commandCount >= plan.orderedCommands.size()) {
    return false;
  }
  plan.orderedCommands[plan.commandCount++] = command;
  return true;
}

[[nodiscard]] bool
appendDigits(LegacyAircraftHudIdentityStatusPlan &plan,
             const LegacyAircraftHudRollingDigitsState &state,
             const float originX, const float originY,
             const LegacyAircraftHudIdentityStatusCommandKind kind,
             const bool atlasAvailable) noexcept {
  if (!atlasAvailable) {
    return true;
  }
  const auto digits = buildLegacyAircraftHudRollingDigitsPlan(
      state, originX, originY, whiteArgb, true);
  if (!digits.ready()) {
    return false;
  }
  for (std::size_t index = 0U; index < digits.commandCount; ++index) {
    const auto *digit = digits.command(index);
    if (digit == nullptr ||
        !append(
            plan,
            {
                .kind = kind,
                .destinationRect = digit->destinationRect,
                .sourceRect = {digit->sourceRect.left, digit->sourceRect.top,
                               digit->sourceRect.right - digit->sourceRect.left,
                               digit->sourceRect.bottom -
                                   digit->sourceRect.top},
                .colourArgb = digit->colourArgb,
                .digitSlotIndex = digit->slotIndex,
            })) {
      return false;
    }
  }
  return true;
}

} // namespace

const LegacyAircraftHudIdentityStatusCommand *
LegacyAircraftHudIdentityStatusPlan::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

LegacyAircraftHudIdentityStatusPlan buildLegacyAircraftHudIdentityStatusPlan(
    const LegacyAircraftHudIdentityStatusInput &input) noexcept {
  if (input.activeWindowPresent) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::activeWindowPresent, input);
  }
  if (!input.cameraAttachedAtEntry) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::cameraNotAttachedAtEntry,
        input);
  }
  if (!input.typeHudEnabled) {
    return failure(LegacyAircraftHudIdentityStatusPlanStatus::typeHudDisabled,
                   input);
  }
  if (!input.cameraAttachedAfterLayout) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::cameraDetachedBeforeDraw,
        input);
  }
  if (input.screenWidth == 0U || input.screenHeight == 0U) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::invalidScreenExtent, input);
  }
  if (!std::isfinite(input.accumulatedElapsedSeconds)) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::elapsedSecondsNotFinite,
        input);
  }
  if (input.accumulatedElapsedSeconds < 0.0F) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::elapsedSecondsNegative,
        input);
  }

  auto healthDigits = input.healthDigitState;
  auto technologyDigits = input.technologyDigitState;
  if (input.healthDigitStateAvailable) {
    const auto advanced = advanceLegacyAircraftHudRollingDigits(
        healthDigits, input.quantizedHealthPercent,
        input.accumulatedElapsedSeconds);
    if (!advanced.ready()) {
      return failure(
          LegacyAircraftHudIdentityStatusPlanStatus::digitAdvanceFailed, input);
    }
    healthDigits = advanced.state;
  }
  if (input.technologyDigitStateAvailable) {
    const auto advanced = advanceLegacyAircraftHudRollingDigits(
        technologyDigits, input.quantizedTechnologyLevel,
        input.accumulatedElapsedSeconds);
    if (!advanced.ready()) {
      return failure(
          LegacyAircraftHudIdentityStatusPlanStatus::digitAdvanceFailed, input);
    }
    technologyDigits = advanced.state;
  }

  const float width = static_cast<float>(input.screenWidth);
  const float height = static_cast<float>(input.screenHeight);
  const float top = input.screenWidth < 640U ? narrowScreenTop
                                             : height - wideScreenBottomInset;
  const float teamX = width * 0.5F + teamBadgeCentreOffset;
  const float teamY = height - teamBadgeBottomInset;
  const float technologyX = width * 0.5F + technologyDigitsCentreOffset;
  const float technologyY = height - technologyDigitsBottomInset;
  if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(top) ||
      !std::isfinite(teamX) || !std::isfinite(teamY) ||
      !std::isfinite(technologyX) || !std::isfinite(technologyY)) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::derivedGeometryNotFinite,
        input);
  }

  LegacyAircraftHudIdentityStatusPlan plan{
      .status = LegacyAircraftHudIdentityStatusPlanStatus::ready,
      .sourceScreenWidth = input.screenWidth,
      .sourceScreenHeight = input.screenHeight,
      .healthDigitState = healthDigits,
      .technologyDigitState = technologyDigits,
  };
  if (input.aircraftIconCatalogMatch && input.aircraftIconTextureAvailable &&
      !append(
          plan,
          {
              .kind = LegacyAircraftHudIdentityStatusCommandKind::aircraftIcon,
              .destinationRect = {aircraftIconX, top + aircraftIconTopOffset,
                                  aircraftIconWidth, aircraftIconHeight},
              .sourceRect = {0.0F, 0.0F, aircraftIconWidth, aircraftIconHeight},
              .colourArgb = whiteArgb,
              .textureIndex = input.aircraftIconTextureIndex,
          })) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::derivedGeometryNotFinite,
        input);
  }

  if (input.healthDigitStateAvailable &&
      !appendDigits(plan, plan.healthDigitState, healthDigitsX,
                    top + healthDigitsTopOffset,
                    LegacyAircraftHudIdentityStatusCommandKind::healthDigit,
                    input.rollingDigitAtlasAvailable)) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::derivedGeometryNotFinite,
        input);
  }

  const bool technologyStar = input.teamId == 1;
  const bool selectedTeamTextureAvailable =
      technologyStar ? input.technologyStarTextureAvailable
                     : input.technologyCrossTextureAvailable;
  if (selectedTeamTextureAvailable &&
      !append(
          plan,
          {
              .kind = LegacyAircraftHudIdentityStatusCommandKind::teamBadge,
              .teamBadge = technologyStar
                               ? LegacyAircraftHudTeamBadge::technologyStar
                               : LegacyAircraftHudTeamBadge::technologyCross,
              .destinationRect = {teamX, teamY, teamBadgeWidth,
                                  teamBadgeHeight},
              .sourceRect = {0.0F, 0.0F, teamBadgeWidth, teamBadgeHeight},
              .colourArgb = whiteArgb,
          })) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::derivedGeometryNotFinite,
        input);
  }

  if (input.technologyDigitStateAvailable &&
      !appendDigits(plan, plan.technologyDigitState, technologyX, technologyY,
                    LegacyAircraftHudIdentityStatusCommandKind::technologyDigit,
                    input.rollingDigitAtlasAvailable)) {
    return failure(
        LegacyAircraftHudIdentityStatusPlanStatus::derivedGeometryNotFinite,
        input);
  }
  return plan;
}

} // namespace airfix::render
