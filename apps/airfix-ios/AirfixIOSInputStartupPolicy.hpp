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

struct GameplayChromeState final {
  bool viewVisible{false};
  bool applicationActive{false};
  bool simulationRunning{false};
  bool settingsPanelOpen{false};
};

struct GameplayChromeVisibility final {
  bool pausedMenuVisible{false};
  bool gameplayOverlayEligible{false};
};

// The paused/start menu and the in-game overlay are mutually exclusive.
// A child settings panel owns the complete presentation while it is open.
[[nodiscard]] constexpr GameplayChromeVisibility
resolveGameplayChromeVisibility(const GameplayChromeState state) noexcept {
  if (!state.viewVisible || !state.applicationActive ||
      state.settingsPanelOpen) {
    return {};
  }
  return {
      .pausedMenuVisible = !state.simulationRunning,
      .gameplayOverlayEligible = state.simulationRunning,
  };
}

} // namespace airfix::ios::startup_policy
