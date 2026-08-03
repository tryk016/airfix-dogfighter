#include "airfix/simulation/LegacyCampaignModel.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(kLegacyCampaignMissionCount == 10U);
static_assert(std::is_trivially_copyable_v<LegacyCampaignState>);
static_assert(noexcept(createLegacyCampaign({})));
static_assert(noexcept(selectLegacyCampaignSide({}, LegacyCampaignSide::axis)));
static_assert(noexcept(selectLegacyCampaignMission({}, 0U)));
static_assert(noexcept(applyLegacyCampaignProgress({}, {})));

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] LegacyCampaignState
requireReady(const LegacyCampaignModelResult &result,
             const char *const message) {
  require(result.ready(), message);
  return result.state;
}

void requireRejectedWithoutMutation(const LegacyCampaignModelResult &result,
                                    const LegacyCampaignState &original,
                                    const LegacyCampaignModelStatus status,
                                    const char *const message) {
  require(result.status == status && !result.ready() &&
              result.state == original,
          message);
}

void testDefaultsAndRawMaximumClamping() {
  const auto defaults = requireReady(createLegacyCampaign({}),
                                     "missing campaign records were rejected");
  require(!defaults.storedThread.has_value() &&
              defaults.currentSide == LegacyCampaignSide::allied &&
              !defaults.axisMaximum.has_value() &&
              !defaults.alliedMaximum.has_value() &&
              defaults.selectedMissionRow == 0U,
          "missing records did not use the recovered frontend defaults");

  const auto clamped = requireReady(createLegacyCampaign({
                                        .storedThread = 0,
                                        .axisMaximum = -7,
                                        .alliedMaximum = 77,
                                    }),
                                    "ordinary Axis seed was rejected");
  require(clamped.storedThread == LegacyCampaignSide::axis &&
              clamped.currentSide == LegacyCampaignSide::axis &&
              clamped.axisMaximum == -7 && clamped.alliedMaximum == 77 &&
              clamped.selectedMissionRow == 0U &&
              legacyCampaignSelectableMaximum(clamped,
                                              LegacyCampaignSide::axis) == 0U &&
              legacyCampaignSelectableMaximum(clamped,
                                              LegacyCampaignSide::allied) == 9U,
          "raw maxima were not retained separately from UI clamping");
}

void testUnsupportedThreadFailsClosed() {
  for (const auto value : {-1, 2, std::numeric_limits<std::int32_t>::max()}) {
    const auto result = createLegacyCampaign({.storedThread = value});
    require(result.status ==
                    LegacyCampaignModelStatus::unsupportedThreadValue &&
                !result.ready() && !result.state.storedThread.has_value(),
            "unsupported THRD was silently normalized");
  }
}

void testSideAndMissionSelection() {
  auto state = requireReady(createLegacyCampaign({
                                .storedThread = 0,
                                .axisMaximum = 2,
                                .alliedMaximum = 6,
                            }),
                            "selection seed was rejected");
  require(state.selectedMissionRow == 2U,
          "campaign did not open at the highest Axis row");

  state =
      requireReady(selectLegacyCampaignSide(state, LegacyCampaignSide::allied),
                   "valid side switch was rejected");
  require(state.currentSide == LegacyCampaignSide::allied &&
              state.selectedMissionRow == 6U &&
              state.storedThread == LegacyCampaignSide::axis,
          "side switch changed persisted THRD or selected the wrong row");

  state = requireReady(selectLegacyCampaignMission(state, 3U),
                       "unlocked mission row was rejected");
  require(state.selectedMissionRow == 3U,
          "unlocked mission row was not selected");
  requireRejectedWithoutMutation(selectLegacyCampaignMission(state, 7U), state,
                                 LegacyCampaignModelStatus::missionLocked,
                                 "locked mission row mutated the campaign");
  requireRejectedWithoutMutation(selectLegacyCampaignMission(state, 10U), state,
                                 LegacyCampaignModelStatus::missionLocked,
                                 "out-of-catalogue row mutated the campaign");

  const auto forgedSide = static_cast<LegacyCampaignSide>(255U);
  requireRejectedWithoutMutation(selectLegacyCampaignSide(state, forgedSide),
                                 state,
                                 LegacyCampaignModelStatus::invalidCampaignSide,
                                 "forged side mutated the campaign");
}

