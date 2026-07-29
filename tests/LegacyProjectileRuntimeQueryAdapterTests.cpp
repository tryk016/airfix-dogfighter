#include "airfix/content/LegacyProjectileRuntimeQueryAdapter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace {

using namespace airfix::assets;
using namespace airfix::content;
using namespace airfix::render;
using namespace airfix::simulation;

std::atomic_bool countAllocations{};
std::atomic_size_t allocationCount{};

[[noreturn]] void fail(const char* const message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* const message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] MissionWorldRoomCatalog catalog(
    const std::size_t roomCount = 1U) {
    MissionWorldRoomCatalog result;
    result.rooms.resize(roomCount);
    return result;
}

[[nodiscard]] MissionWorldRuntimeCombinedLineTraceResult noHitTrace() {
    return {
        .status = MissionWorldRuntimeCombinedLineTraceStatus::noHit,
        .hit = std::nullopt,
    };
}

[[nodiscard]] MissionWorldRuntimeCombinedLineTraceResult staticTrace() {
    return {
        .status = MissionWorldRuntimeCombinedLineTraceStatus::hit,
        .hit =
            MissionWorldRuntimeCombinedLineHit{
                .kind =
                    MissionWorldRuntimeCombinedLineHitKind::staticRoom,
                .legacyFraction = 0.25F,
                .runtimePoint = {0.0F, 0.0F, 1.0F},
                .runtimePlaneNormal = {0.0F, 0.0F, 2.0F},
                .materialCollisionMode2152 = 15U,
                .ownerWorldRoomIndex = 0U,
                .treeIndex = 1U,
                .nodeIndex = 2U,
                .polygonIndex = 3U,
                .portalWorldRoomIndex = std::nullopt,
                .dynamicObjectIndex = std::nullopt,
                .dynamicMeshIndex = std::nullopt,
                .sourceTriangleIndex = std::nullopt,
                .sourceMaterialReference = std::nullopt,
                .actorObjectId = 0U,
                .portalType = -1,
                .portalObjectVisible = false,
                .reverseFacing = false,
                .withinRequestedSegment = true,
            },
    };
}

[[nodiscard]] MissionWorldRuntimeCombinedLineTraceResult dynamicTrace(
    const std::uint32_t actorObjectId = 0U,
    const std::uint32_t material = 4U,
    const std::int32_t portalType = -1,
    const std::optional<std::size_t> portalWorldRoomIndex =
        std::nullopt) {
    return {
        .status = MissionWorldRuntimeCombinedLineTraceStatus::hit,
        .hit =
            MissionWorldRuntimeCombinedLineHit{
                .kind =
                    MissionWorldRuntimeCombinedLineHitKind::dynamicObject,
                .legacyFraction = 0.5F,
                .runtimePoint = {0.0F, 0.0F, 2.0F},
                .runtimePlaneNormal = {0.0F, 0.0F, 1.0F},
                .materialCollisionMode2152 = material,
                .ownerWorldRoomIndex = std::nullopt,
                .treeIndex = std::nullopt,
                .nodeIndex = std::nullopt,
                .polygonIndex = std::nullopt,
                .portalWorldRoomIndex = portalWorldRoomIndex,
                .dynamicObjectIndex = 5U,
                .dynamicMeshIndex = 6U,
                .sourceTriangleIndex = 7U,
                .sourceMaterialReference = 8U,
                .actorObjectId = actorObjectId,
                .portalType = portalType,
                .portalObjectVisible = false,
                .reverseFacing = false,
                .withinRequestedSegment = true,
            },
    };
}

struct ActorQueryState final {
    LegacyProjectileLiveActorQueryResult result;
    std::size_t callCount{};
    std::uint32_t lastActorObjectId{};
};

[[nodiscard]] LegacyProjectileLiveActorQueryResult queryActor(
    void* const context,
    const std::uint32_t actorObjectId) noexcept {
    auto& state = *static_cast<ActorQueryState*>(context);
    ++state.callCount;
    state.lastActorObjectId = actorObjectId;
    return state.result;
}

