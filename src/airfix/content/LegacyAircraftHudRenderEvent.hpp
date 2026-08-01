#pragma once

#include "airfix/content/LegacyAircraftHealthGaugeSubmission.hpp"
#include "airfix/content/LegacyAircraftHudIdentityStatusSubmission.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentReadoutsSubmission.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentsSubmission.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelsSubmission.hpp"
#include "airfix/render/LegacyAircraftHudElapsedClock.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

struct LegacyAircraftHudHealthGaugeSource final {
  float displayedHealth{};
  float maximumHealth{};
  bool armourMeterTextureAvailable{};
  bool armourTextureAvailable{};
};

struct LegacyAircraftHudInstrumentSource final {
  float rightNormalizedValue{};
  float leftNormalizedValue{};
  std::uint32_t leftTintArgb{0xFFFFFFFFU};
  bool rightFaceTextureAvailable{};
  bool leftFaceTextureAvailable{};
  bool indicatorTextureAvailable{};
};

struct LegacyAircraftHudInstrumentReadoutSource final {
  bool rightDigitStateAvailable{};
  render::LegacyAircraftHudRollingDigitsState rightDigitState{};
  std::int32_t quantizedVectorMagnitudeTimesHundred{};
  bool leftDigitStateAvailable{};
  render::LegacyAircraftHudRollingDigitsState leftDigitState{};
  std::int32_t quantizedRemainingRatioPercent{};
  std::uint32_t leftTintArgb{0xFFFFFFFFU};
  bool rollingDigitAtlasAvailable{};
};

struct LegacyAircraftHudWeaponPanelSource final {
  bool primaryBackgroundTextureAvailable{};
  bool secondaryBackgroundTextureAvailable{};
  bool rollingDigitAtlasAvailable{};
  render::LegacyAircraftHudWeaponPanelSlotInput primary{};
  render::LegacyAircraftHudWeaponPanelSlotInput secondary{};
};

struct LegacyAircraftHudIdentityStatusSource final {
  bool aircraftIconCatalogMatch{};
  bool aircraftIconTextureAvailable{};
  std::uint32_t aircraftIconTextureIndex{};
  bool healthDigitStateAvailable{};
  render::LegacyAircraftHudRollingDigitsState healthDigitState{};
  std::int32_t quantizedHealthPercent{};
  std::int32_t teamId{};
  bool technologyStarTextureAvailable{};
  bool technologyCrossTextureAvailable{};
  bool technologyDigitStateAvailable{};
  render::LegacyAircraftHudRollingDigitsState technologyDigitState{};
  std::int32_t quantizedTechnologyLevel{};
  bool rollingDigitAtlasAvailable{};
};

// One coherent source snapshot for the complete recovered AirCraft HUD stage.
// Common gates and logical extent are supplied once; the composer injects one
// elapsed-clock snapshot into all six possible rolling-number consumers.
struct LegacyAircraftHudRenderEventInput final {
  render::LegacyAircraftHudElapsedClockGates gates{};
  std::uint32_t screenWidth{};
  std::uint32_t screenHeight{};
  LegacyAircraftHudInstrumentSource instruments{};
  LegacyAircraftHudInstrumentReadoutSource instrumentReadouts{};
  LegacyAircraftHudWeaponPanelSource weaponPanels{};
  LegacyAircraftHudHealthGaugeSource healthGauge{};
  LegacyAircraftHudIdentityStatusSource identityStatus{};
};

struct LegacyAircraftHudRollingDigitStateUpdates final {
  bool rightInstrumentReadoutAvailable{};
  render::LegacyAircraftHudRollingDigitsState rightInstrumentReadout{};
  bool leftInstrumentReadoutAvailable{};
  render::LegacyAircraftHudRollingDigitsState leftInstrumentReadout{};
  bool primaryWeaponAmmoAvailable{};
  render::LegacyAircraftHudRollingDigitsState primaryWeaponAmmo{};
  bool secondaryWeaponAmmoAvailable{};
  render::LegacyAircraftHudRollingDigitsState secondaryWeaponAmmo{};
  bool healthPercentAvailable{};
  render::LegacyAircraftHudRollingDigitsState healthPercent{};
  bool technologyLevelAvailable{};
  render::LegacyAircraftHudRollingDigitsState technologyLevel{};
};

