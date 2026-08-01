#pragma once

#include "airfix/render/LegacyCanvasLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::render {

namespace recovered_legacy_aircraft_hud_rolling_digits {

inline constexpr std::size_t digitCount = 4U;
inline constexpr std::uint32_t atlasWidth = 16U;
inline constexpr std::uint32_t atlasHeight = 128U;
inline constexpr float glyphWidth = 7.0F;
inline constexpr float glyphHeight = 9.0F;
inline constexpr float glyphPitchY = 11.0F;
inline constexpr float glyphTopInset = 1.0F;
inline constexpr float destinationPitchX = 8.0F;
inline constexpr float destinationTopInset = 1.0F;
inline constexpr double retentionBase = 0.0005;
inline constexpr double shortestRouteThreshold = 5.0;
inline constexpr double digitCycle = 10.0;

} // namespace recovered_legacy_aircraft_hud_rolling_digits

struct LegacyAircraftHudRollingDigitsState final {
  std::array<float, recovered_legacy_aircraft_hud_rolling_digits::digitCount>
      digitPositions{};
  float cachedElapsedSeconds{};
  float retentionFactor{1.0F};
  bool retentionFactorValid{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudRollingDigitsState &,
             const LegacyAircraftHudRollingDigitsState &) noexcept = default;
};

enum class LegacyAircraftHudRollingDigitsAdvanceStatus : std::uint8_t {
  ready,
  elapsedSecondsNotFinite,
  elapsedSecondsNegative,
  retentionFactorNotFinite,
};

struct LegacyAircraftHudRollingDigitsAdvanceResult final {
  LegacyAircraftHudRollingDigitsAdvanceStatus status{
      LegacyAircraftHudRollingDigitsAdvanceStatus::ready};
  LegacyAircraftHudRollingDigitsState state{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHudRollingDigitsAdvanceStatus::ready;
  }
};

struct LegacyAircraftHudRollingDigitSourceRect final {
  float left{};
  float top{};
  float right{};
  float bottom{};

  [[nodiscard]] friend constexpr bool operator==(
      const LegacyAircraftHudRollingDigitSourceRect &,
      const LegacyAircraftHudRollingDigitSourceRect &) noexcept = default;
};

struct LegacyAircraftHudRollingDigitCommand final {
  std::uint8_t slotIndex{};
  LegacyCanvasRect destinationRect{};
  LegacyAircraftHudRollingDigitSourceRect sourceRect{};
  std::uint32_t colourArgb{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudRollingDigitCommand &,
             const LegacyAircraftHudRollingDigitCommand &) noexcept = default;
};

enum class LegacyAircraftHudRollingDigitsPlanStatus : std::uint8_t {
  ready,
  atlasUnavailable,
  originNotFinite,
  derivedGeometryNotFinite,
};

struct LegacyAircraftHudRollingDigitsPlan final {
  LegacyAircraftHudRollingDigitsPlanStatus status{
      LegacyAircraftHudRollingDigitsPlanStatus::atlasUnavailable};
  std::array<LegacyAircraftHudRollingDigitCommand,
             recovered_legacy_aircraft_hud_rolling_digits::digitCount>
      orderedCommands{};
  std::size_t commandCount{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHudRollingDigitsPlanStatus::ready;
  }

  [[nodiscard]] const LegacyAircraftHudRollingDigitCommand *
  command(std::size_t index) const noexcept;
};

// Constructs the four retained digit positions from the native "%04d"
// representation. The original helper accepts all signed 32-bit values and
// consumes only the first four formatted characters; this boundary preserves
// that behaviour, including the leading minus sign becoming position -3.
[[nodiscard]] LegacyAircraftHudRollingDigitsState
makeLegacyAircraftHudRollingDigitsState(std::int32_t initialValue) noexcept;

// Advances one native four-digit display from an already-quantized signed
// integer. AirCraft passes a float through MSVC _ftol before this helper sees
// the decimal characters; the live producer must therefore establish that
// separate x87 conversion policy rather than asking this presentation state
// to guess it.
//
// accumulatedElapsedSeconds is the AirCraft +0x54C value shared by all HUD
// counters and reset after the enclosing HUD stage. The native helper caches
// pow(0.0005, elapsed), follows the shortest cyclic route when the distance is
// strictly greater than five, then wraps each retained position into [0,10).
[[nodiscard]] LegacyAircraftHudRollingDigitsAdvanceResult
advanceLegacyAircraftHudRollingDigits(
    LegacyAircraftHudRollingDigitsState current, std::int32_t quantizedValue,
    float accumulatedElapsedSeconds) noexcept;

// Builds the exact four-call GtScreen::Blit plan for the vertical 16x128
// digits atlas. Each visible 7x9 window advances by eleven source pixels and
// each destination advances by eight legacy UI pixels. Invalid retained
// positions are skipped independently, matching the native draw loop.
[[nodiscard]] LegacyAircraftHudRollingDigitsPlan
buildLegacyAircraftHudRollingDigitsPlan(
    const LegacyAircraftHudRollingDigitsState &state, float originX,
    float originY, std::uint32_t colourArgb, bool atlasAvailable) noexcept;

} // namespace airfix::render