void testNoHitAndStaticMapping() {
    const auto rooms = catalog();
    const auto noHit = legacyProjectileQueryResultFromRuntimeTrace(
        rooms, noHitTrace(), true, nullptr, nullptr);
    require(
        noHit.status == LegacyProjectileCollisionQueryStatus::noHit &&
            !noHit.hit.has_value(),
        "runtime no-hit mapping failed");

    const auto mappedStatic =
        legacyProjectileQueryResultFromRuntimeTrace(
            rooms, staticTrace(), true, nullptr, nullptr);
    require(
        mappedStatic.status ==
                LegacyProjectileCollisionQueryStatus::hit &&
            mappedStatic.hit.has_value() &&
            mappedStatic.hit->fraction == 0.25F &&
            !mappedStatic.hit->ownerObjectPresent &&
            !mappedStatic.hit->material.has_value() &&
            !mappedStatic.hit->actorOwner.has_value() &&
            mappedStatic.hit->normal ==
                LegacyMachineGunVector3{0.0F, 0.0F, 2.0F},
        "static ownerless mapping failed");
}

void testMaterialAndClientActorOrder() {
    const auto rooms = catalog();
    ActorQueryState mustNotRun{
        .result =
            {
                .status =
                    LegacyProjectileLiveActorQueryStatus::rejected,
            },
    };
    const auto material = legacyProjectileQueryResultFromRuntimeTrace(
        rooms,
        dynamicTrace(
            91U,
            static_cast<std::uint32_t>(
                legacyProjectilePassThroughMaterial)),
        true,
        queryActor,
        &mustNotRun);
    require(
        material.status ==
                LegacyProjectileCollisionQueryStatus::hit &&
            material.hit.has_value() &&
            material.hit->ownerObjectPresent &&
            material.hit->material ==
                std::optional<std::int32_t>{
                    legacyProjectilePassThroughMaterial} &&
            !material.hit->actorOwner.has_value() &&
            mustNotRun.callCount == 0U,
        "material 8 did not precede actor resolution");

    const auto materialBeforePortal =
        legacyProjectileQueryResultFromRuntimeTrace(
            rooms,
            dynamicTrace(
                0U,
                static_cast<std::uint32_t>(
                    legacyProjectilePassThroughMaterial),
                2,
                std::nullopt),
            true,
            queryActor,
            &mustNotRun);
    require(
        materialBeforePortal.status ==
                LegacyProjectileCollisionQueryStatus::hit &&
            materialBeforePortal.hit->material ==
                std::optional<std::int32_t>{
                    legacyProjectilePassThroughMaterial} &&
            mustNotRun.callCount == 0U,
        "material 8 was incorrectly gated by later portal metadata");

    const auto client = legacyProjectileQueryResultFromRuntimeTrace(
        rooms,
        dynamicTrace(92U),
        false,
        queryActor,
        &mustNotRun);
    require(
        client.status == LegacyProjectileCollisionQueryStatus::hit &&
            client.hit.has_value() &&
            client.hit->actorOwner.has_value() &&
            client.hit->actorOwner->uid == 92U &&
            !client.hit->actorOwner->projectileIsServer &&
            !client.hit->actorOwner->actorResolved &&
            mustNotRun.callCount == 0U,
        "client actor gate invoked the live resolver");
}

