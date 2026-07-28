#include "airfix/render/LegacyGameplayCameraRoomState.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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

namespace {

using namespace airfix::assets;
using namespace airfix::render;

static_assert(noexcept(proposeLegacyGameplayCameraRoomState(
    std::declval<const MissionWorldSpatialArena&>(),
    std::declval<const BasisTransform&>(),
    std::declval<const LegacyGameplayCameraRoomState&>(),
    std::declval<const Vec3&>())));
static_assert(noexcept(
    proposeLegacyGameplayCameraStaticCollisionState(
        std::declval<const MissionWorldSpatialArena&>(),
        std::declval<const BasisTransform&>(),
        std::declval<
            const LegacyGameplayCameraStaticCollisionState&>(),
        std::declval<const Vec3&>(),
        std::declval<float>(),
        std::declval<
            std::span<MissionWorldRuntimeSphereCandidate>>(),
        std::declval<std::span<Vec3>>())));
static_assert(std::is_nothrow_copy_constructible_v<
              LegacyGameplayCameraRoomState>);
static_assert(std::is_nothrow_copy_assignable_v<
              LegacyGameplayCameraRoomUpdateResult>);
static_assert(std::is_nothrow_copy_assignable_v<
              LegacyGameplayCameraStaticCollisionResult>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 1.0e-6F) noexcept {
    return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] MissionWorldSpatialPolygon portalTriangle(
    const float z,
    const std::size_t target,
    const bool enabled = true) {
    return {
        .faceCross = {0.0F, 0.0F, 1.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {-2.0F, -2.0F, z},
        .edge01 = {4.0F, 0.0F, 0.0F},
        .edge12 = {-2.0F, 4.0F, 0.0F},
        .portalWorldRoomIndex = target,
        .portalMeshSelectionFlagB = enabled,
        .portalObjectVisible = enabled,
    };
}

[[nodiscard]] MissionWorldSpatialArena emptyArena(
    const std::size_t roomCount = 2U) {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(roomCount);
    return arena;
}

[[nodiscard]] MissionWorldSpatialArena onePortalArena(
    const float z = 0.0F,
    const std::size_t target = 1U,
    const bool enabled = true) {
    auto arena = emptyArena();
    arena.rooms[0].portalTreeCount = 1U;
    arena.treeReferences = {0U};
    arena.trees.push_back({
        .kind = CcfBspTreeKind::portalTree,
        .worldRoomIndex = 0U,
        .rootNodeIndex = 0U,
        .firstNodeIndex = 0U,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, z},
        .firstPolygonIndex = 0U,
        .polygonCount = 1U,
    });
    arena.polygons.push_back(
        portalTriangle(z, target, enabled));
    return arena;
}

void addPortalTree(
    MissionWorldSpatialArena& arena,
    const std::size_t ownerRoom,
    const float z,
    const std::size_t target,
    const bool enabled = true) {
    const auto treeIndex = arena.trees.size();
    const auto nodeIndex = arena.nodes.size();
    const auto polygonIndex = arena.polygons.size();
    const auto referenceIndex = arena.treeReferences.size();
    arena.rooms[ownerRoom].firstPortalTreeReference =
        referenceIndex;
    arena.rooms[ownerRoom].portalTreeCount = 1U;
    arena.treeReferences.push_back(treeIndex);
    arena.trees.push_back({
        .kind = CcfBspTreeKind::portalTree,
        .worldRoomIndex = ownerRoom,
        .rootNodeIndex = nodeIndex,
        .firstNodeIndex = nodeIndex,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, z},
        .firstPolygonIndex = polygonIndex,
        .polygonCount = 1U,
    });
    arena.polygons.push_back(
        portalTriangle(z, target, enabled));
}

void addStaticTree(
    MissionWorldSpatialArena& arena,
    const std::size_t ownerRoom,
    const float z) {
    const auto treeIndex = arena.trees.size();
    const auto nodeIndex = arena.nodes.size();
    const auto polygonIndex = arena.polygons.size();
    const auto referenceIndex = arena.treeReferences.size();
    arena.rooms[ownerRoom].firstStaticTreeReference =
        referenceIndex;
    arena.rooms[ownerRoom].staticTreeCount = 1U;
    arena.treeReferences.push_back(treeIndex);
    arena.trees.push_back({
        .kind = CcfBspTreeKind::staticTree,
        .worldRoomIndex = ownerRoom,
        .rootNodeIndex = nodeIndex,
        .firstNodeIndex = nodeIndex,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, z},
        .firstPolygonIndex = polygonIndex,
        .polygonCount = 1U,
    });
    arena.polygons.push_back({
        .faceCross = {0.0F, 0.0F, 16.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {-2.0F, -2.0F, z},
        .edge01 = {4.0F, 0.0F, 0.0F},
        .edge12 = {-2.0F, 4.0F, 0.0F},
        .materialCollisionMode2152 = 7U,
    });
}

