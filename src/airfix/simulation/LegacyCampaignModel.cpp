#include "airfix/simulation/LegacyCampaignModel.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace airfix::simulation {
namespace {

[[nodiscard]] constexpr bool validSide(const LegacyCampaignSide side) noexcept {
  return side == LegacyCampaignSide::axis || side == LegacyCampaignSide::allied;
}

[[nodiscard]] constexpr std::uint8_t
clampMaximum(const std::optional<std::int32_t> maximum) noexcept {
  constexpr auto kLastMissionRow =
      static_cast<std::int32_t>(kLegacyCampaignMissionCount - 1U);
  return static_cast<std::uint8_t>(
      std::clamp(maximum.value_or(0), std::int32_t{0}, kLastMissionRow));
}

[[nodiscard]] constexpr const std::optional<std::int32_t> &
maximumFor(const LegacyCampaignState &state,
           const LegacyCampaignSide side) noexcept {
  return side == LegacyCampaignSide::axis ? state.axisMaximum
                                          : state.alliedMaximum;
}

[[nodiscard]] constexpr std::optional<std::int32_t> &
maximumFor(LegacyCampaignState &state, const LegacyCampaignSide side) noexcept {
  return side == LegacyCampaignSide::axis ? state.axisMaximum
                                          : state.alliedMaximum;
}

[[nodiscard]] constexpr LegacyCampaignModelResult
failure(const LegacyCampaignState &state,
        const LegacyCampaignModelStatus status) noexcept {
  return {.status = status, .state = state};
}

} // namespace

LegacyCampaignModelResult
createLegacyCampaign(const LegacyCampaignSeed &seed) noexcept {
  LegacyCampaignState state{
      .storedThread = std::nullopt,
      .currentSide = LegacyCampaignSide::allied,
      .axisMaximum = seed.axisMaximum,
      .alliedMaximum = seed.alliedMaximum,
      .selectedMissionRow = 0U,
  };

  if (seed.storedThread.has_value()) {
    if (*seed.storedThread ==
        static_cast<std::int32_t>(LegacyCampaignSide::axis)) {
      state.storedThread = LegacyCampaignSide::axis;
    } else if (*seed.storedThread ==
               static_cast<std::int32_t>(LegacyCampaignSide::allied)) {
      state.storedThread = LegacyCampaignSide::allied;
    } else {
      return failure(state, LegacyCampaignModelStatus::unsupportedThreadValue);
    }
    state.currentSide = *state.storedThread;
  }

  state.selectedMissionRow =
      legacyCampaignSelectableMaximum(state, state.currentSide);
  return {.status = LegacyCampaignModelStatus::ready, .state = state};
}

LegacyCampaignModelResult
selectLegacyCampaignSide(const LegacyCampaignState &state,
                         const LegacyCampaignSide side) noexcept {
  if (!validSide(side)) {
    return failure(state, LegacyCampaignModelStatus::invalidCampaignSide);
  }
  auto selected = state;
  selected.currentSide = side;
  selected.selectedMissionRow = legacyCampaignSelectableMaximum(selected, side);
  return {.status = LegacyCampaignModelStatus::ready, .state = selected};
}

LegacyCampaignModelResult
selectLegacyCampaignMission(const LegacyCampaignState &state,
                            const std::uint8_t row) noexcept {
  if (!validSide(state.currentSide)) {
    return failure(state, LegacyCampaignModelStatus::invalidCampaignSide);
  }
  if (row >= kLegacyCampaignMissionCount ||
      row > legacyCampaignSelectableMaximum(state, state.currentSide)) {
    return failure(state, LegacyCampaignModelStatus::missionLocked);
  }
  auto selected = state;
  selected.selectedMissionRow = row;
  return {.status = LegacyCampaignModelStatus::ready, .state = selected};
}

LegacyCampaignModelResult applyLegacyCampaignProgress(
    const LegacyCampaignState &state,
    const LegacyCampaignProgressDirective &directive) noexcept {
  if (!directive.setThread) {
    if (directive.maximumUpdate == LegacyCampaignMaximumUpdateKind::none) {
      return {.status = LegacyCampaignModelStatus::ready, .state = state};
    }
    return failure(state, LegacyCampaignModelStatus::invalidProgressDirective);
  }
  if (!validSide(directive.thread)) {
    return failure(state, LegacyCampaignModelStatus::invalidCampaignSide);
  }
  if (directive.nextMissionNumber < 1 ||
      directive.nextMissionNumber > kLegacyCampaignMissionCount) {
    return failure(state, LegacyCampaignModelStatus::invalidProgressDirective);
  }

  auto progressed = state;
  auto &maximum = maximumFor(progressed, directive.thread);
  switch (directive.maximumUpdate) {
  case LegacyCampaignMaximumUpdateKind::add:
    if (maximum.has_value()) {
      return failure(state,
                     LegacyCampaignModelStatus::invalidProgressDirective);
    }
    maximum = directive.nextMissionNumber;
    break;
  case LegacyCampaignMaximumUpdateKind::replace:
    if (!maximum.has_value() || *maximum >= directive.nextMissionNumber) {
      return failure(state,
                     LegacyCampaignModelStatus::invalidProgressDirective);
    }
    maximum = directive.nextMissionNumber;
    break;
  case LegacyCampaignMaximumUpdateKind::none:
    if (!maximum.has_value() || *maximum < directive.nextMissionNumber) {
      return failure(state,
                     LegacyCampaignModelStatus::invalidProgressDirective);
    }
    break;
  default:
    return failure(state, LegacyCampaignModelStatus::invalidProgressDirective);
  }

  progressed.storedThread = directive.thread;
  progressed.currentSide = directive.thread;
  progressed.selectedMissionRow =
      legacyCampaignSelectableMaximum(progressed, directive.thread);
  return {.status = LegacyCampaignModelStatus::ready, .state = progressed};
}

std::uint8_t
legacyCampaignSelectableMaximum(const LegacyCampaignState &state,
                                const LegacyCampaignSide side) noexcept {
  if (!validSide(side)) {
    return 0U;
  }
  return clampMaximum(maximumFor(state, side));
}

std::optional<std::string>
legacyCampaignMissionIdentifier(const LegacyCampaignState &state) {
  if (!validSide(state.currentSide) ||
      state.selectedMissionRow >= kLegacyCampaignMissionCount ||
      state.selectedMissionRow >
          legacyCampaignSelectableMaximum(state, state.currentSide)) {
    return std::nullopt;
  }
  const auto prefix = state.currentSide == LegacyCampaignSide::allied
                          ? "AlliedMission"
                          : "AxisMission";
  return std::string(prefix) +
         std::to_string(static_cast<unsigned int>(state.selectedMissionRow) +
                        1U);
}

} // namespace airfix::simulation
