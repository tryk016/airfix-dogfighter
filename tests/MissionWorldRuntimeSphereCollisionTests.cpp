#include "airfix/render/MissionWorldRuntimeSphereCollision.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
    if (trackAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
}

} // namespace

void* operator new(const std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0U ? 1U : size);
        memory != nullptr) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

using namespace airfix::assets;
using namespace airfix::render;

static_assert(noexcept(resolveMissionWorldRuntimeStaticSphere(
    std::declval<const MissionWorldSpatialArena&>(),
    std::declval<const BasisTransform&>(),
    std::declval<std::size_t>(),
    std::declval<const Vec3&>(),
    std::declval<float>(),
    std::declval<std::span<MissionWorldRuntimeSphereCandidate>>(),
    std::declval<std::span<Vec3>>(),
    std::declval<
        const MissionWorldRuntimeSphereCollisionOptions&>())));
static_assert(std::is_trivially_copyable_v<
              MissionWorldRuntimeSphereCandidate>);

void require(const bool condition, const char* const message) {
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

void requireVec(
    const Vec3& actual,
    const Vec3& expected,
    const char* const message,
    const float tolerance = 1.0e-6F) {
    require(
        close(actual.x, expected.x, tolerance) &&
            close(actual.y, expected.y, tolerance) &&
            close(actual.z, expected.z, tolerance),
        message);
}

[[nodiscard]] MissionWorldSpatialPolygon triangle(
    const std::optional<std::uint32_t> materialMode = std::nullopt,
    const std::optional<std::size_t> portalTarget = std::nullopt) {
    return {
        .faceCross = {0.0F, 0.0F, 4.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {-1.0F, -1.0F, 0.0F},
        .edge01 = {2.0F, 0.0F, 0.0F},
        .edge12 = {-1.0F, 2.0F, 0.0F},
        .polygonIndex = 17U,
        .placedObjectReference = 9U,
        .materialCollisionMode2152 = materialMode,
        .portalWorldRoomIndex = portalTarget,
        .portalMeshSelectionFlagB = portalTarget.has_value(),
        .portalType = 0U,
        .portalObjectVisible = portalTarget.has_value(),
    };
}

[[nodiscard]] MissionWorldSpatialArena oneTreeArena(
    std::vector<MissionWorldSpatialPolygon> polygons) {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(1U);
    arena.rooms[0].staticTreeCount = 1U;
    arena.treeReferences = {0U};
    arena.trees.push_back({
        .kind = CcfBspTreeKind::staticTree,
        .worldRoomIndex = 0U,
        .rootNodeIndex = 0U,
        .firstNodeIndex = 0U,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, 0.0F},
        .firstPolygonIndex = 0U,
        .polygonCount = polygons.size(),
    });
    arena.polygons = std::move(polygons);
    return arena;
}

void testFaceResolutionAndNativeTieOrder() {
    auto arena = oneTreeArena({triangle(), triangle()});
    arena.polygons[0].polygonIndex = 11U;
    arena.polygons[1].polygonIndex = 22U;
    std::array<MissionWorldRuntimeSphereCandidate, 4U>
        candidates{};
    std::array<Vec3, 4U> planes{};

    const auto result = resolveMissionWorldRuntimeStaticSphere(
        arena,
        {},
        0U,
        Vec3{0.0F, 0.0F, -0.25F},
        0.5F,
        candidates,
        planes);
    require(
        result.status ==
                MissionWorldRuntimeSphereCollisionStatus::resolved &&
            result.valid() &&
            result.candidateCount == 2U &&
            result.resolutionIterations == 1U &&
            result.constraintPlaneCount == 1U &&
            result.lastSelectedContact.has_value(),
        "face collision did not resolve exactly once");
    requireVec(
        result.resolvedCenter,
        Vec3{0.0F, 0.0F, -0.5F},
        "face correction changed");
    requireVec(
        result.correction,
        Vec3{0.0F, 0.0F, -0.25F},
        "reported face correction changed");
    require(
        result.lastSelectedContact->feature ==
                LegacyGameplayCameraSphereContactFeature::face &&
            result.lastSelectedContact->polygonIndex == 1U &&
            arena.polygons[
                result.lastSelectedContact->polygonIndex]
                    .polygonIndex == 22U,
        "native prepend order did not select the last discovered tie");
    requireVec(
        planes[0],
        Vec3{0.0F, 0.0F, -1.0F},
        "face constraint plane changed");
}

void testEdgeResolutionPreservesRawDirection() {
    auto arena = oneTreeArena({triangle()});
    std::array<MissionWorldRuntimeSphereCandidate, 2U>
        candidates{};
    std::array<Vec3, 2U> planes{};

    const auto result = resolveMissionWorldRuntimeStaticSphere(
        arena,
        {},
        0U,
        Vec3{0.0F, -1.2F, -0.2F},
        0.5F,
        candidates,
        planes);
    const float depth = 0.5F - std::sqrt(0.08F);
    require(
        result.status ==
                MissionWorldRuntimeSphereCollisionStatus::resolved &&
            result.lastSelectedContact.has_value() &&
            result.lastSelectedContact->feature ==
                LegacyGameplayCameraSphereContactFeature::edge01,
        "edge collision did not resolve through the edge branch");
    requireVec(
        result.lastSelectedContact->direction,
        Vec3{0.0F, 0.2F, 0.2F},
        "runtime edge direction was incorrectly normalized");
    requireVec(
        result.resolvedCenter,
        Vec3{
            0.0F,
            -1.2F - depth * 0.2F,
            -0.2F - depth * 0.2F,
        },
        "legacy raw edge correction changed",
        2.0e-6F);
}

void testMaterialFilterAndTouchingBoundary() {
    auto filtered = oneTreeArena({
        triangle(0U),
        triangle(8U),
    });
    std::array<MissionWorldRuntimeSphereCandidate, 2U>
        candidates{};
    std::array<Vec3, 2U> planes{};
    const auto noContact = resolveMissionWorldRuntimeStaticSphere(
        filtered,
        {},
        0U,
        Vec3{0.0F, 0.0F, -0.25F},
        0.5F,
        candidates,
        planes);
    require(
        noContact.status ==
                MissionWorldRuntimeSphereCollisionStatus::noContact &&
            noContact.candidateCount == 0U,
        "material modes 0 and 8 were not removed");

    auto touchingArena = oneTreeArena({triangle(7U)});
    const auto touching = resolveMissionWorldRuntimeStaticSphere(
        touchingArena,
        {},
        0U,
        Vec3{0.0F, 0.0F, -0.5F},
        0.5F,
        candidates,
        planes);
    require(
        touching.status ==
                MissionWorldRuntimeSphereCollisionStatus::touching &&
            touching.candidateCount == 1U &&
            touching.resolutionIterations == 0U &&
            touching.resolvedCenter ==
                Vec3{0.0F, 0.0F, -0.5F},
        "zero-depth face tangency did not stop without correction");
}

void testVisibleTypeZeroPortalDiscoversAdjacentRoom() {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(2U);
    arena.rooms[0] = {
        .firstStaticTreeReference = 0U,
        .staticTreeCount = 1U,
    };
    arena.rooms[1] = {
        .firstStaticTreeReference = 1U,
        .staticTreeCount = 1U,
    };
    arena.treeReferences = {0U, 1U};
    arena.trees = {
        {
            .kind = CcfBspTreeKind::staticTree,
            .worldRoomIndex = 0U,
            .rootNodeIndex = 0U,
            .firstNodeIndex = 0U,
            .nodeCount = 1U,
        },
        {
            .kind = CcfBspTreeKind::staticTree,
            .worldRoomIndex = 1U,
            .rootNodeIndex = 1U,
            .firstNodeIndex = 1U,
            .nodeCount = 1U,
        },
    };
    arena.nodes = {
        {
            .splitNormal = {0.0F, 0.0F, 1.0F},
            .pointOnPlane = {0.0F, 0.0F, 0.0F},
            .firstPolygonIndex = 0U,
            .polygonCount = 1U,
        },
        {
            .splitNormal = {0.0F, 0.0F, 1.0F},
            .pointOnPlane = {0.0F, 0.0F, 0.0F},
            .firstPolygonIndex = 1U,
            .polygonCount = 1U,
        },
    };
    arena.polygons = {
        triangle(std::nullopt, 1U),
        triangle(7U),
    };
    std::array<MissionWorldRuntimeSphereCandidate, 4U>
        candidates{};
    std::array<Vec3, 4U> planes{};

    const auto result = resolveMissionWorldRuntimeStaticSphere(
        arena,
        {},
        0U,
        Vec3{0.0F, 0.0F, -0.25F},
        0.5F,
        candidates,
        planes);
    require(
        result.status ==
                MissionWorldRuntimeSphereCollisionStatus::resolved &&
            result.candidateCount == 1U &&
            result.lastSelectedContact.has_value() &&
            result.lastSelectedContact->ownerWorldRoomIndex == 1U &&
            result.lastSelectedContact->treeIndex == 1U &&
            result.lastSelectedContact->polygonIndex == 1U,
        "visible type-zero portal did not discover adjacent static geometry");

    arena.polygons[0].portalObjectVisible = false;
    const auto invisible = resolveMissionWorldRuntimeStaticSphere(
        arena,
        {},
        0U,
        Vec3{0.0F, 0.0F, -0.25F},
        0.5F,
        candidates,
        planes);
    require(
        invisible.status ==
                MissionWorldRuntimeSphereCollisionStatus::noContact &&
            invisible.candidateCount == 0U,
        "invisible type-zero portal was not ignored");

    arena.polygons[0].portalObjectVisible = true;
    arena.polygons[0].portalType = 3U;
    const auto solidPortal = resolveMissionWorldRuntimeStaticSphere(
        arena,
        {},
        0U,
        Vec3{0.0F, 0.0F, -0.25F},
        0.5F,
        candidates,
        planes);
    require(
        solidPortal.status ==
                MissionWorldRuntimeSphereCollisionStatus::resolved &&
            solidPortal.candidateCount == 1U &&
            solidPortal.lastSelectedContact.has_value() &&
            solidPortal.lastSelectedContact->ownerWorldRoomIndex == 0U &&
            solidPortal.lastSelectedContact->polygonIndex == 0U,
        "nonzero portal type did not remain solid");

    arena.polygons[0].portalType = 0U;
    const auto limited = resolveMissionWorldRuntimeStaticSphere(
        arena,
        {},
        0U,
        Vec3{0.0F, 0.0F, -0.25F},
        0.5F,
        candidates,
        planes,
        {.maximumPortalRooms = 1U});
    require(
        limited.status ==
            MissionWorldRuntimeSphereCollisionStatus::
                portalRoomLimitExceeded,
        "portal-room safety limit did not fail closed");
}

void testCapacityAndBasisFailures() {
    auto arena = oneTreeArena({triangle()});
    std::array<MissionWorldRuntimeSphereCandidate, 1U>
        candidates{};
    std::array<Vec3, 1U> planes{};

    const auto candidateOverflow =
        resolveMissionWorldRuntimeStaticSphere(
            arena,
            {},
            0U,
            Vec3{0.0F, 0.0F, -0.25F},
            0.5F,
            {},
            planes);
    const auto constraintOverflow =
        resolveMissionWorldRuntimeStaticSphere(
            arena,
            {},
            0U,
            Vec3{0.0F, 0.0F, -0.25F},
            0.5F,
            candidates,
            {});
    BasisTransform anisotropic;
    anisotropic.sourceToRuntime.columns[0].x = 2.0F;
    const auto invalidBasis =
        resolveMissionWorldRuntimeStaticSphere(
            arena,
            anisotropic,
            0U,
            Vec3{0.0F, 0.0F, -0.25F},
            0.5F,
            candidates,
            planes);
    require(
        candidateOverflow.status ==
                MissionWorldRuntimeSphereCollisionStatus::
                    candidateCapacityExceeded &&
            constraintOverflow.status ==
                MissionWorldRuntimeSphereCollisionStatus::
                    constraintCapacityExceeded &&
            invalidBasis.status ==
                MissionWorldRuntimeSphereCollisionStatus::
                    invalidBasis,
        "workspace or ellipsoid-producing basis failure changed");
}

void testUniformScaleAndAllocationFreeExecution() {
    auto arena = oneTreeArena({triangle()});
    std::array<MissionWorldRuntimeSphereCandidate, 4U>
        candidates{};
    std::array<Vec3, 4U> planes{};
    BasisTransform basis;
    basis.runtimeUnitsPerSourceUnit = 2.0F;

    const auto scaled = resolveMissionWorldRuntimeStaticSphere(
        arena,
        basis,
        0U,
        Vec3{0.0F, 0.0F, -0.5F},
        1.0F,
        candidates,
        planes);
    require(
        scaled.status ==
            MissionWorldRuntimeSphereCollisionStatus::resolved,
        "uniform runtime scale failed");
    requireVec(
        scaled.resolvedCenter,
        Vec3{0.0F, 0.0F, -1.0F},
        "uniform runtime scale correction changed");
    require(
        scaled.lastSelectedContact.has_value() &&
            close(
                scaled.lastSelectedContact->penetrationDepth,
                0.5F),
        "source penetration depth was not adapted to runtime units");

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    bool complete = true;
    float checksum = 0.0F;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const auto result = resolveMissionWorldRuntimeStaticSphere(
            arena,
            {},
            0U,
            Vec3{0.0F, 0.0F, -0.25F},
            0.5F,
            candidates,
            planes);
        if (result.status !=
            MissionWorldRuntimeSphereCollisionStatus::resolved) {
            complete = false;
            break;
        }
        checksum += result.resolvedCenter.z;
    }
    trackAllocations.store(false, std::memory_order_release);
    require(
        complete && std::isfinite(checksum),
        "repeated sphere resolution failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "static sphere resolver allocated");
}

} // namespace

int main() {
    try {
        testFaceResolutionAndNativeTieOrder();
        testEdgeResolutionPreservesRawDirection();
        testMaterialFilterAndTouchingBoundary();
        testVisibleTypeZeroPortalDiscoversAdjacentRoom();
        testCapacityAndBasisFailures();
        testUniformScaleAndAllocationFreeExecution();
        std::cout
            << "MissionWorldRuntimeSphereCollision tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr
            << "MissionWorldRuntimeSphereCollision tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
