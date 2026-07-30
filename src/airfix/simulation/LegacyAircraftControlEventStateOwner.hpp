#pragma once

#include "airfix/simulation/LegacyAircraftPitchBankEventReducer.hpp"
#include "airfix/simulation/LegacyAircraftThrustEventReducer.hpp"
#include "airfix/simulation/LegacyAircraftThrustState.hpp"
#include "airfix/simulation/LegacyAircraftTurnEventReducer.hpp"

#include <array>
#include <bit>
#include <cstdint>

namespace airfix::simulation {

// Portable snapshot of the AirCraft controls involved in native control-event
// writes and the vehicle rest wake gate. The caller supplies a complete
// initial snapshot explicitly. turnBits is the control written by the native
// event named TURN_SET; no stronger physical interpretation is implied.
struct LegacyAircraftControlEventState final {
    LegacyAircraftThrustControlState thrustControl{};
    std::uint32_t turnBits{};
    std::uint32_t pitchBits{};
    std::uint32_t bankBits{};
    std::int64_t restDurationMilliseconds{};

    [[nodiscard]] constexpr float turn() const noexcept {
        return std::bit_cast<float>(turnBits);
    }

    [[nodiscard]] constexpr float pitch() const noexcept {
        return std::bit_cast<float>(pitchBits);
    }

    [[nodiscard]] constexpr float bank() const noexcept {
        return std::bit_cast<float>(bankBits);
    }

    // Confirmed AfVehicle sleep-gate order:
    // +0x444, +0x440, +0x450, +0x44C, +0x448.
    [[nodiscard]] constexpr std::array<float, 5U>
    wakeControlValues() const noexcept {
        return {
            thrustControl.targetThrust,
            thrustControl.thrustApply,
            turn(),
            bank(),
            pitch(),
        };
    }

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyAircraftControlEventState&,
        const LegacyAircraftControlEventState&) noexcept = default;
};

enum class LegacyAircraftControlEventCommitStatus : std::uint8_t {
    committed,
    unsupportedField,
    nonFiniteValue,
};

// A simulation-thread-confined owner for already-decoded, already-ordered
// native control-event writes. "Transactional" means each selected field
// write and its optional shared-rest clear are validated and committed
// together. It does not claim lock-free or cross-thread atomic publication.
//
// This boundary deliberately owns no event decoding, producer ordering, Q15
// conversion, sample-and-hold, scheduler, nominal 12-ms timing, slot-45
// execution, sleep-result commit, rigid-body clear, or renderer publication.
class LegacyAircraftControlEventStateOwner final {
public:
    explicit constexpr LegacyAircraftControlEventStateOwner(
        const LegacyAircraftControlEventState initialState) noexcept
        : state_(initialState) {}

    [[nodiscard]] constexpr LegacyAircraftControlEventState
    snapshot() const noexcept {
        return state_;
    }

    [[nodiscard]] constexpr std::array<float, 5U>
    wakeControlValues() const noexcept {
        return state_.wakeControlValues();
    }

    [[nodiscard]] LegacyAircraftControlEventCommitStatus
    tryApply(const LegacyAircraftThrustWrite& write) noexcept;

    [[nodiscard]] LegacyAircraftControlEventCommitStatus
    tryApply(const LegacyAircraftPitchBankWrite& write) noexcept;

    [[nodiscard]] LegacyAircraftControlEventCommitStatus
    tryApply(const LegacyAircraftTurnWrite& write) noexcept;

private:
    LegacyAircraftControlEventState state_;
};

} // namespace airfix::simulation
