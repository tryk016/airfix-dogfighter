#pragma once

#include "airfix/render/LegacyCanvasLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::render {

namespace recovered_legacy_aircraft_health_gauge {

inline constexpr float narrowScreenThreshold = 640.0F;
inline constexpr float narrowScreenTop = 6.0F;
inline constexpr float normalScreenBottomInset = 136.0F;
inline constexpr float textureLeft = 8.0F;
inline constexpr float centreX = 72.0F;
inline constexpr float centreYOffset = 64.0F;
inline constexpr float outerRadius = 62.0F;
inline constexpr float innerRadius = 44.0F;
inline constexpr double startAngleRadians = -0.78539818525314331;
inline constexpr float segmentStepRadians = 0.1F;
inline constexpr float piRadians = 3.1415927410125732F;
inline constexpr std::uint32_t damageMaskArgb = 0xFF000000U;
inline constexpr std::size_t maximumDamageMaskQuads = 32U;
inline constexpr std::size_t maximumCommands = maximumDamageMaskQuads + 2U;

} // namespace recovered_legacy_aircraft_health_gauge

struct LegacyAircraftHealthGaugeInput final {
  bool activeWindowPresent{};
  bool cameraAttachedAtEntry{};
  bool typeHudEnabled{};
  bool cameraAttachedAfterLayout{};
  std::uint32_t screenWidth{};
  std::uint32_t screenHeight{};
  float displayedHealth{};
  float maximumHealth{};
  bool armourMeterTextureAvailable{};
  bool armourTextureAvailable{};
};

enum class LegacyAircraftHealthGaugePlanStatus : std::uint8_t {
  ready,
  activeWindowPresent,
  cameraNotAttachedAtEntry,
  typeHudDisabled,
  cameraDetachedBeforeDraw,
  invalidScreenExtent,
  nonFiniteHealth,
  maximumHealthNotPositive,
  displayedHealthOutOfRange,
  derivedGeometryNotFinite,
  commandCapacityExceeded,
};

enum class LegacyAircraftHealthGaugeCommandKind : std::uint8_t {
  armourMeterTexture,
  damageMaskQuad,
  armourTexture,
};

struct LegacyAircraftHealthGaugeCommand final {
  LegacyAircraftHealthGaugeCommandKind kind{
      LegacyAircraftHealthGaugeCommandKind::damageMaskQuad};
  LegacyCanvasPoint textureOrigin{};
  std::array<LegacyCanvasPoint, 4U> quad{};
  std::uint32_t colourArgb{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHealthGaugeCommand &,
             const LegacyAircraftHealthGaugeCommand &) noexcept = default;
};

struct LegacyAircraftHealthGaugePlan final {
  LegacyAircraftHealthGaugePlanStatus status{
      LegacyAircraftHealthGaugePlanStatus::activeWindowPresent};
  std::array<LegacyAircraftHealthGaugeCommand,
             recovered_legacy_aircraft_health_gauge::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};
  float damageSweepRadians{};
  LegacyCanvasPoint textureOrigin{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHealthGaugePlanStatus::ready;
  }

  [[nodiscard]] const LegacyAircraftHealthGaugeCommand *
  command(std::size_t index) const noexcept;
};

// Reconstructs the complete armour/health-gauge subsection of the AirCraft
// HUD stage. Commands retain native order: optional armour_meter texture,
// one or more black damage-mask quads, then optional armour foreground.
// Coordinates remain in the legacy screen/UI domain and must be mapped by the
// modern UI layout; they never define a 3D render-target resolution.
//
// displayedHealth is the already-smoothed native HUD value, not raw gameplay
// health. This presentation-only plan does not feed simulation state back into
// gameplay. Invalid or unbounded inputs fail atomically without commands.
[[nodiscard]] LegacyAircraftHealthGaugePlan buildLegacyAircraftHealthGaugePlan(
    const LegacyAircraftHealthGaugeInput &input) noexcept;

} // namespace airfix::render
