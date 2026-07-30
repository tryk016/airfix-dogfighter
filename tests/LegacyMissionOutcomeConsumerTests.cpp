#include "airfix/simulation/LegacyMissionOutcomeConsumer.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(static_cast<std::uint8_t>(LegacyCampaignSide::axis) == 0U);
static_assert(static_cast<std::uint8_t>(LegacyCampaignSide::allied) == 1U);
static_assert(std::is_trivially_copyable_v<LegacyMissionOutcomeConsumerInput>);
static_assert(std::is_trivially_copyable_v<LegacyCampaignProgressDirective>);
static_assert(std::is_trivially_copyable_v<LegacyMissionOutcomeConsumption>);
static_assert(noexcept(legacyMissionConsumeOutcome({})));

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] LegacyMissionOutcomeConsumerInput
successInput(const LegacyCampaignSide side = LegacyCampaignSide::axis,
             const std::int32_t missionNumber = 3,
             const bool storedMaximumPresent = false,
             const std::int32_t storedMaximum = 0) {
  return {
      .missionExists = true,
      .outcome =
          {
              .failed = false,
              .accomplished = true,
          },
      .side = side,
      .missionNumber = missionNumber,
      .storedMaximumPresent = storedMaximumPresent,
      .storedMaximum = storedMaximum,
  };
}

void requireFailure(const LegacyMissionOutcomeConsumption &actual,
                    const char *const message) {
  require(actual.consumed() && actual.result == LegacyMissionResult::failure &&
              actual.primaryAction ==
                  LegacyMissionPrimaryAction::retryMission &&
              actual.progress == LegacyCampaignProgressDirective{},
          message);
}

void testExactOutcomePredicate() {
  struct Vector final {
    bool missionExists;
    LegacyMissionOutcomeState outcome;
    bool success;
  };

  constexpr std::array<Vector, 8> vectors{{
      {false, {false, false}, false},
      {false, {true, false}, false},
      {false, {false, true}, false},
      {false, {true, true}, false},
      {true, {false, false}, false},
      {true, {true, false}, false},
      {true, {false, true}, true},
      {true, {true, true}, false},
  }};

  for (const auto &vector : vectors) {
    auto input = successInput();
    input.missionExists = vector.missionExists;
    input.outcome = vector.outcome;
    const auto actual = legacyMissionConsumeOutcome(input);
    if (vector.success) {
      require(actual.consumed() &&
                  actual.result == LegacyMissionResult::success &&
                  actual.primaryAction ==
                      LegacyMissionPrimaryAction::continueCampaign,
              "exact success predicate rejected the only success vector");
    } else {
      requireFailure(actual, "exact success predicate admitted a failure");
    }
  }
}

void testFailureIgnoresUnusedCampaignMetadata() {
  LegacyMissionOutcomeConsumerInput input{
      .missionExists = false,
      .outcome =
          {
              .failed = false,
              .accomplished = true,
          },
      .side = static_cast<LegacyCampaignSide>(255U),
      .missionNumber = std::numeric_limits<std::int32_t>::max(),
      .storedMaximumPresent = true,
      .storedMaximum = std::numeric_limits<std::int32_t>::min(),
  };
  requireFailure(legacyMissionConsumeOutcome(input),
                 "failure inspected unreachable campaign metadata");

  input.missionExists = true;
  input.outcome.failed = true;
  requireFailure(legacyMissionConsumeOutcome(input),
                 "failed flag did not override accomplished");
}

void testSuccessAlwaysSetsTypedThread() {
  for (const auto side :
       {LegacyCampaignSide::axis, LegacyCampaignSide::allied}) {
    const auto actual = legacyMissionConsumeOutcome(successInput(side));
    require(actual.consumed() &&
                actual.result == LegacyMissionResult::success &&
                actual.primaryAction ==
                    LegacyMissionPrimaryAction::continueCampaign &&
                actual.progress.setThread && actual.progress.thread == side &&
                actual.progress.maximumUpdate ==
                    LegacyCampaignMaximumUpdateKind::add &&
                actual.progress.nextMissionNumber == 4,
            "successful side did not set THRD and add an absent maximum");
  }
}