void testNoTransitionProposesCandidateInCurrentRoom() {
    const auto arena = emptyArena();
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {1.0F, 2.0F, 3.0F},
        .worldRoomIndex = 1U,
    };
    const Vec3 candidate{4.0F, 5.0F, 6.0F};
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena, {}, current, candidate);
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::noTransition &&
            result.valid() &&
            result.proposedState ==
                std::optional<LegacyGameplayCameraRoomState>{
                    LegacyGameplayCameraRoomState{
                        .runtimeWorldPosition = candidate,
                        .worldRoomIndex = 1U,
                    }} &&
            !result.diagnosticHit.has_value() &&
            result.transitionCount == 0U &&
            current.runtimeWorldPosition ==
                Vec3{1.0F, 2.0F, 3.0F},
        "no-transition update did not propose the complete candidate");
}

void testTransitionProposesCandidateInTargetRoom() {
    const auto arena = onePortalArena();
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const Vec3 candidate{0.0F, 0.0F, 1.0F};
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena, {}, current, candidate);
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::transition &&
            result.proposedState.has_value() &&
            result.proposedState->runtimeWorldPosition == candidate &&
            result.proposedState->worldRoomIndex == 1U &&
            result.diagnosticHit.has_value() &&
            result.transitionCount == 1U,
        "portal transition did not propose the target room");
}

void testTransitionUsesRuntimeBasisAndUnitScale() {
    const BasisTransform basis{
        .sourceToRuntime = Mat3{{
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, -1.0F},
        }},
        .runtimeUnitsPerSourceUnit = 4.0F,
    };
    const auto arena = onePortalArena();
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, 8.0F},
        .worldRoomIndex = 0U,
    };
    const Vec3 candidate{0.0F, 0.0F, -8.0F};
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena, basis, current, candidate);
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::transition &&
            result.proposedState ==
                std::optional<LegacyGameplayCameraRoomState>{
                    LegacyGameplayCameraRoomState{
                        .runtimeWorldPosition = candidate,
                        .worldRoomIndex = 1U,
                    }} &&
            result.diagnosticHit.has_value() &&
            result.diagnosticHit->runtimePoint == Vec3{} &&
            result.diagnosticHit->runtimePlaneNormal ==
                Vec3{0.0F, 0.0F, -1.0F},
        "room-state update did not trace through the mission basis");
}

void testBlockedPortalStillAcceptsCandidateInCurrentRoom() {
    const auto arena = onePortalArena(0.0F, 1U, false);
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const Vec3 candidate{0.0F, 0.0F, 1.0F};
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena, {}, current, candidate);
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::noTransition &&
            result.proposedState ==
                std::optional<LegacyGameplayCameraRoomState>{
                    LegacyGameplayCameraRoomState{
                        .runtimeWorldPosition = candidate,
                        .worldRoomIndex = 0U,
                    }} &&
            result.diagnosticHit.has_value() &&
            result.transitionCount == 0U,
        "blocked portal did not keep the room while accepting motion");
}

void testNetCycleReturnsToCurrentRoom() {
    auto arena = emptyArena();
    addPortalTree(arena, 0U, 0.0F, 1U);
    addPortalTree(arena, 1U, 0.5F, 0U);
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const Vec3 candidate{0.0F, 0.0F, 1.0F};
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena, {}, current, candidate);
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::noTransition &&
            result.proposedState.has_value() &&
            result.proposedState->worldRoomIndex == 0U &&
            result.proposedState->runtimeWorldPosition == candidate &&
            result.transitionCount == 2U &&
            result.diagnosticHit.has_value() &&
            result.diagnosticHit->ownerWorldRoomIndex == 1U,
        "net portal cycle did not preserve the original room");
}