// Structural order is the native order. Backends must not sort these fields:
// instruments -> readouts -> weapon panels -> health gauge -> identity/status.
struct LegacyAircraftHudRenderEvent final {
  std::uint64_t stageTransactionToken{};
  float accumulatedElapsedSeconds{};
  float uiScalePercent{100.0F};
  LegacyAircraftHudInstrumentsSubmission instruments;
  LegacyAircraftHudInstrumentReadoutsSubmission instrumentReadouts;
  LegacyAircraftHudWeaponPanelsSubmission weaponPanels;
  LegacyAircraftHealthGaugeSubmission healthGauge;
  LegacyAircraftHudIdentityStatusSubmission identityStatus;

  [[nodiscard]] bool belongsTo(
      const LoadedLegacyAircraftHudInstrumentTextureSet &instrumentTextures,
      const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
      const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
      const LoadedLegacyAircraftHealthGaugeTextureSet &healthTextures,
      const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures)
      const noexcept;

  [[nodiscard]] std::size_t totalCommandCount() const noexcept;
};

enum class LegacyAircraftHudRenderEventStatus : std::uint8_t {
  ready,
  clockBeginRejected,
  elapsedSnapshotInvalid,
  instrumentsPlanRejected,
  instrumentReadoutsPlanRejected,
  weaponPanelsPlanRejected,
  healthGaugePlanRejected,
  identityStatusPlanRejected,
  instrumentsSubmissionRejected,
  instrumentReadoutsSubmissionRejected,
  weaponPanelsSubmissionRejected,
  healthGaugeSubmissionRejected,
  identityStatusSubmissionRejected,
  eventValidationFailed,
  clockAbortFailed,
  clockCommitFailed,
};

struct LegacyAircraftHudRenderEventResult final {
  LegacyAircraftHudRenderEventStatus status{
      LegacyAircraftHudRenderEventStatus::clockBeginRejected};
  render::LegacyAircraftHudElapsedClockBeginStatus clockBeginStatus{
      render::LegacyAircraftHudElapsedClockBeginStatus::activeWindowPresent};
  render::LegacyAircraftHudElapsedClockEndStatus clockEndStatus{
      render::LegacyAircraftHudElapsedClockEndStatus::noStageInProgress};
  std::optional<LegacyAircraftHudRenderEvent> event;
  std::optional<LegacyAircraftHudRollingDigitStateUpdates> stateUpdates;

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyAircraftHudRenderEventStatus::ready &&
           clockBeginStatus ==
               render::LegacyAircraftHudElapsedClockBeginStatus::ready &&
           clockEndStatus ==
               render::LegacyAircraftHudElapsedClockEndStatus::committed &&
           event.has_value() && stateUpdates.has_value();
  }
};

// Builds and authenticates the complete recovered HUD event as one
// allocation-free logical transaction. Any plan, mapping, ownership, or
// clock failure publishes neither an event nor any of the six state updates.
// The elapsed accumulator resets only after the complete packet validates.
// This function owns no scheduler, actor lookup, backend call, or live value
// production and therefore cannot make a dormant HUD appear live.
[[nodiscard]] LegacyAircraftHudRenderEventResult
buildLegacyAircraftHudRenderEvent(
    render::LegacyAircraftHudElapsedClockState &clock,
    const LegacyAircraftHudRenderEventInput &input,
    const LoadedLegacyAircraftHudInstrumentTextureSet &instrumentTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
    const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
    const LoadedLegacyAircraftHealthGaugeTextureSet &healthTextures,
    const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures,
    const render::NativeRenderLayout &layout, float uiScalePercent) noexcept;

} // namespace airfix::content
