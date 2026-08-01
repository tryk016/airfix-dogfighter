#include "airfix/render/LegacyAircraftHudIdentityStatus.hpp"

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

[[nodiscard]] LegacyAircraftHudIdentityStatusInput validInput() noexcept {
  return {
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .accumulatedElapsedSeconds = 0.0F,
      .aircraftIconCatalogMatch = true,
      .aircraftIconTextureAvailable = true,
      .aircraftIconTextureIndex = 3U,
      .healthDigitStateAvailable = true,
      .healthDigitState = makeLegacyAircraftHudRollingDigitsState(100),
      .quantizedHealthPercent = 100,
      .teamId = 1,
      .technologyStarTextureAvailable = true,
      .technologyCrossTextureAvailable = true,
      .technologyDigitStateAvailable = true,
      .technologyDigitState = makeLegacyAircraftHudRollingDigitsState(0),
      .quantizedTechnologyLevel = 0,
      .rollingDigitAtlasAvailable = true,
  };
}

void requireRejected(const LegacyAircraftHudIdentityStatusInput &input,
                     const LegacyAircraftHudIdentityStatusPlanStatus status,
                     const std::string_view message) {
  const auto plan = buildLegacyAircraftHudIdentityStatusPlan(input);
  require(!plan.ready() && plan.status == status && plan.commandCount == 0U &&
              plan.command(0U) == nullptr,
          message);
}

void testNativeOrderAndGeometry() {
  const auto plan = buildLegacyAircraftHudIdentityStatusPlan(validInput());
  require(plan.ready() && plan.commandCount == 10U &&
              plan.command(10U) == nullptr,
          "complete identity/status plan did not publish ten commands");

  using Kind = LegacyAircraftHudIdentityStatusCommandKind;
  const auto *icon = plan.command(0U);
  const auto *health0 = plan.command(1U);
  const auto *health3 = plan.command(4U);
  const auto *team = plan.command(5U);
  const auto *technology0 = plan.command(6U);
  const auto *technology3 = plan.command(9U);
  require(icon != nullptr && health0 != nullptr && health3 != nullptr &&
              team != nullptr && technology0 != nullptr &&
              technology3 != nullptr && icon->kind == Kind::aircraftIcon &&
              health0->kind == Kind::healthDigit &&
              health3->kind == Kind::healthDigit &&
              team->kind == Kind::teamBadge &&
              technology0->kind == Kind::technologyDigit &&
              technology3->kind == Kind::technologyDigit,
          "native icon-health-team-technology order changed");
  require(icon->textureIndex == 3U && close(icon->destinationRect.x, 50.0F) &&
              close(icon->destinationRect.y, 355.0F) &&
              close(icon->destinationRect.width, 64.0F) &&
              close(health0->destinationRect.x, 57.0F) &&
              close(health0->destinationRect.y, 423.0F) &&
              health0->digitSlotIndex == 0U &&
              close(health3->destinationRect.x, 81.0F) &&
              team->teamBadge == LegacyAircraftHudTeamBadge::technologyStar &&
              close(team->destinationRect.x, 288.0F) &&
              close(team->destinationRect.y, 408.0F) &&
              close(technology0->destinationRect.x, 304.0F) &&
              close(technology0->destinationRect.y, 449.0F) &&
              close(technology3->destinationRect.x, 328.0F),
          "recovered 640x480 identity/status geometry changed");
}

