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
    if (!frame.hasValidWeaponSelection()) {
        return {state, PlayerAircraftAdvanceError::invalidWeaponSelection};
    }

    const bool primaryPressed =
        frame.pressed(input::DigitalAction::combatPrimaryFire);
    const bool primaryReleased =
        frame.released(input::DigitalAction::combatPrimaryFire);
    const bool secondaryPressed =
        frame.pressed(input::DigitalAction::combatSecondaryFire);
    const bool secondaryReleased =
        frame.released(input::DigitalAction::combatSecondaryFire);
    const bool weaponNextPressed =
        frame.pressed(input::DigitalAction::combatWeaponNext);
    const bool cameraCyclePressed =
        frame.pressed(input::DigitalAction::cameraCycle);
    const bool rearViewPressed =
        frame.pressed(input::DigitalAction::cameraRearView);
    const bool rearViewReleased =
        frame.released(input::DigitalAction::cameraRearView);
    const bool cameraRecenterPressed =
        frame.pressed(input::DigitalAction::cameraRecenter);
    const bool missionStatusPressed =
        frame.pressed(input::DigitalAction::missionStatus);
    const bool pausePressed =
        frame.pressed(input::DigitalAction::globalPause);
    const bool weaponSelected = frame.hasWeaponSelection();

    const auto incrementWouldOverflow = [](const bool increment,
                                           const std::uint64_t value) noexcept {
        return increment &&
            value == std::numeric_limits<std::uint64_t>::max();
    };
    if (state.completedSteps == std::numeric_limits<std::uint64_t>::max() ||
        incrementWouldOverflow(
            primaryPressed, state.primaryFirePressCount) ||
        incrementWouldOverflow(
            primaryReleased, state.primaryFireReleaseCount) ||
        incrementWouldOverflow(
            secondaryPressed, state.secondaryFirePressCount) ||
        incrementWouldOverflow(
            secondaryReleased, state.secondaryFireReleaseCount) ||
        incrementWouldOverflow(
            weaponNextPressed, state.weaponNextPressCount) ||
        incrementWouldOverflow(
            cameraCyclePressed, state.cameraCyclePressCount) ||
        incrementWouldOverflow(
            rearViewPressed, state.rearViewPressCount) ||
        incrementWouldOverflow(
            rearViewReleased, state.rearViewReleaseCount) ||
        incrementWouldOverflow(
            cameraRecenterPressed, state.cameraRecenterPressCount) ||
        incrementWouldOverflow(
            missionStatusPressed, state.missionStatusPressCount) ||
        incrementWouldOverflow(pausePressed, state.pausePressCount) ||
        incrementWouldOverflow(
            weaponSelected, state.weaponSelectionCount)) {
        return {state, PlayerAircraftAdvanceError::counterOverflow};
    }

    PlayerAircraftState next = state;
    next.bankIntentQ15 = frame.analog(input::AnalogAxis::flightBank);
    next.pitchIntentQ15 = frame.analog(input::AnalogAxis::flightPitch);
    next.throttleDeltaIntentQ15 =
        frame.analog(input::AnalogAxis::flightThrottleDelta);
    next.throttleSetIntentQ15 =
        frame.analog(input::AnalogAxis::flightThrottleSet);
    next.cameraLookXIntentQ15 =
        frame.analog(input::AnalogAxis::cameraLookX);
    next.cameraLookYIntentQ15 =
        frame.analog(input::AnalogAxis::cameraLookY);
    next.primaryFireHeld =
        frame.held(input::DigitalAction::combatPrimaryFire);
    next.secondaryFireHeld =
        frame.held(input::DigitalAction::combatSecondaryFire);
    next.rearViewHeld =
        frame.held(input::DigitalAction::cameraRearView);
    next.primaryFirePressCount += primaryPressed ? 1U : 0U;
    next.primaryFireReleaseCount += primaryReleased ? 1U : 0U;
    next.secondaryFirePressCount += secondaryPressed ? 1U : 0U;
    next.secondaryFireReleaseCount += secondaryReleased ? 1U : 0U;
    next.weaponNextPressCount += weaponNextPressed ? 1U : 0U;
    next.cameraCyclePressCount += cameraCyclePressed ? 1U : 0U;
    next.rearViewPressCount += rearViewPressed ? 1U : 0U;
    next.rearViewReleaseCount += rearViewReleased ? 1U : 0U;
    next.cameraRecenterPressCount += cameraRecenterPressed ? 1U : 0U;
    next.missionStatusPressCount += missionStatusPressed ? 1U : 0U;
    next.pausePressCount += pausePressed ? 1U : 0U;
    if (weaponSelected) {
        next.selectedWeapon = frame.weaponSelection;
        ++next.weaponSelectionCount;
    }
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
    appendU16(hash, 2U);

    appendU16(hash, static_cast<std::uint16_t>(state.bankIntentQ15));
    appendU16(hash, static_cast<std::uint16_t>(state.pitchIntentQ15));
    appendU16(hash, static_cast<std::uint16_t>(
        state.throttleDeltaIntentQ15));
    appendU16(hash, static_cast<std::uint16_t>(
        state.throttleSetIntentQ15));
    appendU16(hash, static_cast<std::uint16_t>(
        state.cameraLookXIntentQ15));
    appendU16(hash, static_cast<std::uint16_t>(
        state.cameraLookYIntentQ15));
    appendByte(hash, state.primaryFireHeld ? 1U : 0U);
    appendByte(hash, state.secondaryFireHeld ? 1U : 0U);
    appendByte(hash, state.rearViewHeld ? 1U : 0U);
    appendU64(hash, state.primaryFirePressCount);
    appendU64(hash, state.primaryFireReleaseCount);
    appendU64(hash, state.secondaryFirePressCount);
    appendU64(hash, state.secondaryFireReleaseCount);
    appendU64(hash, state.weaponNextPressCount);
    appendU64(hash, state.cameraCyclePressCount);
    appendU64(hash, state.rearViewPressCount);
    appendU64(hash, state.rearViewReleaseCount);
    appendU64(hash, state.cameraRecenterPressCount);
    appendU64(hash, state.missionStatusPressCount);
    appendU64(hash, state.pausePressCount);
    appendU64(hash, state.weaponSelectionCount);
    appendByte(hash, state.selectedWeapon);
    appendU64(hash, state.completedSteps);
    appendU64(hash, state.lastInputTick);
    return hash;
}

} // namespace airfix::simulation
