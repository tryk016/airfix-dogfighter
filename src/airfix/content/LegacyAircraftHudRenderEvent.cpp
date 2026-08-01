#include "airfix/content/LegacyAircraftHudRenderEvent.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <utility>

namespace airfix::content {
namespace {

using render::LegacyAircraftHudElapsedClockBeginStatus;
using render::LegacyAircraftHudElapsedClockEndStatus;

[[nodiscard]] LegacyAircraftHudRenderEventResult failure(
    const LegacyAircraftHudRenderEventStatus status,
    const LegacyAircraftHudElapsedClockBeginStatus beginStatus,
    const LegacyAircraftHudElapsedClockEndStatus endStatus =
        LegacyAircraftHudElapsedClockEndStatus::noStageInProgress) noexcept {
  return {
      .status = status,
      .clockBeginStatus = beginStatus,
      .clockEndStatus = endStatus,
      .event = std::nullopt,
      .stateUpdates = std::nullopt,
  };
}

[[nodiscard]] bool sameFloatBits(const float left, const float right) noexcept {
  return std::bit_cast<std::uint32_t>(left) ==
         std::bit_cast<std::uint32_t>(right);
}

} // namespace

bool LegacyAircraftHudRenderEvent::belongsTo(
    const LoadedLegacyAircraftHudInstrumentTextureSet &instrumentTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
    const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
    const LoadedLegacyAircraftHealthGaugeTextureSet &healthTextures,
    const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures)
    const noexcept {
  if (stageTransactionToken == 0U ||
      !std::isfinite(accumulatedElapsedSeconds) ||
      accumulatedElapsedSeconds < 0.0F || !std::isfinite(uiScalePercent) ||
      !instrumentTextures.valid() || !digitTextures.valid() ||
      !weaponTextures.valid() || !healthTextures.valid() ||
      !identityTextures.valid()) {
    return false;
  }

  const auto &revision = instrumentTextures.revision;
  const auto &identity = instrumentTextures.transactionIdentity;
  if (!identity.valid() || digitTextures.revision != revision ||
      weaponTextures.revision != revision ||
      healthTextures.revision != revision ||
      identityTextures.revision != revision ||
      digitTextures.transactionIdentity != identity ||
      weaponTextures.transactionIdentity != identity ||
      healthTextures.transactionIdentity != identity ||
      identityTextures.transactionIdentity != identity ||
      instruments.revision != revision ||
      instrumentReadouts.revision != revision ||
      weaponPanels.revision != revision || healthGauge.revision != revision ||
      identityStatus.revision != revision ||
      instruments.transactionIdentity != identity ||
      instrumentReadouts.transactionIdentity != identity ||
      weaponPanels.transactionIdentity != identity ||
      healthGauge.transactionIdentity != identity ||
      identityStatus.transactionIdentity != identity ||
      instruments.uiScalePercent != uiScalePercent ||
      instrumentReadouts.uiScalePercent != uiScalePercent ||
      weaponPanels.uiScalePercent != uiScalePercent ||
      healthGauge.uiScalePercent != uiScalePercent ||
      identityStatus.uiScalePercent != uiScalePercent) {
    return false;
  }

  return instruments.belongsTo(instrumentTextures) &&
         instrumentReadouts.belongsTo(digitTextures) &&
         weaponPanels.belongsTo(weaponTextures, digitTextures) &&
         healthGauge.belongsTo(healthTextures) &&
         identityStatus.belongsTo(identityTextures, digitTextures);
}

std::size_t LegacyAircraftHudRenderEvent::totalCommandCount() const noexcept {
  if (instrumentReadouts.readoutCount >
      instrumentReadouts.orderedReadouts.size()) {
    return 0U;
  }
  std::size_t count = instruments.commandCount + weaponPanels.commandCount +
                      healthGauge.commandCount + identityStatus.commandCount;
  for (std::size_t index = 0U; index < instrumentReadouts.readoutCount;
       ++index) {
    const auto &readout = instrumentReadouts.orderedReadouts[index];
    if (readout.digits.has_value()) {
      count += readout.digits->commandCount;
    }
  }
  return count;
}

LegacyAircraftHudRenderEventResult buildLegacyAircraftHudRenderEvent(
    render::LegacyAircraftHudElapsedClockState &clock,
    const LegacyAircraftHudRenderEventInput &input,
    const LoadedLegacyAircraftHudInstrumentTextureSet &instrumentTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
    const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
    const LoadedLegacyAircraftHealthGaugeTextureSet &healthTextures,
    const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures,
    const render::NativeRenderLayout &layout,
    const float uiScalePercent) noexcept {
  const auto begun = clock.beginHudStage(input.gates);
  if (!begun.ready()) {
    return failure(LegacyAircraftHudRenderEventStatus::clockBeginRejected,
                   begun.status);
  }
  const auto snapshot = *begun.snapshot;
  const auto abortFailure =
      [&](const LegacyAircraftHudRenderEventStatus status) {
        const auto ended = clock.abortHudStage(snapshot);
        return failure(
            ended == LegacyAircraftHudElapsedClockEndStatus::aborted
                ? status
                : LegacyAircraftHudRenderEventStatus::clockAbortFailed,
            begun.status, ended);
      };

  std::array<float, render::recovered_legacy_aircraft_hud_elapsed_clock::
                        rollingDigitConsumerCount>
      elapsedByConsumer{};
  for (std::size_t index = 0U; index < elapsedByConsumer.size(); ++index) {
    const auto elapsed = snapshot.elapsedSecondsForRollingDigitConsumer(index);
    if (!elapsed.has_value() || !std::isfinite(*elapsed) || *elapsed < 0.0F ||
        (index != 0U && !sameFloatBits(*elapsed, elapsedByConsumer[0U]))) {
      return abortFailure(
          LegacyAircraftHudRenderEventStatus::elapsedSnapshotInvalid);
    }
    elapsedByConsumer[index] = *elapsed;
  }

  const auto &gates = input.gates;
  auto instrumentsPlan = render::buildLegacyAircraftHudInstrumentsPlan({
      .activeWindowPresent = gates.activeWindowPresent,
      .cameraAttachedAtEntry = gates.cameraAttachedAtEntry,
      .typeHudEnabled = gates.typeHudEnabled,
      .cameraAttachedAfterLayout = gates.cameraAttachedAfterLayout,
      .screenWidth = input.screenWidth,
      .screenHeight = input.screenHeight,
      .rightNormalizedValue = input.instruments.rightNormalizedValue,
      .leftNormalizedValue = input.instruments.leftNormalizedValue,
      .leftTintArgb = input.instruments.leftTintArgb,
      .rightFaceTextureAvailable = input.instruments.rightFaceTextureAvailable,
      .leftFaceTextureAvailable = input.instruments.leftFaceTextureAvailable,
      .indicatorTextureAvailable = input.instruments.indicatorTextureAvailable,
  });
  if (!instrumentsPlan.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::instrumentsPlanRejected);
  }

