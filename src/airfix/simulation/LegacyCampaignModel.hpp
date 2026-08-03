#pragma once

#include "airfix/simulation/LegacyMissionOutcomeConsumer.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace airfix::simulation {

struct LegacyCampaignSeed final {
  std::optional<std::int32_t> storedThread;
  std::optional<std::int32_t> axisMaximum;
  std::optional<std::int32_t> alliedMaximum;

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyCampaignSeed &,
             const LegacyCampaignSeed &) noexcept = default;
};

struct LegacyCampaignState final {
  // Absence is preserved even though the original frontend displays Allied by
  // default. A successful result materializes THRD.
  std::optional<LegacyCampaignSide> storedThread;
  LegacyCampaignSide currentSide{LegacyCampaignSide::allied};
  std::optional<std::int32_t> axisMaximum;
  std::optional<std::int32_t> alliedMaximum;
  std::uint8_t selectedMissionRow{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyCampaignState &,
             const LegacyCampaignState &) noexcept = default;
};

enum class LegacyCampaignModelStatus : std::uint8_t {
  ready = 0,
  unsupportedThreadValue,
  invalidCampaignSide,
  missionLocked,
  invalidProgressDirective,
};

struct LegacyCampaignModelResult final {
  LegacyCampaignModelStatus status{LegacyCampaignModelStatus::ready};
  LegacyCampaignState state;

  [[nodiscard]] constexpr bool ready() const noexcept {
    return status == LegacyCampaignModelStatus::ready;
  }
};

inline constexpr std::uint8_t kLegacyCampaignMissionCount = 10U;

// Builds the normal two-side frontend model. Missing THRD defaults the current
// view to Allied while remaining absent in persisted state. Missing maxima
// display row zero. Unsupported signed THRD values fail closed instead of
// copying the native constructor's inconsistent malformed-value behavior.
[[nodiscard]] LegacyCampaignModelResult
createLegacyCampaign(const LegacyCampaignSeed &seed) noexcept;

// Switching side opens at that side's highest currently selectable row. It is
// a UI selection and does not materialize/replace persisted THRD.
[[nodiscard]] LegacyCampaignModelResult
selectLegacyCampaignSide(const LegacyCampaignState &state,
                         LegacyCampaignSide side) noexcept;

// Selects only a row currently unlocked for the active side.
[[nodiscard]] LegacyCampaignModelResult
selectLegacyCampaignMission(const LegacyCampaignState &state,
                            std::uint8_t row) noexcept;

// Applies the already ordered value directive emitted by
// legacyMissionConsumeOutcome. Empty failure directives preserve the state.
// Successful progression is accepted only for ordinary mission rows 0..9
// (nextMissionNumber 1..10) and only when add/replace/keep agrees with the
// current optional maximum.
[[nodiscard]] LegacyCampaignModelResult applyLegacyCampaignProgress(
    const LegacyCampaignState &state,
    const LegacyCampaignProgressDirective &directive) noexcept;

[[nodiscard]] std::uint8_t
legacyCampaignSelectableMaximum(const LegacyCampaignState &state,
                                LegacyCampaignSide side) noexcept;

// Exact recovered catalogue key for the active side and selected row, for
// example AxisMission1 or AlliedMission10.
[[nodiscard]] std::optional<std::string>
legacyCampaignMissionIdentifier(const LegacyCampaignState &state);

} // namespace airfix::simulation
