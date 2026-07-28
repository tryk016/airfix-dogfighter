#pragma once

#include "airfix/simulation/LegacyAircraftThrustState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::uint32_t legacyAircraftEngineAudioCadenceLimit = 4U;
inline constexpr std::size_t legacyAircraftMaximumEngineAudioCommands = 14U;

inline constexpr float legacyAircraftEngineAudioLoadThrottleScale = 0.2F;
inline constexpr float legacyAircraftEngineAudioLoadOffset = 0.3F;
inline constexpr float legacyAircraftEngineAudioPitchScale = 0.2F;
inline constexpr float legacyAircraftEngineIdlePitchScale = 0.5F;

enum class LegacyAircraftEngineSound : std::uint8_t {
    none = 0x00,
    engineOn = 0x1A,
    engineIdle = 0x1B,
    engineTurn = 0x1C,
    engineStart = 0x1D,
    engineStop = 0x1E,
    engineDive = 0x20,
};

enum class LegacyAircraftEngineAudioCommandKind : std::uint8_t {
    playSound,
    stopSound,
    setSoundParameter,
    updateSounds,
    setModelParameter,
};

struct LegacyAircraftEngineAudioCommand final {
    LegacyAircraftEngineAudioCommandKind kind{
        LegacyAircraftEngineAudioCommandKind::updateSounds};
    LegacyAircraftEngineSound sound{LegacyAircraftEngineSound::none};
    std::uint8_t parameterIndex{};
    float value{};
};

struct LegacyAircraftEngineAudioState final {
    float engineStartElapsedSeconds{};
    std::uint32_t cadenceCounter{};
    bool engineStartTransitionActive{};
    bool engineRunning{};
};

struct LegacyAircraftEngineAudioInput final {
    float deltaSeconds{};
    float smoothedThrust{};
    float speedMagnitude{};
    float smoothedOrientationM01{};
};

struct LegacyAircraftEngineAudioStep final {
    LegacyAircraftEngineAudioState state{};
    float smoothedThrust{};
    bool cadenceUpdate{};
    std::array<
        LegacyAircraftEngineAudioCommand,
        legacyAircraftMaximumEngineAudioCommands> commands{};
    std::size_t commandCount{};
};

// Reconstructs the engine-only subset of AirCraft primary vtable slot 44.
// The elapsed timer advances on every call while the start transition is
// active, but transitions, sound parameters, UpdateSounds, and the model
// parameter are emitted only when the recovered 0..4 cadence counter expires.
//
// speedMagnitude is the length already returned by the native velocity query.
// smoothedOrientationM01 is AirCraft +0x558, the low-pass orientation-matrix
// component produced by the flight-force step. The result deliberately
// contains commands rather than a playback dependency, keeping this contract
// portable and allocation-free.
[[nodiscard]] std::optional<LegacyAircraftEngineAudioStep>
legacyAircraftAdvanceEngineAudio(
    LegacyAircraftEngineAudioState current,
    LegacyAircraftEngineAudioInput input) noexcept;

} // namespace airfix::simulation
