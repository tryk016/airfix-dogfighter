#include "airfix/render/LegacyDynamicBsp.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
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

static_assert(noexcept(traceMissionWorldRuntimeCombinedLine(
    std::declval<const MissionWorldSpatialArena&>(),
    std::declval<const BasisTransform&>(),
    std::declval<std::size_t>(),
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>(),
    std::declval<std::span<const LegacyDynamicBspMesh>>(),
    std::declval<std::span<const LegacyDynamicBspLineObject>>())));
static_assert(noexcept(traceMissionWorldRuntimeCombinedPortalLine(
    std::declval<const MissionWorldSpatialArena&>(),
    std::declval<const BasisTransform&>(),
    std::declval<std::size_t>(),
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>(),
    std::declval<std::span<const LegacyDynamicBspMesh>>(),
    std::declval<std::span<const LegacyDynamicBspLineObject>>(),
    std::declval<
        std::span<const LegacyDynamicBspRoomObjectRange>>())));
static_assert(std::is_nothrow_copy_constructible_v<
              MissionWorldRuntimeCombinedLineHit>);

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

[[nodiscard]] ConvertedMeshGeometry parallelTriangles(
    const std::span<const float> heights,
    const std::uint32_t firstMaterial = 100U) {
    ConvertedMeshGeometry geometry;
    for (std::size_t index = 0U; index < heights.size(); ++index) {
        const auto first = static_cast<std::uint32_t>(
            geometry.vertices.size());
        const float height = heights[index];
        geometry.vertices.push_back({{-2.0F, -2.0F, height}});
        geometry.vertices.push_back({{2.0F, -2.0F, height}});
        geometry.vertices.push_back({{0.0F, 2.0F, height}});
        geometry.triangles.push_back({
            .vertexIndices = {
                first,
                static_cast<std::uint32_t>(first + 1U),
                static_cast<std::uint32_t>(first + 2U),
            },
            .materialReference =
                firstMaterial + static_cast<std::uint32_t>(index),
            .textureCoordinates = std::nullopt,
            .faceNormal = {},
        });
    }
    return geometry;
}

[[nodiscard]] ConvertedMeshGeometry crossingTriangles(
    const bool tinyNegativeFragment = false) {
    ConvertedMeshGeometry geometry;
    const float negativeX = tinyNegativeFragment ? -1.0e-5F : -2.0F;
    geometry.vertices = {
        {{negativeX, -1.0F, 0.0F}},
        {{2.0F, -1.0F, 0.0F}},
        {{2.0F, 1.0F, 0.0F}},
        {{0.0F, -2.0F, -2.0F}},
        {{0.0F, 2.0F, -2.0F}},
        {{0.0F, 0.0F, 2.0F}},
    };
    geometry.triangles = {
        {
            .vertexIndices = {0U, 1U, 2U},
            .materialReference = 700U,
        },
        {
            .vertexIndices = {3U, 4U, 5U},
            .materialReference = 701U,
        },
    };
    return geometry;
}

[[nodiscard]] MissionWorldSpatialPolygon staticTriangle(
    const float height,
    const std::uint32_t materialMode = 0U) {
    return {
        .faceCross = {0.0F, 0.0F, 16.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {-2.0F, -2.0F, height},
        .edge01 = {4.0F, 0.0F, 0.0F},
        .edge12 = {-2.0F, 4.0F, 0.0F},
        .polygonIndex = 77U,
        .placedObjectReference = 0U,
        .materialCollisionMode2152 = materialMode,
    };
}

[[nodiscard]] MissionWorldSpatialArena emptyStaticArena(
    const std::size_t roomCount = 1U) {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(roomCount);
    return arena;
}

[[nodiscard]] MissionWorldSpatialArena staticArenaAt(
    const float height,
    const std::uint32_t materialMode = 0U) {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(1U);
    arena.treeReferences.push_back(0U);
    arena.trees.push_back({
        .kind = CcfBspTreeKind::staticTree,
        .sourceIndex = 0U,
        .physicalRoomIndex = 0U,
        .worldRoomIndex = 0U,
        .rootNodeIndex = 0U,
        .firstNodeIndex = 0U,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .childAIndex = std::nullopt,
        .childBIndex = std::nullopt,
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, height},
        .firstPolygonIndex = 0U,
        .polygonCount = 1U,
    });
    arena.polygons.push_back(staticTriangle(height, materialMode));
    arena.rooms[0].firstStaticTreeReference = 0U;
    arena.rooms[0].staticTreeCount = 1U;
    return arena;
}

