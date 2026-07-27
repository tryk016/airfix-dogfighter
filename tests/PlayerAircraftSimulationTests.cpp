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
    auto first = flightFrame(2U, 1200, -3400);
    first.analogValues[airfix::input::toIndex(
        AnalogAxis::flightThrottleDelta)] = 32767;
    first.analogValues[airfix::input::toIndex(
        AnalogAxis::flightThrottleSet)] = 6000;
    first.analogValues[airfix::input::toIndex(
        AnalogAxis::cameraLookX)] = 7000;
    first.analogValues[airfix::input::toIndex(
        AnalogAxis::cameraLookY)] = -8000;
    setAction(first, DigitalAction::combatWeaponNext, true, false, false);
    setAction(first, DigitalAction::cameraCycle, true, false, false);
    first.weaponSelection = 2U;

    auto second =
        flightFrame(3U, 32767, -32767, true, false, true);
    setAction(second, DigitalAction::combatSecondaryFire,
        true, false, true);
    setAction(second, DigitalAction::cameraRearView, true, false, true);

    auto third = flightFrame(9U, -9000, 4200, false, false, true);
    setAction(third, DigitalAction::combatSecondaryFire,
        false, false, true);
    setAction(third, DigitalAction::cameraRearView, false, false, true);

    auto fourth = flightFrame(15U, 0, 0, false, true, false);
    setAction(fourth, DigitalAction::combatSecondaryFire,
        false, true, false);
    setAction(fourth, DigitalAction::cameraRearView, false, true, false);

    auto fifth = flightFrame(16U, -7, 11, true, true, false);
    fifth.analogValues[airfix::input::toIndex(
        AnalogAxis::flightThrottleDelta)] = -111;
    fifth.analogValues[airfix::input::toIndex(
        AnalogAxis::flightThrottleSet)] = 222;
    fifth.analogValues[airfix::input::toIndex(
        AnalogAxis::cameraLookX)] = -333;
    fifth.analogValues[airfix::input::toIndex(
        AnalogAxis::cameraLookY)] = 444;
    setAction(fifth, DigitalAction::combatSecondaryFire,
        true, true, false);
    setAction(fifth, DigitalAction::combatWeaponNext,
        true, false, false);
    setAction(fifth, DigitalAction::cameraRearView, true, true, false);
    setAction(fifth, DigitalAction::cameraRecenter, true, false, false);
    setAction(fifth, DigitalAction::missionStatus, true, false, false);
    setAction(fifth, DigitalAction::globalPause, true, false, false);
    fifth.weaponSelection = 7U;

    const InputFrame replay[] = {first, second, third, fourth, fifth};

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
    require(first.throttleDeltaIntentQ15 == -111 &&
            first.throttleSetIntentQ15 == 222 &&
            first.cameraLookXIntentQ15 == -333 &&
            first.cameraLookYIntentQ15 == 444,
        "replay lost extended analog intentions");
    require(!first.primaryFireHeld, "replay final fire held state is wrong");
    require(!first.secondaryFireHeld && !first.rearViewHeld,
        "replay final extended held state is wrong");
    require(first.primaryFirePressCount == 2U,
        "replay lost exact fire press edges");
    require(first.primaryFireReleaseCount == 2U,
        "replay lost exact fire release edges");
    require(first.secondaryFirePressCount == 2U &&
            first.secondaryFireReleaseCount == 2U &&
            first.rearViewPressCount == 2U &&
            first.rearViewReleaseCount == 2U,
        "replay lost extended held-action edges");
    require(first.weaponNextPressCount == 2U &&
            first.cameraCyclePressCount == 1U &&
            first.cameraRecenterPressCount == 1U &&
            first.missionStatusPressCount == 1U &&
            first.pausePressCount == 1U,
        "replay lost one-shot gameplay actions");
    require(first.weaponSelectionCount == 2U &&
            first.selectedWeapon == 7U,
        "replay lost direct weapon selection");
    require(first.completedSteps == 5U && first.lastInputTick == 16U,
        "replay step/tick accounting is wrong");

    constexpr std::uint64_t expectedGoldenHash = 745229508997165290ULL;
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

    auto invalidWeapon = flightFrame(21U, 1, 2);
    invalidWeapon.weaponSelection = airfix::input::weaponSlotCount;
    requireRejectedUnchanged(state, invalidWeapon,
        PlayerAircraftAdvanceError::invalidWeaponSelection,
        "invalid direct weapon selection");
}

