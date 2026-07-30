#pragma once

#include "airfix/simulation/LegacyAircraftControlCommandReducer.hpp"
#include "airfix/simulation/LegacyAircraftControlEventStateOwner.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace airfix::simulation {

struct LegacyAircraftControlCommandStepState final {
  LegacyAircraftControlCommandState commandState{};
  LegacyAircraftControlEventState controlEventState{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftControlCommandStepState &,
             const LegacyAircraftControlCommandStepState &) noexcept = default;
};

using LegacyAircraftControlCommandWrite =
    std::variant<LegacyAircraftTurnWrite, LegacyAircraftPitchBankWrite,
                 LegacyAircraftThrustWrite>;

enum class LegacyAircraftControlCommandStepStatus : std::uint8_t {
  committed,
  ignoredInactive,
  unsupportedCommand,
  decodeRejected,
  commitRejected,
};

struct LegacyAircraftControlCommandStepResult final {
  LegacyAircraftControlCommandStepStatus status{
      LegacyAircraftControlCommandStepStatus::unsupportedCommand};
  LegacyAircraftControlCommandStepState state{};
  std::optional<LegacyAircraftControlCommandEventInput> event;
  std::optional<LegacyAircraftControlCommandWrite> write;

  [[nodiscard]] constexpr bool committed() const noexcept {
    return status == LegacyAircraftControlCommandStepStatus::committed &&
           event.has_value() && write.has_value();
  }

  [[nodiscard]] constexpr bool ignored() const noexcept {
    return status == LegacyAircraftControlCommandStepStatus::ignoredInactive &&
           event.has_value() && !write.has_value();
  }
};

// Advances exactly one already-ordered bool command invocation through the
// recovered command reducer, its typed native-event decoder, and the separate
// control-event state owner.
//
// The stages intentionally do not form one all-or-nothing transaction. The
// native callback stores its command flag before ProcessEvent. Consequently a
// recognized command updates commandState even when an inactive vehicle
// ignores the event or the portable decoder rejects its reconstruction policy.
// controlEventState changes only after a decoded write commits. An unsupported
// command changes neither state.
//
// This allocation-free boundary owns no InputFrame/Q15 conversion, device or
// producer identity, queue, batching, sorting, deduplication, sample-and-hold,
// focus policy, scheduler, clock, nominal 12-ms timing, player state, physics,
// or renderer publication.
[[nodiscard]] LegacyAircraftControlCommandStepResult
legacyAircraftAdvanceControlCommandStep(
    LegacyAircraftControlCommandStepState state,
    LegacyAircraftControlCommandInput input) noexcept;

} // namespace airfix::simulation
