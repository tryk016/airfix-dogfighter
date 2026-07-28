#include "airfix/simulation/LegacyAircraftDestroyedDiveAudioState.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::simulation::LegacyAircraftDestroyedDiveAudioInput;
using airfix::simulation::LegacyAircraftDestroyedDiveAudioState;
using airfix::simulation::LegacyAircraftDestroyedDiveAudioStep;
using airfix::simulation::LegacyAircraftEngineAudioCommandKind;
using airfix::simulation::LegacyAircraftEngineSound;
using airfix::simulation::legacyAircraftAdvanceDestroyedDiveAudio;
using airfix::simulation::legacyAircraftDiveVelocityScale;
using airfix::simulation::legacyAircraftDiveVolumeOffset;
using airfix::simulation::
    legacyAircraftMaximumDestroyedDiveAudioCommands;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(
    const float actual,
    const float expected,
    const std::string& message) {
    require(
        std::fabs(actual - expected) <= 1.0e-6F,
        message + ": expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
}

void requireCommand(
    const LegacyAircraftDestroyedDiveAudioStep& step,
    const std::size_t index,
    const LegacyAircraftEngineAudioCommandKind kind,
    const std::uint8_t parameterIndex,
    const float value,
    const std::string& message) {
    require(index < step.commandCount, message + ": missing command");
    const auto& command = step.commands[index];
    require(command.kind == kind, message + ": wrong kind");
    require(
        command.sound == LegacyAircraftEngineSound::engineDive,
        message + ": wrong sound");
    require(
        command.parameterIndex == parameterIndex,
        message + ": wrong parameter");
    requireNear(command.value, value, message + ": wrong value");
}

void testRecoveredConstantsAndDeterministicInitialState() {
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftDiveVelocityScale) == 0xBE19999AU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftDiveVolumeOffset) == 0x3E4CCCCDU);
    static_assert(
        static_cast<std::uint8_t>(
            LegacyAircraftEngineSound::engineDive) == 0x20U);
    static_assert(
        legacyAircraftMaximumDestroyedDiveAudioCommands == 3U);
    static_assert(!LegacyAircraftDestroyedDiveAudioState{}.soundActive);
}

void testDestroyedStartAndRetainedUpdates() {
    const auto started = legacyAircraftAdvanceDestroyedDiveAudio(
        {},
        {
            .health = 0.0F,
            .velocityY = -4.0F,
        });
    require(started.has_value(), "valid destroyed state was rejected");
    require(started->state.soundActive, "dive sound was not activated");
    require(started->commandCount == 3U, "start command count changed");
    requireCommand(
        *started,
        0U,
        LegacyAircraftEngineAudioCommandKind::playSound,
        0U,
        0.0F,
        "start playback");
    requireCommand(
        *started,
        1U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        1U,
        1.0F,
        "fixed parameter");
    requireCommand(
        *started,
        2U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        0U,
        0.4F,
        "descent volume");

    const auto retained = legacyAircraftAdvanceDestroyedDiveAudio(
        started->state,
        {
            .health = -10.0F,
            .velocityY = -8.0F,
        });
    require(retained.has_value(), "retained destroyed state was rejected");
    require(retained->state.soundActive, "retained sound was cleared");
    require(
        retained->commandCount == 2U,
        "retained update replayed the sample");
    requireCommand(
        *retained,
        0U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        1U,
        1.0F,
        "retained fixed parameter");
    requireCommand(
        *retained,
        1U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        0U,
        1.0F,
        "exact upper volume");
}

void testVolumeClampsAndPositiveHealthStop() {
    const auto lower = legacyAircraftAdvanceDestroyedDiveAudio(
        {.soundActive = true},
        {
            .health = 0.0F,
            .velocityY = 2.0F,
        });
    require(lower.has_value(), "lower clamp input was rejected");
    requireCommand(
        *lower,
        1U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        0U,
        0.0F,
        "lower clamp");

    const auto upper = legacyAircraftAdvanceDestroyedDiveAudio(
        {.soundActive = true},
        {
            .health = -1.0F,
            .velocityY = -20.0F,
        });
    require(upper.has_value(), "upper clamp input was rejected");
    requireCommand(
        *upper,
        1U,
        LegacyAircraftEngineAudioCommandKind::setSoundParameter,
        0U,
        1.0F,
        "upper clamp");

    const auto stopped = legacyAircraftAdvanceDestroyedDiveAudio(
        {.soundActive = true},
        {
            .health = std::numeric_limits<float>::denorm_min(),
            .velocityY = std::numeric_limits<float>::quiet_NaN(),
        });
    require(stopped.has_value(), "positive-health stop was rejected");
    require(!stopped->state.soundActive, "positive health retained sound");
    require(stopped->commandCount == 1U, "stop command count changed");
    requireCommand(
        *stopped,
        0U,
        LegacyAircraftEngineAudioCommandKind::stopSound,
        0U,
        0.0F,
        "positive-health stop");

    const auto idle = legacyAircraftAdvanceDestroyedDiveAudio(
        {},
        {
            .health = 1.0F,
            .velocityY = std::numeric_limits<float>::quiet_NaN(),
        });
    require(
        idle.has_value() && idle->commandCount == 0U,
        "live inactive branch inspected unused velocity");
}

void testConsumedNonFiniteInputsFailClosed() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(
        !legacyAircraftAdvanceDestroyedDiveAudio(
             {},
             {
                 .health = nan,
                 .velocityY = 0.0F,
             })
             .has_value(),
        "non-finite health was accepted");
    require(
        !legacyAircraftAdvanceDestroyedDiveAudio(
             {},
             {
                 .health = 0.0F,
                 .velocityY = nan,
             })
             .has_value(),
        "destroyed branch accepted non-finite velocity");
    require(
        !legacyAircraftAdvanceDestroyedDiveAudio(
             {},
             {
                 .health = -1.0F,
                 .velocityY =
                     -std::numeric_limits<float>::infinity(),
             })
             .has_value(),
        "destroyed branch accepted infinite velocity");
}

} // namespace

int main() {
    try {
        testRecoveredConstantsAndDeterministicInitialState();
        testDestroyedStartAndRetainedUpdates();
        testVolumeClampsAndPositiveHealthStop();
        testConsumedNonFiniteInputsFailClosed();
    } catch (const std::exception& exception) {
        std::cerr << "LegacyAircraftDestroyedDiveAudioStateTests: "
                  << exception.what() << '\n';
        return 1;
    }

    std::cout
        << "LegacyAircraftDestroyedDiveAudioStateTests: all tests passed\n";
    return 0;
}