void testNarrowAnchorAndTeamSelection() {
  auto input = validInput();
  input.screenWidth = 639U;
  input.teamId = 0;
  const auto plan = buildLegacyAircraftHudIdentityStatusPlan(input);
  require(plan.ready() && plan.commandCount == 10U,
          "639-wide identity/status plan failed");
  require(close(plan.command(0U)->destinationRect.y, 17.0F) &&
              close(plan.command(1U)->destinationRect.y, 85.0F) &&
              close(plan.command(5U)->destinationRect.x, 287.5F) &&
              plan.command(5U)->teamBadge ==
                  LegacyAircraftHudTeamBadge::technologyCross,
          "strict 640-width top anchor or non-one team selection changed");

  input.teamId = -12;
  input.technologyCrossTextureAvailable = false;
  const auto noCross = buildLegacyAircraftHudIdentityStatusPlan(input);
  require(noCross.ready() && noCross.commandCount == 9U &&
              noCross.command(5U)->kind ==
                  LegacyAircraftHudIdentityStatusCommandKind::technologyDigit,
          "missing selected team texture did not suppress only its badge");
}

void testOptionalLayersAndStateAdvance() {
  auto input = validInput();
  input.aircraftIconCatalogMatch = false;
  input.rollingDigitAtlasAvailable = false;
  input.accumulatedElapsedSeconds = 0.25F;
  input.quantizedHealthPercent = 50;
  input.quantizedTechnologyLevel = 4;
  const auto sparse = buildLegacyAircraftHudIdentityStatusPlan(input);
  require(sparse.ready() && sparse.commandCount == 1U &&
              sparse.command(0U)->kind ==
                  LegacyAircraftHudIdentityStatusCommandKind::teamBadge &&
              sparse.healthDigitState != input.healthDigitState &&
              sparse.technologyDigitState != input.technologyDigitState,
          "optional drawing changed retained digit-state advancement");

  input.healthDigitStateAvailable = false;
  input.technologyDigitStateAvailable = false;
  input.technologyStarTextureAvailable = false;
  input.aircraftIconCatalogMatch = true;
  input.aircraftIconTextureAvailable = false;
  const auto empty = buildLegacyAircraftHudIdentityStatusPlan(input);
  require(empty.ready() && empty.commandCount == 0U,
          "absent native pointers fabricated HUD commands");
}

void testGatesAndInvalidInputsFailClosed() {
  auto input = validInput();
  input.activeWindowPresent = true;
  requireRejected(
      input, LegacyAircraftHudIdentityStatusPlanStatus::activeWindowPresent,
      "active window gate changed");
  input = validInput();
  input.cameraAttachedAtEntry = false;
  requireRejected(
      input,
      LegacyAircraftHudIdentityStatusPlanStatus::cameraNotAttachedAtEntry,
      "entry camera gate changed");
  input = validInput();
  input.typeHudEnabled = false;
  requireRejected(input,
                  LegacyAircraftHudIdentityStatusPlanStatus::typeHudDisabled,
                  "type HUD gate changed");
  input = validInput();
  input.cameraAttachedAfterLayout = false;
  requireRejected(
      input,
      LegacyAircraftHudIdentityStatusPlanStatus::cameraDetachedBeforeDraw,
      "repeated camera gate changed");
  input = validInput();
  input.screenHeight = 0U;
  requireRejected(
      input, LegacyAircraftHudIdentityStatusPlanStatus::invalidScreenExtent,
      "zero extent was accepted");
  input = validInput();
  input.accumulatedElapsedSeconds = std::numeric_limits<float>::quiet_NaN();
  requireRejected(
      input, LegacyAircraftHudIdentityStatusPlanStatus::elapsedSecondsNotFinite,
      "NaN elapsed time was accepted");
  input = validInput();
  input.accumulatedElapsedSeconds = -0.001F;
  requireRejected(
      input, LegacyAircraftHudIdentityStatusPlanStatus::elapsedSecondsNegative,
      "negative elapsed time was accepted");
}

} // namespace

int main() {
  try {
    testNativeOrderAndGeometry();
    testNarrowAnchorAndTeamSelection();
    testOptionalLayersAndStateAdvance();
    testGatesAndInvalidInputsFailClosed();
    std::cout << "Legacy aircraft HUD identity/status tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy aircraft HUD identity/status test failure: "
              << error.what() << '\n';
    return 1;
  }
}
