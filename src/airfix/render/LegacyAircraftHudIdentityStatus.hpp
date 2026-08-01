#pragma once

#include "airfix/render/LegacyAircraftHudRollingDigits.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::render {

namespace recovered_legacy_aircraft_hud_identity_status {

inline constexpr float narrowScreenTop = 6.0F;
inline constexpr float wideScreenBottomInset = 136.0F;
inline constexpr float aircraftIconX = 50.0F;
inline constexpr float aircraftIconTopOffset = 11.0F;
inline constexpr float aircraftIconWidth = 64.0F;
inline constexpr float aircraftIconHeight = 64.0F;
inline constexpr float healthDigitsX = 57.0F;
inline constexpr float healthDigitsTopOffset = 78.0F;
inline constexpr float teamBadgeCentreOffset = -32.0F;
inline constexpr float teamBadgeBottomInset = 72.0F;
inline constexpr float teamBadgeWidth = 64.0F;
inline constexpr float teamBadgeHeight = 64.0F;
inline constexpr float technologyDigitsCentreOffset = -16.0F;
inline constexpr float technologyDigitsBottomInset = 32.0F;
inline constexpr std::uint32_t whiteArgb = 0xFFFFFFFFU;
inline constexpr std::size_t maximumCommands =
    2U + 2U * recovered_legacy_aircraft_hud_rolling_digits::digitCount;

} // namespace recovered_legacy_aircraft_hud_identity_status

enum class LegacyAircraftHudIdentityStatusCommandKind : std::uint8_t {
  aircraftIcon,
  healthDigit,
  teamBadge,
  technologyDigit,
};

enum class LegacyAircraftHudTeamBadge : std::uint8_t {
  technologyStar,
  technologyCross,
};

struct LegacyAircraftHudIdentityStatusInput final {
  bool activeWindowPresent{};
  bool cameraAttachedAtEntry{};
  bool typeHudEnabled{};
  bool cameraAttachedAfterLayout{};
  std::uint32_t screenWidth{};
  std::uint32_t screenHeight{};
  float accumulatedElapsedSeconds{};

  bool aircraftIconCatalogMatch{};
  bool aircraftIconTextureAvailable{};
  std::uint32_t aircraftIconTextureIndex{};

  bool healthDigitStateAvailable{};
  LegacyAircraftHudRollingDigitsState healthDigitState{};
  // Native code computes displayedHealth * 100 / maximumHealth + 0.5 and
  // converts it through the unresolved process-wide MSVC _ftol policy inside
  // the rolling-number helper. The producer supplies that signed result.
  std::int32_t quantizedHealthPercent{};

  std::int32_t teamId{};
  bool technologyStarTextureAvailable{};
  bool technologyCrossTextureAvailable{};

  bool technologyDigitStateAvailable{};
  LegacyAircraftHudRollingDigitsState technologyDigitState{};
  // AirCraft +0x418 is the technology level. Native code first converts the
  // retained int32 to binary32 and the helper converts it back through _ftol;
  // the producer owns that conversion boundary.
  std::int32_t quantizedTechnologyLevel{};

  bool rollingDigitAtlasAvailable{};
};

enum class LegacyAircraftHudIdentityStatusPlanStatus : std::uint8_t {
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

struct LegacyAircraftHudIdentityStatusCommand final {
  LegacyAircraftHudIdentityStatusCommandKind kind{
      LegacyAircraftHudIdentityStatusCommandKind::aircraftIcon};
  LegacyAircraftHudTeamBadge teamBadge{
      LegacyAircraftHudTeamBadge::technologyStar};
  LegacyCanvasRect destinationRect{};
  LegacyCanvasRect sourceRect{};
  std::uint32_t colourArgb{};
  std::uint32_t textureIndex{};
  std::uint8_t digitSlotIndex{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudIdentityStatusCommand &,
             const LegacyAircraftHudIdentityStatusCommand &) noexcept = default;
};

struct LegacyAircraftHudIdentityStatusPlan final {
  LegacyAircraftHudIdentityStatusPlanStatus status{
      LegacyAircraftHudIdentityStatusPlanStatus::activeWindowPresent};
  std::array<LegacyAircraftHudIdentityStatusCommand,
             recovered_legacy_aircraft_hud_identity_status::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};
  std::uint32_t sourceScreenWidth{};
  std::uint32_t sourceScreenHeight{};
  LegacyAircraftHudRollingDigitsState healthDigitState{};
  LegacyAircraftHudRollingDigitsState technologyDigitState{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHudIdentityStatusPlanStatus::ready;
  }

  [[nodiscard]] const LegacyAircraftHudIdentityStatusCommand *
  command(std::size_t index) const noexcept;
};

// Reconstructs the final four subsections of AirCraft's native HUD stage in
// exact order: optional aircraft icon, animated health percentage, optional
// team badge, and animated technology level. The result stays in the legacy
// logical UI domain and owns neither simulation nor live event scheduling.
[[nodiscard]] LegacyAircraftHudIdentityStatusPlan
buildLegacyAircraftHudIdentityStatusPlan(
    const LegacyAircraftHudIdentityStatusInput &input) noexcept;

} // namespace airfix::render
