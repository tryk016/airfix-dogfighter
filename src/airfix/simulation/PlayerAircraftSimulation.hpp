#pragma once

#include "airfix/input/InputFrame.hpp"

#include <cstdint>

namespace airfix::simulation {

// This is an intentionally non-physical reconstruction boundary. The complete
// native gameplay action surface is copied as uninterpreted input intentions;
// no pose, motion, weapon spawn, camera behavior, or throttle integration is
// implied by retaining it here.
struct PlayerAircraftState final {
    input::Q15 bankIntentQ15{};
    input::Q15 pitchIntentQ15{};
    input::Q15 throttleDeltaIntentQ15{};
    input::Q15 throttleSetIntentQ15{};
    input::Q15 cameraLookXIntentQ15{};
    input::Q15 cameraLookYIntentQ15{};
    bool primaryFireHeld{};
    bool secondaryFireHeld{};
    bool rearViewHeld{};
    std::uint64_t primaryFirePressCount{};
    std::uint64_t primaryFireReleaseCount{};
    std::uint64_t secondaryFirePressCount{};
    std::uint64_t secondaryFireReleaseCount{};
    std::uint64_t weaponNextPressCount{};
    std::uint64_t cameraCyclePressCount{};
    std::uint64_t rearViewPressCount{};
    std::uint64_t rearViewReleaseCount{};
    std::uint64_t cameraRecenterPressCount{};
    std::uint64_t missionStatusPressCount{};
    std::uint64_t pausePressCount{};
    std::uint64_t weaponSelectionCount{};
    std::uint8_t selectedWeapon{input::noWeaponSelection};
    std::uint64_t completedSteps{};
    std::uint64_t lastInputTick{};

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerAircraftState&, const PlayerAircraftState&) noexcept = default;
};

enum class PlayerAircraftAdvanceError : std::uint8_t {
    none = 0,
    unsupportedInputSchema = 1,
    nonIncreasingInputTick = 2,
    invalidQ15 = 3,
    counterOverflow = 4,
    invalidWeaponSelection = 5,
};

struct PlayerAircraftAdvanceResult final {
    PlayerAircraftState state{};
    PlayerAircraftAdvanceError error{PlayerAircraftAdvanceError::none};

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return error == PlayerAircraftAdvanceError::none;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return accepted();
    }
};

// Pure and allocation-free. A rejected frame returns the original state
// byte-for-field unchanged together with a diagnostic error.
[[nodiscard]] PlayerAircraftAdvanceResult advance(
    const PlayerAircraftState& state,
    const input::InputFrame& frame) noexcept;

// FNV-1a over a versioned, explicit little-endian field encoding. Object
// padding, host endianness, and the in-memory representation of bool never
// participate in the hash.
[[nodiscard]] std::uint64_t canonicalHash(
    const PlayerAircraftState& state) noexcept;

} // namespace airfix::simulation