void testServerActorResolutionStates() {
    const auto rooms = catalog();
    ActorQueryState resolved{
        .result =
            {
                .status =
                    LegacyProjectileLiveActorQueryStatus::resolved,
                .projectileActorCollisionsEnabled = true,
                .actorAcceptsProjectileCollision = false,
                .actorActive = true,
            },
    };
    auto mapped = legacyProjectileQueryResultFromRuntimeTrace(
        rooms,
        dynamicTrace(93U),
        true,
        queryActor,
        &resolved);
    require(
        mapped.status == LegacyProjectileCollisionQueryStatus::hit &&
            mapped.hit->actorOwner.has_value() &&
            mapped.hit->actorOwner->uid == 93U &&
            mapped.hit->actorOwner->projectileIsServer &&
            mapped.hit->actorOwner->actorResolved &&
            mapped.hit->actorOwner
                ->projectileActorCollisionsEnabled &&
            !mapped.hit->actorOwner
                 ->actorAcceptsProjectileCollision &&
            mapped.hit->actorOwner->actorActive &&
            resolved.callCount == 1U &&
            resolved.lastActorObjectId == 93U,
        "resolved server actor gates were not preserved");

    ActorQueryState missing{
        .result =
            {
                .status =
                    LegacyProjectileLiveActorQueryStatus::notFound,
            },
    };
    mapped = legacyProjectileQueryResultFromRuntimeTrace(
        rooms,
        dynamicTrace(94U),
        true,
        queryActor,
        &missing);
    require(
        mapped.status == LegacyProjectileCollisionQueryStatus::hit &&
            mapped.hit->actorOwner.has_value() &&
            mapped.hit->actorOwner->uid == 94U &&
            !mapped.hit->actorOwner->actorResolved &&
            missing.callCount == 1U,
        "server actor lookup miss did not map to surface fallback");

    ActorQueryState rejectedState{
        .result =
            {
                .status =
                    LegacyProjectileLiveActorQueryStatus::rejected,
            },
    };
    const auto rejectedResult =
        legacyProjectileQueryResultFromRuntimeTrace(
            rooms,
            dynamicTrace(95U),
            true,
            queryActor,
            &rejectedState);
    const auto missingResolver =
        legacyProjectileQueryResultFromRuntimeTrace(
            rooms,
            dynamicTrace(96U),
            true,
            nullptr,
            nullptr);
    ActorQueryState unknownState{
        .result =
            {
                .status = static_cast<
                    LegacyProjectileLiveActorQueryStatus>(0xFFU),
            },
    };
    const auto unknownResult =
        legacyProjectileQueryResultFromRuntimeTrace(
            rooms,
            dynamicTrace(97U),
            true,
            queryActor,
            &unknownState);
    require(
        rejectedResult.status ==
                LegacyProjectileCollisionQueryStatus::rejected &&
            missingResolver.status ==
                LegacyProjectileCollisionQueryStatus::rejected &&
            unknownResult.status ==
                LegacyProjectileCollisionQueryStatus::rejected,
        "server actor resolver failure was accepted");
}

void testPortalAndSignedMaterialMapping() {
    const auto rooms = catalog(4U);
    const auto portal = legacyProjectileQueryResultFromRuntimeTrace(
        rooms,
        dynamicTrace(0U, 4U, 0, 1U),
        true,
        nullptr,
        nullptr);
    require(
        portal.status == LegacyProjectileCollisionQueryStatus::hit &&
            portal.hit.has_value() &&
            portal.hit->portalType == 0 &&
            portal.hit->portalRoomId ==
                std::optional<std::int32_t>{3},
        "portal world-room target did not map to legacy room ID");

    const auto rawNegative =
        legacyProjectileQueryResultFromRuntimeTrace(
            rooms,
            dynamicTrace(0U, 0xFFFFFFFFU),
            true,
            nullptr,
            nullptr);
    require(
        rawNegative.status ==
                LegacyProjectileCollisionQueryStatus::hit &&
            rawNegative.hit->material ==
                std::optional<std::int32_t>{-1},
        "raw 0x2152 material bits were not preserved");
}

void testMalformedRuntimeResultsFailClosed() {
    const auto rooms = catalog();
    auto inconsistentNoHit = noHitTrace();
    inconsistentNoHit.hit = staticTrace().hit;
    auto missingHit = noHitTrace();
    missingHit.status =
        MissionWorldRuntimeCombinedLineTraceStatus::hit;
    auto invalidStatus = noHitTrace();
    invalidStatus.status =
        MissionWorldRuntimeCombinedLineTraceStatus::invalidArena;
    auto incompleteDynamic = dynamicTrace();
    incompleteDynamic.hit->materialCollisionMode2152 =
        std::nullopt;
    auto badPortal = dynamicTrace(0U, 4U, 0, std::nullopt);
    auto badType = dynamicTrace(0U, 4U, 2, std::nullopt);
    auto missingSolidPortalTarget =
        dynamicTrace(0U, 4U, 1, std::nullopt);
    auto inconsistentRange = dynamicTrace();
    inconsistentRange.hit->withinRequestedSegment = false;
    auto badStatic = staticTrace();
    badStatic.hit->dynamicObjectIndex = 0U;
    MissionWorldRoomCatalog incompleteCatalog;

    const auto rejectedStatus =
        LegacyProjectileCollisionQueryStatus::rejected;
    require(
        legacyProjectileQueryResultFromRuntimeTrace(
            rooms, inconsistentNoHit, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms, missingHit, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms, invalidStatus, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms, incompleteDynamic, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms, badPortal, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms, badType, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms,
                missingSolidPortalTarget,
                true,
                nullptr,
                nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms, inconsistentRange, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                rooms, badStatic, true, nullptr, nullptr)
                .status == rejectedStatus &&
            legacyProjectileQueryResultFromRuntimeTrace(
                incompleteCatalog,
                noHitTrace(),
                true,
                nullptr,
                nullptr)
                .status == rejectedStatus,
        "malformed runtime trace crossed the adapter");
}