[[nodiscard]] LegacyDynamicBspLineObject object(
    const std::size_t meshIndex = 0U,
    const std::uint32_t actorObjectId = 900U) {
    return {
        .meshIndex = meshIndex,
        .actorObjectId = actorObjectId,
        .active = true,
        .objectLocalToRuntime = {},
        .runtimeTranslation = {},
    };
}

[[nodiscard]] LegacyDynamicBspLineObject portalObject(
    const float height,
    const std::size_t targetWorldRoomIndex,
    const bool visible = true,
    const std::int32_t portalType = 0) {
    auto result = object(0U, 0U);
    result.runtimeTranslation.z = height;
    result.portalType = portalType;
    result.portalWorldRoomIndex = targetWorldRoomIndex;
    result.portalObjectVisible = visible;
    return result;
}

[[nodiscard]] LegacyDynamicBspBuildIssueKind firstIssue(
    const LegacyDynamicBspMesh& mesh) {
    require(!mesh.issues.empty(), "expected a typed build issue");
    return mesh.issues.front().kind;
}

void testSingleTriangleBuildAndTrace() {
    const std::array heights{0.0F};
    const std::array bindings{
        LegacyDynamicBspMaterialBinding{
            .sourceReference = 100U,
            .collisionMode2152 = 42U,
        },
    };
    const auto mesh =
        buildLegacyDynamicBsp(parallelTriangles(heights), bindings);

    require(mesh.complete(), "single triangle BSP did not build");
    require(
        mesh.localArena.rooms.size() == 1U &&
            mesh.localArena.trees.size() == 1U &&
            mesh.localArena.nodes.size() == 1U &&
            mesh.localArena.polygons.size() == 1U,
        "single triangle BSP shape is incorrect");
    require(
        close(mesh.localBoundingRadius, std::sqrt(8.0F)),
        "mesh radius did not use maximum local vertex norm");
    require(
        mesh.localArena.polygons[0].polygonIndex == 0U &&
            mesh.polygonMaterialReferences[0] == 100U &&
            mesh.localArena.polygons[0].materialCollisionMode2152 ==
                std::optional<std::uint32_t>{42U},
        "source triangle or material provenance was lost");
    require(
        mesh.localArena.polygons[0].faceNormal ==
            CcfVector3{0.0F, 0.0F, 1.0F},
        "dynamic polygon did not use the recovered standard cross");
    require(
        mesh.retainedPayloadBytes >
            mesh.localArena.retainedPayloadBytes,
        "parallel material references were not charged");

    const std::array meshes{mesh};
    const std::array objects{object()};
    const auto result = traceMissionWorldRuntimeCombinedLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        meshes,
        objects);
    require(
        result.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            result.hit.has_value() &&
            result.hit->kind ==
                MissionWorldRuntimeCombinedLineHitKind::dynamicObject &&
            result.hit->legacyFraction == 0.5F &&
            result.hit->runtimePoint == Vec3{} &&
            result.hit->runtimePlaneNormal ==
                Vec3{0.0F, 0.0F, 1.0F} &&
            result.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{0U} &&
            result.hit->dynamicMeshIndex ==
                std::optional<std::size_t>{0U} &&
            result.hit->sourceTriangleIndex ==
                std::optional<std::uint32_t>{0U} &&
            result.hit->sourceMaterialReference ==
                std::optional<std::uint32_t>{100U} &&
            result.hit->materialCollisionMode2152 ==
                std::optional<std::uint32_t>{42U} &&
            result.hit->actorObjectId == 900U,
        "combined trace lost the dynamic hit contract");
}

void testNativeListOrderAndExactPolygonTie() {
    const std::array heights{0.0F, 0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 200U));
    require(
        mesh.complete() &&
            mesh.localArena.nodes.size() == 1U &&
            mesh.localArena.polygons.size() == 2U,
        "coplanar BSP fixture did not build");
    // CreateDynamicBsp prepends source triangles (1, 0). CreateBspTree adds
    // the splitter first, then AddToNode prepends source triangle 0.
    require(
        mesh.localArena.polygons[0].polygonIndex == 0U &&
            mesh.localArena.polygons[1].polygonIndex == 1U,
        "native AddToList/AddToNode order changed");

    const std::array meshes{mesh};
    const std::array objects{object()};
    const auto result = traceMissionWorldRuntimeCombinedLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        meshes,
        objects);
    require(
        result.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            result.hit.has_value() &&
            result.hit->sourceTriangleIndex ==
                std::optional<std::uint32_t>{0U} &&
            result.hit->sourceMaterialReference ==
                std::optional<std::uint32_t>{200U},
        "first native node-list polygon did not retain the exact tie");
}

