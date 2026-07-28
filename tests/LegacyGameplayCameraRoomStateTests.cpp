#include "airfix/render/LegacyGameplayCameraRoomState.hpp"

#include <atomic>
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
static_assert(std::is_nothrow_copy_constructible_v<
              LegacyGameplayCameraRoomState>);
static_assert(std::is_nothrow_copy_assignable_v<
              LegacyGameplayCameraRoomUpdateResult>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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