[[nodiscard]] ConvertedMeshGeometry triangleGeometry() {
    ConvertedMeshGeometry geometry;
    geometry.reference = 10U;
    geometry.vertices = {
        {{-2.0F, -2.0F, 0.0F}},
        {{2.0F, -2.0F, 0.0F}},
        {{0.0F, 2.0F, 0.0F}},
    };
    geometry.triangles = {
        {
            .vertexIndices = {0U, 1U, 2U},
            .materialReference = 100U,
        },
    };
    return geometry;
}

[[nodiscard]] MissionPlacedDynamicBspAssembly emptyPlacedCollision() {
    MissionPlacedDynamicBspAssembly placed;
    placed.roomObjectRanges.push_back({
        .firstObjectIndex = 0U,
        .objectCount = 0U,
    });
    placed.retainedPayloadBytes =
        sizeof(LegacyDynamicBspRoomObjectRange);
    require(
        placed.complete(),
        "empty placed collision fixture failed validation");
    return placed;
}

[[nodiscard]] PlayerActorCollisionAssembly playerCollision() {
    PlayerActorCollisionAssembly player;
    const LegacyDynamicBspMaterialBinding binding{
        .sourceReference = 100U,
        .collisionMode2152 = 4U,
    };
    player.meshes.push_back(
        buildLegacyDynamicBsp(
            triangleGeometry(), {&binding, 1U}));
    require(
        player.meshes[0].complete(),
        "player fixture mesh build failed");
    const PlayerActorVisualProvenance actor{
        .legacySkinSlot = 0U,
        .blueprintIndex = 1U,
        .blueprintReference = 20U,
        .physicalMeshIndex = 0U,
    };
    player.meshProvenance.push_back({
        .actor = actor,
        .collisionMeshIndex = 0U,
        .sourceMeshReference = 10U,
    });
    player.instances.push_back({
        .collisionMeshIndex = 0U,
        .actor = actor,
        .actorLocal =
            {
                .linear = {},
                .translation = {},
                .rawScalar = 1.0F,
            },
    });
    player.retainedPayloadBytes =
        sizeof(LegacyDynamicBspMesh) +
        sizeof(PlayerActorCollisionMeshProvenance) +
        sizeof(PlayerActorCollisionInstance) +
        player.meshes[0].retainedPayloadBytes;
    require(
        player.complete(),
        "player collision fixture failed validation");
    return player;
}

