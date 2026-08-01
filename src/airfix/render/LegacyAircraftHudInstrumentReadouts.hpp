#pragma once

#include "airfix/render/LegacyAircraftHudRollingDigits.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace airfix::render {

namespace recovered_legacy_aircraft_hud_instrument_readouts {

inline constexpr float horizontalCentreOffset = 138.0F;
inline constexpr float bottomInset = 40.0F;
inline constexpr float digitsHorizontalOffset = -15.0F;
inline constexpr float digitsVerticalOffset = 10.0F;
inline constexpr std::uint32_t rightTintArgb = 0xFFFFFFFFU;
inline constexpr std::size_t maximumReadouts = 2U;

} // namespace recovered_legacy_aircraft_hud_instrument_readouts

enum class LegacyAircraftHudInstrumentReadoutSide : std::uint8_t {
  right,
  left,
};

struct LegacyAircraftHudInstrumentReadoutsInput final {
  bool activeWindowPresent{};
  bool cameraAttachedAtEntry{};
  bool typeHudEnabled{};
  bool cameraAttachedAfterLayout{};
  std::uint32_t screenWidth{};
  std::uint32_t screenHeight{};
  float accumulatedElapsedSeconds{};

  bool rightDigitStateAvailable{};
  LegacyAircraftHudRollingDigitsState rightDigitState{};
  std::int32_t quantizedVectorMagnitudeTimesHundred{};

  bool leftDigitStateAvailable{};
  LegacyAircraftHudRollingDigitsState leftDigitState{};
  std::int32_t quantizedRemainingRatioPercent{};
  std::uint32_t leftTintArgb{0xFFFFFFFFU};

  bool rollingDigitAtlasAvailable{};
};

enum class LegacyAircraftHudInstrumentReadoutsPlanStatus : std::uint8_t {
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
  digitPlanFailed,
};

struct LegacyAircraftHudInstrumentReadoutPlan final {
  LegacyAircraftHudInstrumentReadoutSide side{
      LegacyAircraftHudInstrumentReadoutSide::right};
  LegacyAircraftHudRollingDigitsPlan digits{};
};

struct LegacyAircraftHudInstrumentReadoutsPlan final {
  LegacyAircraftHudInstrumentReadoutsPlanStatus status{
      LegacyAircraftHudInstrumentReadoutsPlanStatus::activeWindowPresent};
  std::array<LegacyAircraftHudInstrumentReadoutPlan,
             recovered_legacy_aircraft_hud_instrument_readouts::maximumReadouts>
      orderedReadouts{};
  std::size_t readoutCount{};
  std::uint32_t sourceScreenWidth{};
  std::uint32_t sourceScreenHeight{};
  LegacyAircraftHudRollingDigitsState rightDigitState{};
  LegacyAircraftHudRollingDigitsState leftDigitState{};

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyAircraftHudInstrumentReadoutsPlanStatus::ready;
  }

  [[nodiscard]] const LegacyAircraftHudInstrumentReadoutPlan *
  readout(std::size_t index) const noexcept;
};

// Reconstructs the two four-digit readouts immediately following the analog
// instrument pair in AirCraft's HUD stage. Native order is right then left.
// Their logical origins are relative to the same instrument centres, while
// both consume the enclosing stage's one shared elapsed-time snapshot.
//
// The right source is already quantized from 100*length(vtable-slot-0x54
// vector). The left source is already quantized from
// 100*(AirCraft+0x400)/(AirCraft+0x3F8)+0.5. Both conversions cross the
// original external _ftol boundary, so this visual-only plan accepts signed
// int32 values and does not guess the live x87 rounding policy or stronger
// gameplay labels.
//
// State advancement is atomic across both available readouts and is retained
// even when the optional atlas cannot draw. This allocation-free boundary
// owns no actor, scheduler, render-event producer, resource lookup, backend,
// or 3D render-target policy.
[[nodiscard]] LegacyAircraftHudInstrumentReadoutsPlan
buildLegacyAircraftHudInstrumentReadoutsPlan(
    const LegacyAircraftHudInstrumentReadoutsInput &input) noexcept;

} // namespace airfix::render
