#include "airfix/input/InputRouter.hpp"
#include "airfix/simulation/PlayerAircraftSimulation.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::DigitalAction;
using airfix::input::InputFrame;
using airfix::simulation::PlayerAircraftAdvanceError;
using airfix::simulation::PlayerAircraftState;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void setAction(
    InputFrame& frame,
    const DigitalAction action,
    const bool pressed,
    const bool released,
    const bool held) {
    const auto index = airfix::input::toIndex(action);
    const auto mask = std::uint64_t{1U} << (index % 64U);
    const auto word = index / 64U;
    if (pressed) {
        frame.pressedBits[word] |= mask;
    }
    if (released) {
        frame.releasedBits[word] |= mask;
    }
    if (held) {
        frame.heldBits[word] |= mask;
    }
}

InputFrame flightFrame(
    const std::uint64_t tick,
    const airfix::input::Q15 bank,
    const airfix::input::Q15 pitch,
    const bool pressed = false,
    const bool released = false,
    const bool held = false) {
    InputFrame frame;
    frame.simulationTick = tick;
    frame.analogValues[airfix::input::toIndex(AnalogAxis::flightBank)] = bank;
    frame.analogValues[airfix::input::toIndex(AnalogAxis::flightPitch)] = pitch;
    setAction(frame, DigitalAction::combatPrimaryFire, pressed, released, held);
    return frame;
}

void requireRejectedUnchanged(
    const PlayerAircraftState& state,
    const InputFrame& frame,
    const PlayerAircraftAdvanceError expected,
    const std::string& label) {
    const auto result = airfix::simulation::advance(state, frame);
    require(!result.accepted(), label + " was accepted");
    require(result.error == expected, label + " returned the wrong error");
    require(result.state == state, label + " partially changed state");
}

PlayerAircraftState replayScenario() {
    PlayerAircraftState state;
    const InputFrame replay[] = {
        flightFrame(2U, 1200, -3400),
        flightFrame(3U, 32767, -32767, true, false, true),
        flightFrame(9U, -9000, 4200, false, false, true),
        flightFrame(15U, 0, 0, false, true, false),
        flightFrame(16U, -7, 11, true, true, false),
    };

    for (const auto& frame : replay) {
        const auto result = airfix::simulation::advance(state, frame);
        require(result.accepted(), "valid replay frame was rejected");
        state = result.state;
    }
    return state;
}

void testReplayAndGoldenHash() {
    const auto first = replayScenario();
    const auto second = replayScenario();
    require(first == second, "identical replay produced different state");
    require(first.bankIntentQ15 == -7 && first.pitchIntentQ15 == 11,
        "replay did not preserve final uninterpreted intentions");
    require(!first.primaryFireHeld, "replay final fire held state is wrong");
    require(first.primaryFirePressCount == 2U,
        "replay lost exact fire press edges");
    require(first.primaryFireReleaseCount == 2U,
        "replay lost exact fire release edges");
    require(first.completedSteps == 5U && first.lastInputTick == 16U,
        "replay step/tick accounting is wrong");

    constexpr std::uint64_t expectedGoldenHash = 3869944463550016182ULL;
    const auto hash = airfix::simulation::canonicalHash(first);
    require(hash == expectedGoldenHash,
        "canonical replay hash changed: " + std::to_string(hash));
    require(hash == airfix::simulation::canonicalHash(second),
        "identical replay produced different canonical hash");
}

void testTickGapsAndQ15Edges() {
    PlayerAircraftState state;
    auto result = airfix::simulation::advance(
        state, flightFrame(0U, airfix::input::q15Min, airfix::input::q15One));
    require(result.accepted(), "tick zero or valid Q15 extremes were rejected");
    state = result.state;
    require(state.bankIntentQ15 == airfix::input::q15Min &&
            state.pitchIntentQ15 == airfix::input::q15One,
        "valid Q15 extremes were changed");

    result = airfix::simulation::advance(
        state, flightFrame(std::numeric_limits<std::uint64_t>::max(), 17, -23));
    require(result.accepted(), "maximum tick gap was rejected");
    require(result.state.completedSteps == 2U,
        "tick gap was mistaken for elapsed simulation steps");
    require(result.state.lastInputTick ==
            std::numeric_limits<std::uint64_t>::max(),
        "maximum input tick was not preserved");

    requireRejectedUnchanged(result.state,
        flightFrame(std::numeric_limits<std::uint64_t>::max(), 0, 0),
        PlayerAircraftAdvanceError::nonIncreasingInputTick,
        "duplicate maximum tick");
}

