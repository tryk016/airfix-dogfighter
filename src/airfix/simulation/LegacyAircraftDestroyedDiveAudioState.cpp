#include "airfix/simulation/LegacyAircraftDestroyedDiveAudioState.hpp"

#include <algorithm>
#include <cmath>

namespace airfix::simulation {
namespace {

void append(
    LegacyAircraftDestroyedDiveAudioStep& step,
    const LegacyAircraftEngineAudioCommand command) noexcept {
    step.commands[step.commandCount] = command;
    ++step.commandCount;
}

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

} // namespace

std::optional<LegacyAircraftDestroyedDiveAudioStep>
legacyAircraftAdvanceDestroyedDiveAudio(
    LegacyAircraftDestroyedDiveAudioState current,
    const LegacyAircraftDestroyedDiveAudioInput input) noexcept {
    if (!finite(input.health)) {
        return std::nullopt;
    }

    LegacyAircraftDestroyedDiveAudioStep step{
        .state = current,
    };

    if (input.health > 0.0F) {
        if (step.state.soundActive) {
            append(
                step,
                {
                    .kind =
                        LegacyAircraftEngineAudioCommandKind::stopSound,
                    .sound = LegacyAircraftEngineSound::engineDive,
                });
            step.state.soundActive = false;
        }
        return step;
    }

    if (!finite(input.velocityY)) {
        return std::nullopt;
    }

    const float scaledVelocity =
        input.velocityY * legacyAircraftDiveVelocityScale;
    const float unclampedVolume =
        scaledVelocity - legacyAircraftDiveVolumeOffset;
    if (!finite(scaledVelocity) || !finite(unclampedVolume)) {
        return std::nullopt;
    }
    const float volume = std::clamp(unclampedVolume, 0.0F, 1.0F);

    if (!step.state.soundActive) {
        append(
            step,
            {
                .kind = LegacyAircraftEngineAudioCommandKind::playSound,
                .sound = LegacyAircraftEngineSound::engineDive,
            });
        step.state.soundActive = true;
    }
    append(
        step,
        {
            .kind =
                LegacyAircraftEngineAudioCommandKind::setSoundParameter,
            .sound = LegacyAircraftEngineSound::engineDive,
            .parameterIndex = 1U,
            .value = 1.0F,
        });
    append(
        step,
        {
            .kind =
                LegacyAircraftEngineAudioCommandKind::setSoundParameter,
            .sound = LegacyAircraftEngineSound::engineDive,
            .parameterIndex = 0U,
            .value = volume,
        });
    return step;
}

} // namespace airfix::simulation
