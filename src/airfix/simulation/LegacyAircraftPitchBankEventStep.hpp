#pragma once

#include "airfix/simulation/LegacyAircraftControlEventStateOwner.hpp"

#include <cstdint>
#include <optional>

namespace airfix::simulation {

enum class LegacyAircraftPitchBankEventStepStatus : std::uint8_t {
  committed,
  ignoredInactive,
  decodeRejected,
  commitRejected,
};

struct LegacyAircraftPitchBankEventStepResult final {
  LegacyAircraftPitchBankEventStepStatus status{
      LegacyAircraftPitchBankEventStepStatus::decodeRejected};
  LegacyAircraftControlEventState state{};
  std::optional<LegacyAircraftPitchBankWrite> write;

  [[nodiscard]] constexpr bool committed() const noexcept {
    return status == LegacyAircraftPitchBankEventStepStatus::committed &&
           write.has_value();
  }

  [[nodiscard]] constexpr bool ignored() const noexcept {
    return status == LegacyAircraftPitchBankEventStepStatus::ignoredInactive &&
           !write.has_value();
  }

  [[nodiscard]] constexpr bool rejected() const noexcept {
    return (status == LegacyAircraftPitchBankEventStepStatus::decodeRejected ||
            status == LegacyAircraftPitchBankEventStepStatus::commitRejected) &&
           !write.has_value();
  }
};

// Advances exactly one already-formed, already-ordered PITCH_SET or BANK_SET
// event through the recovered decoder and the simulation-thread-confined state
// owner. Every invocation is processed independently, including repeated equal
// values and active zero writes. The caller owns producer order and calls this
// function once for every event in that order.
//
// This allocation-free boundary deliberately owns no device or producer
// cache, event collection, batching, sorting, deduplication, interpolation,
// retry, replay, InputFrame/Q15 conversion, scheduler, clock, nominal 12-ms
// timing, physics, or renderer publication.
[[nodiscard]] LegacyAircraftPitchBankEventStepResult
legacyAircraftAdvancePitchBankEventStep(
    LegacyAircraftControlEventState state,
    LegacyAircraftNativePitchBankEventInput input) noexcept;

} // namespace airfix::simulation
