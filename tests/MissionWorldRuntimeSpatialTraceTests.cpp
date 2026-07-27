#include "airfix/render/MissionWorldRuntimeSpatialTrace.hpp"

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
#include <vector>

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

using namespace airfix;
using namespace airfix::assets;
using namespace airfix::render;

static_assert(noexcept(traceMissionWorldRuntimeSpatialLine(
    std::declval<const MissionWorldSpatialArena&>(),
    std::declval<const BasisTransform&>(),
    std::declval<std::size_t>(),
    std::declval<CcfBspTreeKind>(),
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>())));
static_assert(noexcept(traceMissionWorldRuntimePortalTransition(
    std::declval<const MissionWorldSpatialArena&>(),
    std::declval<const BasisTransform&>(),
    std::declval<std::size_t>(),
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>())));
static_assert(std::is_nothrow_copy_constructible_v<
              MissionWorldRuntimeSpatialLineHit>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float left,
    const float right,
    const float tolerance = 1.0e-5F) noexcept {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool close(
    const Vec3& left,
    const Vec3& right,
    const float tolerance = 1.0e-5F) noexcept {
    return close(left.x, right.x, tolerance) &&
        close(left.y, right.y, tolerance) &&
        close(left.z, right.z, tolerance);
}

[[nodiscard]] MissionWorldSpatialPolygon triangleZ(
    const float z,
    const std::optional<std::size_t> portalTarget =
        std::nullopt) {
    return {
        .faceCross = {0.0F, 0.0F, 1.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {-2.0F, -2.0F, z},
        .edge01 = {4.0F, 0.0F, 0.0F},
        .edge12 = {-2.0F, 4.0F, 0.0F},
        .portalWorldRoomIndex = portalTarget,
        .portalMeshSelectionFlagB = portalTarget.has_value(),
        .portalObjectVisible = portalTarget.has_value(),
    };
}

[[nodiscard]] MissionWorldSpatialPolygon triangleX() {
    return {
        .faceCross = {1.0F, 0.0F, 0.0F},
        .faceNormal = {1.0F, 0.0F, 0.0F},
        .point0 = {0.0F, -2.0F, -2.0F},
        .edge01 = {0.0F, 4.0F, 0.0F},
        .edge12 = {0.0F, -2.0F, 4.0F},
    };
}

[[nodiscard]] MissionWorldSpatialArena oneTreeArena(
    const CcfBspTreeKind kind,
    MissionWorldSpatialPolygon polygon,
    const CcfVector3 splitNormal,
    const CcfVector3 pointOnPlane) {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(2U);
    arena.treeReferences = {0U};
    arena.trees.push_back({
        .kind = kind,
        .worldRoomIndex = 0U,
        .rootNodeIndex = 0U,
        .firstNodeIndex = 0U,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = splitNormal,
        .pointOnPlane = pointOnPlane,
        .firstPolygonIndex = 0U,
        .polygonCount = 1U,
    });
    arena.polygons.push_back(std::move(polygon));
    if (kind == CcfBspTreeKind::staticTree) {
        arena.rooms[0].staticTreeCount = 1U;
    }
    else {
        arena.rooms[0].portalTreeCount = 1U;
    }
    return arena;
}

void testIdentityBasisStaticHit() {
    const auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree,
        triangleZ(0.0F),
        {0.0F, 0.0F, 1.0F},
        {});
    const auto result = traceMissionWorldRuntimeSpatialLine(
        arena,
        {},
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F});
    require(
        result.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::hit &&
            result.hit.has_value() &&
            result.hit->legacyFraction == 0.5F &&
            result.hit->runtimePoint == Vec3{} &&
            result.hit->runtimePlaneNormal ==
                Vec3{0.0F, 0.0F, 1.0F} &&
            result.hit->withinRequestedSegment,
        "identity runtime trace did not preserve the source hit");
}

