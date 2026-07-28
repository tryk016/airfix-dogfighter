#include "airfix/simulation/LegacyAircraftEngineAudioState.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::simulation::LegacyAircraftEngineAudioCommand;
using airfix::simulation::LegacyAircraftEngineAudioCommandKind;
using airfix::simulation::LegacyAircraftEngineAudioInput;
using airfix::simulation::LegacyAircraftEngineAudioState;
using airfix::simulation::LegacyAircraftEngineAudioStep;
using airfix::simulation::LegacyAircraftEngineSound;
using airfix::simulation::legacyAircraftAdvanceEngineAudio;
using airfix::simulation::legacyAircraftEngineAudioCadenceLimit;
using airfix::simulation::legacyAircraftEngineAudioLoadOffset;
using airfix::simulation::legacyAircraftEngineAudioLoadThrottleScale;
using airfix::simulation::legacyAircraftEngineAudioPitchScale;
using airfix::simulation::legacyAircraftEngineIdlePitchScale;
using airfix::simulation::legacyAircraftEngineStartDurationSeconds;
using airfix::simulation::legacyAircraftEngineStartThrottleThreshold;
using airfix::simulation::legacyAircraftMaximumEngineAudioCommands;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(
    const float actual,
    const float expected,
    const float tolerance,
    const std::string& message) {
    require(
        std::fabs(actual - expected) <= tolerance,
        message + ": expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
}

void requireCommand(
    const LegacyAircraftEngineAudioStep& step,
    const std::size_t index,
    const LegacyAircraftEngineAudioCommandKind kind,
    const LegacyAircraftEngineSound sound,
    const std::uint8_t parameterIndex,
    const float value,
    const std::string& message) {
    require(index < step.commandCount, message + ": missing command");
    const LegacyAircraftEngineAudioCommand& command = step.commands[index];
    require(command.kind == kind, message + ": wrong kind");
    require(command.sound == sound, message + ": wrong sound");
    require(
        command.parameterIndex == parameterIndex,
        message + ": wrong parameter");
    requireNear(command.value, value, 1.0e-6F, message + ": wrong value");
}

LegacyAircraftEngineAudioInput cadenceInput(
    const float smoothedThrust = 0.5F) {
    return {
        .deltaSeconds = 0.012F,
        .smoothedThrust = smoothedThrust,
        .speedMagnitude = 2.0F,
        .smoothedOrientationM01 = -0.25F,
    };
}

void testRecoveredConstantsAndIds() {
    static_assert(legacyAircraftEngineAudioCadenceLimit == 4U);
    static_assert(legacyAircraftMaximumEngineAudioCommands == 14U);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftEngineAudioLoadThrottleScale) == 0x3E4CCCCDU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftEngineAudioLoadOffset) == 0x3E99999AU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftEngineAudioPitchScale) == 0x3E4CCCCDU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftEngineIdlePitchScale) == 0x3F000000U);
    static_assert(
        static_cast<std::uint8_t>(
            LegacyAircraftEngineSound::engineOn) == 0x1AU);
    static_assert(
        static_cast<std::uint8_t>(
            LegacyAircraftEngineSound::engineIdle) == 0x1BU);
    static_assert(
        static_cast<std::uint8_t>(
            LegacyAircraftEngineSound::engineTurn) == 0x1CU);
    static_assert(
        static_cast<std::uint8_t>(
            LegacyAircraftEngineSound::engineStart) == 0x1DU);
    static_assert(
        static_cast<std::uint8_t>(
            LegacyAircraftEngineSound::engineStop) == 0x1EU);
}

