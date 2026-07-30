#pragma once

#include "airfix/simulation/LegacyAircraftControlCommandStep.hpp"
#include "airfix/simulation/LegacyVehicleSleepState.hpp"

#include <cstdint>
#include <optional>

namespace airfix::simulation {

// Values already sampled by the owner of one native vehicle refresh. This
// boundary deliberately does not create a clock or infer a scheduler cadence.
struct LegacyAircraftVehicleRefreshGateInput final {
  float linearVelocitySquared{};
  bool onGround{};
  bool waterUnit{};
  std::int64_t refreshDeltaMilliseconds{};
};

enum class LegacyAircraftVehicleRefreshGateStatus : std::uint8_t {
  advanced,
  sleepStepRejected,
};

struct LegacyAircraftVehicleRefreshGateResult final {
  LegacyAircraftVehicleRefreshGateStatus status{
      LegacyAircraftVehicleRefreshGateStatus::sleepStepRejected};
  LegacyAircraftControlCommandStepState state{};
  std::optional<LegacyVehicleSleepStepResult> sleepStep;

  [[nodiscard]] constexpr bool advanced() const noexcept {
    return status == LegacyAircraftVehicleRefreshGateStatus::advanced &&
           sleepStep.has_value();
  }

  [[nodiscard]] constexpr bool rejected() const noexcept {
    return status ==
               LegacyAircraftVehicleRefreshGateStatus::sleepStepRejected &&
           !sleepStep.has_value();
  }
};

// Advances only the recovered vehicle sleep/physics entry gate after the
// caller has applied every already-ordered control event that precedes this
// refresh. Wake-control values and the shared rest duration come from the
// committed control-event state. A successful step commits only the returned
// rest duration; command flags and all control fields remain unchanged.
//
// A rejected active-path sleep step returns the complete input state unchanged.
// The function is allocation-free and owns no InputFrame/Q15 conversion,
// device or producer order, clock, scheduler, nominal cadence, rigid-body or
// flight-law execution, renderer publication, or platform state.
[[nodiscard]] LegacyAircraftVehicleRefreshGateResult
legacyAircraftAdvanceVehicleRefreshGate(
    LegacyAircraftControlCommandStepState state,
    LegacyAircraftVehicleRefreshGateInput input) noexcept;

} // namespace airfix::simulation