void testInvalidFramesAreAtomic() {
    const auto accepted =
        airfix::simulation::advance({}, flightFrame(20U, 10, -10));
    require(accepted.accepted(), "test setup frame was rejected");
    const auto state = accepted.state;

    auto wrongSchema = flightFrame(21U, 1, 2);
    ++wrongSchema.schemaVersion;
    requireRejectedUnchanged(state, wrongSchema,
        PlayerAircraftAdvanceError::unsupportedInputSchema, "wrong schema");

    requireRejectedUnchanged(state, flightFrame(20U, 1, 2),
        PlayerAircraftAdvanceError::nonIncreasingInputTick, "duplicate tick");
    requireRejectedUnchanged(state, flightFrame(19U, 1, 2),
        PlayerAircraftAdvanceError::nonIncreasingInputTick, "backward tick");

    for (std::size_t axis = 0U; axis < airfix::input::analogAxisCount; ++axis) {
        auto invalid = flightFrame(21U, 1, 2);
        invalid.analogValues[axis] =
            std::numeric_limits<airfix::input::Q15>::min();
        requireRejectedUnchanged(state, invalid,
            PlayerAircraftAdvanceError::invalidQ15,
            "invalid Q15 on axis " + std::to_string(axis));
    }
}

void testCounterOverflowIsAtomic() {
    auto stepOverflow = replayScenario();
    stepOverflow.completedSteps = std::numeric_limits<std::uint64_t>::max();
    stepOverflow.lastInputTick = 100U;
    requireRejectedUnchanged(stepOverflow, flightFrame(101U, 1, 2),
        PlayerAircraftAdvanceError::counterOverflow, "step counter overflow");

    auto pressOverflow = replayScenario();
    pressOverflow.primaryFirePressCount =
        std::numeric_limits<std::uint64_t>::max();
    requireRejectedUnchanged(pressOverflow,
        flightFrame(17U, 1, 2, true, false, true),
        PlayerAircraftAdvanceError::counterOverflow, "press counter overflow");

    auto releaseOverflow = replayScenario();
    releaseOverflow.primaryFireReleaseCount =
        std::numeric_limits<std::uint64_t>::max();
    requireRejectedUnchanged(releaseOverflow,
        flightFrame(17U, 1, 2, false, true, false),
        PlayerAircraftAdvanceError::counterOverflow, "release counter overflow");

    auto maxDormantCounts = replayScenario();
    maxDormantCounts.primaryFirePressCount =
        std::numeric_limits<std::uint64_t>::max();
    maxDormantCounts.primaryFireReleaseCount =
        std::numeric_limits<std::uint64_t>::max();
    const auto accepted =
        airfix::simulation::advance(maxDormantCounts, flightFrame(17U, 3, 4));
    require(accepted.accepted(),
        "maximum dormant edge counts incorrectly overflowed");
}

void testInputRouterIntegration() {
    using airfix::input::InputRouter;
    using airfix::input::PhysicalEvent;
    using airfix::input::SourceHandle;
    using airfix::input::SourceKind;

    InputRouter router;
    constexpr SourceHandle touch{SourceKind::touch, 1U};
    require(router.enqueue(PhysicalEvent::axis(
        1U, 1000U, touch, airfix::input::controls::touch::bank, 14000)),
        "router rejected bank event");
    require(router.enqueue(PhysicalEvent::axis(
        2U, 1001U, touch, airfix::input::controls::touch::pitch, -17000)),
        "router rejected pitch event");
    require(router.enqueue(PhysicalEvent::button(
        3U, 1002U, touch, airfix::input::controls::touch::primaryFire, true)),
        "router rejected fire press");

    PlayerAircraftState state;
    auto result = airfix::simulation::advance(state, router.tick(100U));
    require(result.accepted(), "router frame was rejected by simulation");
    state = result.state;
    require(state.bankIntentQ15 == 14000 && state.pitchIntentQ15 == -17000,
        "router intentions changed at the simulation boundary");
    require(state.primaryFireHeld && state.primaryFirePressCount == 1U &&
            state.primaryFireReleaseCount == 0U,
        "router fire press was not represented exactly");

    require(router.enqueue(PhysicalEvent::button(
        4U, 2000U, touch, airfix::input::controls::touch::primaryFire, false)),
        "router rejected fire release");
    result = airfix::simulation::advance(state, router.tick(104U));
    require(result.accepted(), "gapped router frame was rejected");
    require(!result.state.primaryFireHeld &&
            result.state.primaryFirePressCount == 1U &&
            result.state.primaryFireReleaseCount == 1U,
        "router fire release was not represented exactly");
    require(result.state.completedSteps == 2U &&
            result.state.lastInputTick == 104U,
        "router integration confused ticks with completed steps");
}

static_assert(std::is_trivially_copyable_v<PlayerAircraftState>);
static_assert(noexcept(airfix::simulation::advance(
    PlayerAircraftState{}, InputFrame{})));
static_assert(noexcept(airfix::simulation::canonicalHash(
    PlayerAircraftState{})));

} // namespace

int main() {
    try {
        testReplayAndGoldenHash();
        testTickGapsAndQ15Edges();
        testInvalidFramesAreAtomic();
        testCounterOverflowIsAtomic();
        testInputRouterIntegration();
        std::cout << "all player aircraft simulation tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "player aircraft simulation test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
