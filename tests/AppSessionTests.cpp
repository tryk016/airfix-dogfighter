#include "airfix/runtime/AppSession.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using airfix::runtime::AppSession;
using airfix::runtime::ContentState;
using airfix::runtime::LifecycleState;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testMissingContentBoot() {
    AppSession session;
    require(session.contentState() == ContentState::missing,
        "new session must start without content");
    require(session.lifecycleState() == LifecycleState::foregroundPaused,
        "new session must start paused in the foreground");
    require(!session.resume(), "missing content must prevent resume");
    require(!session.simulationRunning(), "missing content started simulation");
}

void testLifecycleResetsInput() {
    AppSession session;
    session.setContentState(ContentState::ready);
    require(session.resume(), "ready content did not allow resume");
    session.noteInputActivity();
    require(!session.inputsNeutral(), "active input was not recorded");

    const auto beforeInactive = session.inputResetGeneration();
    session.enterInactive();
    require(session.lifecycleState() == LifecycleState::inactive,
        "inactive transition failed");
    require(session.inputsNeutral(), "inactive transition left input latched");
    require(session.inputResetGeneration() == beforeInactive + 1U,
        "inactive transition did not publish an input reset");

    session.enterBackground();
    require(session.lifecycleState() == LifecycleState::background,
        "background transition failed");
    session.enterForeground();
    require(session.lifecycleState() == LifecycleState::foregroundPaused,
        "foreground transition must return paused");
    require(!session.simulationRunning(), "foreground transition auto-resumed simulation");
    require(session.resume(), "explicit resume after foreground failed");
}

void testContentLossPauses() {
    AppSession session;
    session.setContentState(ContentState::ready);
    require(session.resume(), "ready content did not start");
    session.noteInputActivity();

    session.setContentState(ContentState::rejected);
    require(session.contentState() == ContentState::rejected,
        "rejected content state was not kept");
    require(session.lifecycleState() == LifecycleState::foregroundPaused,
        "content rejection did not pause simulation");
    require(session.inputsNeutral(), "content rejection left input latched");
    require(!session.resume(), "rejected content allowed resume");
}

} // namespace

int main() {
    try {
        testMissingContentBoot();
        testLifecycleResetsInput();
        testContentLossPauses();
        std::cout << "all app session tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "app session test failure: " << error.what() << '\n';
        return 1;
    }
}