void testFiveCallCadenceAndStart() {
    LegacyAircraftEngineAudioState state{};
    for (std::uint32_t expected = 1U;
         expected <= legacyAircraftEngineAudioCadenceLimit;
         ++expected) {
        const auto skipped =
            legacyAircraftAdvanceEngineAudio(state, cadenceInput());
        require(skipped.has_value(), "valid skipped cadence was rejected");
        require(!skipped->cadenceUpdate, "cadence ran too early");
        require(skipped->commandCount == 0U, "skipped cadence emitted audio");
        require(
            skipped->state.cadenceCounter == expected,
            "skipped cadence counter changed");
        state = skipped->state;
    }

    const auto started =
        legacyAircraftAdvanceEngineAudio(state, cadenceInput());
    require(started.has_value(), "valid start cadence was rejected");
    require(started->cadenceUpdate, "fifth call did not run cadence");
    require(
        started->state.cadenceCounter == 0U,
        "cadence did not reset after update");
    require(
        started->state.engineStartTransitionActive &&
            !started->state.engineRunning,
        "start transition state was not entered");
    require(
        started->state.engineStartElapsedSeconds == 0.0F,
        "start transition timer was not reset");
    require(started->commandCount == 10U, "start command count changed");

    requireCommand(
        *started,
        0U,
        LegacyAircraftEngineAudioCommandKind::playSound,
        LegacyAircraftEngineSound::engineStart,
        0U,
        0.0F,
        "start playback");
    requireCommand(
        *started,
        1U,
        LegacyAircraftEngineAudioCommandKind::setModelParameter,
        LegacyAircraftEngineSound::none,
        1U,
        0.5F,
        "start model update");

    requireCommand(
        *started,
        2U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineOn,
        1U,
        1.26F,
        "engine-on pitch");
    requireCommand(
        *started,
        3U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineOn,
        0U,
        1.0F,
        "engine-on volume");
    requireCommand(
        *started,
        4U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineTurn,
        1U,
        1.26F,
        "turn pitch");
    requireCommand(
        *started,
        5U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineTurn,
        0U,
        0.25F,
        "turn volume");
    requireCommand(
        *started,
        6U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineIdle,
        1U,
        1.65F,
        "idle pitch");
    requireCommand(
        *started,
        7U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineIdle,
        0U,
        0.0F,
        "idle volume");
    requireCommand(
        *started,
        8U,
        LegacyAircraftEngineAudioCommandKind::updateSounds,
        LegacyAircraftEngineSound::none,
        0U,
        0.012F,
        "sound update");
    requireCommand(
        *started,
        9U,
        LegacyAircraftEngineAudioCommandKind::setModelParameter,
        LegacyAircraftEngineSound::none,
        1U,
        0.5F,
        "common model update");
}