void testBalancedSplitterAndChildSides() {
    const std::array heights{-1.0F, 0.0F, 1.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 300U));
    require(
        mesh.complete() && mesh.localArena.nodes.size() == 3U,
        "three-plane BSP fixture did not build");
    const auto& root = mesh.localArena.nodes[0];
    require(
        mesh.localArena.polygons[root.firstPolygonIndex].polygonIndex ==
                1U &&
            root.childAIndex.has_value() &&
            root.childBIndex.has_value(),
        "balanced middle splitter was not selected");
    const auto& negative =
        mesh.localArena.nodes[*root.childAIndex];
    const auto& positive =
        mesh.localArena.nodes[*root.childBIndex];
    require(
        mesh.localArena.polygons[
            negative.firstPolygonIndex].polygonIndex == 0U &&
            mesh.localArena.polygons[
                positive.firstPolygonIndex].polygonIndex == 2U,
        "negative child A or nonnegative child B was reversed");
}

void testCrossingSplitAndSmallFragmentDiscard() {
    const auto split = buildLegacyDynamicBsp(
        crossingTriangles());
    require(
        split.complete() &&
            split.localArena.nodes.size() == 3U &&
            split.localArena.polygons.size() == 3U,
        "crossing polygon did not create both native child fragments");
    const auto& root = split.localArena.nodes[0];
    require(
        split.localArena.polygons[
            root.firstPolygonIndex].polygonIndex == 1U &&
            root.childAIndex.has_value() &&
            root.childBIndex.has_value() &&
            split.localArena.polygons[
                split.localArena.nodes[
                    *root.childAIndex].firstPolygonIndex].polygonIndex ==
                0U &&
            split.localArena.polygons[
                split.localArena.nodes[
                    *root.childBIndex].firstPolygonIndex].polygonIndex ==
                0U,
        "split fragments lost original triangle provenance or side");

    const auto discarded = buildLegacyDynamicBsp(
        crossingTriangles(true));
    require(
        discarded.complete() &&
            discarded.localArena.nodes.size() == 2U &&
            discarded.localArena.polygons.size() == 2U &&
            !discarded.localArena.nodes[0].childAIndex.has_value() &&
            discarded.localArena.nodes[0].childBIndex.has_value(),
        "native 0.0001 minimum fragment area was not enforced");
}

void testCombinedNearestAndTiePolicy() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 400U));
    const std::array meshes{mesh};
    const std::array objects{object()};

    const auto dynamicNearer = traceMissionWorldRuntimeCombinedLine(
        staticArenaAt(1.0F, 71U),
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        meshes,
        objects);
    require(
        dynamicNearer.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            dynamicNearer.hit.has_value() &&
            dynamicNearer.hit->kind ==
                MissionWorldRuntimeCombinedLineHitKind::dynamicObject,
        "nearer dynamic object did not replace static room hit");

    const auto staticNearer = traceMissionWorldRuntimeCombinedLine(
        staticArenaAt(-1.0F, 72U),
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        meshes,
        objects);
    require(
        staticNearer.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            staticNearer.hit.has_value() &&
            staticNearer.hit->kind ==
                MissionWorldRuntimeCombinedLineHitKind::staticRoom &&
            staticNearer.hit->materialCollisionMode2152 ==
                std::optional<std::uint32_t>{72U},
        "nearer static room hit was not retained");

    const auto exactTie = traceMissionWorldRuntimeCombinedLine(
        staticArenaAt(0.0F, 73U),
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        meshes,
        objects);
    require(
        exactTie.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            exactTie.hit.has_value() &&
            exactTie.hit->kind ==
                MissionWorldRuntimeCombinedLineHitKind::staticRoom &&
            exactTie.hit->materialCollisionMode2152 ==
                std::optional<std::uint32_t>{73U},
        "exact static/dynamic tie did not retain static precedence");

    const std::array tiedObjects{
        object(0U, 11U),
        object(0U, 22U),
    };
    const auto dynamicTie = traceMissionWorldRuntimeCombinedLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        meshes,
        tiedObjects);
    require(
        dynamicTie.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            dynamicTie.hit.has_value() &&
            dynamicTie.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{0U} &&
            dynamicTie.hit->actorObjectId == 11U,
        "exact dynamic tie did not retain earlier room-list object");
}