[[nodiscard]] MissionPlacedDynamicBspAssembly portalCollision() {
    MissionPlacedDynamicBspAssembly placed;
    const LegacyDynamicBspMaterialBinding binding{
        .sourceReference = 100U,
        .collisionMode2152 = 4U,
    };
    placed.meshes.push_back(
        buildLegacyDynamicBsp(triangleGeometry(), {&binding, 1U}));
    require(
        placed.meshes[0].complete(),
        "portal fixture mesh build failed");
    placed.meshProvenance.push_back({
        .sourceIndex = 0U,
        .physicalMeshIndex = 0U,
        .firstPlacedNodeIndex = 0U,
        .sourceMeshReference = 10U,
    });
    placed.objects = {
        {
            .meshIndex = 0U,
            .actorObjectId = 0U,
            .active = true,
            .objectLocalToRuntime = {},
            .runtimeTranslation = {0.0F, 0.0F, 2.0F},
            .portalType = 0,
            .portalWorldRoomIndex = 1U,
            .portalObjectVisible = false,
        },
        {
            .meshIndex = 0U,
            .actorObjectId = 0U,
            .active = true,
            .objectLocalToRuntime = {},
            .runtimeTranslation = {0.0F, 0.0F, 3.0F},
            .portalType = -1,
            .portalWorldRoomIndex = std::nullopt,
            .portalObjectVisible = false,
        },
    };
    placed.objectProvenance = {
        {
            .sourceIndex = 0U,
            .placedNodeIndex = 0U,
            .physicalMeshIndex = 0U,
            .worldRoomIndex = 0U,
            .sourceNodeReference = 20U,
        },
        {
            .sourceIndex = 0U,
            .placedNodeIndex = 1U,
            .physicalMeshIndex = 0U,
            .worldRoomIndex = 1U,
            .sourceNodeReference = 21U,
        },
    };
    placed.roomObjectRanges = {
        {.firstObjectIndex = 0U, .objectCount = 1U},
        {.firstObjectIndex = 1U, .objectCount = 1U},
    };
    placed.retainedPayloadBytes =
        sizeof(LegacyDynamicBspMesh) +
        sizeof(MissionPlacedDynamicBspMeshProvenance) +
        2U * sizeof(LegacyDynamicBspLineObject) +
        2U * sizeof(MissionPlacedDynamicBspObjectProvenance) +
        2U * sizeof(LegacyDynamicBspRoomObjectRange) +
        placed.meshes[0].retainedPayloadBytes;
    require(
        placed.complete(),
        "portal collision fixture failed validation");
    return placed;
}

[[nodiscard]] LegacyGameplayCameraStepCoordinatorInitializeInput
initializeInput() {
    return {
        .vehicleWorldPosition = {},
        .vehicleWorldRotation = {},
        .worldRoomIndex = 0U,
        .cameraCyclePressCount = 0U,
    };
}

[[nodiscard]] LegacyGameplayCameraMissionRuntimeBuildResult
buildPortalRuntime() {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(2U);
    auto placed = portalCollision();
    std::optional<PlayerActorCollisionAssembly> noPlayer;
    return LegacyGameplayCameraMissionRuntime::create(
        std::move(arena),
        std::move(placed),
        std::move(noPlayer),
        {},
        initializeInput());
}

[[nodiscard]] LegacyGameplayCameraMissionRuntimeBuildResult
buildPlayerRuntime() {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(1U);
    auto placed = emptyPlacedCollision();
    std::optional<PlayerActorCollisionAssembly> player{
        playerCollision()};
    return LegacyGameplayCameraMissionRuntime::create(
        std::move(arena),
        std::move(placed),
        std::move(player),
        {},
        initializeInput());
}

