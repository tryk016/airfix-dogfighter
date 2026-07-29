#include "airfix/simulation/LegacyAircraftAudioCoordinator.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::audio::AudioClipId;
using airfix::audio::AudioCommand;
using airfix::audio::AudioCommandKind;
using airfix::audio::AudioVoiceId;
using airfix::audio::validAudioCommandBatch;
using airfix::simulation::legacyAircraftAdvanceAudio;
using airfix::simulation::LegacyAircraftAudioBinding;
using airfix::simulation::LegacyAircraftAudioBindings;
using airfix::simulation::LegacyAircraftAudioCoordinatorInput;
using airfix::simulation::LegacyAircraftAudioCoordinatorState;
using airfix::simulation::LegacyAircraftAudioCoordinatorStep;
using airfix::simulation::legacyAircraftAudioSoundCount;
using airfix::simulation::legacyAircraftEngineAudioCadenceLimit;
using airfix::simulation::LegacyAircraftEngineSound;
using airfix::simulation::validLegacyAircraftAudioBindings;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

LegacyAircraftAudioBindings bindings() {
    return {{
        {
            .sound = LegacyAircraftEngineSound::engineOn,
            .clip = AudioClipId{101U},
            .voice = AudioVoiceId{201U},
            .looping = true,
        },
        {
            .sound = LegacyAircraftEngineSound::engineIdle,
            .clip = AudioClipId{102U},
            .voice = AudioVoiceId{202U},
            .looping = true,
        },
        {
            .sound = LegacyAircraftEngineSound::engineTurn,
            .clip = AudioClipId{103U},
            .voice = AudioVoiceId{203U},
            .looping = true,
        },
        {
            .sound = LegacyAircraftEngineSound::engineStart,
            .clip = AudioClipId{104U},
            .voice = AudioVoiceId{204U},
            .looping = true,
        },
        {
            .sound = LegacyAircraftEngineSound::engineStop,
            .clip = AudioClipId{105U},
            .voice = AudioVoiceId{205U},
        },
        {
            .sound = LegacyAircraftEngineSound::engineDive,
            .clip = AudioClipId{106U},
            .voice = AudioVoiceId{206U},
            .looping = true,
        },
    }};
}

LegacyAircraftAudioCoordinatorInput input(const std::uint64_t sequence,
                                          const float thrust = 0.5F,
                                          const float health = 1.0F,
                                          const float velocityY = 0.0F) {
    return {
        .sequence = sequence,
        .engine =
            {
                .deltaSeconds = 0.012F,
                .smoothedThrust = thrust,
                .speedMagnitude = 2.0F,
                .smoothedOrientationM01 = -0.25F,
            },
        .destroyedDive =
            {
                .health = health,
                .velocityY = velocityY,
            },
    };
}

void requireCommand(const LegacyAircraftAudioCoordinatorStep& step,
                    const std::size_t index, const AudioCommandKind kind,
                    const AudioVoiceId voice, const std::string& message) {
    require(index < step.audio.commandCount, message + ": missing command");
    const AudioCommand& command = step.audio.commands[index];
    require(command.kind == kind, message + ": wrong kind");
    require(command.voice == voice, message + ": wrong voice");
}

void requireNear(const float actual, const float expected,
                 const std::string& message) {
    require(std::fabs(actual - expected) <= 1.0e-6F,
            message + ": expected " + std::to_string(expected) + ", got " +
                std::to_string(actual));
}

void testBindingValidation() {
    static_assert(legacyAircraftAudioSoundCount == 6U);
    const auto valid = bindings();
    require(validLegacyAircraftAudioBindings(valid),
            "complete bindings were rejected");

    auto duplicateRole = valid;
    duplicateRole[5].sound = LegacyAircraftEngineSound::engineOn;
    require(!validLegacyAircraftAudioBindings(duplicateRole),
            "duplicate role was accepted");

    auto duplicateVoice = valid;
    duplicateVoice[5].voice = duplicateVoice[0].voice;
    require(!validLegacyAircraftAudioBindings(duplicateVoice),
            "duplicate voice was accepted");

    auto invalidClip = valid;
    invalidClip[0].clip = {};
    require(!validLegacyAircraftAudioBindings(invalidClip),
            "invalid clip was accepted");
}

void testSkippedCadenceDoesNotInspectDiveInputs() {
    auto skippedInput = input(1U);
    skippedInput.destroyedDive.health = std::numeric_limits<float>::quiet_NaN();
    skippedInput.destroyedDive.velocityY =
        std::numeric_limits<float>::quiet_NaN();

    const auto skipped =
        legacyAircraftAdvanceAudio({}, bindings(), skippedInput);
    require(skipped.has_value(), "skipped cadence was rejected");
    require(!skipped->cadenceUpdate, "first call ran the audio cadence");
    require(!skipped->destroyedDiveEvaluated,
            "skipped cadence evaluated destroyed-dive state");
    require(skipped->audio.commandCount == 0U &&
                validAudioCommandBatch(skipped->audio),
            "skipped cadence did not produce a valid empty batch");
    require(skipped->state.engine.cadenceCounter == 1U,
            "skipped cadence state did not advance");
}

