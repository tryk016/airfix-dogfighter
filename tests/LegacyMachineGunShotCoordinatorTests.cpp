#include "airfix/simulation/LegacyMachineGunShotCoordinator.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>

namespace {

using airfix::simulation::LegacyMachineGunFireState;
using airfix::simulation::LegacyMachineGunShotPreparationInput;
using airfix::simulation::LegacyMachineGunTargetLead;
using airfix::simulation::LegacyMachineGunVector3;
using airfix::simulation::legacyMachineGunPrepareShot;
using airfix::simulation::legacyMachineGunProjectileEvent;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 0.00001F) {
    return std::fabs(actual - expected) <= tolerance;
}

[[nodiscard]] bool close(
    const LegacyMachineGunVector3& actual,
    const LegacyMachineGunVector3& expected,
    const float tolerance = 0.00001F) {
    return close(actual.x, expected.x, tolerance) &&
        close(actual.y, expected.y, tolerance) &&
        close(actual.z, expected.z, tolerance);
}

[[nodiscard]] LegacyMachineGunShotPreparationInput inputFor(
    const LegacyMachineGunFireState& fireState,
    const std::span<const LegacyMachineGunVector3> muzzleOffsets) {
    return {
        .fireState = fireState,
        .requestedTechLevel = 0U,
        .deltaSeconds = 0.01F,
        .parentAttached = true,
        .creatorPosition = {10.0F, 20.0F, 30.0F},
        .rotatedMuzzleOffsets = muzzleOffsets,
        .aimPointRelativeToCreator = {1.0F, 12.0F, 3.0F},
        .targetLead = std::nullopt,
        .roomId = 17,
        .creatorUid = 41U,
    };
}

void testIdleStepDoesNotRequireMuzzleAccess() {
    LegacyMachineGunFireState state{
        .accumulatedSeconds = 0.0F,
        .ammunition = 250U,
        .barrelIndex = 0U,
        .barrelCount = 2U,
        .firing = false,
    };
    auto input = inputFor(state, {});
    input.deltaSeconds = 0.25F;

    const auto step = legacyMachineGunPrepareShot(input);
    require(step.has_value(), "valid idle shot preparation failed");
    require(!step->shot.has_value(), "idle weapon prepared a shot");
    require(
        step->fireState.accumulatedSeconds == 0.12F &&
            step->fireState.ammunition == 250U &&
            step->fireState.barrelIndex == 0U,
        "idle cadence clamp/state mismatch");

    input.parentAttached = false;
    input.fireState.firing = true;
    input.fireState.accumulatedSeconds = 0.12F;
    const auto detached = legacyMachineGunPrepareShot(input);
    require(
        detached.has_value() &&
            !detached->shot.has_value() &&
            detached->fireState.accumulatedSeconds == 0.37F,
        "detached parent path touched unavailable muzzle data");
}

void testPreparedShotSelectsBarrelAndPayload() {
    const std::array muzzleOffsets{
        LegacyMachineGunVector3{7.0F, 8.0F, 9.0F},
        LegacyMachineGunVector3{1.0F, 2.0F, 3.0F},
    };
    LegacyMachineGunFireState state{
        .accumulatedSeconds = 0.12F,
        .ammunition = 5U,
        .barrelIndex = 1U,
        .barrelCount = 2U,
        .firing = true,
    };
    const auto step =
        legacyMachineGunPrepareShot(inputFor(state, muzzleOffsets));
    require(
        step.has_value() && step->shot.has_value(),
        "eligible firing state did not prepare a shot");

    require(
        close(step->fireState.accumulatedSeconds, 0.01F) &&
            step->fireState.ammunition == 4U &&
            step->fireState.barrelIndex == 0U &&
            step->fireState.barrelCount == 2U &&
            step->fireState.firing,
        "prepared shot did not commit native fire state");

    const auto& shot = *step->shot;
    require(
        shot.effectiveTechLevel == 0U &&
            shot.command.eventType == legacyMachineGunProjectileEvent &&
            shot.command.barrelIndex == 1U &&
            shot.command.projectileSpeed == 40.0F,
        "prepared shot command mismatch");
    require(
        shot.ammoProfile.impactDamage == 3.0F &&
            shot.ammoProfile.maximumLifetimeSeconds == 4.0F,
        "prepared shot ammo profile mismatch");
    require(
        shot.payload.eventType == legacyMachineGunProjectileEvent &&
            shot.payload.position ==
                LegacyMachineGunVector3{11.0F, 22.0F, 33.0F} &&
            shot.payload.velocity ==
                LegacyMachineGunVector3{0.0F, 40.0F, 0.0F} &&
            shot.payload.roomId == 17 &&
            shot.payload.creatorUid == 41U &&
            shot.payload.targetUid == 0U,
        "prepared shot event payload mismatch");
}