void testObjectTransformAndActivity() {
    const std::array heights{0.0F};
    const std::array bindings{
        LegacyDynamicBspMaterialBinding{
            .sourceReference = 500U,
            .collisionMode2152 = 81U,
        },
    };
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 500U), bindings);
    const std::array meshes{mesh};

    auto inactive = object(0U, 10U);
    inactive.active = false;
    auto transformed = object(0U, 20U);
    transformed.objectLocalToRuntime = {{
        Vec3{0.0F, 0.0F, -1.0F},
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{1.0F, 0.0F, 0.0F},
    }};
    transformed.runtimeTranslation = {2.0F, 0.0F, 0.0F};
    const std::array objects{inactive, transformed};
    const auto result = traceMissionWorldRuntimeCombinedLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, 0.0F},
        {4.0F, 0.0F, 0.0F},
        meshes,
        objects);
    require(
        result.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            result.hit.has_value() &&
            result.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{1U} &&
            result.hit->actorObjectId == 20U &&
            close(result.hit->runtimePoint, {2.0F, 0.0F, 0.0F}) &&
            close(
                result.hit->runtimePlaneNormal,
                {1.0F, 0.0F, 0.0F}) &&
            result.hit->materialCollisionMode2152 ==
                std::optional<std::uint32_t>{81U},
        "object inverse transform or forward normal transform changed");
}

void testEmptyMeshAndBroadPhaseMiss() {
    const ConvertedMeshGeometry empty;
    const auto emptyMesh = buildLegacyDynamicBsp(empty);
    require(
        emptyMesh.complete() &&
            emptyMesh.localArena.rooms.size() == 1U &&
            emptyMesh.localArena.trees.empty(),
        "empty mesh did not produce a valid no-hit collider");
    const std::array emptyMeshes{emptyMesh};
    const std::array emptyObjects{object()};
    const auto emptyResult = traceMissionWorldRuntimeCombinedLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        emptyMeshes,
        emptyObjects);
    require(
        emptyResult.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::noHit &&
            !emptyResult.hit.has_value(),
        "empty dynamic collider did not preserve no-hit");

    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(parallelTriangles(heights));
    const std::array meshes{mesh};
    auto farObject = object();
    farObject.runtimeTranslation = {1'000.0F, 0.0F, 0.0F};
    const std::array objects{farObject};
    const auto miss = traceMissionWorldRuntimeCombinedLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        meshes,
        objects);
    require(
        miss.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::noHit,
        "midpoint/radius broad phase did not reject a distant object");
}

void testBuildValidationAndLimits() {
    const std::array heights{0.0F};
    const auto geometry = parallelTriangles(heights, 600U);

    auto limits = LegacyDynamicBspBuildLimits{};
    limits.maximumVertices = 0U;
    require(
        firstIssue(buildLegacyDynamicBsp(geometry, {}, limits)) ==
            LegacyDynamicBspBuildIssueKind::limitExceeded,
        "vertex limit was not enforced");

    limits = {};
    limits.maximumTriangles = 0U;
    require(
        firstIssue(buildLegacyDynamicBsp(geometry, {}, limits)) ==
            LegacyDynamicBspBuildIssueKind::limitExceeded,
        "triangle limit was not enforced");

    limits = {};
    limits.maximumMaterialBindings = 0U;
    const std::array oneBinding{
        LegacyDynamicBspMaterialBinding{.sourceReference = 600U},
    };
    require(
        firstIssue(buildLegacyDynamicBsp(
            geometry, oneBinding, limits)) ==
            LegacyDynamicBspBuildIssueKind::limitExceeded,
        "material-binding limit was not enforced");

    limits = {};
    limits.maximumNodes = 0U;
    require(
        firstIssue(buildLegacyDynamicBsp(geometry, {}, limits)) ==
            LegacyDynamicBspBuildIssueKind::limitExceeded,
        "node limit was not enforced");

    limits = {};
    limits.maximumRetainedPolygons = 0U;
    require(
        firstIssue(buildLegacyDynamicBsp(geometry, {}, limits)) ==
            LegacyDynamicBspBuildIssueKind::limitExceeded,
        "retained polygon limit was not enforced");

    limits = {};
    limits.maximumRetainedBytes = 0U;
    require(
        firstIssue(buildLegacyDynamicBsp(geometry, {}, limits)) ==
            LegacyDynamicBspBuildIssueKind::
                retainedByteLimitExceeded,
        "retained-byte limit was not enforced");

    const std::array duplicateBindings{
        LegacyDynamicBspMaterialBinding{.sourceReference = 600U},
        LegacyDynamicBspMaterialBinding{.sourceReference = 600U},
    };
    const auto duplicate =
        buildLegacyDynamicBsp(geometry, duplicateBindings);
    require(
        firstIssue(duplicate) ==
                LegacyDynamicBspBuildIssueKind::
                    duplicateMaterialBinding &&
            duplicate.issues[0].materialReference ==
                std::optional<std::uint32_t>{600U},
        "duplicate material binding was not rejected");

    auto invalidIndex = geometry;
    invalidIndex.triangles[0].vertexIndices[2] = 999U;
    require(
        firstIssue(buildLegacyDynamicBsp(invalidIndex)) ==
            LegacyDynamicBspBuildIssueKind::invalidGeometry,
        "out-of-range vertex index was accepted");

    auto degenerate = geometry;
    degenerate.triangles[0].vertexIndices[2] =
        degenerate.triangles[0].vertexIndices[1];
    require(
        firstIssue(buildLegacyDynamicBsp(degenerate)) ==
            LegacyDynamicBspBuildIssueKind::invalidGeometry,
        "degenerate triangle was accepted");

    auto nonFinite = geometry;
    nonFinite.vertices[0].position.x =
        std::numeric_limits<float>::quiet_NaN();
    require(
        firstIssue(buildLegacyDynamicBsp(nonFinite)) ==
            LegacyDynamicBspBuildIssueKind::invalidGeometry,
        "non-finite vertex was accepted");
}

