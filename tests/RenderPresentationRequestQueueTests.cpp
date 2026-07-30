#include "airfix/settings/RenderPresentationRequestQueue.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

using airfix::settings::RenderPresentationRequestDisposition;
using airfix::settings::RenderPresentationRequestPhase;
using airfix::settings::RenderPresentationRequestQueue;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] airfix::render::RenderPresentationSettings
candidate(const float scale) {
  return {
      .renderScalePercent = scale,
      .scenePresentation =
          airfix::render::ScenePresentationMode::widescreenHorPlus,
      .visualProfile = airfix::render::VisualProfile::classic,
      .diagnosticsOverlayEnabled = false,
  };
}

void testLatestCompletePendingRequestWins() {
  RenderPresentationRequestQueue queue;
  const auto active = queue.submit(candidate(100.0F), true);
  require(active.disposition == RenderPresentationRequestDisposition::start &&
              active.request.has_value() && !active.superseded.has_value(),
          "first request did not start");

  const auto firstPending = queue.submit(candidate(125.0F), true);
  require(firstPending.disposition ==
                  RenderPresentationRequestDisposition::queued &&
              firstPending.request.has_value() &&
              !firstPending.superseded.has_value(),
          "first concurrent request did not queue");

  const auto latest = queue.submit(candidate(150.0F), true);
  require(latest.disposition == RenderPresentationRequestDisposition::queued &&
              latest.request.has_value() && latest.superseded.has_value() &&
              *latest.superseded == *firstPending.request,
          "new pending request did not supersede the complete older request");
  require(queue.pendingRequest().has_value() &&
              *queue.pendingRequest() == *latest.request,
          "latest complete pending request was not retained");

  require(queue.beginCompletion(*active.request),
          "active request could not begin completion");
  require(queue.finishCompletion(*active.request),
          "active request could not finish completion");
  const auto promoted = queue.activatePending();
  require(promoted.has_value() && *promoted == *latest.request,
          "superseded request started instead of latest request");
}

void testExternalBusyDefersUntilExplicitActivation() {
  RenderPresentationRequestQueue queue;
  const auto deferred = queue.submit(candidate(125.0F), false);
  require(
      deferred.disposition == RenderPresentationRequestDisposition::queued &&
          deferred.request.has_value() && !deferred.superseded.has_value() &&
          queue.phase() == RenderPresentationRequestPhase::idle,
      "external busy state did not defer the request");
  require(!queue.activeRequest().has_value() &&
              queue.pendingRequest().has_value() &&
              *queue.pendingRequest() == *deferred.request,
          "deferred request entered the wrong slot");

  const auto activated = queue.activatePending();
  require(activated.has_value() && *activated == *deferred.request &&
              queue.phase() == RenderPresentationRequestPhase::active &&
              !queue.pendingRequest().has_value(),
          "deferred request did not activate after external work released");
}

void testCompletionRequiresExactActiveIdentity() {
  RenderPresentationRequestQueue queue;
  const auto active = queue.submit(candidate(100.0F), true);
  require(active.request.has_value(), "identity fixture did not start");

  auto forgedSerial = *active.request;
  ++forgedSerial.serial;
  require(!queue.beginCompletion(forgedSerial),
          "forged serial completed the active request");

  auto forgedCandidate = *active.request;
  forgedCandidate.candidate.renderScalePercent = 200.0F;
  require(!queue.beginCompletion(forgedCandidate),
          "forged candidate completed the active request");
  require(queue.phase() == RenderPresentationRequestPhase::active &&
              queue.activeRequest().has_value() &&
              *queue.activeRequest() == *active.request,
          "rejected completion changed active identity");
}

void testStaleAndDuplicateCompletionsAreRejected() {
  RenderPresentationRequestQueue queue;
  const auto first = queue.submit(candidate(100.0F), true);
  require(first.request.has_value() && queue.beginCompletion(*first.request) &&
              queue.finishCompletion(*first.request),
          "first completion fixture failed");
  require(!queue.beginCompletion(*first.request) &&
              !queue.finishCompletion(*first.request),
          "duplicate completion was accepted");

  const auto second = queue.submit(candidate(150.0F), true);
  require(second.request.has_value(),
          "second completion fixture did not start");
  require(!queue.beginCompletion(*first.request),
          "stale request completed newer active work");
  require(queue.beginCompletion(*second.request),
          "exact newer completion was rejected");
  require(!queue.beginCompletion(*second.request),
          "duplicate begin-completion was accepted");
  require(queue.finishCompletion(*second.request),
          "exact newer finish-completion was rejected");
  require(!queue.finishCompletion(*second.request),
          "duplicate finish-completion was accepted");
}

void testReentrantEnqueueWaitsUntilCompletionReturns() {
  RenderPresentationRequestQueue queue;
  const auto active = queue.submit(candidate(100.0F), true);
  require(active.request.has_value() && queue.beginCompletion(*active.request),
          "reentrant fixture did not begin completion");

  const auto reentrant = queue.submit(candidate(175.0F), true);
  require(
      reentrant.disposition == RenderPresentationRequestDisposition::queued &&
          reentrant.request.has_value() &&
          queue.phase() == RenderPresentationRequestPhase::deliveringCompletion,
      "reentrant request started during completion delivery");
  require(!queue.activatePending().has_value(),
          "pending work activated inside completion delivery");

  require(queue.finishCompletion(*active.request),
          "reentrant fixture could not finish completion");
  const auto promoted = queue.activatePending();
  require(promoted.has_value() && *promoted == *reentrant.request,
          "reentrant request did not start after completion returned");
}

void testSerialExhaustionFailsClosedWithoutWrap() {
  RenderPresentationRequestQueue queue(
      std::numeric_limits<std::uint64_t>::max() - 1U);
  const auto last = queue.submit(candidate(100.0F), true);
  require(last.request.has_value() &&
              last.request->serial ==
                  std::numeric_limits<std::uint64_t>::max() &&
              queue.exhausted(),
          "last representable request serial was not issued");
  require(queue.beginCompletion(*last.request) &&
              queue.finishCompletion(*last.request),
          "last representable request could not complete");

  const auto rejected = queue.submit(candidate(200.0F), true);
  require(rejected.disposition ==
                  RenderPresentationRequestDisposition::exhausted &&
              !rejected.request.has_value() &&
              !rejected.superseded.has_value() && !queue.hasOutstandingWork(),
          "request serial wrapped after exhaustion");
}

} // namespace

int main() {
  try {
    testLatestCompletePendingRequestWins();
    testExternalBusyDefersUntilExplicitActivation();
    testCompletionRequiresExactActiveIdentity();
    testStaleAndDuplicateCompletionsAreRejected();
    testReentrantEnqueueWaitsUntilCompletionReturns();
    testSerialExhaustionFailsClosedWithoutWrap();
    std::cout << "render-presentation request queue tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "render-presentation request queue test failure: "
              << error.what() << '\n';
    return 1;
  }
}