void testTechClampAndTargetLeadCompose() {
    const std::array muzzleOffsets{
        LegacyMachineGunVector3{},
    };
    LegacyMachineGunFireState state{
        .accumulatedSeconds = 0.08F,
        .ammunition = 1U,
        .barrelIndex = 0U,
        .barrelCount = 1U,
        .firing = true,
    };
    auto input = inputFor(state, muzzleOffsets);
    input.requestedTechLevel =
        std::numeric_limits<std::uint32_t>::max();
    input.creatorPosition = {};
    input.aimPointRelativeToCreator = {0.0F, 10.0F, 0.0F};
    input.targetLead = LegacyMachineGunTargetLead{
        .position = {300.0F, 400.0F, 0.0F},
        .velocity = {2.0F, 0.0F, 0.0F},
    };

    const auto step = legacyMachineGunPrepareShot(input);
    require(
        step.has_value() && step->shot.has_value(),
        "clamped technology shot preparation failed");
    const auto& shot = *step->shot;
    require(
        shot.effectiveTechLevel == 4U &&
            shot.command.projectileSpeed == 100.0F &&
            shot.ammoProfile.impactDamage == 7.0F,
        "technology cap did not compose both weapon and ammo profiles");
    constexpr float component = 70.7106781F;
    require(
        close(
            shot.payload.velocity,
            {component, component, 0.0F}),
        "coordinator did not preserve target-velocity lead");
}

void testZeroAmmunitionCadenceStillPrepares() {
    const std::array muzzleOffsets{
        LegacyMachineGunVector3{},
    };
    LegacyMachineGunFireState state{
        .accumulatedSeconds = 1.2F,
        .ammunition = 0U,
        .barrelIndex = 0U,
        .barrelCount = 1U,
        .firing = true,
    };
    auto input = inputFor(state, muzzleOffsets);
    input.deltaSeconds = 0.01F;

    const auto step = legacyMachineGunPrepareShot(input);
    require(
        step.has_value() &&
            step->shot.has_value() &&
            step->fireState.ammunition == 0U &&
            close(step->fireState.accumulatedSeconds, 0.01F),
        "zero-ammunition native cadence did not prepare its request");
}

void testInvalidSelectedMuzzleFailsClosed() {
    const std::array oneMuzzle{
        LegacyMachineGunVector3{},
    };
    LegacyMachineGunFireState state{
        .accumulatedSeconds = 0.12F,
        .ammunition = 1U,
        .barrelIndex = 1U,
        .barrelCount = 2U,
        .firing = true,
    };
    require(
        !legacyMachineGunPrepareShot(inputFor(state, oneMuzzle))
             .has_value(),
        "incomplete muzzle snapshot was accepted");
}

void testInvalidPayloadInputFailsOnlyWhenShotIsDue() {
    const std::array muzzleOffsets{
        LegacyMachineGunVector3{},
    };
    LegacyMachineGunFireState state{
        .accumulatedSeconds = 0.0F,
        .ammunition = 1U,
        .barrelIndex = 0U,
        .barrelCount = 1U,
        .firing = false,
    };
    auto input = inputFor(state, muzzleOffsets);
    input.creatorPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto idle = legacyMachineGunPrepareShot(input);
    require(
        idle.has_value() && !idle->shot.has_value(),
        "idle transition accessed payload-only input");

    input.fireState.firing = true;
    input.fireState.accumulatedSeconds = 0.12F;
    require(
        !legacyMachineGunPrepareShot(input).has_value(),
        "due shot accepted non-finite payload input");
}

void testInvalidTimingInputFailsClosed() {
    const std::array muzzleOffsets{
        LegacyMachineGunVector3{},
    };
    LegacyMachineGunFireState state{
        .accumulatedSeconds = 0.0F,
        .ammunition = 1U,
        .barrelIndex = 0U,
        .barrelCount = 1U,
        .firing = true,
    };
    auto input = inputFor(state, muzzleOffsets);
    input.deltaSeconds = -0.01F;
    require(
        !legacyMachineGunPrepareShot(input).has_value(),
        "negative shot-coordinator delta was accepted");
}

} // namespace

int main() {
    testIdleStepDoesNotRequireMuzzleAccess();
    testPreparedShotSelectsBarrelAndPayload();
    testTechClampAndTargetLeadCompose();
    testZeroAmmunitionCadenceStillPrepares();
    testInvalidSelectedMuzzleFailsClosed();
    testInvalidPayloadInputFailsOnlyWhenShotIsDue();
    testInvalidTimingInputFailsClosed();

    std::cout << "Legacy machine-gun shot coordinator tests passed\n";
    return EXIT_SUCCESS;
}
