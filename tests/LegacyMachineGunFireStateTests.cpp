#include "airfix/simulation/LegacyMachineGunFireState.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::simulation::LegacyMachineGunFireState;
using airfix::simulation::LegacyMachineGunTechProfile;
using airfix::simulation::legacyMachineGunAdvanceFire;
using airfix::simulation::legacyMachineGunInitialAmmunition;
using airfix::simulation::legacyMachineGunInitialFireState;
using airfix::simulation::legacyMachineGunMaximumAmmunition;
using airfix::simulation::legacyMachineGunProjectileEvent;
using airfix::simulation::legacyMachineGunTechLevelCount;
using airfix::simulation::legacyMachineGunTechProfile;
using airfix::simulation::legacyMachineGunZeroAmmunitionCadenceFactor;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(
    const float actual,
    const float expected,
    const float tolerance,
    const std::string& message) {
    require(
        std::fabs(actual - expected) <= tolerance,
        message + ": expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
}

void testRecoveredConstantsAndProfiles() {
    static_assert(legacyMachineGunProjectileEvent == 0xE2U);
    static_assert(legacyMachineGunTechLevelCount == 5U);
    static_assert(legacyMachineGunInitialAmmunition == 250U);
    static_assert(legacyMachineGunMaximumAmmunition == 500U);
    static_assert(
        legacyMachineGunZeroAmmunitionCadenceFactor == 10.0F);

    constexpr std::uint32_t expectedIntervalBits[]{
        0x3DF5C28FU,
        0x3DE147AEU,
        0x3DCCCCCDU,
        0x3DB851ECU,
        0x3DA3D70AU,
    };
    constexpr float expectedSpeeds[]{
        40.0F,
        55.0F,
        70.0F,
        85.0F,
        100.0F,
    };

    for (std::uint32_t level = 0U;
         level < legacyMachineGunTechLevelCount;
         ++level) {
        const LegacyMachineGunTechProfile profile =
            legacyMachineGunTechProfile(level);
        require(
            std::bit_cast<std::uint32_t>(
                profile.shotIntervalSeconds) ==
                expectedIntervalBits[level],
            "technology shot interval bits changed");
        require(
            profile.projectileSpeed == expectedSpeeds[level],
            "technology projectile speed changed");
    }

    const LegacyMachineGunTechProfile capped =
        legacyMachineGunTechProfile(500U);
    require(
        capped.shotIntervalSeconds == 0.08F &&
            capped.projectileSpeed == 100.0F,
        "technology level did not cap to the native final profile");
}

void testInitializationAndFirstShot() {
    const LegacyMachineGunTechProfile profile =
        legacyMachineGunTechProfile(0U);
    auto initial = legacyMachineGunInitialFireState(profile, 2U);
    require(initial.has_value(), "valid machine-gun state was rejected");
    require(
        initial->accumulatedSeconds == profile.shotIntervalSeconds &&
            initial->ammunition == legacyMachineGunInitialAmmunition &&
            initial->barrelIndex == 0U &&
            initial->barrelCount == 2U &&
            !initial->firing,
        "machine-gun initialization changed");

    initial->firing = true;
    const auto step =
        legacyMachineGunAdvanceFire(*initial, profile, 0.012F, true);
    require(
        step.has_value() && step->projectile.has_value(),
        "ready firing state did not request a projectile");
    require(
        step->projectile->eventType == legacyMachineGunProjectileEvent &&
            step->projectile->barrelIndex == 0U &&
            step->projectile->projectileSpeed == 40.0F,
        "first projectile command changed");
    require(
        step->state.ammunition ==
                legacyMachineGunInitialAmmunition - 1U &&
            step->state.barrelIndex == 1U,
        "first shot did not consume ammunition and advance the barrel");
    requireNear(
        step->state.accumulatedSeconds,
        0.012F,
        1.0e-6F,
        "first shot did not retain the fractional accumulator");
}

void testBarrelWrapAndOneShotPerRefresh() {
    const LegacyMachineGunTechProfile profile =
        legacyMachineGunTechProfile(2U);
    const LegacyMachineGunFireState current{
        .accumulatedSeconds = profile.shotIntervalSeconds,
        .ammunition = 3U,
        .barrelIndex = 1U,
        .barrelCount = 2U,
        .firing = true,
    };

    const auto step =
        legacyMachineGunAdvanceFire(current, profile, 1.0F, true);
    require(
        step.has_value() && step->projectile.has_value(),
        "large refresh did not request a projectile");
    require(
        step->projectile->barrelIndex == 1U &&
            step->state.barrelIndex == 0U &&
            step->state.ammunition == 2U,
        "barrel wrap or ammunition decrement changed");
    require(
        step->state.accumulatedSeconds >
            profile.shotIntervalSeconds,
        "large refresh incorrectly consumed multiple shot intervals");
}

void testReleasedTriggerCapsReadyTime() {
    const LegacyMachineGunTechProfile profile =
        legacyMachineGunTechProfile(0U);
    LegacyMachineGunFireState current{
        .accumulatedSeconds = 0.5F,
        .ammunition = 10U,
        .barrelIndex = 0U,
        .barrelCount = 2U,
        .firing = false,
    };

    const auto capped =
        legacyMachineGunAdvanceFire(current, profile, 0.001F, true);
    require(
        capped.has_value() && !capped->projectile.has_value() &&
            capped->state.accumulatedSeconds ==
                profile.shotIntervalSeconds,
        "released trigger did not cap accumulated ready time");

    current.accumulatedSeconds = profile.shotIntervalSeconds;
    const auto exact =
        legacyMachineGunAdvanceFire(current, profile, 0.0F, true);
    require(
        exact.has_value() &&
            exact->state.accumulatedSeconds ==
                profile.shotIntervalSeconds,
        "zero delta did not preserve the native early return");
}

void testZeroAmmunitionCadence() {
    const LegacyMachineGunTechProfile profile =
        legacyMachineGunTechProfile(0U);
    LegacyMachineGunFireState current{
        .accumulatedSeconds = profile.shotIntervalSeconds,
        .ammunition = 0U,
        .barrelIndex = 0U,
        .barrelCount = 2U,
        .firing = true,
    };

    const auto waiting =
        legacyMachineGunAdvanceFire(current, profile, 1.0F, true);
    require(
        waiting.has_value() && !waiting->projectile.has_value(),
        "zero ammunition ignored the recovered tenfold cadence");

    const auto shot =
        legacyMachineGunAdvanceFire(
            waiting->state,
            profile,
            0.1F,
            true);
    require(
        shot.has_value() && shot->projectile.has_value() &&
            shot->state.ammunition == 0U,
        "zero-ammunition shot did not preserve the zero count");
    requireNear(
        shot->state.accumulatedSeconds,
        0.02F,
        1.0e-5F,
        "zero-ammunition shot threshold changed");
}

void testDetachedParentAndBranchValidation() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const LegacyMachineGunTechProfile invalidSpeed{
        .shotIntervalSeconds = 0.12F,
        .projectileSpeed = nan,
    };
    LegacyMachineGunFireState detached{
        .accumulatedSeconds = 0.12F,
        .ammunition = 10U,
        .barrelIndex = 99U,
        .barrelCount = 0U,
        .firing = true,
    };

    const auto accumulated =
        legacyMachineGunAdvanceFire(
            detached,
            invalidSpeed,
            0.5F,
            false);
    require(
        accumulated.has_value() &&
            !accumulated->projectile.has_value() &&
            accumulated->state.accumulatedSeconds == 0.62F,
        "detached-parent path inspected skipped firing inputs");

    const auto rejectedSpawn =
        legacyMachineGunAdvanceFire(
            detached,
            invalidSpeed,
            0.5F,
            true);
    require(
        !rejectedSpawn.has_value(),
        "invalid consumed projectile state was accepted");

    LegacyMachineGunFireState skippedAtZero = detached;
    skippedAtZero.accumulatedSeconds = nan;
    const auto zeroDelta =
        legacyMachineGunAdvanceFire(
            skippedAtZero,
            invalidSpeed,
            0.0F,
            true);
    require(
        zeroDelta.has_value() &&
            std::isnan(zeroDelta->state.accumulatedSeconds),
        "zero scheduler delta inspected skipped weapon state");

    require(
        !legacyMachineGunInitialFireState(
             invalidSpeed,
             2U)
             .has_value(),
        "invalid initial profile was accepted");
    require(
        !legacyMachineGunInitialFireState(
             legacyMachineGunTechProfile(0U),
             0U)
             .has_value(),
        "zero-barrel initial state was accepted");
    require(
        !legacyMachineGunAdvanceFire(
             detached,
             legacyMachineGunTechProfile(0U),
             -0.001F,
             false)
             .has_value(),
        "negative scheduler delta was accepted");
}

} // namespace

int main() {
    try {
        testRecoveredConstantsAndProfiles();
        testInitializationAndFirstShot();
        testBarrelWrapAndOneShotPerRefresh();
        testReleasedTriggerCapsReadyTime();
        testZeroAmmunitionCadence();
        testDetachedParentAndBranchValidation();
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
