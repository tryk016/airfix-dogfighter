#pragma once

namespace airfix::ios::content_lifecycle_policy {

struct PauseState final {
  bool inspectionAlreadyRequired{false};
  bool contentOperationActive{false};
  bool operationIdentityPresent{false};
  bool inspectionQueued{false};
  bool missionRestartAlreadyRequired{false};
  bool missionLoadActive{false};
  bool publicationTicketOutstanding{false};
};

struct PauseDecision final {
  bool inspectAfterLifecycle{false};
  bool restartMissionLoadAfterLifecycle{false};
  bool clearActiveRevision{false};
};

[[nodiscard]] constexpr PauseDecision
pauseDecision(const PauseState state) noexcept {
  const bool interruptedContentOperation = state.contentOperationActive ||
                                           state.operationIdentityPresent ||
                                           state.inspectionQueued;
  const bool inspectAfterLifecycle =
      state.inspectionAlreadyRequired || interruptedContentOperation;
  return {
      .inspectAfterLifecycle = inspectAfterLifecycle,
      .restartMissionLoadAfterLifecycle = state.missionRestartAlreadyRequired ||
                                          state.missionLoadActive ||
                                          state.publicationTicketOutstanding,
      .clearActiveRevision = inspectAfterLifecycle,
  };
}

enum class ActivationAction {
  unavailable,
  waitForContentOperation,
  preservePublishedMission,
  inspectContent,
  restartMissionLoad,
};

struct ActivationState final {
  bool started{false};
  bool pickerPresented{false};
  bool contentOperationActive{false};
  bool inspectionRequired{false};
  bool inspectionQueued{false};
  bool missionRestartRequired{false};
};

[[nodiscard]] constexpr ActivationAction
activationAction(const ActivationState state) noexcept {
  if (!state.started || state.pickerPresented) {
    return ActivationAction::unavailable;
  }
  if (state.contentOperationActive) {
    return ActivationAction::waitForContentOperation;
  }
  if (state.inspectionRequired || state.inspectionQueued) {
    return ActivationAction::inspectContent;
  }
  if (state.missionRestartRequired) {
    return ActivationAction::restartMissionLoad;
  }
  return ActivationAction::preservePublishedMission;
}

} // namespace airfix::ios::content_lifecycle_policy
