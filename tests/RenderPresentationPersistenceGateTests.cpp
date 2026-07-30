#include "airfix/settings/RenderPresentationPersistenceGate.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {

using airfix::settings::RenderPresentationPersistenceAdvance;
using airfix::settings::RenderPresentationPersistenceGate;
using airfix::settings::RenderPresentationPersistencePhase;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] airfix::render::RenderPresentationSettings candidate(
    const float scale = 150.0F) {
    return {
        .renderScalePercent = scale,
        .scenePresentation =
            airfix::render::ScenePresentationMode::originalFourByThree,
        .visualProfile = airfix::render::VisualProfile::enhanced,
        .diagnosticsOverlayEnabled = true,
    };
}

void testNewCandidateRequiresOneSaveBeforeCommit() {
    RenderPresentationPersistenceGate gate;
    const auto ticket = gate.begin(candidate(), {}, false);
    require(ticket.has_value(), "new candidate did not begin");
    require(ticket->serial == 1U && ticket->preparationAttempt == 0U,
            "first persistence ticket identity is wrong");
    require(
        gate.preparationSucceeded(*ticket) ==
            RenderPresentationPersistenceAdvance::save,
        "non-durable candidate skipped saving");
    require(
        gate.isCurrent(
            *ticket, RenderPresentationPersistencePhase::saving),
        "prepared candidate did not enter saving");
    require(!gate.consumeCommitted(*ticket),
            "non-durable candidate committed before saving");
    require(gate.saveSucceeded(*ticket),
            "exact save completion was rejected");
    require(
        gate.isCurrent(
            *ticket, RenderPresentationPersistencePhase::committing),
        "saved candidate did not enter committing");
    require(gate.consumeCommitted(*ticket),
            "saved candidate did not commit");
    require(!gate.busy(), "commit left persistence work outstanding");
}

void testLoadedCandidateSkipsSave() {
    RenderPresentationPersistenceGate gate;
    const auto ticket = gate.begin(candidate(50.0F), {}, true);
    require(ticket.has_value(), "loaded candidate did not begin");
    require(
        gate.preparationSucceeded(*ticket) ==
            RenderPresentationPersistenceAdvance::commit,
        "already-durable candidate requested another save");
    require(!gate.saveSucceeded(*ticket),
            "commit-phase loaded candidate accepted a save callback");
    require(gate.consumeCommitted(*ticket),
            "loaded candidate did not commit");
}

void testStaleCommitRepreparesWithoutSavingAgain() {
    RenderPresentationPersistenceGate gate;
    const auto first = gate.begin(candidate(), {}, false);
    require(first.has_value(), "stale fixture did not begin");
    require(
        gate.preparationSucceeded(*first) ==
            RenderPresentationPersistenceAdvance::save,
        "stale fixture did not request initial save");
    require(gate.saveSucceeded(*first),
            "stale fixture did not become durable");

    const auto retry = gate.retryAfterStaleCommit(*first);
    require(retry.has_value(), "durable stale candidate did not retry");
    require(retry->serial == first->serial &&
            retry->preparationAttempt == 1U &&
            retry->persistentBase == first->persistentBase &&
            retry->effectiveSettings == first->effectiveSettings,
            "stale retry changed candidate identity");
    require(
        gate.preparationSucceeded(*retry) ==
            RenderPresentationPersistenceAdvance::commit,
        "durable retry returned to saving");
    require(!gate.saveSucceeded(*retry),
            "durable retry accepted a duplicate save");
    require(gate.consumeCommitted(*retry),
            "reprepared durable candidate did not commit");
}

void testStaleCallbacksCannotAdvanceOrAbandonNewAttempt() {
    RenderPresentationPersistenceGate gate;
    const auto first = gate.begin(candidate(), {}, true);
    require(first.has_value(), "stale callback fixture did not begin");
    require(
        gate.preparationSucceeded(*first) ==
            RenderPresentationPersistenceAdvance::commit,
        "stale callback fixture did not reach commit");
    const auto retry = gate.retryAfterStaleCommit(*first);
    require(retry.has_value(), "stale callback fixture did not retry");

    require(
        gate.preparationSucceeded(*first) ==
            RenderPresentationPersistenceAdvance::rejected,
        "old attempt advanced the new preparation");
    require(!gate.abandon(*first),
            "old attempt abandoned the new preparation");
    require(
        gate.isCurrent(
            *retry, RenderPresentationPersistencePhase::preparing),
        "rejected old callback changed the new attempt");
    require(!gate.abandon(*retry),
            "durable new attempt was incorrectly abandoned");
    require(
        gate.preparationSucceeded(*retry) ==
            RenderPresentationPersistenceAdvance::commit,
        "exact new attempt did not return to commit");
    require(gate.consumeCommitted(*retry),
            "exact new attempt could not commit");
}

