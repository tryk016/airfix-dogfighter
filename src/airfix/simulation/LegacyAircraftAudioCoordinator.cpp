#include "airfix/simulation/LegacyAircraftAudioCoordinator.hpp"

#include <array>
#include <span>

namespace airfix::simulation {
namespace {

[[nodiscard]] std::optional<std::size_t>
soundIndex(const LegacyAircraftEngineSound sound) noexcept {
    switch (sound) {
    case LegacyAircraftEngineSound::engineOn:
        return 0U;
    case LegacyAircraftEngineSound::engineIdle:
        return 1U;
    case LegacyAircraftEngineSound::engineTurn:
        return 2U;
    case LegacyAircraftEngineSound::engineStart:
        return 3U;
    case LegacyAircraftEngineSound::engineStop:
        return 4U;
    case LegacyAircraftEngineSound::engineDive:
        return 5U;
    case LegacyAircraftEngineSound::none:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] const LegacyAircraftAudioBinding*
findBinding(const LegacyAircraftAudioBindings& bindings,
            const LegacyAircraftEngineSound sound) noexcept {
    for (const LegacyAircraftAudioBinding& binding : bindings) {
        if (binding.sound == sound) {
            return &binding;
        }
    }
    return nullptr;
}

[[nodiscard]] bool translateOne(const LegacyAircraftAudioBindings& bindings,
                                const LegacyAircraftEngineAudioCommand command,
                                audio::AudioCommandBatch& output) noexcept {
    const LegacyAircraftAudioBinding* binding =
        findBinding(bindings, command.sound);
    switch (command.kind) {
    case LegacyAircraftEngineAudioCommandKind::playSound:
        return binding != nullptr &&
               audio::appendAudioCommand(
                   output, {
                               .kind = audio::AudioCommandKind::startVoice,
                               .voice = binding->voice,
                               .clip = binding->clip,
                               .looping = binding->looping,
                           });
    case LegacyAircraftEngineAudioCommandKind::stopSound:
        return binding != nullptr &&
               audio::appendAudioCommand(
                   output, {
                               .kind = audio::AudioCommandKind::stopVoice,
                               .voice = binding->voice,
                           });
    case LegacyAircraftEngineAudioCommandKind::setSoundParameter: {
        if (binding == nullptr) {
            return false;
        }

        audio::AudioCommand translated{
            .voice = binding->voice,
        };
        if (command.parameterIndex == 0U) {
            translated.kind = audio::AudioCommandKind::setVoiceGain;
            translated.gain = command.value;
        } else if (command.parameterIndex == 1U) {
            translated.kind = audio::AudioCommandKind::setVoicePitch;
            translated.pitch = command.value;
        } else {
            return false;
        }
        return audio::appendAudioCommand(output, translated);
    }
    case LegacyAircraftEngineAudioCommandKind::updateSounds:
    case LegacyAircraftEngineAudioCommandKind::setModelParameter:
        return true;
    }
    return false;
}

[[nodiscard]] bool
translate(const LegacyAircraftAudioBindings& bindings,
          const std::span<const LegacyAircraftEngineAudioCommand> commands,
          audio::AudioCommandBatch& output) noexcept {
    for (const LegacyAircraftEngineAudioCommand& command : commands) {
        if (!translateOne(bindings, command, output)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool validLegacyAircraftAudioBindings(
    const LegacyAircraftAudioBindings& bindings) noexcept {
    std::array<bool, legacyAircraftAudioSoundCount> found{};
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
        const LegacyAircraftAudioBinding& binding = bindings[index];
        const std::optional<std::size_t> role = soundIndex(binding.sound);
        if (!role.has_value() || found[*role] || !binding.clip.valid() ||
            !binding.voice.valid()) {
            return false;
        }
        found[*role] = true;

        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (bindings[previous].voice == binding.voice) {
                return false;
            }
        }
    }

    for (const bool roleFound : found) {
        if (!roleFound) {
            return false;
        }
    }
    return true;
}

std::optional<LegacyAircraftAudioCoordinatorStep> legacyAircraftAdvanceAudio(
    const LegacyAircraftAudioCoordinatorState current,
    const LegacyAircraftAudioBindings& bindings,
    const LegacyAircraftAudioCoordinatorInput input) noexcept {
    if (input.sequence == 0U || !validLegacyAircraftAudioBindings(bindings)) {
        return std::nullopt;
    }

    const auto engine =
        legacyAircraftAdvanceEngineAudio(current.engine, input.engine);
    if (!engine.has_value() ||
        engine->phaseCommandCount > engine->commandCount) {
        return std::nullopt;
    }

    LegacyAircraftAudioCoordinatorStep step{
        .state =
            {
                .engine = engine->state,
                .destroyedDive = current.destroyedDive,
            },
        .smoothedThrust = engine->smoothedThrust,
        .cadenceUpdate = engine->cadenceUpdate,
        .audio =
            {
                .sequence = input.sequence,
            },
    };

    if (!engine->cadenceUpdate) {
        return step;
    }

    const auto destroyedDive = legacyAircraftAdvanceDestroyedDiveAudio(
        current.destroyedDive, input.destroyedDive);
    if (!destroyedDive.has_value()) {
        return std::nullopt;
    }
    step.state.destroyedDive = destroyedDive->state;
    step.destroyedDiveEvaluated = true;

    const std::span engineCommands{engine->commands.data(),
                                   engine->commandCount};
    const std::span phaseCommands =
        engineCommands.first(engine->phaseCommandCount);
    const std::span modulationCommands =
        engineCommands.subspan(engine->phaseCommandCount);
    const std::span diveCommands{destroyedDive->commands.data(),
                                 destroyedDive->commandCount};

    if (!translate(bindings, phaseCommands, step.audio) ||
        !translate(bindings, diveCommands, step.audio) ||
        !translate(bindings, modulationCommands, step.audio) ||
        !audio::validAudioCommandBatch(step.audio)) {
        return std::nullopt;
    }
    return step;
}

} // namespace airfix::simulation