void testShearedScaledBasisAndCovectorNormal() {
    const Mat3 sourceToRuntime{{
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{1.0F, 1.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    const BasisTransform basis{
        .sourceToRuntime = sourceToRuntime,
        .runtimeUnitsPerSourceUnit = 3.0F,
    };
    const auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree,
        triangleX(),
        {1.0F, 0.0F, 0.0F},
        {});
    const auto result = traceMissionWorldRuntimeSpatialLine(
        arena,
        basis,
        0U,
        CcfBspTreeKind::staticTree,
        {-12.0F, 0.0F, 0.0F},
        {12.0F, 0.0F, 0.0F});
    require(
        result.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::hit &&
            result.hit.has_value() &&
            close(result.hit->runtimePoint, {}) &&
            close(
                result.hit->runtimePlaneNormal,
                {0.5F, -0.5F, 0.0F}),
        "sheared/scaled basis did not use the inverse-transpose normal");
}

void testReflectionPreservesPlaneOrientation() {
    const BasisTransform basis{
        .sourceToRuntime = Mat3{{
            Vec3{-1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{0.0F, 0.0F, 1.0F},
        }},
        .runtimeUnitsPerSourceUnit = 2.0F,
    };
    const auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree,
        triangleX(),
        {1.0F, 0.0F, 0.0F},
        {});
    const auto result = traceMissionWorldRuntimeSpatialLine(
        arena,
        basis,
        0U,
        CcfBspTreeKind::staticTree,
        {4.0F, 0.0F, 0.0F},
        {-4.0F, 0.0F, 0.0F});
    require(
        result.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::hit &&
            result.hit.has_value() &&
            result.hit->runtimePlaneNormal ==
                Vec3{-1.0F, 0.0F, 0.0F},
        "reflected basis did not reflect the plane normal");
}

void testInvalidBasisAndInputFailClosed() {
    const auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree,
        triangleZ(0.0F),
        {0.0F, 0.0F, 1.0F},
        {});
    const BasisTransform singular{
        .sourceToRuntime = Mat3{{
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 1.0F},
        }},
    };
    require(
        traceMissionWorldRuntimeSpatialLine(
            arena,
            singular,
            0U,
            CcfBspTreeKind::staticTree,
            {},
            {})
                .status ==
            MissionWorldRuntimeSpatialLineTraceStatus::invalidBasis,
        "singular basis was accepted");

    const auto nonFinite = traceMissionWorldRuntimeSpatialLine(
        arena,
        {},
        0U,
        CcfBspTreeKind::staticTree,
        {std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
        {});
    require(
        nonFinite.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::invalidInput &&
            !nonFinite.hit.has_value(),
        "non-finite runtime endpoint did not fail closed");
}

void testEpsilonOutsideFractionIsPreserved() {
    const auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree,
        triangleZ(1.0F),
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, 1.0F});
    const auto result = traceMissionWorldRuntimeSpatialLine(
        arena,
        {},
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 0.9999995F});
    require(
        result.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::
                    outOfSegmentHit &&
            result.hit.has_value() &&
            result.hit->legacyFraction > 1.0F &&
            !result.hit->withinRequestedSegment &&
            result.hit->runtimePoint.z >= 1.0F,
        "legacy epsilon endpoint hit was not rejected without clamping");
}

void testBothEpsilonDirectionsAndExactEndpoints() {
    const auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree,
        triangleZ(0.0F),
        {0.0F, 0.0F, 1.0F},
        {});
    const auto below = traceMissionWorldRuntimeSpatialLine(
        arena,
        {},
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, 0.0000005F},
        {0.0F, 0.0F, 0.00000075F});
    require(
        below.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::
                    outOfSegmentHit &&
            below.hit.has_value() &&
            below.hit->legacyFraction < 0.0F &&
            !below.hit->withinRequestedSegment,
        "negative epsilon fraction was accepted as a segment hit");

    const auto atStart = traceMissionWorldRuntimeSpatialLine(
        arena,
        {},
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F});
    const auto atEnd = traceMissionWorldRuntimeSpatialLine(
        arena,
        {},
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 0.0F});
    require(
        atStart.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::hit &&
            atStart.hit.has_value() &&
            atStart.hit->legacyFraction == 0.0F &&
            atStart.hit->withinRequestedSegment &&
            atEnd.status ==
                MissionWorldRuntimeSpatialLineTraceStatus::hit &&
            atEnd.hit.has_value() &&
            atEnd.hit->legacyFraction == 1.0F &&
            atEnd.hit->withinRequestedSegment,
        "exact segment endpoints were rejected");
}

void testPortalRejectsOutOfSegmentHitBeforeRoomChange() {
    const auto arena = oneTreeArena(
        CcfBspTreeKind::portalTree,
        triangleZ(1.0F, 1U),
        {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, 1.0F});
    const auto result =
        traceMissionWorldRuntimePortalTransition(
            arena,
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 0.9999995F});
    require(
        result.status ==
                MissionWorldRuntimePortalTraceStatus::
                    outOfSegmentHit &&
            result.hit.has_value() &&
            !result.hit->withinRequestedSegment &&
            !result.targetWorldRoomIndex.has_value() &&
            result.transitionCount == 0U,
        "out-of-segment portal hit partially changed the room");
}

