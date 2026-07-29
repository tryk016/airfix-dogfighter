#pragma once

#include "airfix/audio/AudioCommand.hpp"
#include "airfix/simulation/LegacyAircraftDestroyedDiveAudioState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::size_t legacyAircraftAudioSoundCount = 6U;

struct LegacyAircraftAudioBinding final {
    LegacyAircraftEngineSound sound{LegacyAircraftEngineSound::none};
    audio::AudioClipId clip{};
    audio::AudioVoiceId voice{};
    bool looping{};
};

using LegacyAircraftAudioBindings =
    std::array<LegacyAircraftAudioBinding, legacyAircraftAudioSoundCount>;

struct LegacyAircraftAudioCoordinatorState final {
    LegacyAircraftEngineAudioState engine{};
    LegacyAircraftDestroyedDiveAudioState destroyedDive{};
};

struct LegacyAircraftAudioCoordinatorInput final {
    std::uint64_t sequence{};
    LegacyAircraftEngineAudioInput engine{};
    LegacyAircraftDestroyedDiveAudioInput destroyedDive{};
};

struct LegacyAircraftAudioCoordinatorStep final {
    LegacyAircraftAudioCoordinatorState state{};
    float smoothedThrust{};
    bool cadenceUpdate{};
    bool destroyedDiveEvaluated{};
    audio::AudioCommandBatch audio{};
};

// Bindings are supplied by the private content/runtime layer. Every recovered
// sound role must occur exactly once, all clip/voice IDs must be valid, and
// voices must be unique for one aircraft instance. Clips may be shared.
[[nodiscard]] bool validLegacyAircraftAudioBindings(
    const LegacyAircraftAudioBindings& bindings) noexcept;

// Joins the recovered engine and destroyed-dive state machines and translates
// their sound operations into the platform-neutral audio command contract.
// The original slot-44 order is preserved:
//   engine phase transitions -> destroyed dive -> common modulation.
//
// SetModelParameter and UpdateSounds remain semantic simulation operations;
// the former is represented by the returned smoothedThrust and the latter is
// unnecessary for the immediate command backend. Values outside the bounded
// portable gain/pitch contract fail closed instead of being clamped.
[[nodiscard]] std::optional<LegacyAircraftAudioCoordinatorStep>
legacyAircraftAdvanceAudio(LegacyAircraftAudioCoordinatorState current,
                           const LegacyAircraftAudioBindings& bindings,
                           LegacyAircraftAudioCoordinatorInput input) noexcept;

} // namespace airfix::simulation
