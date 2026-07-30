#pragma once

#include "airfix/simulation/LegacyMissionOutcomeState.hpp"

#include <cstdint>

namespace airfix::simulation {

enum class LegacyMissionResult : std::uint8_t {
  failure = 0,
  success,
};

enum class LegacyMissionPrimaryAction : std::uint8_t {
  unavailable = 0,
  retryMission,
  continueCampaign,
};

// Matches the native THRD payload while keeping serialized FourCC/chunk
// concerns outside the pure consumer.
enum class LegacyCampaignSide : std::uint8_t {
  axis = 0,
  allied = 1,
};

enum class LegacyCampaignMaximumUpdateKind : std::uint8_t {
  none = 0,
  add,
  replace,
};

enum class LegacyMissionOutcomeConsumeStatus : std::uint8_t {
  consumed = 0,
  invalidCampaignSide,
  missionNumberWouldOverflow,
};

struct LegacyMissionOutcomeConsumerInput final {
  bool missionExists{};
  LegacyMissionOutcomeState outcome{};

  // Read from mission metadata only after the native success predicate holds.
  // Unknown side values and INT32_MAX fail closed instead of manufacturing
  // THRD/AXMI/ALMI writes.
  LegacyCampaignSide side{LegacyCampaignSide::axis};
  std::int32_t missionNumber{};

  bool storedMaximumPresent{};
  std::int32_t storedMaximum{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyMissionOutcomeConsumerInput &,
             const LegacyMissionOutcomeConsumerInput &) noexcept = default;
};

struct LegacyCampaignProgressDirective final {
  // Native success always replaces THRD before considering the side maximum.
  bool setThread{};
  LegacyCampaignSide thread{LegacyCampaignSide::axis};

  LegacyCampaignMaximumUpdateKind maximumUpdate{
      LegacyCampaignMaximumUpdateKind::none};
  std::int32_t nextMissionNumber{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyCampaignProgressDirective &,
             const LegacyCampaignProgressDirective &) noexcept = default;
};

struct LegacyMissionOutcomeConsumption final {
  LegacyMissionOutcomeConsumeStatus status{
      LegacyMissionOutcomeConsumeStatus::consumed};
  LegacyMissionResult result{LegacyMissionResult::failure};
  LegacyMissionPrimaryAction primaryAction{
      LegacyMissionPrimaryAction::unavailable};
  LegacyCampaignProgressDirective progress{};

  [[nodiscard]] constexpr bool consumed() const noexcept {
    return status == LegacyMissionOutcomeConsumeStatus::consumed;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyMissionOutcomeConsumption &,
             const LegacyMissionOutcomeConsumption &) noexcept = default;
};

// Reconstructs the outcome decision made by the native single-player result
// screen and emits value-only campaign/UI directives.
//
// Exact native success predicate:
//   missionExists && outcome.accomplished && !outcome.failed
//
// Failure requests retry and never emits campaign progress. Success requests
// continue, always sets the thread side, and adds/replaces the selected stored
// maximum only when absent or signed-less-than missionNumber + 1.
//
// The original x86 INC wraps INT32_MAX to INT32_MIN. This portable boundary
// deliberately fails closed for that input so C++ arithmetic never overflows
// and no corrupt progression directive is published.
//
// This function owns no trigger/AFS scheduling, strings, FourCC chunks, score,
// stats, file I/O, roster codec, console, UI execution, or platform adapter.
[[nodiscard]] LegacyMissionOutcomeConsumption legacyMissionConsumeOutcome(
    const LegacyMissionOutcomeConsumerInput &input) noexcept;

} // namespace airfix::simulation
