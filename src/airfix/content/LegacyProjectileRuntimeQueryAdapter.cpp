#include "airfix/content/LegacyProjectileRuntimeQueryAdapter.hpp"

#include <bit>
#include <cmath>
#include <optional>
#include <utility>

namespace airfix::content {
namespace {

using simulation::LegacyProjectileCollisionActorOwner;
using simulation::LegacyProjectileCollisionHit;
using simulation::LegacyProjectileCollisionLoopResult;
using simulation::LegacyProjectileCollisionLoopStatus;
using simulation::LegacyProjectileCollisionQueryInput;
using simulation::LegacyProjectileCollisionQueryResult;
using simulation::LegacyProjectileCollisionQueryStatus;

[[nodiscard]] LegacyProjectileCollisionQueryResult rejected() noexcept {
    return {
        .status = LegacyProjectileCollisionQueryStatus::rejected,
        .hit = std::nullopt,
    };
}

[[nodiscard]] LegacyProjectileCollisionQueryResult mappedHit(
    LegacyProjectileCollisionHit hit) noexcept {
    return {
        .status = LegacyProjectileCollisionQueryStatus::hit,
        .hit = std::move(hit),
    };
}

struct PublishedQueryContext final {
    const assets::MissionWorldRoomCatalog* catalog{};
    const render::LegacyGameplayCameraMissionRuntime* runtime{};
    bool projectileIsServer{};
    LegacyProjectileLiveActorQuery actorQuery{};
    void* actorQueryContext{};
    const render::MissionWorldRuntimeCombinedPortalLineTraceOptions*
        lineTraceOptions{};
};

struct PublishedPlayerActorQueryContext final {
    const render::LegacyGameplayCameraMissionRuntime* runtime{};
};

[[nodiscard]] LegacyProjectileLiveActorQueryResult
queryPublishedPlayerActor(
    void* const opaqueContext,
    const std::uint32_t actorObjectId) noexcept {
    if (opaqueContext == nullptr) {
        return {};
    }
    const auto& context =
        *static_cast<const PublishedPlayerActorQueryContext*>(
            opaqueContext);
    if (context.runtime == nullptr) {
        return {};
    }
    return queryPublishedLegacyProjectilePlayerActor(
        *context.runtime, actorObjectId);
}

[[nodiscard]] LegacyProjectileCollisionQueryResult queryPublished(
    void* const opaqueContext,
    const LegacyProjectileCollisionQueryInput& input) noexcept {
    if (opaqueContext == nullptr) {
        return rejected();
    }
    const auto& context =
        *static_cast<const PublishedQueryContext*>(opaqueContext);
    if (context.catalog == nullptr ||
        context.runtime == nullptr ||
        context.lineTraceOptions == nullptr) {
        return rejected();
    }

    const auto worldRoomIndex =
        assets::worldRoomIndexForLegacyCcRoomId(
            *context.catalog, input.roomId);
    if (!worldRoomIndex.has_value()) {
        return rejected();
    }
    const auto trace =
        context.runtime->tracePublishedDynamicCollisionPortalLine(
            *worldRoomIndex,
            {
                input.segmentStart.x,
                input.segmentStart.y,
                input.segmentStart.z,
            },
            {
                input.segmentEnd.x,
                input.segmentEnd.y,
                input.segmentEnd.z,
            },
            *context.lineTraceOptions);
    return legacyProjectileQueryResultFromRuntimeTrace(
        *context.catalog,
        trace,
        context.projectileIsServer,
        context.actorQuery,
        context.actorQueryContext);
}

} // namespace

LegacyProjectileLiveActorQueryResult
queryPublishedLegacyProjectilePlayerActor(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const std::uint32_t actorObjectId) noexcept {
    if (actorObjectId == 0U ||
        !runtime.dynamicCollisionFramePublished()) {
        return {};
    }

    const auto player =
        runtime.currentDynamicCollisionPlayerActorState();
    if (!player.has_value() ||
        player->objectId != actorObjectId) {
        return {
            .status =
                LegacyProjectileLiveActorQueryStatus::notFound,
        };
    }
    return {
        .status = LegacyProjectileLiveActorQueryStatus::resolved,
        .projectileActorCollisionsEnabled =
            player->projectileActorCollisionsEnabled,
        .actorAcceptsProjectileCollision =
            player->actorAcceptsProjectileCollision,
        .actorActive = player->active,
    };
}

LegacyProjectileCollisionQueryResult
legacyProjectileQueryResultFromRuntimeTrace(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::MissionWorldRuntimeCombinedLineTraceResult& trace,
    const bool projectileIsServer,
    const LegacyProjectileLiveActorQuery actorQuery,
    void* const actorQueryContext) noexcept {
    if (!catalog.complete()) {
        return rejected();
    }

    if (trace.status ==
        render::MissionWorldRuntimeCombinedLineTraceStatus::noHit) {
        if (trace.hit.has_value()) {
            return rejected();
        }
        return {
            .status = LegacyProjectileCollisionQueryStatus::noHit,
            .hit = std::nullopt,
        };
    }
    if (trace.status !=
            render::MissionWorldRuntimeCombinedLineTraceStatus::hit ||
        !trace.hit.has_value()) {
        return rejected();
    }

    const auto& source = *trace.hit;
    if (!source.withinRequestedSegment) {
        return rejected();
    }
    LegacyProjectileCollisionHit hit{
        .fraction = source.legacyFraction,
        .normal =
            {
                source.runtimePlaneNormal.x,
                source.runtimePlaneNormal.y,
                source.runtimePlaneNormal.z,
            },
        .material = std::nullopt,
        .ownerObjectPresent = false,
        .portalType = -1,
        .portalRoomId = std::nullopt,
        .actorOwner = std::nullopt,
    };

    if (source.kind ==
        render::MissionWorldRuntimeCombinedLineHitKind::staticRoom) {
        if (!source.ownerWorldRoomIndex.has_value() ||
            !source.treeIndex.has_value() ||
            !source.nodeIndex.has_value() ||
            !source.polygonIndex.has_value() ||
            source.dynamicObjectIndex.has_value() ||
            source.dynamicMeshIndex.has_value() ||
            source.sourceTriangleIndex.has_value() ||
            source.sourceMaterialReference.has_value() ||
            source.actorObjectId != 0U) {
            return rejected();
        }
        return mappedHit(std::move(hit));
    }
    if (source.kind !=
            render::MissionWorldRuntimeCombinedLineHitKind::dynamicObject ||
        source.ownerWorldRoomIndex.has_value() ||
        source.treeIndex.has_value() ||
        source.nodeIndex.has_value() ||
        source.polygonIndex.has_value() ||
        !source.dynamicObjectIndex.has_value() ||
        !source.dynamicMeshIndex.has_value() ||
        !source.sourceTriangleIndex.has_value() ||
        !source.sourceMaterialReference.has_value() ||
        !source.materialCollisionMode2152.has_value()) {
        return rejected();
    }

    hit.ownerObjectPresent = true;
    hit.material = std::bit_cast<std::int32_t>(
        *source.materialCollisionMode2152);
    hit.portalType = source.portalType;

    if (*hit.material ==
        simulation::legacyProjectilePassThroughMaterial) {
        return mappedHit(std::move(hit));
    }

    if (source.actorObjectId == 0U) {
        if (source.portalType == 0) {
            if (!source.portalWorldRoomIndex.has_value()) {
                return rejected();
            }
            hit.portalRoomId =
                assets::legacyCcRoomIdForWorldRoomIndex(
                    catalog, *source.portalWorldRoomIndex);
            if (!hit.portalRoomId.has_value()) {
                return rejected();
            }
        } else if (source.portalType == 1) {
            if (!source.portalWorldRoomIndex.has_value() ||
                !assets::legacyCcRoomIdForWorldRoomIndex(
                     catalog, *source.portalWorldRoomIndex)
                     .has_value()) {
                return rejected();
            }
        } else if (source.portalType == -1) {
            if (source.portalWorldRoomIndex.has_value()) {
                return rejected();
            }
        } else {
            return rejected();
        }
        return mappedHit(std::move(hit));
    }

    LegacyProjectileCollisionActorOwner actor{
        .uid = source.actorObjectId,
        .projectileIsServer = projectileIsServer,
        .actorResolved = false,
        .projectileActorCollisionsEnabled = false,
        .actorAcceptsProjectileCollision = false,
        .actorActive = false,
    };
    if (!projectileIsServer) {
        hit.actorOwner = actor;
        return mappedHit(std::move(hit));
    }
    if (actorQuery == nullptr) {
        return rejected();
    }

    const auto queriedActor =
        actorQuery(actorQueryContext, source.actorObjectId);
    if (queriedActor.status ==
        LegacyProjectileLiveActorQueryStatus::notFound) {
        hit.actorOwner = actor;
        return mappedHit(std::move(hit));
    }
    if (queriedActor.status !=
        LegacyProjectileLiveActorQueryStatus::resolved) {
        return rejected();
    }

    actor.actorResolved = true;
    actor.projectileActorCollisionsEnabled =
        queriedActor.projectileActorCollisionsEnabled;
    actor.actorAcceptsProjectileCollision =
        queriedActor.actorAcceptsProjectileCollision;
    actor.actorActive = queriedActor.actorActive;
    hit.actorOwner = actor;
    return mappedHit(std::move(hit));
}

LegacyProjectileCollisionLoopResult
resolvePublishedLegacyProjectileCollisionLoop(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const LegacyProjectileCollisionQueryInput& input,
    const bool projectileIsServer,
    const LegacyProjectileLiveActorQuery actorQuery,
    void* const actorQueryContext,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    if (!catalog.complete() ||
        catalog.rooms.size() != runtime.worldRoomCount()) {
        return {
            .status = LegacyProjectileCollisionLoopStatus::invalidInput,
            .decision = std::nullopt,
            .queryCount = 0U,
            .portalTransitionCount = 0U,
        };
    }

    PublishedQueryContext context{
        .catalog = &catalog,
        .runtime = &runtime,
        .projectileIsServer = projectileIsServer,
        .actorQuery = actorQuery,
        .actorQueryContext = actorQueryContext,
        .lineTraceOptions = &options.lineTrace,
    };
    return simulation::resolveLegacyProjectileCollisionLoop(
        input,
        queryPublished,
        &context,
        options.collisionLoop);
}

LegacyProjectileCollisionLoopResult
resolvePublishedLegacyProjectileCollisionLoop(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const LegacyProjectileCollisionQueryInput& input,
    const bool projectileIsServer,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    PublishedPlayerActorQueryContext actorContext{
        .runtime = &runtime,
    };
    return resolvePublishedLegacyProjectileCollisionLoop(
        catalog,
        runtime,
        input,
        projectileIsServer,
        queryPublishedPlayerActor,
        &actorContext,
        options);
}

LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollision(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const LegacyProjectileCollisionQueryInput& input,
    const bool projectileIsServer,
    const LegacyProjectileLiveActorQuery actorQuery,
    void* const actorQueryContext,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    LegacyPublishedMachineGunProjectileCollisionResult result;
    if (current.position != input.segmentEnd ||
        current.roomId != input.roomId) {
        result.collision.status =
            LegacyProjectileCollisionLoopStatus::invalidInput;
        return result;
    }

    result.collision = resolvePublishedLegacyProjectileCollisionLoop(
        catalog,
        runtime,
        input,
        projectileIsServer,
        actorQuery,
        actorQueryContext,
        options);
    if (result.collision.completed()) {
        result.commit =
            simulation::commitLegacyMachineGunProjectileCollision(
                current, profile, result.collision);
    }
    return result;
}

LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollision(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const LegacyProjectileCollisionQueryInput& input,
    const bool projectileIsServer,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    PublishedPlayerActorQueryContext actorContext{
        .runtime = &runtime,
    };
    return resolvePublishedLegacyMachineGunProjectileCollision(
        catalog,
        runtime,
        current,
        profile,
        input,
        projectileIsServer,
        queryPublishedPlayerActor,
        &actorContext,
        options);
}

LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollisionWithCreatorBspGuard(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const LegacyProjectileCollisionQueryInput& input,
    const bool projectileIsServer,
    const LegacyProjectileLiveActorQuery actorQuery,
    void* const actorQueryContext,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    LegacyPublishedMachineGunProjectileCollisionResult result;
    result.creatorBspGuard =
        LegacyProjectileCreatorBspGuardStatus::notEntered;

    // Do not touch a live actor when the state/query join already fails the
    // same local precondition as the unguarded transaction.
    if (current.position != input.segmentEnd ||
        current.roomId != input.roomId) {
        return result;
    }

    if (current.creatorUid == 0U) {
        result = resolvePublishedLegacyMachineGunProjectileCollision(
            catalog,
            runtime,
            current,
            profile,
            input,
            projectileIsServer,
            actorQuery,
            actorQueryContext,
            options);
        result.creatorBspGuard =
            LegacyProjectileCreatorBspGuardStatus::noCreatorUid;
        return result;
    }

    if (creatorBspGuard.disableBsp == nullptr ||
        creatorBspGuard.enableBsp == nullptr) {
        result.creatorBspGuard =
            LegacyProjectileCreatorBspGuardStatus::disableRejected;
        return result;
    }

    const auto disabled = creatorBspGuard.disableBsp(
        creatorBspGuard.context, current.creatorUid);
    if (disabled.status ==
        LegacyProjectileCreatorBspDisableStatus::actorNotFound) {
        if (disabled.actorHandle != nullptr ||
            disabled.bspWasEnabled) {
            result.creatorBspGuard =
                LegacyProjectileCreatorBspGuardStatus::disableRejected;
            return result;
        }
        result = resolvePublishedLegacyMachineGunProjectileCollision(
            catalog,
            runtime,
            current,
            profile,
            input,
            projectileIsServer,
            actorQuery,
            actorQueryContext,
            options);
        result.creatorBspGuard =
            LegacyProjectileCreatorBspGuardStatus::actorNotFound;
        return result;
    }
    if (disabled.status !=
            LegacyProjectileCreatorBspDisableStatus::completed ||
        (disabled.bspWasEnabled &&
         disabled.actorHandle == nullptr)) {
        result.creatorBspGuard =
            LegacyProjectileCreatorBspGuardStatus::disableRejected;
        return result;
    }

    result = resolvePublishedLegacyMachineGunProjectileCollision(
        catalog,
        runtime,
        current,
        profile,
        input,
        projectileIsServer,
        actorQuery,
        actorQueryContext,
        options);
    if (!disabled.bspWasEnabled) {
        result.creatorBspGuard =
            LegacyProjectileCreatorBspGuardStatus::alreadyDisabled;
        return result;
    }

    const auto enabled = creatorBspGuard.enableBsp(
        creatorBspGuard.context, disabled.actorHandle);
    if (enabled != LegacyProjectileCreatorBspEnableStatus::completed) {
        result.creatorBspGuard =
            LegacyProjectileCreatorBspGuardStatus::enableRejected;
        result.commit.reset();
        return result;
    }
    result.creatorBspGuard =
        LegacyProjectileCreatorBspGuardStatus::restored;
    return result;
}

LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollisionWithCreatorBspGuard(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const LegacyProjectileCollisionQueryInput& input,
    const bool projectileIsServer,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    PublishedPlayerActorQueryContext actorContext{
        .runtime = &runtime,
    };
    return
        resolvePublishedLegacyMachineGunProjectileCollisionWithCreatorBspGuard(
            catalog,
            runtime,
            current,
            profile,
            input,
            projectileIsServer,
            queryPublishedPlayerActor,
            &actorContext,
            creatorBspGuard,
            options);
}

LegacyPublishedMachineGunProjectileSlotsAdvanceResult
advancePublishedLegacyMachineGunProjectileSlots(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const std::span<simulation::LegacyMachineGunProjectileSlot> slots,
    const std::span<LegacyPublishedMachineGunProjectileSlotAdvanceResult>
        results,
    const float deltaSeconds, const bool projectileIsServer,
    const LegacyProjectileLiveActorQuery actorQuery,
    void* const actorQueryContext,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    LegacyPublishedMachineGunProjectileSlotsAdvanceResult result;
    if (results.size() != slots.size()) {
        result.status = LegacyPublishedMachineGunProjectileSlotsAdvanceStatus::
            outputSizeMismatch;
        return result;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F ||
        !catalog.complete() ||
        catalog.rooms.size() != runtime.worldRoomCount()) {
        return result;
    }

    for (auto& slotResult : results) {
        slotResult = {};
    }

    for (std::size_t slotIndex = 0U; slotIndex < slots.size(); ++slotIndex) {
        auto& slot = slots[slotIndex];
        auto& slotResult = results[slotIndex];
        ++result.visitedSlotCount;
        if (!slot.state.active) {
            slotResult.status =
                LegacyPublishedMachineGunProjectileSlotAdvanceStatus::inactive;
            continue;
        }

        ++result.activeSlotCount;
        if (slot.generation == 0U) {
            slotResult.status =
                LegacyPublishedMachineGunProjectileSlotAdvanceStatus::
                    invalidSlot;
            ++result.rejectedSlotCount;
            continue;
        }
        slotResult.projectile = simulation::LegacyMachineGunProjectileHandle{
            .slotIndex = slotIndex,
            .generation = slot.generation,
        };

        const auto flight =
            simulation::legacyMachineGunProjectileAdvanceUnobstructed(
                slot.state, slot.ammoProfile, deltaSeconds);
        if (!flight.has_value()) {
            slotResult.status =
                LegacyPublishedMachineGunProjectileSlotAdvanceStatus::
                    invalidSlot;
            ++result.rejectedSlotCount;
            continue;
        }
        if (flight->deactivatedByLifetime) {
            slot.state = flight->state;
            slotResult.status =
                LegacyPublishedMachineGunProjectileSlotAdvanceStatus::
                    deactivatedByLifetime;
            ++result.stateCommitCount;
            continue;
        }

        const simulation::LegacyProjectileCollisionQueryInput query{
            .segmentStart = flight->segmentStart,
            .segmentEnd = flight->segmentEnd,
            .roomId = flight->state.roomId,
        };
        const auto collision =
            resolvePublishedLegacyMachineGunProjectileCollisionWithCreatorBspGuard(
                catalog, runtime, flight->state, slot.ammoProfile, query,
                projectileIsServer, actorQuery, actorQueryContext,
                creatorBspGuard, options);
        slotResult.creatorBspGuard = collision.creatorBspGuard;
        slotResult.queryCount = collision.collision.queryCount;
        slotResult.portalTransitionCount =
            collision.collision.portalTransitionCount;

        if (collision.creatorBspGuard ==
            LegacyProjectileCreatorBspGuardStatus::enableRejected) {
            slotResult.status =
                LegacyPublishedMachineGunProjectileSlotAdvanceStatus::
                    creatorRestoreFailed;
            ++result.rejectedSlotCount;
            result.status =
                LegacyPublishedMachineGunProjectileSlotsAdvanceStatus::
                    creatorRestoreFailed;
            return result;
        }
        if (!collision.committed()) {
            slotResult.status =
                LegacyPublishedMachineGunProjectileSlotAdvanceStatus::
                    collisionRejected;
            ++result.rejectedSlotCount;
            continue;
        }

        slot.state = collision.commit->state;
        slotResult.status =
            LegacyPublishedMachineGunProjectileSlotAdvanceStatus::advanced;
        slotResult.outcome = collision.commit->outcome;
        slotResult.damage = collision.commit->damage;
        slotResult.surface = collision.commit->surface;
        ++result.stateCommitCount;
    }

    result.status =
        result.rejectedSlotCount == 0U
            ? LegacyPublishedMachineGunProjectileSlotsAdvanceStatus::completed
            : LegacyPublishedMachineGunProjectileSlotsAdvanceStatus::
                  completedWithRejectedSlots;
    return result;
}

LegacyPublishedMachineGunProjectileSlotsAdvanceResult
advancePublishedLegacyMachineGunProjectileSlots(
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const std::span<simulation::LegacyMachineGunProjectileSlot> slots,
    const std::span<LegacyPublishedMachineGunProjectileSlotAdvanceResult>
        results,
    const float deltaSeconds, const bool projectileIsServer,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options) noexcept {
    PublishedPlayerActorQueryContext actorContext{
        .runtime = &runtime,
    };
    return advancePublishedLegacyMachineGunProjectileSlots(
        catalog, runtime, slots, results, deltaSeconds, projectileIsServer,
        queryPublishedPlayerActor, &actorContext, creatorBspGuard, options);
}

} // namespace airfix::content
