#include "airfix/render/LegacyAircraftHudWeaponPanels.hpp"

#include <cmath>

namespace airfix::render {
namespace {

using namespace recovered_legacy_aircraft_hud_weapon_panels;

[[nodiscard]] LegacyAircraftHudWeaponPanelsPlan
failure(const LegacyAircraftHudWeaponPanelsPlanStatus status,
        const LegacyAircraftHudWeaponPanelsInput &input) noexcept {
  return {
      .status = status,
      .primaryDigitState = input.primary.digitState,
      .secondaryDigitState = input.secondary.digitState,
  };
}

[[nodiscard]] bool finiteRect(const LegacyCanvasRect &rectangle) noexcept {
  return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
         std::isfinite(rectangle.width) && std::isfinite(rectangle.height) &&
         rectangle.width > 0.0F && rectangle.height > 0.0F;
}

[[nodiscard]] bool
append(LegacyAircraftHudWeaponPanelsPlan &plan,
       const LegacyAircraftHudWeaponPanelCommand &command) noexcept {
  if (!finiteRect(command.destinationRect) ||
      (command.textured() && !finiteRect(command.sourceRect))) {
    return false;
  }
  if (plan.commandCount >= plan.orderedCommands.size()) {
    return false;
  }
  plan.orderedCommands[plan.commandCount++] = command;
  return true;
}

[[nodiscard]] bool appendBackground(LegacyAircraftHudWeaponPanelsPlan &plan,
                                    const LegacyAircraftHudWeaponPanelSlot slot,
                                    const float originX, const float originY,
                                    const bool available) noexcept {
  return !available ||
         append(
             plan,
             {
                 .slot = slot,
                 .kind = LegacyAircraftHudWeaponPanelCommandKind::background,
                 .destinationRect = {originX, originY, panelWidth, panelHeight},
                 .sourceRect = {0.0F, 0.0F, panelWidth, panelHeight},
                 .colourArgb = whiteArgb,
             });
}

[[nodiscard]] bool
appendSlot(LegacyAircraftHudWeaponPanelsPlan &plan,
           const LegacyAircraftHudWeaponPanelSlot slot,
           const LegacyAircraftHudWeaponPanelSlotInput &input,
           const float originX, const float originY,
           const bool rollingDigitAtlasAvailable,
           LegacyAircraftHudRollingDigitsState &nextDigitState) noexcept {
  if (!input.weaponPresent) {
    return true;
  }

  if (input.digitStateAvailable && rollingDigitAtlasAvailable) {
    const auto digits = buildLegacyAircraftHudRollingDigitsPlan(
        nextDigitState, originX + digitsOffsetX, originY + digitsOffsetY,
        whiteArgb, true);
    if (!digits.ready()) {
      return false;
    }
    for (std::size_t index = 0U; index < digits.commandCount; ++index) {
      const auto *digit = digits.command(index);
      if (digit == nullptr ||
          !append(plan,
                  {
                      .slot = slot,
                      .kind = LegacyAircraftHudWeaponPanelCommandKind::digit,
                      .destinationRect = digit->destinationRect,
                      .sourceRect =
                          {digit->sourceRect.left, digit->sourceRect.top,
                           digit->sourceRect.right - digit->sourceRect.left,
                           digit->sourceRect.bottom - digit->sourceRect.top},
                      .colourArgb = digit->colourArgb,
                      .digitSlotIndex = digit->slotIndex,
                  })) {
        return false;
      }
    }
  }

  if (!input.iconCatalogMatch) {
    return true;
  }
  if (input.iconTextureAvailable &&
      !append(plan, {
                        .slot = slot,
                        .kind = LegacyAircraftHudWeaponPanelCommandKind::icon,
                        .destinationRect = {originX + iconOffsetX,
                                            originY + iconOffsetY, iconWidth,
                                            iconHeight},
                        .sourceRect = {0.0F, 0.0F, iconWidth, iconHeight},
                        .colourArgb = whiteArgb,
                        .textureIndex = input.iconTextureIndex,
                    })) {
    return false;
  }
  return append(
      plan,
      {
          .slot = slot,
          .kind = LegacyAircraftHudWeaponPanelCommandKind::statusOverlay,
          .destinationRect = {originX + iconOffsetX, originY + iconOffsetY,
                              statusWidth, statusHeight},
          .colourArgb =
              legacyAircraftHudWeaponStatusColour(input.quantizedStatusIndex),
      });
}

} // namespace

const LegacyAircraftHudWeaponPanelCommand *
LegacyAircraftHudWeaponPanelsPlan::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

LegacyAircraftHudWeaponPanelsPlan buildLegacyAircraftHudWeaponPanelsPlan(
    const LegacyAircraftHudWeaponPanelsInput &input) noexcept {
  if (input.activeWindowPresent) {
    return failure(LegacyAircraftHudWeaponPanelsPlanStatus::activeWindowPresent,
                   input);
  }
  if (!input.cameraAttachedAtEntry) {
    return failure(
        LegacyAircraftHudWeaponPanelsPlanStatus::cameraNotAttachedAtEntry,
        input);
  }
  if (!input.typeHudEnabled) {
    return failure(LegacyAircraftHudWeaponPanelsPlanStatus::typeHudDisabled,
                   input);
  }
  if (!input.cameraAttachedAfterLayout) {
    return failure(
        LegacyAircraftHudWeaponPanelsPlanStatus::cameraDetachedBeforeDraw,
        input);
  }
  if (input.screenWidth == 0U || input.screenHeight == 0U) {
    return failure(LegacyAircraftHudWeaponPanelsPlanStatus::invalidScreenExtent,
                   input);
  }
  if (!std::isfinite(input.accumulatedElapsedSeconds)) {
    return failure(
        LegacyAircraftHudWeaponPanelsPlanStatus::elapsedSecondsNotFinite,
        input);
  }
  if (input.accumulatedElapsedSeconds < 0.0F) {
    return failure(
        LegacyAircraftHudWeaponPanelsPlanStatus::elapsedSecondsNegative, input);
  }

  auto primaryDigits = input.primary.digitState;
  auto secondaryDigits = input.secondary.digitState;
  const auto advance = [&](const LegacyAircraftHudWeaponPanelSlotInput &slot,
                           LegacyAircraftHudRollingDigitsState &state) {
    if (!slot.weaponPresent || !slot.digitStateAvailable) {
      return true;
    }
    const auto result = advanceLegacyAircraftHudRollingDigits(
        state, slot.quantizedAmmo, input.accumulatedElapsedSeconds);
    if (!result.ready()) {
      return false;
    }
    state = result.state;
    return true;
  };
  if (!advance(input.primary, primaryDigits) ||
      !advance(input.secondary, secondaryDigits)) {
    return failure(LegacyAircraftHudWeaponPanelsPlanStatus::digitAdvanceFailed,
                   input);
  }

  const float width = static_cast<float>(input.screenWidth);
  const float height = static_cast<float>(input.screenHeight);
  const float primaryX = width * 0.5F + primaryPanelCentreOffset;
  const float secondaryX = width * 0.5F + secondaryPanelCentreOffset;
  const float originY = height - panelBottomInset;
  if (!std::isfinite(width) || !std::isfinite(height) ||
      !std::isfinite(primaryX) || !std::isfinite(secondaryX) ||
      !std::isfinite(originY)) {
    return failure(
        LegacyAircraftHudWeaponPanelsPlanStatus::derivedGeometryNotFinite,
        input);
  }

  LegacyAircraftHudWeaponPanelsPlan plan{
      .status = LegacyAircraftHudWeaponPanelsPlanStatus::ready,
      .sourceScreenWidth = input.screenWidth,
      .sourceScreenHeight = input.screenHeight,
      .primaryDigitState = primaryDigits,
      .secondaryDigitState = secondaryDigits,
  };
  if (!appendBackground(plan, LegacyAircraftHudWeaponPanelSlot::primary,
                        primaryX, originY,
                        input.primaryBackgroundTextureAvailable) ||
      !appendBackground(plan, LegacyAircraftHudWeaponPanelSlot::secondary,
                        secondaryX, originY,
                        input.secondaryBackgroundTextureAvailable) ||
      !appendSlot(plan, LegacyAircraftHudWeaponPanelSlot::primary,
                  input.primary, primaryX, originY,
                  input.rollingDigitAtlasAvailable, plan.primaryDigitState) ||
      !appendSlot(plan, LegacyAircraftHudWeaponPanelSlot::secondary,
                  input.secondary, secondaryX, originY,
                  input.rollingDigitAtlasAvailable, plan.secondaryDigitState)) {
    return failure(
        LegacyAircraftHudWeaponPanelsPlanStatus::derivedGeometryNotFinite,
        input);
  }
  return plan;
}

} // namespace airfix::render
