#include "airfix/simulation/LegacyMachineGunProjectileCollisionCommit.hpp"

#include <cmath>

namespace airfix::simulation {
namespace {

[[nodiscard]] bool finite(
    const LegacyMachineGunVector3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
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

[[nodiscard]] bool valid(
    const LegacyMachineGunProjectileState& state) noexcept {
    return finite(state.position) &&
        finite(state.velocity) &&
        std::isfinite(state.ageSeconds) &&
        state.ageSeconds >= 0.0F &&
        state.active;
}

[[nodiscard]] bool zero(
    const LegacyMachineGunVector3& value) noexcept {
    return value == LegacyMachineGunVector3{};
}

[[nodiscard]] bool validTerminalDecision(
    const LegacyProjectileCollisionDecision& decision) noexcept {
    if (!finite(decision.position) ||
        !finite(decision.previousPosition) ||
        !std::isfinite(decision.collisionFraction) ||
        decision.collisionFraction < 0.0F ||
        decision.collisionFraction > 1.0F ||
        !finite(decision.normal) ||
        (decision.actorUid.has_value() &&
         *decision.actorUid == 0U)) {
        return false;
    }

    switch (decision.outcome) {
    case LegacyProjectileCollisionOutcome::advanceNoHit:
        return decision.collisionFraction == 0.0F &&
            zero(decision.normal) &&
            !decision.material.has_value() &&
            !decision.actorUid.has_value() &&
            !decision.marksWater;
    case LegacyProjectileCollisionOutcome::advanceMaterialPassThrough:
        return decision.material ==
                legacyProjectilePassThroughMaterial &&
            !decision.actorUid.has_value() &&
            !decision.marksWater;
    case LegacyProjectileCollisionOutcome::advanceActorGate:
        return decision.material.has_value() &&
            !decision.actorUid.has_value() &&
            !decision.marksWater;
    case LegacyProjectileCollisionOutcome::actorContact:
        return decision.material.has_value() &&
            decision.actorUid.has_value() &&
            !zero(decision.normal) &&
            !decision.marksWater;
    case LegacyProjectileCollisionOutcome::surfaceContact:
        return !zero(decision.normal) &&
            decision.marksWater ==
                (decision.material ==
                 legacyMachineGunInterpolatedMaterial);
    case LegacyProjectileCollisionOutcome::followPortal:
        return false;
    }
    return false;
}

[[nodiscard]] LegacyMachineGunProjectileCollisionCommitResult failure(
    const LegacyMachineGunProjectileCollisionCommitStatus status,
    const LegacyMachineGunProjectileState& current) noexcept {
    return {
        .status = status,
        .state = current,
        .outcome = LegacyProjectileCollisionOutcome::advanceNoHit,
        .damage = std::nullopt,
        .surface = std::nullopt,
    };
}

} // namespace

LegacyMachineGunProjectileCollisionCommitResult
commitLegacyMachineGunProjectileCollision(
    const LegacyMachineGunProjectileState& current,
    const LegacyMachineGunAmmoProfile& profile,
    const LegacyProjectileCollisionLoopResult& collision) noexcept {
    if (!valid(current) || !valid(profile)) {
        return failure(
            LegacyMachineGunProjectileCollisionCommitStatus::
                invalidInput,
            current);
    }
    if (!collision.completed()) {
        return failure(
            LegacyMachineGunProjectileCollisionCommitStatus::
                incompleteCollision,
            current);
    }

    const auto& decision = *collision.decision;
    if (!validTerminalDecision(decision)) {
        return failure(
            LegacyMachineGunProjectileCollisionCommitStatus::
                rejectedTerminalDecision,
            current);
    }

    LegacyMachineGunProjectileCollisionCommitResult result{
        .status =
            LegacyMachineGunProjectileCollisionCommitStatus::committed,
        .state = current,
        .outcome = decision.outcome,
        .damage = std::nullopt,
        .surface = std::nullopt,
    };
    result.state.position = decision.position;
    result.state.roomId = decision.roomId;

    if (decision.outcome ==
        LegacyProjectileCollisionOutcome::actorContact) {
        result.damage = legacyMachineGunProjectileActorHit(
            profile, current.creatorUid, *decision.actorUid);
        if (result.damage.has_value() &&
            result.damage->deactivateProjectile) {
            result.state.active = false;
        }
        return result;
    }

    if (decision.outcome ==
        LegacyProjectileCollisionOutcome::surfaceContact) {
        result.state.waterContacted =
            result.state.waterContacted || decision.marksWater;
        result.surface = legacyMachineGunProjectileSurfaceContact({
            .creatorUid = current.creatorUid,
            .ownerActorUid = decision.actorUid,
            .position = decision.position,
            .previousPosition = decision.previousPosition,
            .collisionFraction = decision.collisionFraction,
            .normal = decision.normal,
            .material = decision.material,
            .roomId = decision.roomId,
        });
        if (!result.surface.has_value()) {
            return failure(
                LegacyMachineGunProjectileCollisionCommitStatus::
                    rejectedTerminalDecision,
                current);
        }
        result.state.position = result.surface->position;
        if (result.surface->deactivateProjectile) {
            result.state.active = false;
        }
    }
    return result;
}

} // namespace airfix::simulation