void testBlockedLaterPortalKeepsLastCompletedRoom() {
    auto arena = emptyArena(3U);
    addPortalTree(arena, 0U, 0.0F, 1U);
    addPortalTree(arena, 1U, 0.5F, 2U, false);
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const Vec3 candidate{0.0F, 0.0F, 1.0F};
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena, {}, current, candidate);
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::transition &&
            result.proposedState ==
                std::optional<LegacyGameplayCameraRoomState>{
                    LegacyGameplayCameraRoomState{
                        .runtimeWorldPosition = candidate,
                        .worldRoomIndex = 1U,
                    }} &&
            result.diagnosticHit.has_value() &&
            result.diagnosticHit->ownerWorldRoomIndex == 1U &&
            result.diagnosticHit->portalWorldRoomIndex ==
                std::optional<std::size_t>{2U} &&
            result.transitionCount == 1U,
        "blocked later portal replaced the last completed room");
}

void testFailuresNeverProposeState() {
    const auto arena = emptyArena();
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {},
        .worldRoomIndex = 0U,
    };
    const BasisTransform singular{
        .sourceToRuntime = Mat3{{
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 1.0F},
        }},
    };
    const auto invalidBasis = proposeLegacyGameplayCameraRoomState(
        arena, singular, current, {});
    const auto invalidRoom = proposeLegacyGameplayCameraRoomState(
        arena,
        {},
        LegacyGameplayCameraRoomState{
            .runtimeWorldPosition = {},
            .worldRoomIndex = 2U,
        },
        {});
    const auto invalidInput = proposeLegacyGameplayCameraRoomState(
        arena,
        {},
        current,
        {std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    const auto invalidLimit = proposeLegacyGameplayCameraRoomState(
        arena,
        {},
        current,
        {},
        {
            .maximumPortalTransitions =
                kMissionWorldSpatialMaximumPortalTransitions + 1U,
        });
    require(
        invalidBasis.status ==
                LegacyGameplayCameraRoomUpdateStatus::invalidBasis &&
            invalidRoom.status ==
                LegacyGameplayCameraRoomUpdateStatus::invalidWorldRoom &&
            invalidInput.status ==
                LegacyGameplayCameraRoomUpdateStatus::invalidInput &&
            invalidLimit.status ==
                LegacyGameplayCameraRoomUpdateStatus::invalidInput &&
            !invalidBasis.proposedState.has_value() &&
            !invalidRoom.proposedState.has_value() &&
            !invalidInput.proposedState.has_value() &&
            !invalidLimit.proposedState.has_value(),
        "invalid room-state input produced a proposed state");
}

void testOutOfSegmentAndPartialChainRemainDiagnosticOnly() {
    auto oneHop = onePortalArena(1.0F);
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const Vec3 candidate{0.0F, 0.0F, 0.9999995F};
    const auto invalidFirst = proposeLegacyGameplayCameraRoomState(
        oneHop, {}, current, candidate);
    require(
        invalidFirst.status ==
                LegacyGameplayCameraRoomUpdateStatus::outOfSegmentHit &&
            !invalidFirst.proposedState.has_value() &&
            invalidFirst.diagnosticHit.has_value() &&
            invalidFirst.transitionCount == 0U,
        "out-of-segment first hop produced a state");

    auto partial = emptyArena(3U);
    addPortalTree(partial, 0U, 0.0F, 1U);
    addPortalTree(partial, 1U, 1.0F, 2U);
    const auto invalidSecond = proposeLegacyGameplayCameraRoomState(
        partial, {}, current, candidate);
    require(
        invalidSecond.status ==
                LegacyGameplayCameraRoomUpdateStatus::outOfSegmentHit &&
            !invalidSecond.proposedState.has_value() &&
            invalidSecond.diagnosticHit.has_value() &&
            invalidSecond.diagnosticHit->ownerWorldRoomIndex == 1U &&
            invalidSecond.transitionCount == 1U,
        "partial portal chain published the intermediate room");
}

void testTransitionLimitFailureIsAtomic() {
    auto arena = onePortalArena(0.0F, 0U);
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena,
        {},
        current,
        {0.0F, 0.0F, 1.0F},
        {.maximumPortalTransitions = 2U});
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::
                    transitionLimitExceeded &&
            !result.proposedState.has_value() &&
            result.diagnosticHit.has_value() &&
            result.transitionCount == 2U,
        "bounded self-portal failure produced a state");
}

