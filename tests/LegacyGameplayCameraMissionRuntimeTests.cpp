#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

std::atomic<std::size_t> allocationCount{0U};
std::atomic<bool> countAllocations{false};

[[nodiscard]] void* allocate(const std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const memory = std::malloc(size == 0U ? 1U : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

} // namespace

void* operator new(const std::size_t size) {
    return allocate(size);
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

void operator delete(void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

namespace {

using namespace airfix::assets;
using namespace airfix::render;

static_assert(
    !std::is_copy_constructible_v<LegacyGameplayCameraMissionRuntime>);
static_assert(
    !std::is_move_constructible_v<LegacyGameplayCameraMissionRuntime>);
static_assert(noexcept(
    std::declval<LegacyGameplayCameraMissionRuntime&>().tryAdvance(
        std::declval<
            const LegacyGameplayCameraStepCoordinatorInput&>())));
static_assert(noexcept(
    std::declval<LegacyGameplayCameraMissionRuntime&>().tryAcquire()));
static_assert(noexcept(
    std::declval<LegacyGameplayCameraMissionRuntime&>()
        .tryPublishDynamicCollisionFrame(
            std::declval<const ConvertedNodeTransform&>(),
            0U,
            false,
            0U)));
static_assert(noexcept(
    std::declval<const LegacyGameplayCameraMissionRuntime&>()
        .tracePublishedDynamicCollisionPortalLine(
            0U,
            std::declval<const Vec3&>(),
            std::declval<const Vec3&>())));

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] MissionWorldSpatialArena arena(
    const std::size_t polygonCount = 0U) {
    MissionWorldSpatialArena result;
    result.rooms.resize(1U);
    result.polygons.resize(polygonCount);
    return result;
}

[[nodiscard]] ConvertedMeshGeometry triangleGeometry(
    const std::uint32_t reference,
    const std::uint32_t materialReference) {
    ConvertedMeshGeometry geometry;
    geometry.reference = reference;
    geometry.vertices = {
        {{-2.0F, -2.0F, 0.0F}},
        {{2.0F, -2.0F, 0.0F}},
        {{0.0F, 2.0F, 0.0F}},
    };
    geometry.triangles = {
        {
            .vertexIndices = {0U, 1U, 2U},
            .materialReference = materialReference,
        },
    };
    return geometry;
}

[[nodiscard]] MissionPlacedDynamicBspAssembly placedCollision() {
    MissionPlacedDynamicBspAssembly placed;
    placed.meshes.push_back(
        buildLegacyDynamicBsp(triangleGeometry(10U, 100U)));
    require(placed.meshes[0].complete(), "placed mesh fixture failed");
    placed.meshProvenance.push_back({
        .sourceIndex = 0U,
        .physicalMeshIndex = 1U,
        .firstPlacedNodeIndex = 2U,
        .sourceMeshReference = 10U,
    });
    placed.objects.push_back({
        .meshIndex = 0U,
        .actorObjectId = 0U,
        .active = true,
        .objectLocalToRuntime = {},
        .runtimeTranslation = {0.0F, 0.0F, 2.0F},
        .portalType = -1,
        .portalWorldRoomIndex = std::nullopt,
        .portalObjectVisible = false,
    });
    placed.objectProvenance.push_back({
        .sourceIndex = 0U,
        .placedNodeIndex = 2U,
        .physicalMeshIndex = 1U,
        .worldRoomIndex = 0U,
        .sourceNodeReference = 20U,
    });
    placed.roomObjectRanges.push_back({
        .firstObjectIndex = 0U,
        .objectCount = 1U,
    });
    placed.retainedPayloadBytes =
        sizeof(LegacyDynamicBspMesh) +
        sizeof(MissionPlacedDynamicBspMeshProvenance) +
        sizeof(LegacyDynamicBspLineObject) +
        sizeof(MissionPlacedDynamicBspObjectProvenance) +
        sizeof(LegacyDynamicBspRoomObjectRange) +
        placed.meshes[0].retainedPayloadBytes;
    require(placed.complete(), "placed collision fixture failed");
    return placed;
}

[[nodiscard]] PlayerActorCollisionAssembly playerCollision() {
    PlayerActorCollisionAssembly player;
    player.meshes.push_back(
        buildLegacyDynamicBsp(triangleGeometry(30U, 300U)));
    require(player.meshes[0].complete(), "player mesh fixture failed");
    const PlayerActorVisualProvenance actor{
        .legacySkinSlot = 0U,
        .blueprintIndex = 3U,
        .blueprintReference = 40U,
        .physicalMeshIndex = 4U,
    };
    player.meshProvenance.push_back({
        .actor = actor,
        .collisionMeshIndex = 0U,
        .sourceMeshReference = 30U,
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
    require(player.complete(), "player collision fixture failed");
    return player;
}

[[nodiscard]] LegacyGameplayCameraStepCoordinatorInitializeInput
initializeInput() {
    return {
        .vehicleWorldPosition = {0.0F, 0.0F, 0.0F},
        .vehicleWorldRotation = {},
        .worldRoomIndex = 0U,
        .cameraCyclePressCount = 0U,
    };
}

[[nodiscard]] LegacyGameplayCameraStepCoordinatorInput input(
    const std::uint64_t simulationStep,
    const std::uint64_t cameraCyclePressCount = 0U) {
    return {
        .vehicleChaseWorldPosition = {0.0F, 0.0F, 0.0F},
        .vehicleWorldAnchor = {0.0F, 0.0F, 0.0F},
        .vehicleWorldRotation = {},
        .refreshDeltaSeconds =
            legacyAircraftNominalRefreshDeltaSeconds,
        .vehicleHealth = 1.0F,
        .vehicleInactive = false,
        .cameraCyclePressCount = cameraCyclePressCount,
        .rearViewHeld = false,
        .simulationStep = simulationStep,
    };
}

[[nodiscard]] LegacyGameplayCameraMissionRuntimeBuildResult
buildRuntime() {
    auto missionArena = arena();
    return LegacyGameplayCameraMissionRuntime::create(
        std::move(missionArena), {}, initializeInput());
}

[[nodiscard]] LegacyGameplayCameraMissionRuntimeBuildResult
buildDynamicRuntime(
    const LegacyGameplayCameraMissionRuntimeLimits& limits = {}) {
    auto missionArena = arena();
    auto placed = placedCollision();
    std::optional<PlayerActorCollisionAssembly> player{
        playerCollision()};
    return LegacyGameplayCameraMissionRuntime::create(
        std::move(missionArena),
        std::move(placed),
        std::move(player),
        {},
        initializeInput(),
        limits);
}

void requireBuildIssue(
    const LegacyGameplayCameraMissionRuntimeBuildResult& result,
    const LegacyGameplayCameraMissionRuntimeBuildIssueKind kind,
    const char* const message) {
    require(
        !result.complete() && result.runtime == nullptr &&
            result.additionalRetainedBytes == 0U &&
            result.issue.has_value() && result.issue->kind == kind,
        message);
}

void testCreationOwnsBootstrapArenaAndExactWorkspaces() {
    auto missionArena = arena(3U);
    const auto originalPolygonCount = missionArena.polygons.size();
    auto built = LegacyGameplayCameraMissionRuntime::create(
        std::move(missionArena), {}, initializeInput());
    require(built.complete(), "mission runtime creation failed");
    require(
        built.runtime->candidateCapacity() == originalPolygonCount &&
            built.runtime->constraintPlaneCapacity() ==
                originalPolygonCount &&
            !built.runtime->dynamicCollisionAvailable() &&
            !built.runtime->dynamicCollisionFramePublished() &&
            built.runtime->dynamicObjectCapacity() == 0U &&
            built.runtime->dynamicRoomRangeCapacity() == 0U &&
            built.runtime->additionalRetainedBytes() ==
                built.additionalRetainedBytes &&
            built.additionalRetainedBytes ==
                LegacyGameplayCameraPacketExchange::retainedBytes() +
                    originalPolygonCount *
                        sizeof(MissionWorldRuntimeSphereCandidate) +
                    originalPolygonCount * sizeof(Vec3),
        "mission runtime workspace admission diverged");

    auto initial = built.runtime->tryAcquire();
    require(
        initial.has_value() && initial->valid() &&
            initial->simulationStep() == 0U &&
            initial->cameraPublicationGeneration() == 1U &&
            built.runtime->currentSnapshot().has_value() &&
            built.runtime->currentSnapshot()->simulationStep == 0U &&
            built.runtime->currentMode() ==
                LegacyGameplayCameraMode::camera0,
        "mission runtime bootstrap was incoherent");
}

void testDynamicCollisionOwnershipBuffersAndTrace() {
    auto built = buildDynamicRuntime();
    require(built.complete(), "dynamic mission runtime creation failed");
    require(
        built.runtime->dynamicCollisionAvailable() &&
            !built.runtime->dynamicCollisionFramePublished() &&
            built.runtime->dynamicObjectCapacity() == 2U &&
            built.runtime->dynamicRoomRangeCapacity() == 1U &&
            built.additionalRetainedBytes ==
                LegacyGameplayCameraPacketExchange::retainedBytes() +
                    2U * sizeof(LegacyDynamicBspLineObject) +
                    sizeof(LegacyDynamicBspRoomObjectRange),
        "dynamic frame ownership or exact retained bytes diverged");
    require(
        !built.runtime->currentDynamicCollisionFrame().has_value() &&
            built.runtime
                    ->tracePublishedDynamicCollisionPortalLine(
                        0U,
                        {0.0F, 0.0F, 0.0F},
                        {0.0F, 0.0F, 4.0F})
                    .status ==
                MissionWorldRuntimeCombinedLineTraceStatus::invalidInput,
        "unpublished dynamic frame became observable");

    const ConvertedNodeTransform playerWorld{
        .linear = {},
        .translation = {0.0F, 0.0F, 2.0F},
        .rawScalar = 1.0F,
    };
    const auto published =
        built.runtime->tryPublishDynamicCollisionFrame(
            playerWorld, 123U, true, 0U);
    const auto frame =
        built.runtime->currentDynamicCollisionFrame();
    require(
        published.published() &&
            built.runtime->dynamicCollisionFramePublished() &&
            frame.has_value() &&
            frame->meshes.primary.size() == 1U &&
            frame->meshes.secondary.size() == 1U &&
            frame->objects.size() == 2U &&
            frame->objects[0].actorObjectId == 123U &&
            frame->objects[1].actorObjectId == 0U &&
            frame->roomObjectRanges.size() == 1U &&
            frame->roomObjectRanges[0] ==
                LegacyDynamicBspRoomObjectRange{0U, 2U},
        "owned dynamic collision frame was incomplete");

    const auto traced =
        built.runtime->tracePublishedDynamicCollisionPortalLine(
            0U,
            {0.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 4.0F});
    require(
        traced.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            traced.hit.has_value() &&
            traced.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{0U} &&
            traced.hit->dynamicMeshIndex ==
                std::optional<std::size_t>{1U} &&
            traced.hit->actorObjectId == 123U &&
            traced.hit->sourceMaterialReference ==
                std::optional<std::uint32_t>{300U},
        "runtime-owned player did not win native-order tie");

    auto invalidWorld = playerWorld;
    invalidWorld.translation.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto rejected =
        built.runtime->tryPublishDynamicCollisionFrame(
            invalidWorld, 999U, false, 0U);
    const auto retained =
        built.runtime->currentDynamicCollisionFrame();
    require(
        rejected.status ==
                MissionWorldDynamicCollisionPublicationStatus::
                    invalidTransform &&
            built.runtime->dynamicCollisionFramePublished() &&
            retained.has_value() &&
            retained->objects[0].actorObjectId == 123U &&
            retained->objects[0].active,
        "failed republish changed the last complete dynamic frame");
}

void testDynamicCollisionWithoutPlayerPublishesPlacedOnly() {
    auto missionArena = arena();
    auto placed = placedCollision();
    std::optional<PlayerActorCollisionAssembly> noPlayer;
    auto built = LegacyGameplayCameraMissionRuntime::create(
        std::move(missionArena),
        std::move(placed),
        std::move(noPlayer),
        {},
        initializeInput());
    require(
        built.complete() &&
            built.runtime->dynamicCollisionAvailable() &&
            built.runtime->dynamicObjectCapacity() == 1U &&
            built.runtime->dynamicRoomRangeCapacity() == 1U,
        "no-player dynamic runtime ownership failed");

    const auto published =
        built.runtime->tryPublishDynamicCollisionFrame(
            {},
            999U,
            true,
            std::numeric_limits<std::size_t>::max());
    const auto frame =
        built.runtime->currentDynamicCollisionFrame();
    require(
        published.published() && frame.has_value() &&
            frame->meshes.secondary.empty() &&
            frame->objects.size() == 1U &&
            frame->objects[0].actorObjectId == 0U &&
            frame->roomObjectRanges[0] ==
                LegacyDynamicBspRoomObjectRange{0U, 1U},
        "no-player runtime did not publish placed collision alone");
}

void testInvalidOwnershipInputsFailBeforePublication() {
    MissionWorldSpatialArena invalidArena;
    auto invalid = LegacyGameplayCameraMissionRuntime::create(
        std::move(invalidArena), {}, initializeInput());
    requireBuildIssue(
        invalid,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::incompleteArena,
        "incomplete arena was accepted");

    auto basisArena = arena();
    BasisTransform singularBasis;
    singularBasis.sourceToRuntime.columns[2] = {0.0F, 0.0F, 0.0F};
    auto invalidBasis = LegacyGameplayCameraMissionRuntime::create(
        std::move(basisArena), singularBasis, initializeInput());
    requireBuildIssue(
        invalidBasis,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::invalidBasis,
        "invalid basis was accepted");

    auto roomArena = arena();
    auto wrongRoom = initializeInput();
    wrongRoom.worldRoomIndex = 1U;
    auto invalidRoom = LegacyGameplayCameraMissionRuntime::create(
        std::move(roomArena), {}, wrongRoom);
    requireBuildIssue(
        invalidRoom,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            initialWorldRoomOutOfRange,
        "out-of-range initial room was accepted");

    {
        auto dynamicArena = arena();
        auto placed = placedCollision();
        placed.issues.push_back({});
        std::optional<PlayerActorCollisionAssembly> player{
            playerCollision()};
        auto invalidPlaced =
            LegacyGameplayCameraMissionRuntime::create(
                std::move(dynamicArena),
                std::move(placed),
                std::move(player),
                {},
                initializeInput());
        requireBuildIssue(
            invalidPlaced,
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                invalidPlacedCollision,
            "invalid placed collision was accepted");
    }

    {
        auto dynamicArena = arena();
        auto placed = placedCollision();
        placed.roomObjectRanges.push_back({
            .firstObjectIndex = 1U,
            .objectCount = 0U,
        });
        placed.retainedPayloadBytes +=
            sizeof(LegacyDynamicBspRoomObjectRange);
        require(
            placed.complete(),
            "room-count mismatch fixture is internally invalid");
        std::optional<PlayerActorCollisionAssembly> player{
            playerCollision()};
        auto wrongRoomCount =
            LegacyGameplayCameraMissionRuntime::create(
                std::move(dynamicArena),
                std::move(placed),
                std::move(player),
                {},
                initializeInput());
        requireBuildIssue(
            wrongRoomCount,
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                placedCollisionRoomCountMismatch,
            "placed collision room mismatch was accepted");
    }

    {
        auto dynamicArena = arena();
        auto placed = placedCollision();
        std::optional<PlayerActorCollisionAssembly> player{
            playerCollision()};
        player->issues.push_back({});
        auto invalidPlayer =
            LegacyGameplayCameraMissionRuntime::create(
                std::move(dynamicArena),
                std::move(placed),
                std::move(player),
                {},
                initializeInput());
        requireBuildIssue(
            invalidPlayer,
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                invalidPlayerCollision,
            "invalid player collision was accepted");
    }
}

void testWorkspaceAndRetainedLimitsFailClosed() {
    LegacyGameplayCameraMissionRuntimeLimits candidateLimits;
    candidateLimits.maximumCandidateRecords = 1U;
    auto candidateArena = arena(2U);
    auto candidates = LegacyGameplayCameraMissionRuntime::create(
        std::move(candidateArena),
        {},
        initializeInput(),
        candidateLimits);
    requireBuildIssue(
        candidates,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            candidateRecordLimitExceeded,
        "candidate limit was ignored");

    LegacyGameplayCameraMissionRuntimeLimits constraintLimits;
    constraintLimits.maximumConstraintPlanes = 1U;
    auto constraintArena = arena(2U);
    auto constraints = LegacyGameplayCameraMissionRuntime::create(
        std::move(constraintArena),
        {},
        initializeInput(),
        constraintLimits);
    requireBuildIssue(
        constraints,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            constraintPlaneLimitExceeded,
        "constraint limit was ignored");

    LegacyGameplayCameraMissionRuntimeLimits byteLimits;
    byteLimits.maximumAdditionalRetainedBytes =
        LegacyGameplayCameraPacketExchange::retainedBytes() - 1U;
    auto byteArena = arena();
    auto bytes = LegacyGameplayCameraMissionRuntime::create(
        std::move(byteArena), {}, initializeInput(), byteLimits);
    requireBuildIssue(
        bytes,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            retainedByteLimitExceeded,
        "retained-byte limit was ignored");

    LegacyGameplayCameraMissionRuntimeLimits objectLimits;
    objectLimits.maximumDynamicObjects = 1U;
    auto objects = buildDynamicRuntime(objectLimits);
    requireBuildIssue(
        objects,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            dynamicObjectLimitExceeded,
        "dynamic-object limit was ignored");

    LegacyGameplayCameraMissionRuntimeLimits roomLimits;
    roomLimits.maximumDynamicRoomRanges = 0U;
    auto rooms = buildDynamicRuntime(roomLimits);
    requireBuildIssue(
        rooms,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            dynamicRoomRangeLimitExceeded,
        "dynamic-room limit was ignored");

    LegacyGameplayCameraMissionRuntimeLimits dynamicByteLimits;
    dynamicByteLimits.maximumAdditionalRetainedBytes =
        LegacyGameplayCameraPacketExchange::retainedBytes() +
        2U * sizeof(LegacyDynamicBspLineObject) +
        sizeof(LegacyDynamicBspRoomObjectRange) - 1U;
    auto dynamicBytes = buildDynamicRuntime(dynamicByteLimits);
    requireBuildIssue(
        dynamicBytes,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            retainedByteLimitExceeded,
        "dynamic-frame retained-byte limit was ignored");
}

void testAdvancePublishesCompletePacketsAndInputMode() {
    auto built = buildRuntime();
    require(built.complete(), "advance fixture creation failed");

    const auto camera1 = built.runtime->tryAdvance(input(1U, 1U));
    require(
        camera1.advanced() && camera1.published() &&
            camera1.coordinator.complete() &&
            camera1.coordinator.committedMode ==
                LegacyGameplayCameraMode::camera1 &&
            camera1.transportResult ==
                LegacyGameplayCameraPacketPublishResult::published,
        "complete camera step was not published");

    auto lease = built.runtime->tryAcquire();
    require(
        lease.has_value() && lease->simulationStep() == 1U &&
            lease->cameraPublicationGeneration() == 2U &&
            lease->packet() != nullptr &&
            built.runtime->currentSnapshot() ==
                lease->packet()->pose().frame() &&
            built.runtime->currentMode() ==
                LegacyGameplayCameraMode::camera1,
        "published runtime packet was torn");
}

void testBusyTransportDropsOnlyVisualPacket() {
    auto built = buildRuntime();
    require(built.complete(), "busy fixture creation failed");

    auto initialLease = built.runtime->tryAcquire();
    require(initialLease.has_value(), "initial retained lease missing");
    const auto first = built.runtime->tryAdvance(input(1U));
    require(first.published(), "first alternate-slot publication failed");
    auto frontLease = built.runtime->tryAcquire();
    require(frontLease.has_value(), "front retained lease missing");

    const auto busy = built.runtime->tryAdvance(input(2U));
    require(
        busy.advanced() && !busy.published() &&
            busy.status ==
                LegacyGameplayCameraMissionRuntimeAdvanceStatus::
                    advancedPacketBusy &&
            busy.transportResult ==
                LegacyGameplayCameraPacketPublishResult::busy &&
            built.runtime->currentSnapshot()->simulationStep == 2U,
        "busy transport rolled back or rejected coordinator state");

    initialLease.reset();
    const auto later = built.runtime->tryAdvance(input(3U));
    require(
        later.published(),
        "later generation after a busy packet did not publish");
    auto latest = built.runtime->tryAcquire();
    require(
        latest.has_value() && latest->simulationStep() == 3U &&
            latest->cameraPublicationGeneration() == 4U &&
            frontLease->simulationStep() == 1U,
        "generation skip corrupted a retained packet");
}

void testCoordinatorFailurePreservesPublishedPacket() {
    auto built = buildRuntime();
    require(built.complete(), "failure fixture creation failed");
    auto invalid = input(1U);
    invalid.refreshDeltaSeconds =
        std::numeric_limits<float>::quiet_NaN();
    const auto rejected = built.runtime->tryAdvance(invalid);
    require(
        !rejected.advanced() &&
            rejected.status ==
                LegacyGameplayCameraMissionRuntimeAdvanceStatus::
                    coordinatorRejected &&
            rejected.coordinator.status ==
                LegacyGameplayCameraStepCoordinatorStatus::
                    axisFactorRecoveryFailed &&
            !rejected.transportResult.has_value() &&
            built.runtime->currentSnapshot()->simulationStep == 0U,
        "coordinator failure changed mission runtime state");

    auto latest = built.runtime->tryAcquire();
    require(
        latest.has_value() && latest->simulationStep() == 0U &&
            latest->cameraPublicationGeneration() == 1U,
        "coordinator failure changed the published packet");
}

void testAdvanceResultKeepsCoordinatorAndTransportOutcomesSeparate() {
    LegacyGameplayCameraMissionRuntimeAdvanceResult result;
    result.status =
        LegacyGameplayCameraMissionRuntimeAdvanceStatus::transportRejected;
    result.transportResult =
        LegacyGameplayCameraPacketPublishResult::
            exchangeGenerationExhausted;
    require(
        result.advanced() && !result.published(),
        "transport rejection hid an already committed coordinator state");
}

void testWeakEndpointAndLeaseRemainLifetimeSafe() {
    auto built = buildRuntime();
    require(built.complete(), "lifetime fixture creation failed");
    std::shared_ptr<LegacyGameplayCameraMissionRuntime> runtime(
        std::move(built.runtime));
    std::weak_ptr<LegacyGameplayCameraMissionRuntime> endpoint(runtime);
    auto lease = runtime->tryAcquire();
    require(lease.has_value(), "lifetime lease missing");

    runtime.reset();
    require(
        endpoint.expired() && lease->valid() &&
            lease->simulationStep() == 0U &&
            lease->packet() != nullptr,
        "weak endpoint or retained render lease outlived ownership unsafely");
}

void testSteadyStateDoesNotAllocate() {
    auto built = buildDynamicRuntime();
    require(built.complete(), "allocation fixture creation failed");
    const ConvertedNodeTransform playerWorld{
        .linear = {},
        .translation = {0.0F, 0.0F, 2.0F},
        .rawScalar = 1.0F,
    };

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    bool complete = true;
    for (std::uint64_t step = 1U; step <= 4'096U; ++step) {
        const auto advanced = built.runtime->tryAdvance(input(step));
        auto lease = built.runtime->tryAcquire();
        const auto collision =
            built.runtime->tryPublishDynamicCollisionFrame(
                playerWorld, 123U, true, 0U);
        const auto traced =
            built.runtime->tracePublishedDynamicCollisionPortalLine(
                0U,
                {0.0F, 0.0F, 0.0F},
                {0.0F, 0.0F, 4.0F});
        complete = complete && advanced.published() &&
            lease.has_value() && lease->simulationStep() == step &&
            lease->cameraPublicationGeneration() == step + 1U &&
            collision.published() &&
            traced.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            traced.hit.has_value() &&
            traced.hit->actorObjectId == 123U;
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(complete, "allocation probe runtime step failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "mission camera runtime allocated in the steady-state path");
}

} // namespace

int main() {
    try {
        testCreationOwnsBootstrapArenaAndExactWorkspaces();
        testDynamicCollisionOwnershipBuffersAndTrace();
        testDynamicCollisionWithoutPlayerPublishesPlacedOnly();
        testInvalidOwnershipInputsFailBeforePublication();
        testWorkspaceAndRetainedLimitsFailClosed();
        testAdvancePublishesCompletePacketsAndInputMode();
        testBusyTransportDropsOnlyVisualPacket();
        testCoordinatorFailurePreservesPublishedPacket();
        testAdvanceResultKeepsCoordinatorAndTransportOutcomesSeparate();
        testWeakEndpointAndLeaseRemainLifetimeSafe();
        testSteadyStateDoesNotAllocate();
        std::cout << "LegacyGameplayCameraMissionRuntime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        countAllocations.store(false, std::memory_order_relaxed);
        std::cerr << "LegacyGameplayCameraMissionRuntime tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
