#include "airfix/render/LegacyAircraftHudInstrumentReadouts.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace airfix::render;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 1.0e-5F) noexcept {
  return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyAircraftHudInstrumentReadoutsInput validInput() noexcept {
  return {
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .accumulatedElapsedSeconds = 0.0F,
      .rightDigitStateAvailable = true,
      .rightDigitState = makeLegacyAircraftHudRollingDigitsState(123),
      .quantizedVectorMagnitudeTimesHundred = 123,
      .leftDigitStateAvailable = true,
      .leftDigitState = makeLegacyAircraftHudRollingDigitsState(75),
      .quantizedRemainingRatioPercent = 75,
      .leftTintArgb = 0xFF3366CCU,
      .rollingDigitAtlasAvailable = true,
  };
}

void requireRejected(const LegacyAircraftHudInstrumentReadoutsInput &input,
                     const LegacyAircraftHudInstrumentReadoutsPlanStatus status,
                     const std::string_view message) {
  const auto plan = buildLegacyAircraftHudInstrumentReadoutsPlan(input);
  require(!plan.ready() && plan.status == status && plan.readoutCount == 0U &&
              plan.readout(0U) == nullptr &&
              plan.rightDigitState == input.rightDigitState &&
              plan.leftDigitState == input.leftDigitState,
          message);
}

void testNativeRightThenLeftGeometryAndTint() {
  const auto plan = buildLegacyAircraftHudInstrumentReadoutsPlan(validInput());
  require(plan.ready() && plan.readoutCount == 2U &&
              plan.readout(2U) == nullptr,
          "complete instrument-readout plan did not publish two entries");
  const auto *right = plan.readout(0U);
  const auto *left = plan.readout(1U);
  require(right != nullptr && left != nullptr &&
              right->side == LegacyAircraftHudInstrumentReadoutSide::right &&
              left->side == LegacyAircraftHudInstrumentReadoutSide::left &&
              right->digits.ready() && left->digits.ready() &&
              right->digits.commandCount == 4U &&
              left->digits.commandCount == 4U,
          "native right-before-left readout order changed");

  const auto *rightFirst = right->digits.command(0U);
  const auto *rightLast = right->digits.command(3U);
  const auto *leftFirst = left->digits.command(0U);
  const auto *leftLast = left->digits.command(3U);
  require(rightFirst != nullptr && rightLast != nullptr &&
              leftFirst != nullptr && leftLast != nullptr &&
              close(rightFirst->destinationRect.x, 443.0F) &&
              close(rightFirst->destinationRect.y, 451.0F) &&
              close(rightLast->destinationRect.x, 467.0F) &&
              close(leftFirst->destinationRect.x, 167.0F) &&
              close(leftFirst->destinationRect.y, 451.0F) &&
              close(leftLast->destinationRect.x, 191.0F) &&
              rightFirst->colourArgb == 0xFFFFFFFFU &&
              leftFirst->colourArgb == 0xFF3366CCU,
          "recovered instrument-readout geometry or tint changed");
}

void testSharedElapsedSnapshotAndOptionalLayers() {
  auto input = validInput();
  input.rightDigitState = makeLegacyAircraftHudRollingDigitsState(0);
  input.leftDigitState = makeLegacyAircraftHudRollingDigitsState(100);
  input.quantizedVectorMagnitudeTimesHundred = 9999;
  input.quantizedRemainingRatioPercent = 0;
  input.accumulatedElapsedSeconds = 0.1F;
  const auto advanced = buildLegacyAircraftHudInstrumentReadoutsPlan(input);
  require(advanced.ready() &&
              advanced.rightDigitState != input.rightDigitState &&
              advanced.leftDigitState != input.leftDigitState &&
              advanced.rightDigitState.cachedElapsedSeconds == 0.1F &&
              advanced.leftDigitState.cachedElapsedSeconds == 0.1F &&
              advanced.rightDigitState.retentionFactor ==
                  advanced.leftDigitState.retentionFactor,
          "two readouts did not consume one shared elapsed snapshot");

  input.rollingDigitAtlasAvailable = false;
  const auto noAtlas = buildLegacyAircraftHudInstrumentReadoutsPlan(input);
  require(noAtlas.ready() && noAtlas.readoutCount == 0U &&
              noAtlas.rightDigitState != input.rightDigitState &&
              noAtlas.leftDigitState != input.leftDigitState,
          "missing atlas changed retained readout-state advancement");

  input = validInput();
  input.rightDigitStateAvailable = false;
  const auto leftOnly = buildLegacyAircraftHudInstrumentReadoutsPlan(input);
  require(leftOnly.ready() && leftOnly.readoutCount == 1U &&
              leftOnly.readout(0U)->side ==
                  LegacyAircraftHudInstrumentReadoutSide::left &&
              leftOnly.rightDigitState == input.rightDigitState,
          "missing right state fabricated a readout or mutated its state");

  input.leftDigitStateAvailable = false;
  const auto empty = buildLegacyAircraftHudInstrumentReadoutsPlan(input);
  require(empty.ready() && empty.readoutCount == 0U,
          "absent native digit states fabricated readouts");
}