void testStrictThresholdsAndDelayedCompletion() {
    LegacyAircraftEngineAudioState thresholdState{
        .cadenceCounter = legacyAircraftEngineAudioCadenceLimit,
        .engineRunning = true,
    };
    const auto threshold = legacyAircraftAdvanceEngineAudio(
        thresholdState,
        cadenceInput(legacyAircraftEngineStartThrottleThreshold));
    require(threshold.has_value(), "exact threshold was rejected");
    require(
        !threshold->state.engineStartTransitionActive &&
            threshold->state.engineRunning,
        "exact thrust threshold triggered a transition");
    require(
        threshold->commandCount == 8U,
        "exact threshold did not emit only common updates");

    LegacyAircraftEngineAudioState exactDuration{
        .engineStartElapsedSeconds =
            legacyAircraftEngineStartDurationSeconds,
        .cadenceCounter = legacyAircraftEngineAudioCadenceLimit,
        .engineStartTransitionActive = true,
    };
    LegacyAircraftEngineAudioInput exactDurationInput = cadenceInput();
    exactDurationInput.deltaSeconds = 0.0F;
    const auto atDuration = legacyAircraftAdvanceEngineAudio(
        exactDuration,
        exactDurationInput);
    require(atDuration.has_value(), "exact start duration was rejected");
    require(
        atDuration->state.engineStartTransitionActive &&
            !atDuration->state.engineRunning &&
            atDuration->commandCount == 8U,
        "exact four-second duration completed the start transition");

    LegacyAircraftEngineAudioState starting{
        .engineStartElapsedSeconds =
            legacyAircraftEngineStartDurationSeconds - 0.01F,
        .cadenceCounter = 0U,
        .engineStartTransitionActive = true,
    };
    LegacyAircraftEngineAudioInput input = cadenceInput();
    input.deltaSeconds = 0.02F;
    const auto crossed =
        legacyAircraftAdvanceEngineAudio(starting, input);
    require(crossed.has_value(), "timer crossing was rejected");
    require(
        crossed->state.engineStartElapsedSeconds >
            legacyAircraftEngineStartDurationSeconds,
        "timer did not cross four seconds");
    require(
        crossed->state.engineStartTransitionActive &&
            !crossed->state.engineRunning &&
            crossed->commandCount == 0U,
        "timer crossing completed outside the cadence block");

    LegacyAircraftEngineAudioState state = crossed->state;
    input.deltaSeconds = 0.0F;
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        const auto skipped =
            legacyAircraftAdvanceEngineAudio(state, input);
        require(skipped.has_value(), "completion delay was rejected");
        state = skipped->state;
    }
    require(
        state.cadenceCounter == legacyAircraftEngineAudioCadenceLimit,
        "completion fixture did not reach cadence");

    const auto completed =
        legacyAircraftAdvanceEngineAudio(state, input);
    require(completed.has_value(), "completion cadence was rejected");
    require(
        completed->state.engineRunning &&
            !completed->state.engineStartTransitionActive,
        "strict four-second completion did not enter running state");
    require(completed->commandCount == 13U, "completion command count changed");
    requireCommand(
        *completed,
        0U,
        LegacyAircraftEngineAudioCommandKind::stopSound,
        LegacyAircraftEngineSound::engineStart,
        0U,
        0.0F,
        "completion start stop");
    requireCommand(
        *completed,
        1U,
        LegacyAircraftEngineAudioCommandKind::playSound,
        LegacyAircraftEngineSound::engineIdle,
        0U,
        0.0F,
        "completion idle play");
    requireCommand(
        *completed,
        2U,
        LegacyAircraftEngineAudioCommandKind::playSound,
        LegacyAircraftEngineSound::engineOn,
        0U,
        0.0F,
        "completion engine-on play");
    requireCommand(
        *completed,
        3U,
        LegacyAircraftEngineAudioCommandKind::playSound,
        LegacyAircraftEngineSound::engineTurn,
        0U,
        0.0F,
        "completion turn play");
}

void testStopOrderAndTimerResetOnFollowingCall() {
    LegacyAircraftEngineAudioState starting{
        .engineStartElapsedSeconds = 3.0F,
        .cadenceCounter = legacyAircraftEngineAudioCadenceLimit,
        .engineStartTransitionActive = true,
        .engineRunning = false,
    };
    const auto stopped =
        legacyAircraftAdvanceEngineAudio(starting, cadenceInput(0.0005F));
    require(stopped.has_value(), "valid stop cadence was rejected");
    require(
        !stopped->state.engineStartTransitionActive &&
            !stopped->state.engineRunning,
        "stop did not clear both engine flags");
    require(
        stopped->smoothedThrust == 0.0F,
        "stop did not clear smoothed thrust");
    requireNear(
        stopped->state.engineStartElapsedSeconds,
        3.012F,
        1.0e-6F,
        "native stop-call timer value was not retained");
    require(
        stopped->commandCount ==
            legacyAircraftMaximumEngineAudioCommands,
        "maximum stop command sequence changed");

    constexpr LegacyAircraftEngineSound stoppedSounds[]{
        LegacyAircraftEngineSound::engineStart,
        LegacyAircraftEngineSound::engineTurn,
        LegacyAircraftEngineSound::engineOn,
        LegacyAircraftEngineSound::engineIdle,
    };
    for (std::size_t index = 0U; index < 4U; ++index) {
        requireCommand(
            *stopped,
            index,
            LegacyAircraftEngineAudioCommandKind::stopSound,
            stoppedSounds[index],
            0U,
            0.0F,
            "stop ordering");
    }
    requireCommand(
        *stopped,
        4U,
        LegacyAircraftEngineAudioCommandKind::playSound,
        LegacyAircraftEngineSound::engineStop,
        0U,
        0.0F,
        "stop playback");

    LegacyAircraftEngineAudioInput ignoredInput = cadenceInput();
    ignoredInput.deltaSeconds =
        std::numeric_limits<float>::quiet_NaN();
    ignoredInput.speedMagnitude =
        std::numeric_limits<float>::quiet_NaN();
    ignoredInput.smoothedOrientationM01 =
        std::numeric_limits<float>::quiet_NaN();
    const auto next = legacyAircraftAdvanceEngineAudio(
        stopped->state,
        ignoredInput);
    require(next.has_value(), "skipped branch inspected unused inputs");
    require(
        next->state.engineStartElapsedSeconds == 0.0F,
        "inactive start timer was not cleared on the following call");
}

