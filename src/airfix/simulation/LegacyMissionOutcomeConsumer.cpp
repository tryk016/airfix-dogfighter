#include "airfix/simulation/LegacyMissionOutcomeConsumer.hpp"

#include <limits>

namespace airfix::simulation {
namespace {

[[nodiscard]] constexpr LegacyMissionOutcomeConsumption
invalidConsumption(const LegacyMissionOutcomeConsumeStatus status) noexcept {
  return {
      .status = status,
      .result = LegacyMissionResult::success,
      .primaryAction = LegacyMissionPrimaryAction::unavailable,
      .progress = {},
  };
}

} // namespace

LegacyMissionOutcomeConsumption legacyMissionConsumeOutcome(
    const LegacyMissionOutcomeConsumerInput &input) noexcept {
  const bool succeeded = input.missionExists && input.outcome.accomplished &&
                         !input.outcome.failed;
  if (!succeeded) {
    return {
        .status = LegacyMissionOutcomeConsumeStatus::consumed,
        .result = LegacyMissionResult::failure,
        .primaryAction = LegacyMissionPrimaryAction::retryMission,
        .progress = {},
    };
  }

  if (!campaign::validCampaignSide(input.side)) {
    return invalidConsumption(
        LegacyMissionOutcomeConsumeStatus::invalidCampaignSide);
  }
  if (input.missionNumber == std::numeric_limits<std::int32_t>::max()) {
    return invalidConsumption(
        LegacyMissionOutcomeConsumeStatus::missionNumberWouldOverflow);
  }

  const auto nextMissionNumber =
      static_cast<std::int32_t>(input.missionNumber + 1);
  auto maximumUpdate = LegacyCampaignMaximumUpdateKind::none;
  if (!input.storedMaximumPresent) {
    maximumUpdate = LegacyCampaignMaximumUpdateKind::add;
  } else if (input.storedMaximum < nextMissionNumber) {
    maximumUpdate = LegacyCampaignMaximumUpdateKind::replace;
  }

  return {
      .status = LegacyMissionOutcomeConsumeStatus::consumed,
      .result = LegacyMissionResult::success,
      .primaryAction = LegacyMissionPrimaryAction::continueCampaign,
      .progress =
          {
              .setThread = true,
              .thread = input.side,
              .maximumUpdate = maximumUpdate,
              .nextMissionNumber = nextMissionNumber,
          },
  };
}

} // namespace airfix::simulation