void testPortalRejectsLaterInvalidHopWithoutPublishingIntermediateRoom() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::portalTree,
        triangleZ(0.0F, 1U),
        {0.0F, 0.0F, 1.0F},
        {});
    arena.rooms.resize(3U);
    arena.rooms[1].firstPortalTreeReference = 1U;
    arena.rooms[1].portalTreeCount = 1U;
    arena.treeReferences.push_back(1U);
    arena.trees.push_back({
        .kind = CcfBspTreeKind::portalTree,
        .worldRoomIndex = 1U,
        .rootNodeIndex = 1U,
        .firstNodeIndex = 1U,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, 1.0F},
        .firstPolygonIndex = 1U,
        .polygonCount = 1U,
    });
    arena.polygons.push_back(triangleZ(1.0F, 2U));

    const auto result =
        traceMissionWorldRuntimePortalTransition(
            arena,
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 0.9999995F});
    require(
        result.status ==
                MissionWorldRuntimePortalTraceStatus::
                    outOfSegmentHit &&
            result.hit.has_value() &&
            result.hit->ownerWorldRoomIndex == 1U &&
            !result.hit->withinRequestedSegment &&
            result.transitionCount == 1U &&
            !result.targetWorldRoomIndex.has_value(),
        "runtime adapter published an intermediate room after a later "
        "invalid portal hop");
}

void testPortalTransitionUsesCommonRuntimeBasis() {
    const BasisTransform basis{
        .sourceToRuntime = Mat3{{
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, -1.0F},
        }},
        .runtimeUnitsPerSourceUnit = 4.0F,
    };
    const auto arena = oneTreeArena(
        CcfBspTreeKind::portalTree,
        triangleZ(0.0F, 1U),
        {0.0F, 0.0F, 1.0F},
        {});
    const auto result =
        traceMissionWorldRuntimePortalTransition(
            arena,
            basis,
            0U,
            {0.0F, 0.0F, 8.0F},
            {0.0F, 0.0F, -8.0F});
    require(
        result.status ==
                MissionWorldRuntimePortalTraceStatus::transition &&
            result.targetWorldRoomIndex ==
                std::optional<std::size_t>{1U} &&
            result.transitionCount == 1U &&
            result.hit.has_value() &&
            result.hit->runtimePoint == Vec3{} &&
            result.hit->runtimePlaneNormal ==
                Vec3{0.0F, 0.0F, -1.0F},
        "runtime portal adapter did not preserve transition data");
}

void testHotPathDoesNotAllocate() {
    const auto staticArena = oneTreeArena(
        CcfBspTreeKind::staticTree,
        triangleZ(0.0F),
        {0.0F, 0.0F, 1.0F},
        {});
    const auto portalArena = oneTreeArena(
        CcfBspTreeKind::portalTree,
        triangleZ(0.0F, 1U),
        {0.0F, 0.0F, 1.0F},
        {});

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    MissionWorldRuntimeSpatialLineTraceResult line;
    MissionWorldRuntimePortalTraceResult portal;
    for (std::size_t iteration = 0U; iteration < 4'096U;
         ++iteration) {
        line = traceMissionWorldRuntimeSpatialLine(
            staticArena,
            {},
            0U,
            CcfBspTreeKind::staticTree,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F});
        portal = traceMissionWorldRuntimePortalTransition(
            portalArena,
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F});
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(line.valid() && portal.valid(), "allocation probe failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "runtime spatial trace allocated on the hot path");
}

} // namespace

int main() {
    try {
        testIdentityBasisStaticHit();
        testShearedScaledBasisAndCovectorNormal();
        testReflectionPreservesPlaneOrientation();
        testInvalidBasisAndInputFailClosed();
        testEpsilonOutsideFractionIsPreserved();
        testBothEpsilonDirectionsAndExactEndpoints();
        testPortalRejectsOutOfSegmentHitBeforeRoomChange();
        testPortalRejectsLaterInvalidHopWithoutPublishingIntermediateRoom();
        testPortalTransitionUsesCommonRuntimeBasis();
        testHotPathDoesNotAllocate();
        std::cout
            << "MissionWorldRuntimeSpatialTrace tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "MissionWorldRuntimeSpatialTrace tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