void testCounterOverflowIsAtomic() {
    auto stepOverflow = replayScenario();
    stepOverflow.completedSteps = std::numeric_limits<std::uint64_t>::max();
    stepOverflow.lastInputTick = 100U;
    requireRejectedUnchanged(stepOverflow, flightFrame(101U, 1, 2),
        PlayerAircraftAdvanceError::counterOverflow, "step counter overflow");

    struct EdgeCounterCase final {
        std::uint64_t PlayerAircraftState::*counter;
        DigitalAction action;
        bool release;
        const char* label;
    };
    constexpr EdgeCounterCase edgeCounters[] = {
        {&PlayerAircraftState::primaryFirePressCount,
            DigitalAction::combatPrimaryFire, false, "primary press"},
        {&PlayerAircraftState::primaryFireReleaseCount,
            DigitalAction::combatPrimaryFire, true, "primary release"},
        {&PlayerAircraftState::secondaryFirePressCount,
            DigitalAction::combatSecondaryFire, false, "secondary press"},
        {&PlayerAircraftState::secondaryFireReleaseCount,
            DigitalAction::combatSecondaryFire, true, "secondary release"},
        {&PlayerAircraftState::weaponNextPressCount,
            DigitalAction::combatWeaponNext, false, "weapon next"},
        {&PlayerAircraftState::cameraCyclePressCount,
            DigitalAction::cameraCycle, false, "camera cycle"},
        {&PlayerAircraftState::rearViewPressCount,
            DigitalAction::cameraRearView, false, "rear-view press"},
        {&PlayerAircraftState::rearViewReleaseCount,
            DigitalAction::cameraRearView, true, "rear-view release"},
        {&PlayerAircraftState::cameraRecenterPressCount,
            DigitalAction::cameraRecenter, false, "camera recenter"},
        {&PlayerAircraftState::missionStatusPressCount,
            DigitalAction::missionStatus, false, "mission status"},
        {&PlayerAircraftState::pausePressCount,
            DigitalAction::globalPause, false, "pause"},
    };

    for (const auto& counterCase : edgeCounters) {
        auto overflow = replayScenario();
        overflow.*(counterCase.counter) =
            std::numeric_limits<std::uint64_t>::max();
        auto frame = flightFrame(17U, 1, 2);
        setAction(frame, counterCase.action, !counterCase.release,
            counterCase.release, false);
        requireRejectedUnchanged(overflow, frame,
            PlayerAircraftAdvanceError::counterOverflow,
            std::string(counterCase.label) + " counter overflow");
    }

    auto selectionOverflow = replayScenario();
    selectionOverflow.weaponSelectionCount =
        std::numeric_limits<std::uint64_t>::max();
    auto selectionFrame = flightFrame(17U, 1, 2);
    selectionFrame.weaponSelection = 3U;
    requireRejectedUnchanged(selectionOverflow, selectionFrame,
        PlayerAircraftAdvanceError::counterOverflow,
        "weapon selection counter overflow");

    auto maxDormantCounts = replayScenario();
    for (const auto& counterCase : edgeCounters) {
        maxDormantCounts.*(counterCase.counter) =
            std::numeric_limits<std::uint64_t>::max();
    }
    maxDormantCounts.weaponSelectionCount =
        std::numeric_limits<std::uint64_t>::max();
    const auto accepted = airfix::simulation::advance(
        maxDormantCounts, flightFrame(17U, 3, 4));
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
    require(router.enqueue(PhysicalEvent::axis(
        4U, 1003U, touch, airfix::input::controls::touch::throttleSet, 19000)),
        "router rejected throttle target");
    require(router.enqueue(PhysicalEvent::button(
        5U, 1004U, touch,
        airfix::input::controls::touch::throttleIncrease, true)),
        "router rejected throttle increase");
    require(router.enqueue(PhysicalEvent::axis(
        6U, 1005U, touch, airfix::input::controls::touch::lookX, -6000)),
        "router rejected camera look X");
    require(router.enqueue(PhysicalEvent::axis(
        7U, 1006U, touch, airfix::input::controls::touch::lookY, 7000)),
        "router rejected camera look Y");
    require(router.enqueue(PhysicalEvent::button(
        8U, 1007U, touch,
        airfix::input::controls::touch::secondaryFire, true)),
        "router rejected secondary-fire press");
    require(router.enqueue(PhysicalEvent::button(
        9U, 1008U, touch, airfix::input::controls::touch::weaponNext, true)),
        "router rejected weapon-next press");
    require(router.enqueue(PhysicalEvent::button(
        10U, 1009U, touch, airfix::input::controls::touch::rearView, true)),
        "router rejected rear-view press");
    require(router.enqueue(PhysicalEvent::button(
        11U, 1010U, touch, airfix::input::controls::touch::cameraCycle, true)),
        "router rejected camera-cycle press");
    require(router.enqueue(PhysicalEvent::button(
        12U, 1011U, touch,
        airfix::input::controls::touch::cameraRecenter, true)),
        "router rejected camera-recenter press");
    require(router.enqueue(PhysicalEvent::button(
        13U, 1012U, touch,
        airfix::input::controls::touch::missionStatus, true)),
        "router rejected mission-status press");
    require(router.enqueue(PhysicalEvent::selectWeapon(
        14U, 1013U, touch,
        airfix::input::controls::touch::weaponSelection, 5U)),
        "router rejected direct weapon selection");

    PlayerAircraftState state;
    auto result = airfix::simulation::advance(state, router.tick(100U));
    require(result.accepted(), "router frame was rejected by simulation");
    state = result.state;
    require(state.bankIntentQ15 == 14000 && state.pitchIntentQ15 == -17000,
        "router intentions changed at the simulation boundary");
    require(state.throttleDeltaIntentQ15 == airfix::input::q15One &&
            state.throttleSetIntentQ15 == 19000 &&
            state.cameraLookXIntentQ15 == -6000 &&
            state.cameraLookYIntentQ15 == 7000,
        "router extended analog intentions changed at the simulation boundary");
    require(state.primaryFireHeld && state.primaryFirePressCount == 1U &&
            state.primaryFireReleaseCount == 0U,
        "router fire press was not represented exactly");
    require(state.secondaryFireHeld &&
            state.secondaryFirePressCount == 1U &&
            state.weaponNextPressCount == 1U &&
            state.rearViewHeld && state.rearViewPressCount == 1U &&
            state.cameraCyclePressCount == 1U &&
            state.cameraRecenterPressCount == 1U &&
            state.missionStatusPressCount == 1U,
        "router extended actions were not represented exactly");
    require(state.selectedWeapon == 5U &&
            state.weaponSelectionCount == 1U,
        "router direct weapon selection was not retained");

    require(router.enqueue(PhysicalEvent::button(
        15U, 2000U, touch, airfix::input::controls::touch::primaryFire, false)),
        "router rejected fire release");
    require(router.enqueue(PhysicalEvent::button(
        16U, 2001U, touch,
        airfix::input::controls::touch::secondaryFire, false)),
        "router rejected secondary-fire release");
    require(router.enqueue(PhysicalEvent::button(
        17U, 2002U, touch, airfix::input::controls::touch::rearView, false)),
        "router rejected rear-view release");
    require(router.enqueue(PhysicalEvent::button(
        18U, 2003U, touch,
        airfix::input::controls::touch::throttleIncrease, false)),
        "router rejected throttle-increase release");
    result = airfix::simulation::advance(state, router.tick(104U));
    require(result.accepted(), "gapped router frame was rejected");
    require(!result.state.primaryFireHeld &&
            result.state.primaryFirePressCount == 1U &&
            result.state.primaryFireReleaseCount == 1U,
        "router fire release was not represented exactly");
    require(!result.state.secondaryFireHeld &&
            result.state.secondaryFireReleaseCount == 1U &&
            !result.state.rearViewHeld &&
            result.state.rearViewReleaseCount == 1U &&
            result.state.throttleDeltaIntentQ15 == 0,
        "router extended releases were not represented exactly");
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
