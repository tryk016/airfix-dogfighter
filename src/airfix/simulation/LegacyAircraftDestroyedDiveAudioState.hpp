#pragma once

#include "airfix/simulation/LegacyAircraftEngineAudioState.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace airfix::simulation {

inline constexpr float legacyAircraftDiveVelocityScale = -0.15F;
inline constexpr float legacyAircraftDiveVolumeOffset = 0.2F;
inline constexpr std::size_t
    legacyAircraftMaximumDestroyedDiveAudioCommands = 3U;

struct LegacyAircraftDestroyedDiveAudioState final {
    bool soundActive{};
};

struct LegacyAircraftDestroyedDiveAudioInput final {
    float health{};
    float velocityY{};
};

struct LegacyAircraftDestroyedDiveAudioStep final {
    LegacyAircraftDestroyedDiveAudioState state{};
    std::array<
        LegacyAircraftEngineAudioCommand,
        legacyAircraftMaximumDestroyedDiveAudioCommands> commands{};
    std::size_t commandCount{};
};

// Reconstructs AirCraft slot 44's enginedive sample state. The native caller
// reaches this block only when its shared five-call audio cadence expires,
// after engine phase transitions and before common engine modulation.
//
// Health <= 0 starts and retains the sample. Parameter 1 is held at 1.0 while
// parameter 0 follows clamp(-0.15 * velocityY - 0.2, 0, 1). Positive health
// stops an active sample. The original +0x566 flag is not initialized by the
// AirCraft constructor; the portable state deliberately defaults it to false
// so construction is deterministic.
[[nodiscard]] std::optional<LegacyAircraftDestroyedDiveAudioStep>
legacyAircraftAdvanceDestroyedDiveAudio(
    LegacyAircraftDestroyedDiveAudioState current,
    LegacyAircraftDestroyedDiveAudioInput input) noexcept;

} // namespace airfix::simulation