void testRuntimeValidation() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(parallelTriangles(heights));
    const std::array meshes{mesh};
    const std::array objects{object()};
    auto options = MissionWorldRuntimeCombinedLineTraceOptions{};
    options.maximumDynamicObjects = 0U;
    require(
        traceMissionWorldRuntimeCombinedLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects,
            options).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::
                dynamicObjectLimitExceeded,
        "dynamic object limit was not enforced");

    auto invalidObject = object(1U);
    const std::array invalidIndexObjects{invalidObject};
    require(
        traceMissionWorldRuntimeCombinedLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            invalidIndexObjects).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::
                invalidDynamicObject,
        "invalid dynamic mesh index was accepted");

    invalidObject = object();
    invalidObject.objectLocalToRuntime.columns[0].x = 2.0F;
    const std::array scaledObjects{invalidObject};
    require(
        traceMissionWorldRuntimeCombinedLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            scaledObjects).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::
                invalidDynamicObject,
        "unconfirmed scaled object relation was accepted");

    require(
        traceMissionWorldRuntimeCombinedLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F,
             std::numeric_limits<float>::quiet_NaN()},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::invalidInput,
        "non-finite runtime line was accepted");

    require(
        traceMissionWorldRuntimeCombinedLine(
            MissionWorldSpatialArena{},
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::invalidArena,
        "invalid static arena did not fail closed");
}

void testOutOfSegmentHitFailsClosed() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(parallelTriangles(heights));
    const std::array meshes{mesh};
    const std::array objects{object()};
    const auto result = traceMissionWorldRuntimeCombinedLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, 0.5e-6F},
        {0.0F, 0.0F, 0.75e-6F},
        meshes,
        objects);
    require(
        result.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::
                    outOfSegmentHit &&
            result.hit.has_value() &&
            result.hit->kind ==
                MissionWorldRuntimeCombinedLineHitKind::dynamicObject &&
            result.hit->legacyFraction < 0.0F &&
            !result.hit->withinRequestedSegment,
        "epsilon-admitted out-of-segment hit did not fail closed");
}

void testCombinedPortalContinuationAndFraction() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 800U));
    const std::array meshes{mesh};
    auto solid = object(0U, 22U);
    solid.runtimeTranslation.z = 1.0F;
    const std::array objects{
        portalObject(-1.0F, 1U),
        solid,
    };
    const std::array ranges{
        LegacyDynamicBspRoomObjectRange{
            .firstObjectIndex = 0U,
            .objectCount = 1U,
        },
        LegacyDynamicBspRoomObjectRange{
            .firstObjectIndex = 1U,
            .objectCount = 1U,
        },
    };

    const auto result = traceMissionWorldRuntimeCombinedPortalLine(
        emptyStaticArena(2U),
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        meshes,
        objects,
        ranges);
    require(
        result.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            result.hit.has_value() &&
            result.portalTransitionCount == 1U &&
            result.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{1U} &&
            result.hit->actorObjectId == 22U &&
            result.hit->portalType == -1 &&
            !result.hit->portalWorldRoomIndex.has_value() &&
            close(result.hit->legacyFraction, 0.75F) &&
            close(
                result.hit->runtimePoint,
                Vec3{0.0F, 0.0F, 1.0F}),
        "visible type-zero portal did not continue to the later hit");
}

