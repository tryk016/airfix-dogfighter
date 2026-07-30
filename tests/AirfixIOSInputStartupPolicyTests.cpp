#include "AirfixIOSInputStartupPolicy.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(const bool condition, const char *const message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void inputStartRequiresEveryGate() {
  using airfix::ios::startup_policy::InputStartState;
  using airfix::ios::startup_policy::shouldStartInput;

  require(shouldStartInput({
              .viewVisible = true,
              .profileLoadCompleted = true,
              .inputPipelineReady = true,
              .applicationActive = true,
          }),
          "all input startup gates should permit start");
  require(!shouldStartInput({
              .viewVisible = false,
              .profileLoadCompleted = true,
              .inputPipelineReady = true,
              .applicationActive = true,
          }),
          "completion after disappearance must not start input");
  require(!shouldStartInput({
              .viewVisible = true,
              .profileLoadCompleted = false,
              .inputPipelineReady = false,
              .applicationActive = true,
          }),
          "content-before-profile must not start input early");
  require(!shouldStartInput({
              .viewVisible = true,
              .profileLoadCompleted = true,
              .inputPipelineReady = true,
              .applicationActive = false,
          }),
          "completion in background must not start input");
}

void resumeRequiresTheCompletePausedMissionBoundary() {
  using airfix::ios::startup_policy::PausedMissionState;
  using airfix::ios::startup_policy::shouldOfferResume;

  const PausedMissionState ready{
      .contentReady = true,
      .rendererInstalled = true,
      .inputPipelineReady = true,
      .simulationPipelineReady = true,
      .inputOperational = true,
      .settingsPanelClosed = true,
  };
  require(shouldOfferResume(ready),
          "late profile completion should expose paused mission resume");

  auto state = ready;
  state.inputPipelineReady = false;
  require(!shouldOfferResume(state),
          "an unavailable input pipeline must hide resume");
  state = ready;
  state.settingsPanelClosed = false;
  require(!shouldOfferResume(state),
          "a settings overlay must retain the modal pause boundary");
  state = ready;
  state.rendererInstalled = false;
  require(!shouldOfferResume(state),
          "unpublished mission rendering must hide resume");
}

} // namespace

int main() {
  inputStartRequiresEveryGate();
  resumeRequiresTheCompletePausedMissionBoundary();
  std::cout << "AirfixIOSInputStartupPolicyTests passed\n";
  return EXIT_SUCCESS;
}
