#include "airfix/simulation/LegacyMachineGunProjectile.hpp"

#include "airfix/simulation/LegacyMachineGunFireState.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>

namespace {

using airfix::simulation::LegacyMachineGunAmmoProfile;
using airfix::simulation::LegacyMachineGunProjectileSpawnInput;
using airfix::simulation::LegacyMachineGunProjectileSpawnPayload;
using airfix::simulation::LegacyMachineGunProjectileState;
using airfix::simulation::LegacyMachineGunSurfaceContactInput;
using airfix::simulation::LegacyMachineGunTargetLead;
using airfix::simulation::LegacyMachineGunVector3;
using airfix::simulation::legacyMachineGunAmmoProfile;
using airfix::simulation::legacyMachineGunDamageEvent;
using airfix::simulation::legacyMachineGunInterpolatedMaterial;
using airfix::simulation::legacyMachineGunProjectileActorHit;
using airfix::simulation::legacyMachineGunProjectileAdvanceUnobstructed;
using airfix::simulation::legacyMachineGunProjectileEvent;
using airfix::simulation::legacyMachineGunProjectileInitialState;
using airfix::simulation::legacyMachineGunProjectileSpawnPayload;
using airfix::simulation::legacyMachineGunProjectileSurfaceContact;
using airfix::simulation::legacyRicochetMaterialEvent;
using airfix::simulation::legacyRicochetNormalEvent;
using airfix::simulation::legacyRicochetPositionEvent;
using airfix::simulation::legacyRicochetPrimaryScalar;
using airfix::simulation::legacyRicochetPrimaryScalarEvent;
using airfix::simulation::legacyRicochetSecondaryScalar;
using airfix::simulation::legacyRicochetSecondaryScalarEvent;

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

[[nodiscard]] LegacyMachineGunAmmoProfile profile(
    const std::uint32_t level = 0U) {
    const auto result = legacyMachineGunAmmoProfile(level);
    require(result.has_value(), "supported ammo profile was rejected");
    return *result;
}

[[nodiscard]] LegacyMachineGunProjectileSpawnPayload spawn(
    const LegacyMachineGunProjectileSpawnInput& input) {
    const auto result = legacyMachineGunProjectileSpawnPayload(input);
    require(result.has_value(), "valid projectile spawn was rejected");
    return *result;
}

void testAmmoProfiles() {
    constexpr float expectedDamage[] = {
        3.0F,
        4.0F,
        5.0F,
        6.0F,
        7.0F,
    };
    for (std::uint32_t level = 0U; level < 5U; ++level) {
        const LegacyMachineGunAmmoProfile value = profile(level);
        require(
            value.impactDamage == expectedDamage[level],
            "ammo impact damage mismatch");
        require(
            value.maximumLifetimeSeconds == 4.0F,
            "ammo maximum lifetime mismatch");
        require(
            std::bit_cast<std::uint32_t>(value.acceleration.y) ==
                0xBF96D5CFU,
            "ammo vertical acceleration bits mismatch");
        require(
            value.acceleration.x == 0.0F &&
                value.acceleration.z == 0.0F,
            "ammo lateral acceleration mismatch");
        require(
            value.broadHitRadiusSquared == 0.0F,
            "aircraft ammo broad-hit radius must remain disabled");
    }
    require(
        !legacyMachineGunAmmoProfile(5U).has_value() &&
            !legacyMachineGunAmmoProfile(
                std::numeric_limits<std::uint32_t>::max()).has_value(),
        "unsupported ammo technology level was accepted");
}

void testSpawnPayloadWithoutLead() {
    const auto payload = spawn({
        .creatorPosition = {10.0F, 20.0F, 30.0F},
        .rotatedMuzzleOffset = {1.0F, 2.0F, 3.0F},
        .aimPointRelativeToCreator = {1.0F, 12.0F, 3.0F},
        .targetLead = std::nullopt,
        .projectileSpeed = 40.0F,
        .roomId = 17,
        .creatorUid = 41U,
    });

    require(
        payload.eventType == legacyMachineGunProjectileEvent,
        "projectile event type mismatch");
    require(
        payload.position == LegacyMachineGunVector3{11.0F, 22.0F, 33.0F},
        "projectile muzzle position mismatch");
    require(
        payload.velocity == LegacyMachineGunVector3{0.0F, 40.0F, 0.0F},
        "projectile velocity mismatch");
    require(
        payload.roomId == 17 &&
            payload.creatorUid == 41U &&
            payload.targetUid == 0U,
        "projectile identity payload mismatch");
}

void testSpawnNormalizesAndUsesLegacyFallback() {
    const auto normalized = spawn({
        .creatorPosition = {},
        .rotatedMuzzleOffset = {},
        .aimPointRelativeToCreator = {3.0F, 4.0F, 0.0F},
        .targetLead = std::nullopt,
        .projectileSpeed = 10.0F,
        .roomId = 0,
        .creatorUid = 1U,
    });
    require(
        close(normalized.velocity, {6.0F, 8.0F, 0.0F}),
        "projectile direction normalization mismatch");

    const auto fallback = spawn({
        .creatorPosition = {1.0F, 2.0F, 3.0F},
        .rotatedMuzzleOffset = {4.0F, 5.0F, 6.0F},
        .aimPointRelativeToCreator = {4.0F, 5.0F, 6.0F},
        .targetLead = std::nullopt,
        .projectileSpeed = 55.0F,
        .roomId = 0,
        .creatorUid = 1U,
    });
    require(
        fallback.velocity ==
            LegacyMachineGunVector3{0.0F, 55.0F, 0.0F},
        "zero direction did not use the recovered +Y fallback");
}

void testSpawnTargetLead() {
    const auto payload = spawn({
        .creatorPosition = {},
        .rotatedMuzzleOffset = {},
        .aimPointRelativeToCreator = {0.0F, 10.0F, 0.0F},
        .targetLead = LegacyMachineGunTargetLead{
            .position = {30.0F, 40.0F, 0.0F},
            .velocity = {2.0F, 0.0F, 0.0F},
        },
        .projectileSpeed = 10.0F,
        .roomId = 0,
        .creatorUid = 2U,
    });
    constexpr float component = 7.07106781F;
    require(
        close(payload.velocity, {component, component, 0.0F}),
        "target velocity lead mismatch");
    require(
        payload.targetUid == 0U,
        "selected lead target leaked into the legacy payload target field");
}

void testSpawnRejectsUnsafeInput() {
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    LegacyMachineGunProjectileSpawnInput input{
        .creatorPosition = {},
        .rotatedMuzzleOffset = {},
        .aimPointRelativeToCreator = {0.0F, 1.0F, 0.0F},
        .targetLead = std::nullopt,
        .projectileSpeed = 40.0F,
        .roomId = 0,
        .creatorUid = 1U,
    };

    input.projectileSpeed = 0.0F;
    require(
        !legacyMachineGunProjectileSpawnPayload(input).has_value(),
        "zero projectile speed was accepted");
    input.projectileSpeed = infinity;
    require(
        !legacyMachineGunProjectileSpawnPayload(input).has_value(),
        "infinite projectile speed was accepted");
    input.projectileSpeed = 40.0F;
    input.creatorPosition.x = nan;
    require(
        !legacyMachineGunProjectileSpawnPayload(input).has_value(),
        "non-finite creator position was accepted");
    input.creatorPosition.x = 0.0F;
    input.targetLead = LegacyMachineGunTargetLead{
        .position = {},
        .velocity = {infinity, 0.0F, 0.0F},
    };
    require(
        !legacyMachineGunProjectileSpawnPayload(input).has_value(),
        "non-finite target lead was accepted");
}

void testInitialStateAndUnobstructedFlight() {
    const LegacyMachineGunAmmoProfile ammo = profile();
    const auto payload = spawn({
        .creatorPosition = {},
        .rotatedMuzzleOffset = {},
        .aimPointRelativeToCreator = {1.0F, 2.0F, 3.0F},
        .targetLead = std::nullopt,
        .projectileSpeed = std::sqrt(1400.0F),
        .roomId = 8,
        .creatorUid = 9U,
    });
    const auto initial =
        legacyMachineGunProjectileInitialState(payload, ammo);
    require(initial.has_value(), "valid projectile initial state rejected");
    require(
        initial->position == payload.position &&
            initial->velocity == payload.velocity &&
            initial->ageSeconds == 0.0F &&
            initial->roomId == 8 &&
            initial->creatorUid == 9U &&
            initial->targetUid == 0U &&
            initial->active,
        "projectile initial state mismatch");

    LegacyMachineGunProjectileState state{
        .position = {},
        .velocity = {10.0F, 20.0F, 30.0F},
        .ageSeconds = 0.0F,
        .roomId = 8,
        .creatorUid = 9U,
        .targetUid = 0U,
        .active = true,
    };
    const auto step =
        legacyMachineGunProjectileAdvanceUnobstructed(state, ammo, 2.0F);
    require(step.has_value(), "valid unobstructed flight rejected");
    require(
        step->segmentStart == LegacyMachineGunVector3{},
        "flight segment start mismatch");
    require(
        close(step->segmentEnd, {20.0F, 37.6432F, 60.0F}),
        "flight segment end mismatch");
    require(
        close(step->state.velocity, {10.0F, 17.6432F, 30.0F}),
        "flight velocity integration mismatch");
    require(
        step->state.ageSeconds == 2.0F &&
            step->state.position == step->segmentEnd &&
            step->state.active &&
            !step->deactivatedByLifetime,
        "flight state transition mismatch");
}

void testFlightLifetimeAndInactiveGate() {
    const LegacyMachineGunAmmoProfile ammo = profile();
    LegacyMachineGunProjectileState state{
        .position = {1.0F, 2.0F, 3.0F},
        .velocity = {4.0F, 5.0F, 6.0F},
        .ageSeconds = 3.0F,
        .roomId = 1,
        .creatorUid = 2U,
        .targetUid = 0U,
        .active = true,
    };
    const auto atLimit =
        legacyMachineGunProjectileAdvanceUnobstructed(state, ammo, 1.0F);
    require(
        atLimit.has_value() &&
            atLimit->state.active &&
            atLimit->state.ageSeconds == 4.0F,
        "projectile expired at the inclusive lifetime boundary");

    const auto expired =
        legacyMachineGunProjectileAdvanceUnobstructed(state, ammo, 1.01F);
    require(
        expired.has_value() &&
            !expired->state.active &&
            expired->deactivatedByLifetime &&
            expired->segmentStart == state.position &&
            expired->segmentEnd == state.position &&
            expired->state.velocity == state.velocity,
        "projectile lifetime deactivation mismatch");

    state.active = false;
    const auto inactive =
        legacyMachineGunProjectileAdvanceUnobstructed(state, ammo, 1.0F);
    require(
        inactive.has_value() &&
            inactive->state == state &&
            !inactive->deactivatedByLifetime,
        "inactive projectile did not remain unchanged");
}

void testFlightRejectsUnsafeInput() {
    const LegacyMachineGunAmmoProfile ammo = profile();
    LegacyMachineGunProjectileState state{
        .position = {},
        .velocity = {},
        .ageSeconds = 0.0F,
        .roomId = 0,
        .creatorUid = 1U,
        .targetUid = 0U,
        .active = true,
    };
    require(
        !legacyMachineGunProjectileAdvanceUnobstructed(
             state, ammo, -0.01F).has_value(),
        "negative flight delta was accepted");
    state.velocity.x = std::numeric_limits<float>::quiet_NaN();
    require(
        !legacyMachineGunProjectileAdvanceUnobstructed(
             state, ammo, 0.0F).has_value(),
        "non-finite projectile state was accepted");
}

void testActorDamage() {
    for (std::uint32_t level = 0U; level < 5U; ++level) {
        const auto damage =
            legacyMachineGunProjectileActorHit(
                profile(level), 100U, 200U);
        require(damage.has_value(), "valid actor hit produced no damage");
        require(
            damage->eventType == legacyMachineGunDamageEvent &&
                damage->targetUid == 200U &&
                damage->damage == static_cast<float>(level + 3U) &&
                damage->creatorUid == 100U &&
                damage->deactivateProjectile,
            "actor damage command mismatch");
    }
    require(
        !legacyMachineGunProjectileActorHit(profile(), 0U, 200U)
             .has_value(),
        "zero creator produced damage");
    require(
        !legacyMachineGunProjectileActorHit(profile(), 100U, 100U)
             .has_value(),
        "creator self-hit produced damage");
}

void testSurfaceCreatorGate() {
    const auto result = legacyMachineGunProjectileSurfaceContact({
        .creatorUid = 7U,
        .ownerActorUid = 7U,
        .position = {1.0F, 2.0F, 3.0F},
        .previousPosition = {},
        .collisionFraction = 0.5F,
        .normal = {0.0F, 1.0F, 0.0F},
        .material = 4,
        .roomId = 3,
    });
    require(
        result.has_value() &&
            result->ignoredCreatorSurface &&
            !result->deactivateProjectile &&
            !result->ricochet.has_value() &&
            result->position ==
                LegacyMachineGunVector3{1.0F, 2.0F, 3.0F},
        "creator-owned surface gate mismatch");
}

void testSurfaceRicochetSequence() {
    const auto result = legacyMachineGunProjectileSurfaceContact({
        .creatorUid = 7U,
        .ownerActorUid = 8U,
        .position = {10.0F, 20.0F, 30.0F},
        .previousPosition = {2.0F, 4.0F, 6.0F},
        .collisionFraction = 0.25F,
        .normal = {0.0F, 0.5F, -0.5F},
        .material = legacyMachineGunInterpolatedMaterial,
        .roomId = 12,
    });
    require(
        result.has_value() &&
            !result->ignoredCreatorSurface &&
            result->deactivateProjectile &&
            result->ricochet.has_value(),
        "valid surface hit did not produce ricochet/deactivation");
    require(
        result->position == LegacyMachineGunVector3{4.0F, 8.0F, 12.0F},
        "material-15 contact interpolation mismatch");

    const auto& ricochet = *result->ricochet;
    require(
        ricochet.primaryScalarEvent ==
                legacyRicochetPrimaryScalarEvent &&
            ricochet.primaryScalar == legacyRicochetPrimaryScalar &&
            ricochet.secondaryScalarEvent ==
                legacyRicochetSecondaryScalarEvent &&
            ricochet.secondaryScalar == legacyRicochetSecondaryScalar &&
            ricochet.normalEvent == legacyRicochetNormalEvent &&
            ricochet.normal ==
                LegacyMachineGunVector3{0.0F, 0.5F, -0.5F} &&
            ricochet.materialEvent == legacyRicochetMaterialEvent &&
            ricochet.material == legacyMachineGunInterpolatedMaterial &&
            ricochet.positionEvent == legacyRicochetPositionEvent &&
            ricochet.position == result->position &&
            ricochet.roomId == 12,
        "ricochet event sequence mismatch");
}

void testSurfaceWithoutRoomStillDeactivates() {
    const auto result = legacyMachineGunProjectileSurfaceContact({
        .creatorUid = 7U,
        .ownerActorUid = std::nullopt,
        .position = {3.0F, 4.0F, 5.0F},
        .previousPosition = {1.0F, 1.0F, 1.0F},
        .collisionFraction = 0.75F,
        .normal = {0.0F, 1.0F, 0.0F},
        .material = 4,
        .roomId = std::nullopt,
    });
    require(
        result.has_value() &&
            result->position ==
                LegacyMachineGunVector3{3.0F, 4.0F, 5.0F} &&
            result->deactivateProjectile &&
            !result->ricochet.has_value(),
        "roomless surface contact mismatch");
}

void testSurfaceRejectsUnsafeInput() {
    LegacyMachineGunSurfaceContactInput input{
        .creatorUid = 1U,
        .ownerActorUid = std::nullopt,
        .position = {},
        .previousPosition = {},
        .collisionFraction = -0.1F,
        .normal = {0.0F, 1.0F, 0.0F},
        .material = 0,
        .roomId = 0,
    };
    require(
        !legacyMachineGunProjectileSurfaceContact(input).has_value(),
        "negative collision fraction was accepted");
    input.collisionFraction = 1.1F;
    require(
        !legacyMachineGunProjectileSurfaceContact(input).has_value(),
        "collision fraction above one was accepted");
    input.collisionFraction = 0.5F;
    input.normal.z = std::numeric_limits<float>::infinity();
    require(
        !legacyMachineGunProjectileSurfaceContact(input).has_value(),
        "non-finite contact normal was accepted");
}

} // namespace

int main() {
    static_assert(legacyMachineGunProjectileEvent == 0xE2U);
    static_assert(legacyMachineGunDamageEvent == 0x7DU);
    static_assert(legacyRicochetPrimaryScalarEvent == 0xA0U);
    static_assert(legacyRicochetSecondaryScalarEvent == 0xA2U);
    static_assert(legacyRicochetPositionEvent == 0xA6U);
    static_assert(legacyRicochetMaterialEvent == 0xB6U);
    static_assert(legacyRicochetNormalEvent == 0xB7U);

    testAmmoProfiles();
    testSpawnPayloadWithoutLead();
    testSpawnNormalizesAndUsesLegacyFallback();
    testSpawnTargetLead();
    testSpawnRejectsUnsafeInput();
    testInitialStateAndUnobstructedFlight();
    testFlightLifetimeAndInactiveGate();
    testFlightRejectsUnsafeInput();
    testActorDamage();
    testSurfaceCreatorGate();
    testSurfaceRicochetSequence();
    testSurfaceWithoutRoomStillDeactivates();
    testSurfaceRejectsUnsafeInput();

    std::cout << "Legacy machine-gun projectile tests passed\n";
    return EXIT_SUCCESS;
}
