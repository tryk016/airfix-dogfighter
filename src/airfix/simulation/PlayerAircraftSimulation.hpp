#pragma once

#include "airfix/input/InputFrame.hpp"

#include <cstdint>

namespace airfix::simulation {

// This is an intentionally narrow reconstruction boundary. Bank and pitch are
// copied as uninterpreted input intentions; no pose, motion, throttle, or pitch
// sign convention is part of this first deterministic slice.
struct PlayerAircraftState final {
    input::Q15 bankIntentQ15{};
    input::Q15 pitchIntentQ15{};
    bool primaryFireHeld{};
    std::uint64_t primaryFirePressCount{};
    std::uint64_t primaryFireReleaseCount{};
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
