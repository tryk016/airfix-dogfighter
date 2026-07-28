#include "airfix/simulation/LegacyMachineGunProjectile.hpp"

#include "airfix/simulation/LegacyMachineGunFireState.hpp"

#include <array>
#include <bit>
#include <cmath>

namespace airfix::simulation {

namespace {

constexpr std::array<float, legacyMachineGunTechLevelCount> impactDamage{
    3.0F,
    4.0F,
    5.0F,
    6.0F,
    7.0F,
};

constexpr float maximumLifetimeSeconds = 4.0F;
constexpr float gravityY = std::bit_cast<float>(0xBF96D5CFU);
constexpr float half = 0.5F;

[[nodiscard]] bool finite(
    const LegacyMachineGunVector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool valid(
    const LegacyMachineGunAmmoProfile& profile) noexcept {
    return std::isfinite(profile.impactDamage) &&
        profile.impactDamage >= 0.0F &&
        std::isfinite(profile.maximumLifetimeSeconds) &&
        profile.maximumLifetimeSeconds >= 0.0F &&
        finite(profile.acceleration) &&
        std::isfinite(profile.broadHitRadiusSquared) &&
        profile.broadHitRadiusSquared >= 0.0F;
}

[[nodiscard]] LegacyMachineGunVector3 add(
    const LegacyMachineGunVector3& left,
    const LegacyMachineGunVector3& right) noexcept {
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

[[nodiscard]] LegacyMachineGunVector3 subtract(
    const LegacyMachineGunVector3& left,
    const LegacyMachineGunVector3& right) noexcept {
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

[[nodiscard]] LegacyMachineGunVector3 scale(
    const LegacyMachineGunVector3& value,
    const float factor) noexcept {
    return {
        value.x * factor,
        value.y * factor,
        value.z * factor,
    };
}

[[nodiscard]] float lengthSquared(
    const LegacyMachineGunVector3& value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

} // namespace

std::optional<LegacyMachineGunAmmoProfile>
legacyMachineGunAmmoProfile(const std::uint32_t techLevel) noexcept {
    if (techLevel >= impactDamage.size()) {
        return std::nullopt;
    }

    return LegacyMachineGunAmmoProfile{
        .impactDamage = impactDamage[techLevel],
        .maximumLifetimeSeconds = maximumLifetimeSeconds,
        .acceleration = {
            .x = 0.0F,
            .y = gravityY,
            .z = 0.0F,
        },
        .broadHitRadiusSquared = 0.0F,
    };
}

std::optional<LegacyMachineGunProjectileSpawnPayload>
legacyMachineGunProjectileSpawnPayload(
    const LegacyMachineGunProjectileSpawnInput& input) noexcept {
    if (!finite(input.creatorPosition) ||
        !finite(input.rotatedMuzzleOffset) ||
        !finite(input.aimPointRelativeToCreator) ||
        !std::isfinite(input.projectileSpeed) ||
        input.projectileSpeed <= 0.0F ||
        (input.targetLead.has_value() &&
         (!finite(input.targetLead->position) ||
          !finite(input.targetLead->velocity)))) {
        return std::nullopt;
    }

    const LegacyMachineGunVector3 position =
        add(input.creatorPosition, input.rotatedMuzzleOffset);
    LegacyMachineGunVector3 direction =
        subtract(
            input.aimPointRelativeToCreator,
            input.rotatedMuzzleOffset);
    if (!finite(position) || !finite(direction)) {
        return std::nullopt;
    }

    if (input.targetLead.has_value()) {
        const LegacyMachineGunVector3 targetDelta =
            subtract(input.targetLead->position, position);
        const float targetDistanceSquared = lengthSquared(targetDelta);
        if (!std::isfinite(targetDistanceSquared) ||
            targetDistanceSquared < 0.0F) {
            return std::nullopt;
        }
        const float leadSeconds =
            std::sqrt(targetDistanceSquared) / input.projectileSpeed;
        if (!std::isfinite(leadSeconds)) {
            return std::nullopt;
        }
        direction = add(
            direction,
            scale(input.targetLead->velocity, leadSeconds));
        if (!finite(direction)) {
            return std::nullopt;
        }
    }

    float directionLengthSquared = lengthSquared(direction);
    if (!std::isfinite(directionLengthSquared) ||
        directionLengthSquared < 0.0F) {
        return std::nullopt;
    }
    if (directionLengthSquared == 0.0F) {
        direction = {
            .x = 0.0F,
            .y = 1.0F,
            .z = 0.0F,
        };
        directionLengthSquared = 1.0F;
    }
    if (directionLengthSquared != 1.0F) {
        const float inverseLength =
            1.0F / std::sqrt(directionLengthSquared);
        if (!std::isfinite(inverseLength)) {
            return std::nullopt;
        }
        direction = scale(direction, inverseLength);
    }

    const LegacyMachineGunVector3 velocity =
        scale(direction, input.projectileSpeed);
    if (!finite(velocity)) {
        return std::nullopt;
    }

    return LegacyMachineGunProjectileSpawnPayload{
        .eventType = legacyMachineGunProjectileEvent,
        .position = position,
        .velocity = velocity,
        .roomId = input.roomId,
        .creatorUid = input.creatorUid,
        .targetUid = 0U,
    };
}

std::optional<LegacyMachineGunProjectileState>
legacyMachineGunProjectileInitialState(
    const LegacyMachineGunProjectileSpawnPayload& payload,
    const LegacyMachineGunAmmoProfile& profile) noexcept {
    if (payload.eventType != legacyMachineGunProjectileEvent ||
        !finite(payload.position) ||
        !finite(payload.velocity) ||
        !valid(profile)) {
        return std::nullopt;
    }

    return LegacyMachineGunProjectileState{
        .position = payload.position,
        .velocity = payload.velocity,
        .ageSeconds = 0.0F,
        .roomId = payload.roomId,
        .creatorUid = payload.creatorUid,
        .targetUid = payload.targetUid,
        .active = true,
    };
}

std::optional<LegacyMachineGunProjectileFlightStep>
legacyMachineGunProjectileAdvanceUnobstructed(
    const LegacyMachineGunProjectileState& current,
    const LegacyMachineGunAmmoProfile& profile,
    const float deltaSeconds) noexcept {
    if (!valid(profile) ||
        !finite(current.position) ||
        !finite(current.velocity) ||
        !std::isfinite(current.ageSeconds) ||
        current.ageSeconds < 0.0F ||
        !std::isfinite(deltaSeconds) ||
        deltaSeconds < 0.0F) {
        return std::nullopt;
    }

    LegacyMachineGunProjectileFlightStep step{
        .state = current,
        .segmentStart = current.position,
        .segmentEnd = current.position,
        .deactivatedByLifetime = false,
    };
    if (!current.active) {
        return step;
    }

    step.state.ageSeconds += deltaSeconds;
    if (!std::isfinite(step.state.ageSeconds)) {
        return std::nullopt;
    }
    if (step.state.ageSeconds > profile.maximumLifetimeSeconds) {
        step.state.active = false;
        step.deactivatedByLifetime = true;
        return step;
    }

    const float halfDeltaSquared =
        half * deltaSeconds * deltaSeconds;
    if (!std::isfinite(halfDeltaSquared)) {
        return std::nullopt;
    }
    const LegacyMachineGunVector3 linear =
        scale(current.velocity, deltaSeconds);
    const LegacyMachineGunVector3 quadratic =
        scale(profile.acceleration, halfDeltaSquared);
    step.segmentEnd = add(current.position, add(linear, quadratic));
    step.state.position = step.segmentEnd;
    step.state.velocity = add(
        current.velocity,
        scale(profile.acceleration, deltaSeconds));
    if (!finite(step.segmentEnd) || !finite(step.state.velocity)) {
        return std::nullopt;
    }
    return step;
}

std::optional<LegacyMachineGunDamageCommand>
legacyMachineGunProjectileActorHit(
    const LegacyMachineGunAmmoProfile& profile,
    const std::uint32_t creatorUid,
    const std::uint32_t hitActorUid) noexcept {
    if (!valid(profile)) {
        return std::nullopt;
    }
    if (creatorUid == 0U || creatorUid == hitActorUid) {
        return std::nullopt;
    }

    return LegacyMachineGunDamageCommand{
        .eventType = legacyMachineGunDamageEvent,
        .targetUid = hitActorUid,
        .damage = profile.impactDamage,
        .creatorUid = creatorUid,
        .deactivateProjectile = true,
    };
}

std::optional<LegacyMachineGunSurfaceContactResult>
legacyMachineGunProjectileSurfaceContact(
    const LegacyMachineGunSurfaceContactInput& input) noexcept {
    if (!finite(input.position) ||
        !finite(input.previousPosition) ||
        !std::isfinite(input.collisionFraction) ||
        input.collisionFraction < 0.0F ||
        input.collisionFraction > 1.0F ||
        !finite(input.normal)) {
        return std::nullopt;
    }

    if (input.ownerActorUid.has_value() &&
        *input.ownerActorUid == input.creatorUid) {
        return LegacyMachineGunSurfaceContactResult{
            .position = input.position,
            .ignoredCreatorSurface = true,
            .deactivateProjectile = false,
            .ricochet = std::nullopt,
        };
    }

    LegacyMachineGunVector3 position = input.position;
    if (input.material == legacyMachineGunInterpolatedMaterial) {
        position = add(
            input.previousPosition,
            scale(
                subtract(input.position, input.previousPosition),
                input.collisionFraction));
        if (!finite(position)) {
            return std::nullopt;
        }
    }

    LegacyMachineGunSurfaceContactResult result{
        .position = position,
        .ignoredCreatorSurface = false,
        .deactivateProjectile = true,
        .ricochet = std::nullopt,
    };
    if (input.roomId.has_value()) {
        result.ricochet = LegacyMachineGunRicochetCommand{
            .normal = input.normal,
            .material = input.material,
            .position = position,
            .roomId = *input.roomId,
        };
    }
    return result;
}

} // namespace airfix::simulation