  auto readoutsPlan = render::buildLegacyAircraftHudInstrumentReadoutsPlan({
      .activeWindowPresent = gates.activeWindowPresent,
      .cameraAttachedAtEntry = gates.cameraAttachedAtEntry,
      .typeHudEnabled = gates.typeHudEnabled,
      .cameraAttachedAfterLayout = gates.cameraAttachedAfterLayout,
      .screenWidth = input.screenWidth,
      .screenHeight = input.screenHeight,
      .accumulatedElapsedSeconds = elapsedByConsumer[0U],
      .rightDigitStateAvailable =
          input.instrumentReadouts.rightDigitStateAvailable,
      .rightDigitState = input.instrumentReadouts.rightDigitState,
      .quantizedVectorMagnitudeTimesHundred =
          input.instrumentReadouts.quantizedVectorMagnitudeTimesHundred,
      .leftDigitStateAvailable =
          input.instrumentReadouts.leftDigitStateAvailable,
      .leftDigitState = input.instrumentReadouts.leftDigitState,
      .quantizedRemainingRatioPercent =
          input.instrumentReadouts.quantizedRemainingRatioPercent,
      .leftTintArgb = input.instrumentReadouts.leftTintArgb,
      .rollingDigitAtlasAvailable =
          input.instrumentReadouts.rollingDigitAtlasAvailable,
  });
  if (!readoutsPlan.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::instrumentReadoutsPlanRejected);
  }

  auto weaponPanelsPlan = render::buildLegacyAircraftHudWeaponPanelsPlan({
      .activeWindowPresent = gates.activeWindowPresent,
      .cameraAttachedAtEntry = gates.cameraAttachedAtEntry,
      .typeHudEnabled = gates.typeHudEnabled,
      .cameraAttachedAfterLayout = gates.cameraAttachedAfterLayout,
      .screenWidth = input.screenWidth,
      .screenHeight = input.screenHeight,
      .accumulatedElapsedSeconds = elapsedByConsumer[2U],
      .primaryBackgroundTextureAvailable =
          input.weaponPanels.primaryBackgroundTextureAvailable,
      .secondaryBackgroundTextureAvailable =
          input.weaponPanels.secondaryBackgroundTextureAvailable,
      .rollingDigitAtlasAvailable =
          input.weaponPanels.rollingDigitAtlasAvailable,
      .primary = input.weaponPanels.primary,
      .secondary = input.weaponPanels.secondary,
  });
  if (!weaponPanelsPlan.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::weaponPanelsPlanRejected);
  }

  auto healthGaugePlan = render::buildLegacyAircraftHealthGaugePlan({
      .activeWindowPresent = gates.activeWindowPresent,
      .cameraAttachedAtEntry = gates.cameraAttachedAtEntry,
      .typeHudEnabled = gates.typeHudEnabled,
      .cameraAttachedAfterLayout = gates.cameraAttachedAfterLayout,
      .screenWidth = input.screenWidth,
      .screenHeight = input.screenHeight,
      .displayedHealth = input.healthGauge.displayedHealth,
      .maximumHealth = input.healthGauge.maximumHealth,
      .armourMeterTextureAvailable =
          input.healthGauge.armourMeterTextureAvailable,
      .armourTextureAvailable = input.healthGauge.armourTextureAvailable,
  });
  if (!healthGaugePlan.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::healthGaugePlanRejected);
  }

  auto identityStatusPlan = render::buildLegacyAircraftHudIdentityStatusPlan({
      .activeWindowPresent = gates.activeWindowPresent,
      .cameraAttachedAtEntry = gates.cameraAttachedAtEntry,
      .typeHudEnabled = gates.typeHudEnabled,
      .cameraAttachedAfterLayout = gates.cameraAttachedAfterLayout,
      .screenWidth = input.screenWidth,
      .screenHeight = input.screenHeight,
      .accumulatedElapsedSeconds = elapsedByConsumer[4U],
      .aircraftIconCatalogMatch = input.identityStatus.aircraftIconCatalogMatch,
      .aircraftIconTextureAvailable =
          input.identityStatus.aircraftIconTextureAvailable,
      .aircraftIconTextureIndex = input.identityStatus.aircraftIconTextureIndex,
      .healthDigitStateAvailable =
          input.identityStatus.healthDigitStateAvailable,
      .healthDigitState = input.identityStatus.healthDigitState,
      .quantizedHealthPercent = input.identityStatus.quantizedHealthPercent,
      .teamId = input.identityStatus.teamId,
      .technologyStarTextureAvailable =
          input.identityStatus.technologyStarTextureAvailable,
      .technologyCrossTextureAvailable =
          input.identityStatus.technologyCrossTextureAvailable,
      .technologyDigitStateAvailable =
          input.identityStatus.technologyDigitStateAvailable,
      .technologyDigitState = input.identityStatus.technologyDigitState,
      .quantizedTechnologyLevel = input.identityStatus.quantizedTechnologyLevel,
      .rollingDigitAtlasAvailable =
          input.identityStatus.rollingDigitAtlasAvailable,
  });
  if (!identityStatusPlan.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::identityStatusPlanRejected);
  }

  auto instrumentsSubmission = buildLegacyAircraftHudInstrumentsSubmission(
      instrumentsPlan, instrumentTextures, layout, uiScalePercent);
  if (!instrumentsSubmission.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::instrumentsSubmissionRejected);
  }
  auto readoutsSubmission = buildLegacyAircraftHudInstrumentReadoutsSubmission(
      readoutsPlan, digitTextures, layout, uiScalePercent);
  if (!readoutsSubmission.ready()) {
    return abortFailure(LegacyAircraftHudRenderEventStatus::
                            instrumentReadoutsSubmissionRejected);
  }
  auto weaponPanelsSubmission = buildLegacyAircraftHudWeaponPanelsSubmission(
      weaponPanelsPlan, weaponTextures, digitTextures, layout, uiScalePercent);
  if (!weaponPanelsSubmission.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::weaponPanelsSubmissionRejected);
  }
  auto healthGaugeSubmission = buildLegacyAircraftHealthGaugeSubmission(
      healthGaugePlan, healthTextures, layout, uiScalePercent);
  if (!healthGaugeSubmission.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::healthGaugeSubmissionRejected);
  }
  auto identityStatusSubmission =
      buildLegacyAircraftHudIdentityStatusSubmission(
          identityStatusPlan, identityTextures, digitTextures, layout,
          uiScalePercent);
  if (!identityStatusSubmission.ready()) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::identityStatusSubmissionRejected);
  }

  LegacyAircraftHudRenderEvent event{
      .stageTransactionToken = snapshot.transactionToken,
      .accumulatedElapsedSeconds = snapshot.accumulatedElapsedSeconds,
      .uiScalePercent = uiScalePercent,
      .instruments = std::move(*instrumentsSubmission.submission),
      .instrumentReadouts = std::move(*readoutsSubmission.submission),
      .weaponPanels = std::move(*weaponPanelsSubmission.submission),
      .healthGauge = std::move(*healthGaugeSubmission.submission),
      .identityStatus = std::move(*identityStatusSubmission.submission),
  };
  if (!event.belongsTo(instrumentTextures, digitTextures, weaponTextures,
                       healthTextures, identityTextures)) {
    return abortFailure(
        LegacyAircraftHudRenderEventStatus::eventValidationFailed);
  }

  LegacyAircraftHudRollingDigitStateUpdates updates{
      .rightInstrumentReadoutAvailable =
          input.instrumentReadouts.rightDigitStateAvailable,
      .rightInstrumentReadout = readoutsPlan.rightDigitState,
      .leftInstrumentReadoutAvailable =
          input.instrumentReadouts.leftDigitStateAvailable,
      .leftInstrumentReadout = readoutsPlan.leftDigitState,
      .primaryWeaponAmmoAvailable =
          input.weaponPanels.primary.digitStateAvailable,
      .primaryWeaponAmmo = weaponPanelsPlan.primaryDigitState,
      .secondaryWeaponAmmoAvailable =
          input.weaponPanels.secondary.digitStateAvailable,
      .secondaryWeaponAmmo = weaponPanelsPlan.secondaryDigitState,
      .healthPercentAvailable = input.identityStatus.healthDigitStateAvailable,
      .healthPercent = identityStatusPlan.healthDigitState,
      .technologyLevelAvailable =
          input.identityStatus.technologyDigitStateAvailable,
      .technologyLevel = identityStatusPlan.technologyDigitState,
  };

  const auto committed = clock.commitHudStage(snapshot);
  if (committed != LegacyAircraftHudElapsedClockEndStatus::committed) {
    if (clock.stageInProgress()) {
      const auto aborted = clock.abortHudStage(snapshot);
      if (aborted != LegacyAircraftHudElapsedClockEndStatus::aborted) {
        return failure(LegacyAircraftHudRenderEventStatus::clockAbortFailed,
                       begun.status, aborted);
      }
    }
    return failure(LegacyAircraftHudRenderEventStatus::clockCommitFailed,
                   begun.status, committed);
  }

  return {
      .status = LegacyAircraftHudRenderEventStatus::ready,
      .clockBeginStatus = begun.status,
      .clockEndStatus = committed,
      .event = std::move(event),
      .stateUpdates = std::move(updates),
  };
}

} // namespace airfix::content