void testPublishedPlayerActorResolverAndNoAllocations() {
    auto built = buildPlayerRuntime();
    require(built.complete(), "player runtime creation failed");
    require(
        queryPublishedLegacyProjectilePlayerActor(
            *built.runtime, 91U)
                .status ==
            LegacyProjectileLiveActorQueryStatus::rejected,
        "unpublished player resolver did not fail closed");

    const ConvertedNodeTransform playerWorld{
        .linear = {},
        .translation = {0.0F, 0.0F, 2.0F},
        .rawScalar = 1.0F,
    };
    const auto published =
        built.runtime->tryPublishDynamicCollisionFrame(
            playerWorld,
            {
                .objectId = 91U,
                .active = true,
                .projectileActorCollisionsEnabled = true,
                .actorAcceptsProjectileCollision = true,
            },
            0U);
    const auto resolvedActor =
        queryPublishedLegacyProjectilePlayerActor(
            *built.runtime, 91U);
    require(
        published.published() &&
            resolvedActor.status ==
                LegacyProjectileLiveActorQueryStatus::resolved &&
            resolvedActor.projectileActorCollisionsEnabled &&
            resolvedActor.actorAcceptsProjectileCollision &&
            resolvedActor.actorActive &&
            queryPublishedLegacyProjectilePlayerActor(
                *built.runtime, 92U)
                    .status ==
                LegacyProjectileLiveActorQueryStatus::notFound &&
            queryPublishedLegacyProjectilePlayerActor(
                *built.runtime, 0U)
                    .status ==
                LegacyProjectileLiveActorQueryStatus::rejected,
        "published player resolver state mismatch");

    const auto rooms = catalog();
    const LegacyProjectileCollisionQueryInput input{
        .segmentStart = {0.0F, 0.0F, 0.0F},
        .segmentEnd = {0.0F, 0.0F, 4.0F},
        .roomId = 0,
    };
    const auto actorContact =
        resolvePublishedLegacyProjectileCollisionLoop(
            rooms, *built.runtime, input, true);
    require(
        actorContact.completed() &&
            actorContact.queryCount == 1U &&
            actorContact.portalTransitionCount == 0U &&
            actorContact.decision->outcome ==
                LegacyProjectileCollisionOutcome::actorContact &&
            actorContact.decision->actorUid ==
                std::optional<std::uint32_t>{91U} &&
            actorContact.decision->position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 2.0F} &&
            actorContact.decision->material ==
                std::optional<std::int32_t>{4},
        "published player did not resolve to actor contact");

    const auto ammo = legacyMachineGunAmmoProfile(0U);
    require(ammo.has_value(), "machine-gun profile fixture missing");
    const LegacyMachineGunProjectileState projectile{
        .position = input.segmentEnd,
        .velocity = {0.0F, 0.0F, 20.0F},
        .ageSeconds = 1.0F,
        .roomId = input.roomId,
        .creatorUid = 7U,
        .targetUid = 0U,
        .active = true,
        .waterContacted = false,
    };
    const auto actorTransaction =
        resolvePublishedLegacyMachineGunProjectileCollision(
            rooms,
            *built.runtime,
            projectile,
            *ammo,
            input,
            true);
    require(
        actorTransaction.committed() &&
            actorTransaction.collision.queryCount == 1U &&
            actorTransaction.commit->outcome ==
                LegacyProjectileCollisionOutcome::actorContact &&
            !actorTransaction.commit->state.active &&
            actorTransaction.commit->state.position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 2.0F} &&
            actorTransaction.commit->damage.has_value() &&
            actorTransaction.commit->damage->targetUid == 91U &&
            actorTransaction.commit->damage->creatorUid == 7U &&
            !actorTransaction.commit->surface.has_value(),
        "published actor terminal transaction mismatch");

    auto mismatchedProjectile = projectile;
    mismatchedProjectile.position.z = 3.0F;
    const auto mismatchedTransaction =
        resolvePublishedLegacyMachineGunProjectileCollision(
            rooms,
            *built.runtime,
            mismatchedProjectile,
            *ammo,
            input,
            true);
    require(
        !mismatchedTransaction.committed() &&
            mismatchedTransaction.collision.status ==
                LegacyProjectileCollisionLoopStatus::invalidInput &&
            mismatchedTransaction.collision.queryCount == 0U &&
            !mismatchedTransaction.commit.has_value(),
        "mismatched flight/query transaction reached the runtime");

    const auto projectileGatePublication =
        built.runtime->tryPublishDynamicCollisionFrame(
            playerWorld,
            {
                .objectId = 91U,
                .active = true,
                .projectileActorCollisionsEnabled = false,
                .actorAcceptsProjectileCollision = true,
            },
            0U);
    const auto projectileGate =
        resolvePublishedLegacyProjectileCollisionLoop(
            rooms, *built.runtime, input, true);
    require(
        projectileGatePublication.published() &&
            projectileGate.completed() &&
            projectileGate.decision->outcome ==
                LegacyProjectileCollisionOutcome::advanceActorGate,
        "published projectile collision gate was ignored");

    const auto gatedPublication =
        built.runtime->tryPublishDynamicCollisionFrame(
            playerWorld,
            {
                .objectId = 91U,
                .active = true,
                .projectileActorCollisionsEnabled = true,
                .actorAcceptsProjectileCollision = false,
            },
            0U);
    const auto actorGate =
        resolvePublishedLegacyProjectileCollisionLoop(
            rooms, *built.runtime, input, true);
    const auto clientGate =
        resolvePublishedLegacyProjectileCollisionLoop(
            rooms, *built.runtime, input, false);
    require(
        gatedPublication.published() &&
            actorGate.completed() &&
            actorGate.decision->outcome ==
                LegacyProjectileCollisionOutcome::advanceActorGate &&
            clientGate.completed() &&
            clientGate.decision->outcome ==
                LegacyProjectileCollisionOutcome::advanceActorGate,
        "published player actor gate order diverged");

    bool complete = true;
    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t index = 0U; index < 4'096U; ++index) {
        const auto repeated =
            resolvePublishedLegacyMachineGunProjectileCollision(
                rooms,
                *built.runtime,
                projectile,
                *ammo,
                input,
                true);
        complete = complete && repeated.committed() &&
            repeated.collision.queryCount == 1U &&
            repeated.commit->outcome ==
                LegacyProjectileCollisionOutcome::advanceActorGate &&
            repeated.commit->state.active &&
            !repeated.commit->damage.has_value() &&
            !repeated.commit->surface.has_value();
    }
    countAllocations.store(false, std::memory_order_relaxed);
    require(
        complete,
        "steady-state published player resolver failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "steady-state published player resolver allocated");

    const auto inactivePublication =
        built.runtime->tryPublishDynamicCollisionFrame(
            playerWorld,
            {
                .objectId = 91U,
                .active = false,
                .projectileActorCollisionsEnabled = true,
                .actorAcceptsProjectileCollision = true,
            },
            0U);
    const auto inactiveActor =
        queryPublishedLegacyProjectilePlayerActor(
            *built.runtime, 91U);
    const auto inactiveTrace =
        resolvePublishedLegacyProjectileCollisionLoop(
            rooms, *built.runtime, input, true);
    require(
        inactivePublication.published() &&
            inactiveActor.status ==
                LegacyProjectileLiveActorQueryStatus::resolved &&
            !inactiveActor.actorActive &&
            inactiveTrace.completed() &&
            inactiveTrace.decision->outcome ==
                LegacyProjectileCollisionOutcome::advanceNoHit,
        "inactive published player state diverged from geometry");
}