void testZeroTransitionLimitFailsBeforeRoomChange() {
    const auto arena = onePortalArena();
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const auto result = proposeLegacyGameplayCameraRoomState(
        arena,
        {},
        current,
        {0.0F, 0.0F, 1.0F},
        {.maximumPortalTransitions = 0U});
    require(
        result.status ==
                LegacyGameplayCameraRoomUpdateStatus::
                    transitionLimitExceeded &&
            !result.proposedState.has_value() &&
            result.diagnosticHit.has_value() &&
            result.transitionCount == 0U,
        "zero transition limit changed the room before failing");
}

void testStaticCollisionProposesCorrectedStateAndFactors() {
    auto arena = emptyArena(1U);
    addStaticTree(arena, 0U, 0.0F);
    const LegacyGameplayCameraStaticCollisionState current{
        .roomState = {
            .runtimeWorldPosition = {0.0F, 0.0F, -2.0F},
            .worldRoomIndex = 0U,
        },
        .axisFactors = {0.8F, 0.6F, 1.0F},
    };
    std::array<MissionWorldRuntimeSphereCandidate, 4U>
        candidates{};
    std::array<Vec3, 4U> planes{};

    const auto result =
        proposeLegacyGameplayCameraStaticCollisionState(
            arena,
            {},
            current,
            {0.0F, 0.0F, -0.5F},
            1.0F,
            candidates,
            planes);
    require(
        result.status ==
                LegacyGameplayCameraStaticCollisionStatus::noTransition &&
            result.valid() &&
            result.proposedState.has_value() &&
            result.sphereCollision.has_value() &&
            result.sphereCollision->status ==
                MissionWorldRuntimeSphereCollisionStatus::resolved &&
            result.roomUpdate.has_value() &&
            result.roomUpdate->status ==
                LegacyGameplayCameraRoomUpdateStatus::noTransition,
        "integrated static correction did not complete");
    require(
        close(
            result.proposedState->roomState.runtimeWorldPosition.z,
            -1.1F) &&
            result.proposedState->roomState.worldRoomIndex == 0U &&
            close(result.proposedState->axisFactors.x, 0.8F) &&
            close(result.proposedState->axisFactors.y, 0.6F) &&
            result.proposedState->axisFactors.z == 0.0F &&
            current.roomState.runtimeWorldPosition ==
                Vec3{0.0F, 0.0F, -2.0F} &&
            current.axisFactors == Vec3{0.8F, 0.6F, 1.0F},
        "corrected position, factors, or input atomicity changed");
}

void testCorrectedPositionControlsPortalTransition() {
    auto blockedByCorrection = emptyArena();
    addPortalTree(blockedByCorrection, 0U, 0.0F, 1U);
    addStaticTree(blockedByCorrection, 0U, 0.5F);
    const LegacyGameplayCameraStaticCollisionState current{
        .roomState = {
            .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
            .worldRoomIndex = 0U,
        },
        .axisFactors = {1.0F, 1.0F, 1.0F},
    };
    std::array<MissionWorldRuntimeSphereCandidate, 4U>
        candidates{};
    std::array<Vec3, 4U> planes{};

    const auto noTransition =
        proposeLegacyGameplayCameraStaticCollisionState(
            blockedByCorrection,
            {},
            current,
            {0.0F, 0.0F, 0.1F},
            0.5F,
            candidates,
            planes);
    require(
        noTransition.status ==
                LegacyGameplayCameraStaticCollisionStatus::noTransition &&
            noTransition.proposedState.has_value() &&
            noTransition.proposedState->roomState.worldRoomIndex == 0U &&
            close(
                noTransition.proposedState->roomState
                    .runtimeWorldPosition.z,
                -0.05F,
                2.0e-6F) &&
            close(
                noTransition.proposedState->axisFactors.z,
                0.7F,
                2.0e-6F) &&
            noTransition.roomUpdate.has_value() &&
            !noTransition.roomUpdate->diagnosticHit.has_value(),
        "portal trace used the uncorrected candidate");

    auto transitionAfterCorrection = emptyArena();
    addPortalTree(transitionAfterCorrection, 0U, 0.0F, 1U);
    addStaticTree(transitionAfterCorrection, 0U, 1.0F);
    const auto transition =
        proposeLegacyGameplayCameraStaticCollisionState(
            transitionAfterCorrection,
            {},
            current,
            {0.0F, 0.0F, 0.75F},
            0.5F,
            candidates,
            planes);
    require(
        transition.status ==
                LegacyGameplayCameraStaticCollisionStatus::transition &&
            transition.proposedState.has_value() &&
            transition.proposedState->roomState.worldRoomIndex == 1U &&
            close(
                transition.proposedState->roomState
                    .runtimeWorldPosition.z,
                0.45F,
                2.0e-6F) &&
            close(
                transition.proposedState->axisFactors.z,
                0.4F,
                2.0e-6F) &&
            transition.roomUpdate.has_value() &&
            transition.roomUpdate->transitionCount == 1U,
        "corrected endpoint did not drive the final room transition");
}