void testCombinedPortalNoLaterHitAndBlockingGates() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 810U));
    const std::array meshes{mesh};
    const std::array ranges{
        LegacyDynamicBspRoomObjectRange{
            .firstObjectIndex = 0U,
            .objectCount = 1U,
        },
        LegacyDynamicBspRoomObjectRange{
            .firstObjectIndex = 1U,
            .objectCount = 0U,
        },
    };

    std::array objects{portalObject(0.0F, 1U)};
    const auto noLaterHit =
        traceMissionWorldRuntimeCombinedPortalLine(
            emptyStaticArena(2U),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects,
            ranges);
    require(
        noLaterHit.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::noHit &&
            !noLaterHit.hit.has_value() &&
            noLaterHit.portalTransitionCount == 1U,
        "transparent portal without a later hit did not clear collision");

    objects[0].portalObjectVisible = false;
    const auto hidden = traceMissionWorldRuntimeCombinedPortalLine(
        emptyStaticArena(2U),
        {},
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        meshes,
        objects,
        ranges);
    require(
        hidden.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            hidden.hit.has_value() &&
            hidden.portalTransitionCount == 0U &&
            hidden.hit->portalType == 0 &&
            !hidden.hit->portalObjectVisible &&
            hidden.hit->portalWorldRoomIndex ==
                std::optional<std::size_t>{1U},
        "hidden type-zero portal was incorrectly followed");

    objects[0].portalObjectVisible = true;
    objects[0].portalType = 1;
    const auto solidPortal =
        traceMissionWorldRuntimeCombinedPortalLine(
            emptyStaticArena(2U),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects,
            ranges);
    require(
        solidPortal.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            solidPortal.hit.has_value() &&
            solidPortal.portalTransitionCount == 0U &&
            solidPortal.hit->portalType == 1,
        "type-one portal was incorrectly treated as transparent");
}

void testCombinedPortalChainAndStrictNearest() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 820U));
    const std::array meshes{mesh};
    auto solid = object(0U, 55U);
    solid.runtimeTranslation.z = 1.0F;
    const std::array objects{
        portalObject(-1.0F, 1U),
        portalObject(0.0F, 2U),
        solid,
    };
    const std::array ranges{
        LegacyDynamicBspRoomObjectRange{0U, 1U},
        LegacyDynamicBspRoomObjectRange{1U, 1U},
        LegacyDynamicBspRoomObjectRange{2U, 1U},
    };
    const auto chain = traceMissionWorldRuntimeCombinedPortalLine(
        emptyStaticArena(3U),
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        meshes,
        objects,
        ranges);
    require(
        chain.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            chain.hit.has_value() &&
            chain.portalTransitionCount == 2U &&
            chain.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{2U} &&
            close(chain.hit->legacyFraction, 0.75F),
        "two-portal fraction composition or absolute provenance changed");

    auto portalBehindStatic = portalObject(-1.0F, 0U);
    const std::array oneObject{portalBehindStatic};
    const std::array oneRange{
        LegacyDynamicBspRoomObjectRange{0U, 1U},
    };
    const auto staticFirst =
        traceMissionWorldRuntimeCombinedPortalLine(
            staticArenaAt(-1.5F, 99U),
            {},
            0U,
            {0.0F, 0.0F, -2.0F},
            {0.0F, 0.0F, 2.0F},
            meshes,
            oneObject,
            oneRange);
    require(
        staticFirst.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            staticFirst.hit.has_value() &&
            staticFirst.hit->kind ==
                MissionWorldRuntimeCombinedLineHitKind::staticRoom &&
            staticFirst.portalTransitionCount == 0U &&
            staticFirst.hit->materialCollisionMode2152 ==
                std::optional<std::uint32_t>{99U},
        "nearer static hit did not block a farther transparent portal");
}