void testSignedMaximumDecision() {
  struct Vector final {
    std::int32_t missionNumber;
    bool storedMaximumPresent;
    std::int32_t storedMaximum;
    LegacyCampaignMaximumUpdateKind expected;
    std::int32_t expectedNext;
  };

  constexpr std::array<Vector, 8> vectors{{
      {-1, false, 99, LegacyCampaignMaximumUpdateKind::add, 0},
      {-1, true, -1, LegacyCampaignMaximumUpdateKind::replace, 0},
      {-1, true, 0, LegacyCampaignMaximumUpdateKind::none, 0},
      {-1, true, 1, LegacyCampaignMaximumUpdateKind::none, 0},
      {3, true, 3, LegacyCampaignMaximumUpdateKind::replace, 4},
      {3, true, 4, LegacyCampaignMaximumUpdateKind::none, 4},
      {3, true, 5, LegacyCampaignMaximumUpdateKind::none, 4},
      {std::numeric_limits<std::int32_t>::max() - 1, true,
       std::numeric_limits<std::int32_t>::max() - 1,
       LegacyCampaignMaximumUpdateKind::replace,
       std::numeric_limits<std::int32_t>::max()},
  }};

  for (const auto &vector : vectors) {
    const auto actual = legacyMissionConsumeOutcome(
        successInput(LegacyCampaignSide::allied, vector.missionNumber,
                     vector.storedMaximumPresent, vector.storedMaximum));
    require(actual.consumed() && actual.progress.setThread &&
                actual.progress.thread == LegacyCampaignSide::allied &&
                actual.progress.maximumUpdate == vector.expected &&
                actual.progress.nextMissionNumber == vector.expectedNext,
            "signed maximum add/replace/keep decision changed");
  }
}

void testNoMaximumUpdateStillSetsThread() {
  const auto actual = legacyMissionConsumeOutcome(
      successInput(LegacyCampaignSide::allied, 3, true, 10));
  require(actual.consumed() && actual.progress.setThread &&
              actual.progress.thread == LegacyCampaignSide::allied &&
              actual.progress.maximumUpdate ==
                  LegacyCampaignMaximumUpdateKind::none &&
              actual.progress.nextMissionNumber == 4,
          "unchanged maximum suppressed the preceding THRD directive");
}

void testInvalidSuccessMetadataFailsClosed() {
  auto invalidSide =
      successInput(static_cast<LegacyCampaignSide>(2U), 3, false, 0);
  const auto sideResult = legacyMissionConsumeOutcome(invalidSide);
  require(sideResult.status ==
                  LegacyMissionOutcomeConsumeStatus::invalidCampaignSide &&
              !sideResult.consumed() &&
              sideResult.result == LegacyMissionResult::success &&
              sideResult.primaryAction ==
                  LegacyMissionPrimaryAction::unavailable &&
              sideResult.progress == LegacyCampaignProgressDirective{},
          "forged campaign side emitted UI or progress directives");

  const auto overflow = legacyMissionConsumeOutcome(
      successInput(LegacyCampaignSide::axis,
                   std::numeric_limits<std::int32_t>::max(), false, 0));
  require(
      overflow.status ==
              LegacyMissionOutcomeConsumeStatus::missionNumberWouldOverflow &&
          !overflow.consumed() &&
          overflow.result == LegacyMissionResult::success &&
          overflow.primaryAction == LegacyMissionPrimaryAction::unavailable &&
          overflow.progress == LegacyCampaignProgressDirective{},
      "INT32_MAX copied native wraparound into portable directives");
}

void testDeterministicValueResult() {
  constexpr auto input = LegacyMissionOutcomeConsumerInput{
      .missionExists = true,
      .outcome =
          {
              .failed = false,
              .accomplished = true,
          },
      .side = LegacyCampaignSide::allied,
      .missionNumber = -1,
      .storedMaximumPresent = true,
      .storedMaximum = -5,
  };
  const auto first = legacyMissionConsumeOutcome(input);
  const auto second = legacyMissionConsumeOutcome(input);
  require(first == second,
          "identical outcome-consumer inputs produced different values");
}

} // namespace

int main() {
  testExactOutcomePredicate();
  testFailureIgnoresUnusedCampaignMetadata();
  testSuccessAlwaysSetsTypedThread();
  testSignedMaximumDecision();
  testNoMaximumUpdateStillSetsThread();
  testInvalidSuccessMetadataFailsClosed();
  testDeterministicValueResult();
  std::cout << "Legacy mission outcome consumer tests passed\n";
  return EXIT_SUCCESS;
}
