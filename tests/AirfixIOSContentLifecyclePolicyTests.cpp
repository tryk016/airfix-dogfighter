#include "AirfixIOSContentLifecyclePolicy.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(const bool condition, const char *const message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void stablePublishedMissionSurvivesLifecycle() {
  using namespace airfix::ios::content_lifecycle_policy;
  require(activationAction({
              .started = true,
              .contentOperationActive = true,
          }) == ActivationAction::unavailable,
          "initial activation was mistaken for a lifecycle resume");
  const auto pause = pauseDecision({});
  require(!pause.inspectAfterLifecycle &&
              !pause.restartMissionLoadAfterLifecycle &&
              !pause.clearActiveRevision,
          "stable mission lifecycle pause invalidated authenticated content");
  require(activationAction({
              .lifecyclePauseObserved = true,
              .started = true,
          }) == ActivationAction::preservePublishedMission,
          "stable foreground transition reloaded the published mission");
}

void interruptedMissionLoadRestartsWithoutInspection() {
  using namespace airfix::ios::content_lifecycle_policy;
  const auto pause = pauseDecision({
      .missionLoadActive = true,
      .publicationTicketOutstanding = true,
  });
  require(!pause.inspectAfterLifecycle &&
              pause.restartMissionLoadAfterLifecycle &&
              !pause.clearActiveRevision,
          "mission-only cancellation discarded its authenticated revision");
  require(activationAction({
              .lifecyclePauseObserved = true,
              .started = true,
              .missionRestartRequired = true,
          }) == ActivationAction::restartMissionLoad,
          "interrupted mission load was not restarted in the foreground");
}

void interruptedContentTransactionForcesInspection() {
  using namespace airfix::ios::content_lifecycle_policy;
  const auto pause = pauseDecision({
      .contentOperationActive = true,
      .missionLoadActive = true,
  });
  require(pause.inspectAfterLifecycle &&
              pause.restartMissionLoadAfterLifecycle &&
              pause.clearActiveRevision,
          "interrupted content transaction retained an unverified revision");
  require(activationAction({
              .lifecyclePauseObserved = true,
              .started = true,
              .inspectionRequired = pause.inspectAfterLifecycle,
              .missionRestartRequired = pause.restartMissionLoadAfterLifecycle,
          }) == ActivationAction::inspectContent,
          "content inspection did not take precedence over mission restart");
}

void lifecycleDecisionsRemainFailClosedAcrossAsyncCompletion() {
  using namespace airfix::ios::content_lifecycle_policy;
  const auto repeatedPause = pauseDecision({
      .inspectionAlreadyRequired = true,
      .missionRestartAlreadyRequired = true,
  });
  require(repeatedPause.inspectAfterLifecycle &&
              repeatedPause.restartMissionLoadAfterLifecycle &&
              repeatedPause.clearActiveRevision,
          "repeated lifecycle callback forgot a pending inspection");
  require(activationAction({
              .lifecyclePauseObserved = true,
              .started = true,
              .contentOperationActive = true,
              .inspectionRequired = true,
          }) == ActivationAction::waitForContentOperation,
          "active callback raced an unfinished content operation");
  require(activationAction({
              .lifecyclePauseObserved = true,
              .started = true,
              .inspectionQueued = true,
          }) == ActivationAction::inspectContent,
          "async cancellation completion did not retain inspection intent");
  require(activationAction({
              .lifecyclePauseObserved = true,
              .started = true,
              .pickerPresented = true,
              .inspectionRequired = true,
          }) == ActivationAction::unavailable,
          "foreground transition bypassed an active document picker");
}

} // namespace

int main() {
  stablePublishedMissionSurvivesLifecycle();
  interruptedMissionLoadRestartsWithoutInspection();
  interruptedContentTransactionForcesInspection();
  lifecycleDecisionsRemainFailClosedAcrossAsyncCompletion();
  std::cout << "AirfixIOSContentLifecyclePolicyTests passed\n";
  return EXIT_SUCCESS;
}
