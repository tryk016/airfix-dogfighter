#include "airfix/simulation/PlayerAircraftSimulation.hpp"

#include <limits>

namespace airfix::simulation {
namespace {

constexpr std::uint64_t fnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t fnv1a64Prime = 1099511628211ULL;

[[nodiscard]] constexpr bool frameHasInvalidQ15(
    const input::InputFrame& frame) noexcept {
    for (const auto value : frame.analogValues) {
        if (value == std::numeric_limits<input::Q15>::min()) {
            return true;
        }
    }
    return false;
}

constexpr void appendByte(
    std::uint64_t& hash, const std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv1a64Prime;
}

constexpr void appendU16(
    std::uint64_t& hash, const std::uint16_t value) noexcept {
    appendByte(hash, static_cast<std::uint8_t>(value));
    appendByte(hash, static_cast<std::uint8_t>(value >> 8U));
}

constexpr void appendU64(
    std::uint64_t& hash, const std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        appendByte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

PlayerAircraftAdvanceResult advance(
    const PlayerAircraftState& state,
    const input::InputFrame& frame) noexcept {
    if (frame.schemaVersion != input::inputFrameSchemaVersion) {
        return {state, PlayerAircraftAdvanceError::unsupportedInputSchema};
    }
    if (state.completedSteps != 0U &&
        frame.simulationTick <= state.lastInputTick) {
        return {state, PlayerAircraftAdvanceError::nonIncreasingInputTick};
    }
    if (frameHasInvalidQ15(frame)) {
        return {state, PlayerAircraftAdvanceError::invalidQ15};
    }

    const bool pressed =
        frame.pressed(input::DigitalAction::combatPrimaryFire);
    const bool released =
        frame.released(input::DigitalAction::combatPrimaryFire);
    if (state.completedSteps == std::numeric_limits<std::uint64_t>::max() ||
        (pressed &&
            state.primaryFirePressCount ==
                std::numeric_limits<std::uint64_t>::max()) ||
        (released &&
            state.primaryFireReleaseCount ==
                std::numeric_limits<std::uint64_t>::max())) {
        return {state, PlayerAircraftAdvanceError::counterOverflow};
    }

    PlayerAircraftState next = state;
    next.bankIntentQ15 = frame.analog(input::AnalogAxis::flightBank);
    next.pitchIntentQ15 = frame.analog(input::AnalogAxis::flightPitch);
    next.primaryFireHeld =
        frame.held(input::DigitalAction::combatPrimaryFire);
    next.primaryFirePressCount += pressed ? 1U : 0U;
    next.primaryFireReleaseCount += released ? 1U : 0U;
    ++next.completedSteps;
    next.lastInputTick = frame.simulationTick;
    return {next, PlayerAircraftAdvanceError::none};
}

std::uint64_t canonicalHash(const PlayerAircraftState& state) noexcept {
    std::uint64_t hash = fnv1a64OffsetBasis;

    // Domain marker and encoding version.
    appendByte(hash, 0x41U);
    appendByte(hash, 0x46U);
    appendByte(hash, 0x50U);
    appendByte(hash, 0x53U);
    appendU16(hash, 1U);

    appendU16(hash, static_cast<std::uint16_t>(state.bankIntentQ15));
    appendU16(hash, static_cast<std::uint16_t>(state.pitchIntentQ15));
    appendByte(hash, state.primaryFireHeld ? 1U : 0U);
    appendU64(hash, state.primaryFirePressCount);
    appendU64(hash, state.primaryFireReleaseCount);
    appendU64(hash, state.completedSteps);
    appendU64(hash, state.lastInputTick);
    return hash;
}

} // namespace airfix::simulation