void testDurableObligationSurvivesRepeatedStaleResults() {
    RenderPresentationPersistenceGate gate;
    auto ticket = gate.begin(candidate(), {}, true);
    require(ticket.has_value(), "durable retry fixture did not begin");
    for (std::uint64_t attempt = 0U; attempt < 64U; ++attempt) {
        require(
            gate.preparationSucceeded(*ticket) ==
                RenderPresentationPersistenceAdvance::commit,
            "durable retry preparation did not reach commit");
        const auto retry = gate.retryAfterStaleCommit(*ticket);
        require(retry.has_value(),
                "durable obligation stopped retrying");
        ticket = retry;
    }
    require(
        gate.preparationSucceeded(*ticket) ==
            RenderPresentationPersistenceAdvance::commit,
        "final durable retry did not reach commit");
    require(!gate.abandon(*ticket),
            "durable obligation was incorrectly abandoned");
    require(gate.consumeCommitted(*ticket),
            "repeatedly stale durable candidate did not commit");
}

void testInvalidBusyWrongThreadAndOverflowFailClosed() {
    RenderPresentationPersistenceGate gate;
    auto invalid = candidate();
    invalid.renderScalePercent = 0.0F;
    require(!gate.begin(invalid, {}, false).has_value(),
            "invalid settings received a ticket");

    const auto active = gate.begin(candidate(), {}, false);
    require(active.has_value(), "busy fixture did not begin");
    require(!gate.begin(candidate(200.0F), {}, false).has_value(),
            "busy gate issued a concurrent ticket");

    bool wrongThreadAbandoned = true;
    bool wrongThreadBegan = true;
    std::thread worker([&] {
        wrongThreadAbandoned = gate.abandon(*active);
        wrongThreadBegan =
            gate.begin(candidate(200.0F), {}, false).has_value();
    });
    worker.join();
    require(!wrongThreadAbandoned && !wrongThreadBegan,
            "non-owner thread mutated the gate");
    require(
        gate.isCurrent(
            *active, RenderPresentationPersistencePhase::preparing),
        "wrong-thread calls changed owner state");
    require(gate.abandon(*active),
            "owner could not abandon after wrong-thread rejection");

    RenderPresentationPersistenceGate exhausted(
        std::numeric_limits<std::uint64_t>::max() - 1U);
    const auto last = exhausted.begin(candidate(), {}, true);
    require(last.has_value() &&
            last->serial == std::numeric_limits<std::uint64_t>::max(),
            "last representable persistence serial was not issued");
    require(exhausted.exhausted(), "maximum serial was not exhausted");
    require(
        exhausted.preparationSucceeded(*last) ==
            RenderPresentationPersistenceAdvance::commit,
        "last representable durable ticket did not reach commit");
    require(exhausted.consumeCommitted(*last),
            "last representable durable ticket could not commit");
    require(!exhausted.begin(candidate(), {}, true).has_value(),
            "persistence serial wrapped after exhaustion");
}

void testPersistentBaseAndSessionOverrideRemainSeparate() {
    RenderPresentationPersistenceGate gate;
    const auto base = candidate(75.0F);
    airfix::render::RenderPresentationSettingsOverride overrides;
    overrides.renderScalePercent = 200.0F;
    overrides.diagnosticsOverlayEnabled = false;

    const auto ticket = gate.begin(base, overrides, false);
    require(ticket.has_value(), "base/override fixture did not begin");
    require(ticket->persistentBase == base,
            "session override changed the persistent base");
    require(ticket->effectiveSettings.renderScalePercent == 200.0F &&
            !ticket->effectiveSettings.diagnosticsOverlayEnabled,
            "session override was not applied to the effective snapshot");
    require(ticket->effectiveSettings.scenePresentation ==
                base.scenePresentation &&
            ticket->effectiveSettings.visualProfile ==
                base.visualProfile,
            "absent overrides changed unrelated effective fields");
    require(gate.abandon(*ticket),
            "non-durable base/override fixture could not be abandoned");
}

} // namespace

int main() {
    try {
        testNewCandidateRequiresOneSaveBeforeCommit();
        testLoadedCandidateSkipsSave();
        testStaleCommitRepreparesWithoutSavingAgain();
        testStaleCallbacksCannotAdvanceOrAbandonNewAttempt();
        testDurableObligationSurvivesRepeatedStaleResults();
        testInvalidBusyWrongThreadAndOverflowFailClosed();
        testPersistentBaseAndSessionOverrideRemainSeparate();
        std::cout << "render-presentation persistence gate tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "render-presentation persistence gate test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