void testGatesAndInvalidInputsFailClosed() {
  auto input = validInput();
  input.activeWindowPresent = true;
  requireRejected(
      input, LegacyAircraftHudInstrumentReadoutsPlanStatus::activeWindowPresent,
      "active-window gate changed");
  input.cameraAttachedAtEntry = false;
  requireRejected(
      input, LegacyAircraftHudInstrumentReadoutsPlanStatus::activeWindowPresent,
      "gate precedence changed");
  input = validInput();
  input.cameraAttachedAtEntry = false;
  requireRejected(
      input,
      LegacyAircraftHudInstrumentReadoutsPlanStatus::cameraNotAttachedAtEntry,
      "entry-camera gate changed");
  input = validInput();
  input.typeHudEnabled = false;
  requireRejected(
      input, LegacyAircraftHudInstrumentReadoutsPlanStatus::typeHudDisabled,
      "type-HUD gate changed");
  input = validInput();
  input.cameraAttachedAfterLayout = false;
  requireRejected(
      input,
      LegacyAircraftHudInstrumentReadoutsPlanStatus::cameraDetachedBeforeDraw,
      "repeated-camera gate changed");
  input = validInput();
  input.screenWidth = 0U;
  requireRejected(
      input, LegacyAircraftHudInstrumentReadoutsPlanStatus::invalidScreenExtent,
      "zero screen extent was accepted");
  input = validInput();
  input.accumulatedElapsedSeconds = std::numeric_limits<float>::quiet_NaN();
  requireRejected(
      input,
      LegacyAircraftHudInstrumentReadoutsPlanStatus::elapsedSecondsNotFinite,
      "NaN elapsed time was accepted");
  input = validInput();
  input.accumulatedElapsedSeconds = -0.001F;
  requireRejected(
      input,
      LegacyAircraftHudInstrumentReadoutsPlanStatus::elapsedSecondsNegative,
      "negative elapsed time was accepted");
}

void testPartialAdvanceFailureIsAtomic() {
  auto input = validInput();
  input.accumulatedElapsedSeconds = 0.1F;
  input.leftDigitState.cachedElapsedSeconds = 0.1F;
  input.leftDigitState.retentionFactorValid = true;
  input.leftDigitState.retentionFactor =
      std::numeric_limits<float>::quiet_NaN();
  const auto plan = buildLegacyAircraftHudInstrumentReadoutsPlan(input);
  require(!plan.ready() &&
              plan.status == LegacyAircraftHudInstrumentReadoutsPlanStatus::
                                 digitAdvanceFailed &&
              plan.readoutCount == 0U &&
              plan.rightDigitState == input.rightDigitState &&
              plan.leftDigitState.digitPositions ==
                  input.leftDigitState.digitPositions &&
              plan.leftDigitState.cachedElapsedSeconds == 0.1F &&
              plan.leftDigitState.retentionFactorValid &&
              std::isnan(plan.leftDigitState.retentionFactor),
          "second readout failure published the first state update");
}

} // namespace

int main() {
  try {
    testNativeRightThenLeftGeometryAndTint();
    testSharedElapsedSnapshotAndOptionalLayers();
    testGatesAndInvalidInputsFailClosed();
    testPartialAdvanceFailureIsAtomic();
    std::cout << "Legacy aircraft HUD instrument-readout tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft HUD instrument-readout test failure: "
              << error.what() << '\n';
    return 1;
  }
}
