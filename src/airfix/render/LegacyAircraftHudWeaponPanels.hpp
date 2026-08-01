#pragma once

#include "airfix/render/LegacyAircraftHudRollingDigits.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::render {

namespace recovered_legacy_aircraft_hud_weapon_panels {

inline constexpr float primaryPanelCentreOffset = -97.0F;
inline constexpr float secondaryPanelCentreOffset = 33.0F;
inline constexpr float panelBottomInset = 72.0F;
inline constexpr float panelWidth = 64.0F;
inline constexpr float panelHeight = 64.0F;
inline constexpr float iconOffsetX = 6.0F;
inline constexpr float iconOffsetY = 6.0F;
inline constexpr float iconWidth = 64.0F;
inline constexpr float iconHeight = 32.0F;
inline constexpr float statusWidth = 52.0F;
inline constexpr float statusHeight = 30.0F;
inline constexpr float digitsOffsetX = 16.0F;
inline constexpr float digitsOffsetY = 40.0F;
inline constexpr std::uint32_t whiteArgb = 0xFFFFFFFFU;
inline constexpr std::array<std::uint32_t, 5U> statusColoursArgb{{
    0xBF269A1AU,
    0xBF94C61CU,
    0xBFEDC610U,
    0xBFE26D00U,
    0xBFBD1700U,
}};
inline constexpr std::size_t maximumCommands =
    2U + 2U * (recovered_legacy_aircraft_hud_rolling_digits::digitCount + 2U);

} // namespace recovered_legacy_aircraft_hud_weapon_panels

enum class LegacyAircraftHudWeaponPanelSlot : std::uint8_t {
  primary,
  secondary,
};

enum class LegacyAircraftHudWeaponPanelCommandKind : std::uint8_t {
  background,
  digit,
  icon,
  statusOverlay,
};

struct LegacyAircraftHudWeaponPanelSlotInput final {
  bool weaponPresent{};
  bool digitStateAvailable{};
  LegacyAircraftHudRollingDigitsState digitState{};
  std::int32_t quantizedAmmo{};
  bool iconCatalogMatch{};
  bool iconTextureAvailable{};
  std::uint32_t iconTextureIndex{};
  std::int32_t quantizedStatusIndex{};
};

struct LegacyAircraftHudWeaponPanelsInput final {
  bool activeWindowPresent{};
  bool cameraAttachedAtEntry{};
  bool typeHudEnabled{};
  bool cameraAttachedAfterLayout{};
  std::uint32_t screenWidth{};
  std::uint32_t screenHeight{};
  float accumulatedElapsedSeconds{};
  bool primaryBackgroundTextureAvailable{};
  bool secondaryBackgroundTextureAvailable{};
  bool rollingDigitAtlasAvailable{};
  LegacyAircraftHudWeaponPanelSlotInput primary{};
  LegacyAircraftHudWeaponPanelSlotInput secondary{};
};

enum class LegacyAircraftHudWeaponPanelsPlanStatus : std::uint8_t {
  ready,
  activeWindowPresent,
  cameraNotAttachedAtEntry,
  typeHudDisabled,
  cameraDetachedBeforeDraw,
  invalidScreenExtent,
  elapsedSecondsNotFinite,
  elapsedSecondsNegative,
  digitAdvanceFailed,
  derivedGeometryNotFinite,
  commandCapacityExceeded,
};

struct LegacyAircraftHudWeaponPanelCommand final {
  LegacyAircraftHudWeaponPanelSlot slot{
      LegacyAircraftHudWeaponPanelSlot::primary};
  LegacyAircraftHudWeaponPanelCommandKind kind{
      LegacyAircraftHudWeaponPanelCommandKind::background};
  LegacyCanvasRect destinationRect{};
  LegacyCanvasRect sourceRect{};
  std::uint32_t colourArgb{};
  std::uint32_t textureIndex{};
  std::uint8_t digitSlotIndex{};

  [[nodiscard]] constexpr bool textured() const noexcept {
    return kind != LegacyAircraftHudWeaponPanelCommandKind::statusOverlay;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudWeaponPanelCommand &,
             const LegacyAircraftHudWeaponPanelCommand &) noexcept = default;
};

struct LegacyAircraftHudWeaponPanelsPlan final {
  LegacyAircraftHudWeaponPanelsPlanStatus status{
      LegacyAircraftHudWeaponPanelsPlanStatus::activeWindowPresent};
  std::array<LegacyAircraftHudWeaponPanelCommand,
             recovered_legacy_aircraft_hud_weapon_panels::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};
  std::uint32_t sourceScreenWidth{};
  std::uint32_t sourceScreenHeight{};
  LegacyAircraftHudRollingDigitsState primaryDigitState{};
  LegacyAircraftHudRollingDigitsState secondaryDigitState{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHudWeaponPanelsPlanStatus::ready;
  }

  [[nodiscard]] const LegacyAircraftHudWeaponPanelCommand *
  command(std::size_t index) const noexcept;
};

// The native AirCraft HUD receives a value already converted by MSVC _ftol,
// clamps that signed integer to [0,4], then selects one of these five exact
// AARRGGBB values. The live producer owns the unresolved x87 conversion.
[[nodiscard]] constexpr std::uint32_t legacyAircraftHudWeaponStatusColour(
    const std::int32_t quantizedIndex) noexcept {
  const auto clamped = quantizedIndex < 0 ? 0U
                       : quantizedIndex > 4
                           ? 4U
                           : static_cast<std::uint32_t>(quantizedIndex);
  return recovered_legacy_aircraft_hud_weapon_panels::statusColoursArgb
      [clamped];
}

// Reconstructs the complete two-panel subsection of AirCraft's HUD stage.
// Both fixed backgrounds are emitted before either weapon slot. Each present
// slot then preserves native order: rolling digits, optional catalog icon,
// and the destination-multiply status overlay. A catalog match emits the
// overlay even when that record has no usable icon image.
//
// The result remains in the logical 640x480 UI domain. It advances only the
// two weapon-owned rolling-digit states, using the enclosing HUD stage's
// shared elapsed value without resetting or owning that accumulator.
[[nodiscard]] LegacyAircraftHudWeaponPanelsPlan
buildLegacyAircraftHudWeaponPanelsPlan(
    const LegacyAircraftHudWeaponPanelsInput &input) noexcept;

} // namespace airfix::render