void testFactorReductionRemovesRuntimeUnitScale() {
    auto arena = emptyArena(1U);
    addStaticTree(arena, 0U, 0.0F);
    const BasisTransform basis{
        .runtimeUnitsPerSourceUnit = 2.0F,
    };
    const LegacyGameplayCameraStaticCollisionState current{
        .roomState = {
            .runtimeWorldPosition = {0.0F, 0.0F, -4.0F},
            .worldRoomIndex = 0U,
        },
        .axisFactors = {1.0F, 1.0F, 2.0F},
    };
    std::array<MissionWorldRuntimeSphereCandidate, 4U>
        candidates{};
    std::array<Vec3, 4U> planes{};

    const auto result =
        proposeLegacyGameplayCameraStaticCollisionState(
            arena,
            basis,
            current,
            {0.0F, 0.0F, -1.0F},
            2.0F,
            candidates,
            planes);
    require(
        result.valid() &&
            result.proposedState.has_value() &&
            close(
                result.proposedState->roomState
                    .runtimeWorldPosition.z,
                -2.2F,
                3.0e-6F) &&
            close(
                result.proposedState->axisFactors.z,
                0.8F,
                3.0e-6F),
        "runtime unit scale leaked into legacy factor reduction");
}

void testStaticCollisionFailuresNeverProposeState() {
    auto colliding = emptyArena(1U);
    addStaticTree(colliding, 0U, 0.0F);
    const LegacyGameplayCameraStaticCollisionState current{
        .roomState = {
            .runtimeWorldPosition = {0.0F, 0.0F, -2.0F},
            .worldRoomIndex = 0U,
        },
        .axisFactors = {1.0F, 1.0F, 1.0F},
    };
    std::array<MissionWorldRuntimeSphereCandidate, 1U>
        candidates{};
    std::array<Vec3, 1U> planes{};
    const auto candidateOverflow =
        proposeLegacyGameplayCameraStaticCollisionState(
            colliding,
            {},
            current,
            {0.0F, 0.0F, -0.5F},
            1.0F,
            {},
            planes);
    const auto constraintOverflow =
        proposeLegacyGameplayCameraStaticCollisionState(
            colliding,
            {},
            current,
            {0.0F, 0.0F, -0.5F},
            1.0F,
            candidates,
            {});

    auto selfPortal = onePortalArena(0.0F, 0U);
    const auto portalFailure =
        proposeLegacyGameplayCameraStaticCollisionState(
            selfPortal,
            {},
            current,
            {0.0F, 0.0F, 1.0F},
            0.5F,
            candidates,
            planes,
            {.maximumPortalTransitions = 0U});
    auto invalidFactors = current;
    invalidFactors.axisFactors.x =
        std::numeric_limits<float>::infinity();
    const auto factorFailure =
        proposeLegacyGameplayCameraStaticCollisionState(
            emptyArena(1U),
            {},
            invalidFactors,
            {},
            0.5F,
            candidates,
            planes);
    BasisTransform anisotropic;
    anisotropic.sourceToRuntime.columns[0].x = 2.0F;
    const auto basisFailure =
        proposeLegacyGameplayCameraStaticCollisionState(
            colliding,
            anisotropic,
            current,
            {0.0F, 0.0F, -0.5F},
            1.0F,
            candidates,
            planes);
    const auto nearFailure =
        proposeLegacyGameplayCameraStaticCollisionState(
            emptyArena(1U),
            {},
            current,
            {},
            std::numeric_limits<float>::infinity(),
            candidates,
            planes);

    require(
        candidateOverflow.status ==
                LegacyGameplayCameraStaticCollisionStatus::
                    candidateCapacityExceeded &&
            constraintOverflow.status ==
                LegacyGameplayCameraStaticCollisionStatus::
                    constraintCapacityExceeded &&
            portalFailure.status ==
                LegacyGameplayCameraStaticCollisionStatus::
                    transitionLimitExceeded &&
            factorFailure.status ==
                LegacyGameplayCameraStaticCollisionStatus::invalidInput &&
            basisFailure.status ==
                LegacyGameplayCameraStaticCollisionStatus::invalidBasis &&
            nearFailure.status ==
                LegacyGameplayCameraStaticCollisionStatus::invalidInput,
        "integrated failure status mapping changed: candidate=" +
            std::to_string(static_cast<int>(candidateOverflow.status)) +
            " constraint=" +
            std::to_string(static_cast<int>(constraintOverflow.status)) +
            " portal=" +
            std::to_string(static_cast<int>(portalFailure.status)) +
            " factor=" +
            std::to_string(static_cast<int>(factorFailure.status)) +
            " basis=" +
            std::to_string(static_cast<int>(basisFailure.status)) +
            " near=" +
            std::to_string(static_cast<int>(nearFailure.status)));
    require(
        !candidateOverflow.proposedState.has_value() &&
            candidateOverflow.sphereCollision.has_value() &&
            !candidateOverflow.roomUpdate.has_value() &&
            !constraintOverflow.proposedState.has_value() &&
            constraintOverflow.sphereCollision.has_value() &&
            !constraintOverflow.roomUpdate.has_value() &&
            !portalFailure.proposedState.has_value() &&
            portalFailure.sphereCollision.has_value() &&
            portalFailure.roomUpdate.has_value() &&
            !factorFailure.proposedState.has_value() &&
            factorFailure.sphereCollision.has_value() &&
            !factorFailure.roomUpdate.has_value() &&
            !basisFailure.proposedState.has_value() &&
            basisFailure.sphereCollision.has_value() &&
            !basisFailure.roomUpdate.has_value() &&
            !nearFailure.proposedState.has_value() &&
            !nearFailure.sphereCollision.has_value() &&
            !nearFailure.roomUpdate.has_value(),
        "failed integrated update exposed a partial proposal");
}

