#include "airfix/simulation/LegacyAircraftEngineAudioState.hpp"

#include <algorithm>
#include <cmath>

namespace airfix::simulation {
namespace {

class CommandWriter final {
public:
    explicit CommandWriter(LegacyAircraftEngineAudioStep& step) noexcept
        : step_(step) {}

    void play(const LegacyAircraftEngineSound sound) noexcept {
        append({
            .kind = LegacyAircraftEngineAudioCommandKind::playSound,
            .sound = sound,
        });
    }

    void stop(const LegacyAircraftEngineSound sound) noexcept {
        append({
            .kind = LegacyAircraftEngineAudioCommandKind::stopSound,
            .sound = sound,
        });
    }

    void setSoundParameter(
        const LegacyAircraftEngineSound sound,
        const std::uint8_t parameterIndex,
        const float value) noexcept {
        append({
            .kind =
                LegacyAircraftEngineAudioCommandKind::setSoundParameter,
            .sound = sound,
            .parameterIndex = parameterIndex,
            .value = value,
        });
    }

    void updateSounds(const float deltaSeconds) noexcept {
        append({
            .kind = LegacyAircraftEngineAudioCommandKind::updateSounds,
            .value = deltaSeconds,
        });
    }

    void setModelParameter(
        const std::uint8_t parameterIndex,
        const float value) noexcept {
        append({
            .kind =
                LegacyAircraftEngineAudioCommandKind::setModelParameter,
            .parameterIndex = parameterIndex,
            .value = value,
        });
    }

private:
    void append(const LegacyAircraftEngineAudioCommand command) noexcept {
        step_.commands[step_.commandCount] = command;
        ++step_.commandCount;
    }

    LegacyAircraftEngineAudioStep& step_;
};

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

} // namespace

std::optional<LegacyAircraftEngineAudioStep>
legacyAircraftAdvanceEngineAudio(
    LegacyAircraftEngineAudioState current,
    const LegacyAircraftEngineAudioInput input) noexcept {
    if (current.cadenceCounter >
            legacyAircraftEngineAudioCadenceLimit) {
        return std::nullopt;
    }

    if (current.engineStartTransitionActive) {
        if (!finite(current.engineStartElapsedSeconds) ||
            !finite(input.deltaSeconds)) {
            return std::nullopt;
        }
        current.engineStartElapsedSeconds += input.deltaSeconds;
        if (!finite(current.engineStartElapsedSeconds)) {
            return std::nullopt;
        }
    } else {
        current.engineStartElapsedSeconds = 0.0F;
    }

    LegacyAircraftEngineAudioStep step{
        .state = current,
        .smoothedThrust = input.smoothedThrust,
    };

    if (current.cadenceCounter <
            legacyAircraftEngineAudioCadenceLimit) {
        ++step.state.cadenceCounter;
        return step;
    }

    if (!finite(input.deltaSeconds) ||
        !finite(input.smoothedThrust) ||
        !finite(input.speedMagnitude) ||
        input.speedMagnitude < 0.0F ||
        !finite(input.smoothedOrientationM01)) {
        return std::nullopt;
    }

    step.cadenceUpdate = true;
    step.state.cadenceCounter = 0U;
    CommandWriter commands(step);

    if (step.smoothedThrust <
            legacyAircraftEngineStartThrottleThreshold &&
        (step.state.engineRunning ||
         step.state.engineStartTransitionActive)) {
        commands.stop(LegacyAircraftEngineSound::engineStart);
        commands.stop(LegacyAircraftEngineSound::engineTurn);
        commands.stop(LegacyAircraftEngineSound::engineOn);
        commands.stop(LegacyAircraftEngineSound::engineIdle);
        commands.play(LegacyAircraftEngineSound::engineStop);
        commands.setModelParameter(1U, 0.0F);

        step.smoothedThrust = 0.0F;
        step.state.engineRunning = false;
        step.state.engineStartTransitionActive = false;
    }

    if (step.smoothedThrust >
            legacyAircraftEngineStartThrottleThreshold &&
        !step.state.engineStartTransitionActive &&
        !step.state.engineRunning) {
        commands.play(LegacyAircraftEngineSound::engineStart);
        commands.setModelParameter(1U, step.smoothedThrust);
        step.state.engineStartTransitionActive = true;
        step.state.engineStartElapsedSeconds = 0.0F;
    }

    if (step.state.engineStartTransitionActive &&
        !step.state.engineRunning &&
        step.state.engineStartElapsedSeconds >
            legacyAircraftEngineStartDurationSeconds) {
        commands.stop(LegacyAircraftEngineSound::engineStart);
        commands.play(LegacyAircraftEngineSound::engineIdle);
        commands.play(LegacyAircraftEngineSound::engineOn);
        commands.play(LegacyAircraftEngineSound::engineTurn);
        commands.setModelParameter(1U, step.smoothedThrust);
        step.state.engineRunning = true;
        step.state.engineStartTransitionActive = false;
    }

    step.phaseCommandCount = step.commandCount;

    const float throttleWeightedLoad =
        step.smoothedThrust *
            legacyAircraftEngineAudioLoadThrottleScale +
        legacyAircraftEngineAudioLoadOffset;
    const float speedLoad =
        throttleWeightedLoad * input.speedMagnitude;
    const float engineLoad = speedLoad + step.smoothedThrust;
    const float runningPitch =
        engineLoad * legacyAircraftEngineAudioPitchScale + 1.0F;
    const float runningVolume = std::min(engineLoad, 1.0F);
    const float turnVolume =
        std::fabs(input.smoothedOrientationM01) * runningVolume;
    const float idlePitch =
        engineLoad * legacyAircraftEngineIdlePitchScale + 1.0F;
    const float idleVolume = std::max(1.0F - runningVolume, 0.0F);

    if (!finite(throttleWeightedLoad) ||
        !finite(speedLoad) ||
        !finite(engineLoad) ||
        !finite(runningPitch) ||
        !finite(runningVolume) ||
        !finite(turnVolume) ||
        !finite(idlePitch) ||
        !finite(idleVolume)) {
        return std::nullopt;
    }

    commands.setSoundParameter(
        LegacyAircraftEngineSound::engineOn,
        1U,
        runningPitch);
    commands.setSoundParameter(
        LegacyAircraftEngineSound::engineOn,
        0U,
        runningVolume);
    commands.setSoundParameter(
        LegacyAircraftEngineSound::engineTurn,
        1U,
        runningPitch);
    commands.setSoundParameter(
        LegacyAircraftEngineSound::engineTurn,
        0U,
        turnVolume);
    commands.setSoundParameter(
        LegacyAircraftEngineSound::engineIdle,
        1U,
        idlePitch);
    commands.setSoundParameter(
        LegacyAircraftEngineSound::engineIdle,
        0U,
        idleVolume);
    commands.updateSounds(input.deltaSeconds);
    commands.setModelParameter(1U, step.smoothedThrust);
    return step;
}

} // namespace airfix::simulation
