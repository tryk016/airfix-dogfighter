#include "airfix/simulation/LegacyAircraftVehicleRefreshGate.hpp"

namespace airfix::simulation {

LegacyAircraftVehicleRefreshGateResult legacyAircraftAdvanceVehicleRefreshGate(
    LegacyAircraftControlCommandStepState state,
    const LegacyAircraftVehicleRefreshGateInput input) noexcept {
  const auto sleepStep = legacyVehicleAdvanceSleepStep(
      state.controlEventState.restDurationMilliseconds,
      {
          .wakeControlValues = state.controlEventState.wakeControlValues(),
          .linearVelocitySquared = input.linearVelocitySquared,
          .onGround = input.onGround,
          .waterUnit = input.waterUnit,
          .refreshDeltaMilliseconds = input.refreshDeltaMilliseconds,
      });
  if (!sleepStep.has_value()) {
    return {
        .status = LegacyAircraftVehicleRefreshGateStatus::sleepStepRejected,
        .state = state,
        .sleepStep = std::nullopt,
    };
  }

  state.controlEventState.restDurationMilliseconds =
      sleepStep->restDurationMilliseconds;
  return {
      .status = LegacyAircraftVehicleRefreshGateStatus::advanced,
      .state = state,
      .sleepStep = sleepStep,
  };
}

} // namespace airfix::simulation