void testPublishedRuntimePortalLoopAndNoAllocations() {
    auto built = buildPortalRuntime();
    require(built.complete(), "portal runtime creation failed");
    const auto published =
        built.runtime->tryPublishDynamicCollisionFrame(
            {},
            {
                .objectId = 0U,
                .active = false,
                .projectileActorCollisionsEnabled = false,
                .actorAcceptsProjectileCollision = false,
            },
            std::numeric_limits<std::size_t>::max());
    require(
        published.published() &&
            built.runtime->worldRoomCount() == 2U &&
            queryPublishedLegacyProjectilePlayerActor(
                *built.runtime, 91U)
                    .status ==
                LegacyProjectileLiveActorQueryStatus::notFound,
        "portal runtime publication failed");

    const auto rooms = catalog(2U);
    const LegacyProjectileCollisionQueryInput input{
        .segmentStart = {0.0F, 0.0F, 0.0F},
        .segmentEnd = {0.0F, 0.0F, 4.0F},
        .roomId = 0,
    };
    const auto resolved =
        resolvePublishedLegacyProjectileCollisionLoop(
            rooms,
            *built.runtime,
            input,
            true);
    require(
        resolved.completed() &&
            resolved.queryCount == 2U &&
            resolved.portalTransitionCount == 1U &&
            resolved.decision->outcome ==
                LegacyProjectileCollisionOutcome::surfaceContact &&
            resolved.decision->position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 3.0F} &&
            resolved.decision->previousPosition ==
                LegacyMachineGunVector3{0.0F, 0.0F, 2.0F} &&
            resolved.decision->roomId == 1 &&
            resolved.decision->material ==
                std::optional<std::int32_t>{4},
        "published runtime portal loop result mismatch");

    const auto ammo = legacyMachineGunAmmoProfile(0U);
    require(ammo.has_value(), "portal transaction profile missing");
    const LegacyMachineGunProjectileState projectile{
        .position = input.segmentEnd,
        .velocity = {0.0F, 0.0F, 20.0F},
        .ageSeconds = 1.0F,
        .roomId = input.roomId,
        .creatorUid = 7U,
        .targetUid = 0U,
        .active = true,
        .waterContacted = false,
    };
    const auto transaction =
        resolvePublishedLegacyMachineGunProjectileCollision(
            rooms,
            *built.runtime,
            projectile,
            *ammo,
            input,
            true);
    require(
        transaction.committed() &&
            transaction.collision.queryCount == 2U &&
            transaction.collision.portalTransitionCount == 1U &&
            transaction.commit->outcome ==
                LegacyProjectileCollisionOutcome::surfaceContact &&
            !transaction.commit->state.active &&
            transaction.commit->state.roomId == 1 &&
            transaction.commit->state.position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 3.0F} &&
            transaction.commit->surface.has_value() &&
            transaction.commit->surface->ricochet.has_value() &&
            transaction.commit->surface->ricochet->material == 4 &&
            transaction.commit->surface->ricochet->roomId == 1,
        "published portal/surface terminal transaction mismatch");

    bool complete = true;
    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t index = 0U; index < 4'096U; ++index) {
        const auto repeated =
            resolvePublishedLegacyMachineGunProjectileCollision(
                rooms,
                *built.runtime,
                projectile,
                *ammo,
                input,
                true);
        complete = complete && repeated.committed() &&
            repeated.collision.queryCount == 2U &&
            repeated.collision.portalTransitionCount == 1U &&
            repeated.commit->surface.has_value() &&
            !repeated.commit->state.active;
    }
    countAllocations.store(false, std::memory_order_relaxed);
    require(complete, "steady-state runtime projectile query failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "steady-state runtime projectile query allocated");

    const auto mismatchedRooms = catalog(1U);
    const auto mismatch =
        resolvePublishedLegacyProjectileCollisionLoop(
            mismatchedRooms,
            *built.runtime,
            input,
            true);
    require(
        mismatch.status ==
                LegacyProjectileCollisionLoopStatus::invalidInput &&
            mismatch.queryCount == 0U,
        "catalog/runtime room mismatch reached the trace");
}

} // namespace