void testCombinedPortalUsesRecursiveFractionOrder() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 825U));
    const std::array meshes{mesh};
    auto solid = object(0U, 66U);
    solid.runtimeTranslation.z = 9.0F;
    const std::array objects{
        portalObject(1.0F, 1U),
        portalObject(2.0F, 2U),
        solid,
    };
    const std::array ranges{
        LegacyDynamicBspRoomObjectRange{0U, 1U},
        LegacyDynamicBspRoomObjectRange{1U, 1U},
        LegacyDynamicBspRoomObjectRange{2U, 1U},
    };
    const auto arena = emptyStaticArena(3U);
    const Vec3 end{0.0F, 0.0F, 1'000.0F};

    const auto first = traceMissionWorldRuntimeCombinedLine(
        arena, {}, 0U, {}, end, meshes,
        std::span{objects}.subspan(0U, 1U));
    require(
        first.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            first.hit.has_value(),
        "first recursive-order fixture portal did not hit");
    const auto second = traceMissionWorldRuntimeCombinedLine(
        arena, {}, 1U, first.hit->runtimePoint, end, meshes,
        std::span{objects}.subspan(1U, 1U));
    require(
        second.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            second.hit.has_value(),
        "second recursive-order fixture portal did not hit");
    const auto third = traceMissionWorldRuntimeCombinedLine(
        arena, {}, 2U, second.hit->runtimePoint, end, meshes,
        std::span{objects}.subspan(2U, 1U));
    require(
        third.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            third.hit.has_value(),
        "recursive-order fixture final surface did not hit");

    const auto compose = [](const float outer, const float inner) {
        return static_cast<float>(
            static_cast<double>(outer) +
            (1.0 - static_cast<double>(outer)) *
                static_cast<double>(inner));
    };
    const float recursiveExpected = compose(
        first.hit->legacyFraction,
        compose(
            second.hit->legacyFraction,
            third.hit->legacyFraction));
    const float forwardRounded = compose(
        compose(
            first.hit->legacyFraction,
            second.hit->legacyFraction),
        third.hit->legacyFraction);
    require(
        recursiveExpected != forwardRounded,
        "recursive-order fixture did not distinguish rounding order");

    const auto combined = traceMissionWorldRuntimeCombinedPortalLine(
        arena,
        {},
        0U,
        {},
        end,
        meshes,
        objects,
        ranges);
    require(
        combined.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            combined.hit.has_value() &&
            combined.hit->legacyFraction == recursiveExpected,
        "portal fractions were not composed in native recursion-unwind order");
}

void testCombinedPortalLimitsAndMalformedPublication() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 830U));
    const std::array meshes{mesh};
    const std::array objects{portalObject(0.0F, 0U)};
    const std::array ranges{
        LegacyDynamicBspRoomObjectRange{0U, 1U},
    };
    auto options =
        MissionWorldRuntimeCombinedPortalLineTraceOptions{};
    options.maximumPortalTransitions = 0U;
    const auto zeroBudgetPortal =
        traceMissionWorldRuntimeCombinedPortalLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects,
            ranges,
            options);
    require(
        zeroBudgetPortal.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::
                    portalTransitionLimitExceeded &&
            zeroBudgetPortal.hit.has_value() &&
            zeroBudgetPortal.portalTransitionCount == 0U,
        "zero portal budget did not reject a winning portal");

    const std::array solidObjects{object()};
    const auto zeroBudgetSolid =
        traceMissionWorldRuntimeCombinedPortalLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            solidObjects,
            ranges,
            options);
    require(
        zeroBudgetSolid.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            zeroBudgetSolid.hit.has_value() &&
            zeroBudgetSolid.portalTransitionCount == 0U,
        "zero portal budget rejected an ordinary collision");

    options.maximumPortalTransitions = 2U;
    const auto cycle = traceMissionWorldRuntimeCombinedPortalLine(
        emptyStaticArena(),
        {},
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        meshes,
        objects,
        ranges,
        options);
    require(
        cycle.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::
                    portalTransitionLimitExceeded &&
            cycle.hit.has_value() &&
            cycle.portalTransitionCount == 2U,
        "self-portal did not stop at the explicit transition budget");

    options.maximumPortalTransitions =
        kMissionWorldSpatialMaximumPortalTransitions + 1U;
    require(
        traceMissionWorldRuntimeCombinedPortalLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects,
            ranges,
            options).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::invalidInput,
        "portal budget above the hard safety ceiling was accepted");

    const std::array badRange{
        LegacyDynamicBspRoomObjectRange{1U, 1U},
    };
    require(
        traceMissionWorldRuntimeCombinedPortalLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            objects,
            badRange).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::
                invalidDynamicObject,
        "out-of-range room object publication was accepted");

    auto malformed = object();
    malformed.portalType = 0;
    const std::array malformedObjects{malformed};
    require(
        traceMissionWorldRuntimeCombinedLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            malformedObjects).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::
                invalidDynamicObject,
        "type-zero object without a target room was accepted");

    malformed.portalType = 2;
    malformed.portalWorldRoomIndex = 0U;
    const std::array invalidTypeObjects{malformed};
    require(
        traceMissionWorldRuntimeCombinedLine(
            emptyStaticArena(),
            {},
            0U,
            {0.0F, 0.0F, -1.0F},
            {0.0F, 0.0F, 1.0F},
            meshes,
            invalidTypeObjects).status ==
            MissionWorldRuntimeCombinedLineTraceStatus::
                invalidDynamicObject,
        "portal type outside the native domain was accepted");
}

