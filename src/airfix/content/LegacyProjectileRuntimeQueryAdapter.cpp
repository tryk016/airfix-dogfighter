#include "airfix/content/LegacyProjectileRuntimeQueryAdapter.hpp"

#include <bit>
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

} // namespace airfix::content