void testValidationAndNativeVolumeShape() {
    LegacyAircraftEngineAudioState cadence{
        .cadenceCounter = legacyAircraftEngineAudioCadenceLimit,
        .engineRunning = true,
    };
    LegacyAircraftEngineAudioInput lowLoad{
        .deltaSeconds = 0.012F,
        .smoothedThrust = 0.5F,
        .speedMagnitude = 0.0F,
        .smoothedOrientationM01 = -2.0F,
    };
    const auto low =
        legacyAircraftAdvanceEngineAudio(cadence, lowLoad);
    require(low.has_value(), "low-load modulation was rejected");
    requireCommand(
        *low,
        1U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineOn,
        0U,
        0.5F,
        "unclamped-low running volume");
    requireCommand(
        *low,
        3U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineTurn,
        0U,
        1.0F,
        "absolute turn modulation");
    requireCommand(
        *low,
        5U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        LegacyAircraftEngineSound::engineIdle,
        0U,
        0.5F,
        "complementary idle volume");

    LegacyAircraftEngineAudioInput invalid = lowLoad;
    invalid.speedMagnitude = -1.0F;
    require(
        !legacyAircraftAdvanceEngineAudio(cadence, invalid)
             .has_value(),
        "negative precomputed speed was accepted");
    invalid = lowLoad;
    invalid.smoothedOrientationM01 =
        std::numeric_limits<float>::infinity();
    require(
        !legacyAircraftAdvanceEngineAudio(cadence, invalid)
             .has_value(),
        "non-finite turn state was accepted");
    invalid = lowLoad;
    invalid.deltaSeconds =
        std::numeric_limits<float>::quiet_NaN();
    require(
        !legacyAircraftAdvanceEngineAudio(cadence, invalid)
             .has_value(),
        "cadence sound update accepted non-finite dt");

    LegacyAircraftEngineAudioState badCounter{
        .cadenceCounter =
            legacyAircraftEngineAudioCadenceLimit + 1U,
    };
    require(
        !legacyAircraftAdvanceEngineAudio(badCounter, lowLoad)
             .has_value(),
        "out-of-contract cadence counter was accepted");

    LegacyAircraftEngineAudioState activeTimer{
        .engineStartTransitionActive = true,
    };
    require(
        !legacyAircraftAdvanceEngineAudio(activeTimer, invalid)
             .has_value(),
        "active timer accepted non-finite dt before skipped cadence");

    LegacyAircraftEngineAudioState ignoredTimer{
        .engineStartElapsedSeconds =
            std::numeric_limits<float>::quiet_NaN(),
    };
    const auto resetTimer =
        legacyAircraftAdvanceEngineAudio(ignoredTimer, lowLoad);
    require(
        resetTimer.has_value() &&
            resetTimer->state.engineStartElapsedSeconds == 0.0F,
        "inactive branch inspected the overwritten start timer");
}

} // namespace

int main() {
    try {
        testRecoveredConstantsAndIds();
        testFiveCallCadenceAndStart();
        testStrictThresholdsAndDelayedCompletion();
        testStopOrderAndTimerResetOnFollowingCall();
        testValidationAndNativeVolumeShape();
    } catch (const std::exception& exception) {
        std::cerr << "LegacyAircraftEngineAudioStateTests: "
                  << exception.what() << '\n';
        return 1;
    }

    std::cout << "LegacyAircraftEngineAudioStateTests: all tests passed\n";
    return 0;
}