void testCombinedPortalRejectsLaterOutOfSegmentHit() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(
        parallelTriangles(heights, 840U));
    const std::array meshes{mesh};
    auto behindPortal = object(0U, 77U);
    behindPortal.runtimeTranslation.z = -0.5e-6F;
    const std::array objects{
        portalObject(0.0F, 1U),
        behindPortal,
    };
    const std::array ranges{
        LegacyDynamicBspRoomObjectRange{0U, 1U},
        LegacyDynamicBspRoomObjectRange{1U, 1U},
    };
    const auto result = traceMissionWorldRuntimeCombinedPortalLine(
        emptyStaticArena(2U),
        {},
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        meshes,
        objects,
        ranges);
    require(
        result.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::
                    outOfSegmentHit &&
            result.hit.has_value() &&
            result.portalTransitionCount == 1U &&
            result.hit->legacyFraction < 0.0F &&
            !result.hit->withinRequestedSegment,
        "later epsilon-admitted hit did not fail before publication");
}

void testRuntimeTraceDoesNotAllocate() {
    const std::array heights{0.0F};
    const auto mesh = buildLegacyDynamicBsp(parallelTriangles(heights));
    const std::array meshes{mesh};
    const std::array objects{object()};
    const auto staticArena = staticArenaAt(1.0F);

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    MissionWorldRuntimeCombinedLineTraceResult result;
    for (std::size_t iteration = 0U; iteration < 4'096U;
         ++iteration) {
        result = traceMissionWorldRuntimeCombinedLine(
            staticArena,
            {},
            0U,
            {0.0F, 0.0F, -2.0F},
            {0.0F, 0.0F, 2.0F},
            meshes,
            objects);
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(result.valid(), "allocation probe trace failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "combined static/dynamic trace allocated on the hot path");

    const std::array portalObjects{
        portalObject(-1.0F, 1U),
        object(0U, 44U),
    };
    const std::array portalRanges{
        LegacyDynamicBspRoomObjectRange{0U, 1U},
        LegacyDynamicBspRoomObjectRange{1U, 1U},
    };
    const auto portalArena = emptyStaticArena(2U);
    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0U; iteration < 4'096U;
         ++iteration) {
        result = traceMissionWorldRuntimeCombinedPortalLine(
            portalArena,
            {},
            0U,
            {0.0F, 0.0F, -2.0F},
            {0.0F, 0.0F, 2.0F},
            meshes,
            portalObjects,
            portalRanges);
    }
    countAllocations.store(false, std::memory_order_relaxed);
    require(
        result.valid() &&
            result.portalTransitionCount == 1U,
        "allocation probe portal trace failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "combined portal trace allocated on the hot path");
}

} // namespace

int main() {
    try {
        testSingleTriangleBuildAndTrace();
        testNativeListOrderAndExactPolygonTie();
        testBalancedSplitterAndChildSides();
        testCrossingSplitAndSmallFragmentDiscard();
        testCombinedNearestAndTiePolicy();
        testObjectTransformAndActivity();
        testEmptyMeshAndBroadPhaseMiss();
        testBuildValidationAndLimits();
        testRuntimeValidation();
        testOutOfSegmentHitFailsClosed();
        testCombinedPortalContinuationAndFraction();
        testCombinedPortalNoLaterHitAndBlockingGates();
        testCombinedPortalChainAndStrictNearest();
        testCombinedPortalUsesRecursiveFractionOrder();
        testCombinedPortalLimitsAndMalformedPublication();
        testCombinedPortalRejectsLaterOutOfSegmentHit();
        testRuntimeTraceDoesNotAllocate();
        std::cout << "LegacyDynamicBsp tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "LegacyDynamicBsp tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
