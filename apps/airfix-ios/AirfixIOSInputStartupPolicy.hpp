#pragma once

namespace airfix::ios::startup_policy {

struct InputStartState final {
  bool viewVisible{false};
  bool profileLoadCompleted{false};
  bool inputPipelineReady{false};
  bool applicationActive{false};
};

[[nodiscard]] constexpr bool
shouldStartInput(const InputStartState state) noexcept {
  return state.viewVisible && state.profileLoadCompleted &&
         state.inputPipelineReady && state.applicationActive;
}

struct PausedMissionState final {
  bool contentReady{false};
  bool rendererInstalled{false};
  bool inputPipelineReady{false};
  bool simulationPipelineReady{false};
  bool inputOperational{false};
  bool settingsPanelClosed{false};
};

[[nodiscard]] constexpr bool
shouldOfferResume(const PausedMissionState state) noexcept {
  return state.contentReady && state.rendererInstalled &&
         state.inputPipelineReady && state.simulationPipelineReady &&
         state.inputOperational && state.settingsPanelClosed;
}

} // namespace airfix::ios::startup_policy