void testExactPhaseDiveModulationOrder() {
    const LegacyAircraftAudioCoordinatorState state{
        .engine =
            {
                .cadenceCounter = legacyAircraftEngineAudioCadenceLimit,
                .engineRunning = true,
            },
    };
    const auto composed = legacyAircraftAdvanceAudio(
        state, bindings(), input(7U, 0.0005F, 0.0F, -8.0F));
    require(composed.has_value(), "combined shutdown/dive step was rejected");
    require(composed->cadenceUpdate && composed->destroyedDiveEvaluated,
            "cadence did not evaluate both recovered state machines");
    require(!composed->state.engine.engineRunning &&
                composed->state.destroyedDive.soundActive,
            "combined next state changed");
    require(composed->audio.sequence == 7U &&
                composed->audio.commandCount == 14U &&
                validAudioCommandBatch(composed->audio),
            "combined audio batch changed");

    constexpr AudioVoiceId engineOn{201U};
    constexpr AudioVoiceId engineIdle{202U};
    constexpr AudioVoiceId engineTurn{203U};
    constexpr AudioVoiceId engineStart{204U};
    constexpr AudioVoiceId engineStop{205U};
    constexpr AudioVoiceId engineDive{206U};

    requireCommand(*composed, 0U, AudioCommandKind::stopVoice, engineStart,
                   "phase start stop");
    requireCommand(*composed, 1U, AudioCommandKind::stopVoice, engineTurn,
                   "phase turn stop");
    requireCommand(*composed, 2U, AudioCommandKind::stopVoice, engineOn,
                   "phase engine-on stop");
    requireCommand(*composed, 3U, AudioCommandKind::stopVoice, engineIdle,
                   "phase idle stop");
    requireCommand(*composed, 4U, AudioCommandKind::startVoice, engineStop,
                   "phase stop-sample start");

    requireCommand(*composed, 5U, AudioCommandKind::startVoice, engineDive,
                   "dive start");
    requireCommand(*composed, 6U, AudioCommandKind::setVoicePitch, engineDive,
                   "dive pitch");
    requireNear(composed->audio.commands[6U].pitch, 1.0F, "dive pitch value");
    requireCommand(*composed, 7U, AudioCommandKind::setVoiceGain, engineDive,
                   "dive gain");
    requireNear(composed->audio.commands[7U].gain, 1.0F, "dive gain value");

    requireCommand(*composed, 8U, AudioCommandKind::setVoicePitch, engineOn,
                   "engine-on pitch");
    requireCommand(*composed, 9U, AudioCommandKind::setVoiceGain, engineOn,
                   "engine-on gain");
    requireCommand(*composed, 10U, AudioCommandKind::setVoicePitch, engineTurn,
                   "turn pitch");
    requireCommand(*composed, 11U, AudioCommandKind::setVoiceGain, engineTurn,
                   "turn gain");
    requireCommand(*composed, 12U, AudioCommandKind::setVoicePitch, engineIdle,
                   "idle pitch");
    requireCommand(*composed, 13U, AudioCommandKind::setVoiceGain, engineIdle,
                   "idle gain");
}

void testStartMappingAndLoopPolicy() {
    LegacyAircraftAudioCoordinatorState state{};
    std::optional<LegacyAircraftAudioCoordinatorStep> step;
    for (std::uint64_t sequence = 1U; sequence <= 5U; ++sequence) {
        step = legacyAircraftAdvanceAudio(state, bindings(), input(sequence));
        require(step.has_value(), "valid startup sequence was rejected");
        state = step->state;
    }

    require(step->audio.commandCount == 7U, "startup batch size changed");
    requireCommand(*step, 0U, AudioCommandKind::startVoice, AudioVoiceId{204U},
                   "engine-start mapping");
    require(step->audio.commands[0U].clip == AudioClipId{104U} &&
                step->audio.commands[0U].looping,
            "engine-start binding was not preserved");
}

void testInvalidInputsFailTransactionally() {
    require(!legacyAircraftAdvanceAudio({}, bindings(), input(0U)).has_value(),
            "zero sequence was accepted");

    auto invalidBindings = bindings();
    invalidBindings[0].voice = invalidBindings[1].voice;
    require(
        !legacyAircraftAdvanceAudio({}, invalidBindings, input(1U)).has_value(),
        "invalid bindings were accepted");

    LegacyAircraftAudioCoordinatorState running{
        .engine =
            {
                .cadenceCounter = legacyAircraftEngineAudioCadenceLimit,
                .engineRunning = true,
            },
    };
    auto excessivePitch = input(1U, 1.0F);
    excessivePitch.engine.speedMagnitude = 100.0F;
    require(!legacyAircraftAdvanceAudio(running, bindings(), excessivePitch)
                 .has_value(),
            "out-of-contract pitch was silently clamped");

    auto invalidDive = input(1U);
    invalidDive.destroyedDive.health = std::numeric_limits<float>::quiet_NaN();
    running.engine.cadenceCounter = legacyAircraftEngineAudioCadenceLimit;
    const auto rejected =
        legacyAircraftAdvanceAudio(running, bindings(), invalidDive);
    require(!rejected.has_value(),
            "consumed invalid destroyed-dive state was accepted");
    require(running.engine.cadenceCounter ==
                    legacyAircraftEngineAudioCadenceLimit &&
                !running.destroyedDive.soundActive,
            "failed composition mutated caller-owned state");
}

} // namespace

int main() {
    try {
        testBindingValidation();
        testSkippedCadenceDoesNotInspectDiveInputs();
        testExactPhaseDiveModulationOrder();
        testStartMappingAndLoopPolicy();
        testInvalidInputsFailTransactionally();
    } catch (const std::exception& exception) {
        std::cerr << "LegacyAircraftAudioCoordinatorTests: " << exception.what()
                  << '\n';
        return 1;
    }

    std::cout << "LegacyAircraftAudioCoordinatorTests: all tests passed\n";
    return 0;
}
