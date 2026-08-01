#pragma once

#include "airfix/render/LegacyCanvasLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::render {

namespace recovered_legacy_aircraft_hud_instruments {

inline constexpr float horizontalCentreOffset = 138.0F;
inline constexpr float bottomInset = 40.0F;
inline constexpr float faceHalfExtent = 32.0F;
inline constexpr float faceExtent = 64.0F;
inline constexpr float indicatorSourceWidth = 7.0F;
inline constexpr float indicatorSourceHeight = 30.0F;
inline constexpr float indicatorPivotX = 3.5F;
inline constexpr float indicatorPivotY = 23.0F;
inline constexpr float indicatorCentrePixelOffset = 0.5F;
inline constexpr float indicatorAngleSpanRadians = 3.926990985870361328125F;
inline constexpr std::uint32_t rightInstrumentTintArgb = 0xFFFFFFFFU;
inline constexpr std::size_t maximumCommands = 4U;

} // namespace recovered_legacy_aircraft_hud_instruments

enum class LegacyAircraftHudInstrumentSide : std::uint8_t {
  right,
  left,
};

enum class LegacyAircraftHudInstrumentCommandKind : std::uint8_t {
  face,
  indicator,
};

struct LegacyAircraftHudInstrumentsInput final {
  bool activeWindowPresent{};
  bool cameraAttachedAtEntry{};
  bool typeHudEnabled{};
  bool cameraAttachedAfterLayout{};
  std::uint32_t screenWidth{};
  std::uint32_t screenHeight{};
  float rightNormalizedValue{};
  float leftNormalizedValue{};
  std::uint32_t leftTintArgb{0xFFFFFFFFU};
  bool rightFaceTextureAvailable{};
  bool leftFaceTextureAvailable{};
  bool indicatorTextureAvailable{};
};

enum class LegacyAircraftHudInstrumentsPlanStatus : std::uint8_t {
  ready,
  activeWindowPresent,
  cameraNotAttachedAtEntry,
  typeHudDisabled,
  cameraDetachedBeforeDraw,
  invalidScreenExtent,
  normalizedValueNotFinite,
  derivedGeometryNotFinite,
  commandCapacityExceeded,
};

struct LegacyAircraftHudInstrumentCommand final {
  LegacyAircraftHudInstrumentSide side{LegacyAircraftHudInstrumentSide::right};
  LegacyAircraftHudInstrumentCommandKind kind{
      LegacyAircraftHudInstrumentCommandKind::face};
  std::array<LegacyCanvasPoint, 4U> destinationQuad{};
  LegacyCanvasRect sourceRect{};
  std::uint32_t tintArgb{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudInstrumentCommand &,
             const LegacyAircraftHudInstrumentCommand &) noexcept = default;
};

struct LegacyAircraftHudInstrumentsPlan final {
  LegacyAircraftHudInstrumentsPlanStatus status{
      LegacyAircraftHudInstrumentsPlanStatus::activeWindowPresent};
  std::array<LegacyAircraftHudInstrumentCommand,
             recovered_legacy_aircraft_hud_instruments::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};
  std::uint32_t sourceScreenWidth{};
  std::uint32_t sourceScreenHeight{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHudInstrumentsPlanStatus::ready;
  }

  [[nodiscard]] const LegacyAircraftHudInstrumentCommand *
  command(std::size_t index) const noexcept;
};

// Reconstructs the complete two-clock AirCraft HUD subsection. Native order
// is right face -> right indicator -> left face -> left indicator, with each
// missing texture skipped independently. Inputs are the presentation values
// already produced by the actor: the smoothed right-hand value and the
// left-hand remaining/initial ratio. They are intentionally not clamped
// because the original helper extrapolates every finite input.
//
// Coordinates remain in the legacy UI domain and never select the physical
// render-target resolution. The result is allocation-free and visual-only.
[[nodiscard]] LegacyAircraftHudInstrumentsPlan
buildLegacyAircraftHudInstrumentsPlan(
    const LegacyAircraftHudInstrumentsInput &input) noexcept;

} // namespace airfix::render
