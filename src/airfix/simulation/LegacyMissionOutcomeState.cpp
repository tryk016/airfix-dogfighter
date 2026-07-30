#include "airfix/simulation/LegacyMissionOutcomeState.hpp"

namespace airfix::simulation {

LegacyMissionOutcomeStep
legacyMissionApplyOutcomeCall(LegacyMissionOutcomeState current,
                              const LegacyMissionOutcomeCall call) noexcept {
  const bool hadTerminalOutcome = current.failed || current.accomplished;

  switch (call) {
  case LegacyMissionOutcomeCall::missionFail:
    current.failed = true;
    break;
  case LegacyMissionOutcomeCall::missionSuccess:
    current.accomplished = true;
    break;
  default:
    return {
        .status = LegacyMissionOutcomeApplyStatus::unsupportedCall,
        .state = current,
        .requestPauseThenMenu = false,
    };
  }

  return {
      .status = LegacyMissionOutcomeApplyStatus::applied,
      .state = current,
      .requestPauseThenMenu = !hadTerminalOutcome,
  };
}

LegacyMissionOutcomeState
legacyMissionMarkFailed(LegacyMissionOutcomeState current) noexcept {
  current.failed = true;
  return current;
}

LegacyMissionOutcomeState legacyMissionResetOutcomeState() noexcept {
  return {};
}

} // namespace airfix::simulation