void* operator new(const std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* const memory) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory) noexcept {
    std::free(memory);
}

void operator delete(
    void* const memory,
    const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](
    void* const memory,
    const std::size_t) noexcept {
    std::free(memory);
}

int main() {
    static_assert(noexcept(
        legacyProjectileQueryResultFromRuntimeTrace(
            std::declval<const MissionWorldRoomCatalog&>(),
            std::declval<
                const MissionWorldRuntimeCombinedLineTraceResult&>(),
            true,
            nullptr,
            nullptr)));
    static_assert(noexcept(
        resolvePublishedLegacyProjectileCollisionLoop(
            std::declval<const MissionWorldRoomCatalog&>(),
            std::declval<
                const LegacyGameplayCameraMissionRuntime&>(),
            std::declval<
                const LegacyProjectileCollisionQueryInput&>(),
            true,
            nullptr,
            nullptr)));
    static_assert(noexcept(
        queryPublishedLegacyProjectilePlayerActor(
            std::declval<
                const LegacyGameplayCameraMissionRuntime&>(),
            1U)));
    static_assert(noexcept(
        resolvePublishedLegacyProjectileCollisionLoop(
            std::declval<const MissionWorldRoomCatalog&>(),
            std::declval<
                const LegacyGameplayCameraMissionRuntime&>(),
            std::declval<
                const LegacyProjectileCollisionQueryInput&>(),
            true)));
    static_assert(noexcept(
        resolvePublishedLegacyMachineGunProjectileCollision(
            std::declval<const MissionWorldRoomCatalog&>(),
            std::declval<
                const LegacyGameplayCameraMissionRuntime&>(),
            std::declval<
                const LegacyMachineGunProjectileState&>(),
            std::declval<
                const LegacyMachineGunAmmoProfile&>(),
            std::declval<
                const LegacyProjectileCollisionQueryInput&>(),
            true)));

    testNoHitAndStaticMapping();
    testMaterialAndClientActorOrder();
    testServerActorResolutionStates();
    testPortalAndSignedMaterialMapping();
    testMalformedRuntimeResultsFailClosed();
    testPublishedPlayerActorResolverAndNoAllocations();
    testPublishedRuntimePortalLoopAndNoAllocations();

    std::cout
        << "Legacy projectile runtime-query adapter tests passed\n";
    return EXIT_SUCCESS;
}