void testStaticCollisionProposalDoesNotAllocate() {
    auto arena = emptyArena();
    addPortalTree(arena, 0U, 0.0F, 1U);
    addStaticTree(arena, 0U, 0.5F);
    const LegacyGameplayCameraStaticCollisionState current{
        .roomState = {
            .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
            .worldRoomIndex = 0U,
        },
        .axisFactors = {1.0F, 1.0F, 1.0F},
    };
    std::array<MissionWorldRuntimeSphereCandidate, 4U>
        candidates{};
    std::array<Vec3, 4U> planes{};

    LegacyGameplayCameraStaticCollisionResult result;
    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0U; iteration < 4'096U;
         ++iteration) {
        result =
            proposeLegacyGameplayCameraStaticCollisionState(
                arena,
                {},
                current,
                {0.0F, 0.0F, 0.1F},
                0.5F,
                candidates,
                planes);
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(result.valid(), "integrated allocation probe failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "integrated static collision proposal allocated");
}

void testHotPathDoesNotAllocate() {
    const auto arena = onePortalArena();
    const LegacyGameplayCameraRoomState current{
        .runtimeWorldPosition = {0.0F, 0.0F, -1.0F},
        .worldRoomIndex = 0U,
    };
    const Vec3 candidate{0.0F, 0.0F, 1.0F};

    LegacyGameplayCameraRoomUpdateResult result;
    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0U; iteration < 4'096U;
         ++iteration) {
        result = proposeLegacyGameplayCameraRoomState(
            arena, {}, current, candidate);
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(result.valid(), "allocation probe failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "room-state proposal allocated on the hot path");
}

} // namespace

int main() {
    try {
        testNoTransitionProposesCandidateInCurrentRoom();
        testTransitionProposesCandidateInTargetRoom();
        testTransitionUsesRuntimeBasisAndUnitScale();
        testBlockedPortalStillAcceptsCandidateInCurrentRoom();
        testNetCycleReturnsToCurrentRoom();
        testBlockedLaterPortalKeepsLastCompletedRoom();
        testFailuresNeverProposeState();
        testOutOfSegmentAndPartialChainRemainDiagnosticOnly();
        testTransitionLimitFailureIsAtomic();
        testZeroTransitionLimitFailsBeforeRoomChange();
        testStaticCollisionProposesCorrectedStateAndFactors();
        testCorrectedPositionControlsPortalTransition();
        testFactorReductionRemovesRuntimeUnitScale();
        testStaticCollisionFailuresNeverProposeState();
        testStaticCollisionProposalDoesNotAllocate();
        testHotPathDoesNotAllocate();
        std::cout << "LegacyGameplayCameraRoomState tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "LegacyGameplayCameraRoomState tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