void testOutcomeDirectiveIntegrationAndMissionTenClamp() {
  auto state = requireReady(createLegacyCampaign({
                                .storedThread = 0,
                                .axisMaximum = 0,
                            }),
                            "progress seed was rejected");

  const auto firstOutcome = legacyMissionConsumeOutcome({
      .missionExists = true,
      .outcome = {.failed = false, .accomplished = true},
      .side = LegacyCampaignSide::axis,
      .missionNumber = 0,
      .storedMaximumPresent = true,
      .storedMaximum = 0,
  });
  require(firstOutcome.consumed(), "valid mission outcome was not consumed");
  state =
      requireReady(applyLegacyCampaignProgress(state, firstOutcome.progress),
                   "valid replace directive was rejected");
  require(state.axisMaximum == 1 && state.selectedMissionRow == 1U &&
              state.storedThread == LegacyCampaignSide::axis,
          "first success did not unlock the next Axis row");

  const auto alliedOutcome = legacyMissionConsumeOutcome({
      .missionExists = true,
      .outcome = {.failed = false, .accomplished = true},
      .side = LegacyCampaignSide::allied,
      .missionNumber = 3,
      .storedMaximumPresent = false,
      .storedMaximum = 0,
  });
  state =
      requireReady(applyLegacyCampaignProgress(state, alliedOutcome.progress),
                   "valid add directive was rejected");
  require(state.alliedMaximum == 4 &&
              state.currentSide == LegacyCampaignSide::allied &&
              state.storedThread == LegacyCampaignSide::allied &&
              state.selectedMissionRow == 4U,
          "Allied success did not materialize THRD/ALMI in order");

  state.alliedMaximum = 9;
  const auto finalOutcome = legacyMissionConsumeOutcome({
      .missionExists = true,
      .outcome = {.failed = false, .accomplished = true},
      .side = LegacyCampaignSide::allied,
      .missionNumber = 9,
      .storedMaximumPresent = true,
      .storedMaximum = 9,
  });
  state =
      requireReady(applyLegacyCampaignProgress(state, finalOutcome.progress),
                   "mission-ten raw maximum was rejected");
  require(state.alliedMaximum == 10 && state.selectedMissionRow == 9U,
          "raw mission-ten maximum was not retained behind the row-nine clamp");

  const auto noChange = legacyMissionConsumeOutcome({
      .missionExists = true,
      .outcome = {.failed = false, .accomplished = true},
      .side = LegacyCampaignSide::allied,
      .missionNumber = 3,
      .storedMaximumPresent = true,
      .storedMaximum = 10,
  });
  const auto unchanged =
      requireReady(applyLegacyCampaignProgress(state, noChange.progress),
                   "valid keep-maximum directive was rejected");
  require(unchanged == state,
          "keep-maximum directive changed already unlocked progress");

  const auto failureOutcome = legacyMissionConsumeOutcome({});
  const auto afterFailure =
      requireReady(applyLegacyCampaignProgress(state, failureOutcome.progress),
                   "empty failure directive was rejected");
  require(afterFailure == state, "failure directive changed campaign progress");
}

void testForgedProgressDirectivesFailClosed() {
  const auto state = requireReady(createLegacyCampaign({
                                      .storedThread = 0,
                                      .axisMaximum = 3,
                                  }),
                                  "forged-directive seed was rejected");

  requireRejectedWithoutMutation(
      applyLegacyCampaignProgress(
          state, {.setThread = false,
                  .thread = LegacyCampaignSide::axis,
                  .maximumUpdate = LegacyCampaignMaximumUpdateKind::replace,
                  .nextMissionNumber = 4}),
      state, LegacyCampaignModelStatus::invalidProgressDirective,
      "maximum update without THRD mutated state");
  requireRejectedWithoutMutation(
      applyLegacyCampaignProgress(
          state, {.setThread = true,
                  .thread = LegacyCampaignSide::axis,
                  .maximumUpdate = LegacyCampaignMaximumUpdateKind::add,
                  .nextMissionNumber = 4}),
      state, LegacyCampaignModelStatus::invalidProgressDirective,
      "add directive overwrote a present maximum");
  requireRejectedWithoutMutation(
      applyLegacyCampaignProgress(
          state, {.setThread = true,
                  .thread = LegacyCampaignSide::axis,
                  .maximumUpdate = LegacyCampaignMaximumUpdateKind::replace,
                  .nextMissionNumber = 3}),
      state, LegacyCampaignModelStatus::invalidProgressDirective,
      "replace directive accepted a non-increasing value");
  requireRejectedWithoutMutation(
      applyLegacyCampaignProgress(
          state, {.setThread = true,
                  .thread = LegacyCampaignSide::axis,
                  .maximumUpdate = LegacyCampaignMaximumUpdateKind::none,
                  .nextMissionNumber = 11}),
      state, LegacyCampaignModelStatus::invalidProgressDirective,
      "out-of-catalogue progression mutated state");
  requireRejectedWithoutMutation(
      applyLegacyCampaignProgress(
          state, {.setThread = true,
                  .thread = static_cast<LegacyCampaignSide>(2U),
                  .maximumUpdate = LegacyCampaignMaximumUpdateKind::none,
                  .nextMissionNumber = 1}),
      state, LegacyCampaignModelStatus::invalidCampaignSide,
      "forged directive side mutated state");
  requireRejectedWithoutMutation(
      applyLegacyCampaignProgress(
          state,
          {.setThread = true,
           .thread = LegacyCampaignSide::axis,
           .maximumUpdate = static_cast<LegacyCampaignMaximumUpdateKind>(255U),
           .nextMissionNumber = 4}),
      state, LegacyCampaignModelStatus::invalidProgressDirective,
      "unknown maximum-update kind mutated state");
}

void testRecoveredMissionIdentifiers() {
  auto state =
      requireReady(createLegacyCampaign({.storedThread = 0, .axisMaximum = 9}),
                   "identifier seed was rejected");
  require(legacyCampaignMissionIdentifier(state) ==
              std::optional<std::string>{"AxisMission10"},
          "Axis catalogue identifier changed");
  state =
      requireReady(selectLegacyCampaignSide(state, LegacyCampaignSide::allied),
                   "Allied identifier side switch was rejected");
  require(legacyCampaignMissionIdentifier(state) ==
              std::optional<std::string>{"AlliedMission1"},
          "Allied catalogue identifier changed");

  state.selectedMissionRow = 10U;
  require(!legacyCampaignMissionIdentifier(state).has_value(),
          "forged mission row produced a catalogue identifier");
}

} // namespace

int main() {
  testDefaultsAndRawMaximumClamping();
  testUnsupportedThreadFailsClosed();
  testSideAndMissionSelection();
  testOutcomeDirectiveIntegrationAndMissionTenClamp();
  testForgedProgressDirectivesFailClosed();
  testRecoveredMissionIdentifiers();
  std::cout << "Legacy campaign model tests passed\n";
  return EXIT_SUCCESS;
}
