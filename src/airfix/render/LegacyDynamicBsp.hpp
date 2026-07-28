#pragma once

#include "airfix/assets/MissionWorldSpatialArena.hpp"
#include "airfix/render/LegacyGeometry.hpp"
#include "airfix/render/MissionWorldRuntimeSpatialTrace.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

struct LegacyDynamicBspMaterialBinding final {
    std::uint32_t sourceReference{};
    std::optional<std::uint32_t> collisionMode2152;

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyDynamicBspMaterialBinding&,
        const LegacyDynamicBspMaterialBinding&) noexcept = default;
};

struct LegacyDynamicBspBuildLimits final {
    std::size_t maximumVertices{1'000'000U};
    std::size_t maximumTriangles{1'000'000U};
    std::size_t maximumMaterialBindings{1'000'000U};
    std::size_t maximumNodes{1'000'000U};
    std::size_t maximumRetainedPolygons{2'000'000U};
    std::size_t maximumWorkingPolygons{4'000'000U};
    std::size_t maximumWorkingVertices{16'000'000U};
    std::size_t maximumDepth{
        assets::kMissionWorldSpatialMaximumTraceDepth};
    std::uint64_t maximumRetainedBytes{512U * 1024U * 1024U};
};

enum class LegacyDynamicBspBuildIssueKind : std::uint8_t {
    invalidGeometry,
    duplicateMaterialBinding,
    limitExceeded,
    retainedByteLimitExceeded,
    integerOverflow,
    allocationFailure,
};

struct LegacyDynamicBspBuildIssue final {
    LegacyDynamicBspBuildIssueKind kind{
        LegacyDynamicBspBuildIssueKind::invalidGeometry};
    std::optional<std::size_t> triangleIndex;
    std::optional<std::uint32_t> materialReference;
};

// One native-style, object-local dynamic BSP. The synthetic single room is an
// implementation detail that allows the already recovered PhLine tree walker
// to be reused byte-for-byte at the semantic boundary. polygonMaterialReferences
// is parallel to localArena.polygons; polygonIndex on each retained polygon is
// the source triangle index.
struct LegacyDynamicBspMesh final {
    assets::MissionWorldSpatialArena localArena;
    std::vector<std::uint32_t> polygonMaterialReferences;
    float localBoundingRadius{};
    std::uint64_t retainedPayloadBytes{};
    std::vector<LegacyDynamicBspBuildIssue> issues;

    [[nodiscard]] bool complete() const noexcept;
};

// Reconstructs CcObject::CreateDynamicBsp and
// CcBspPolyList::CreateBspTree for one already converted mesh. Construction is
// a bounded mission-load operation and may allocate. The resulting tree is
// immutable and its line traversal is allocation-free.
[[nodiscard]] LegacyDynamicBspMesh buildLegacyDynamicBsp(
    const ConvertedMeshGeometry& geometry,
    std::span<const LegacyDynamicBspMaterialBinding> materialBindings = {},
    const LegacyDynamicBspBuildLimits& limits = {});

struct LegacyDynamicBspLineObject final {
    std::size_t meshIndex{};
    // Native CcObject object ID. Zero remains meaningful for scene-owned
    // dynamic objects and is not rejected at this geometry boundary.
    std::uint32_t actorObjectId{};
    bool active{true};
    // Confirmed F050 runtime relation: orthonormal object-local-to-runtime
    // linear transform with unit scale, plus absolute runtime translation.
    Mat3 objectLocalToRuntime{};
    Vec3 runtimeTranslation{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyDynamicBspLineObject&,
        const LegacyDynamicBspLineObject&) noexcept = default;
};

enum class MissionWorldRuntimeCombinedLineHitKind : std::uint8_t {
    staticRoom,
    dynamicObject,
};

struct MissionWorldRuntimeCombinedLineHit final {
    MissionWorldRuntimeCombinedLineHitKind kind{
        MissionWorldRuntimeCombinedLineHitKind::staticRoom};
    float legacyFraction{};
    Vec3 runtimePoint{};
    Vec3 runtimePlaneNormal{};
    std::optional<std::uint32_t> materialCollisionMode2152;

    // Static-room provenance.
    std::optional<std::size_t> ownerWorldRoomIndex;
    std::optional<std::size_t> treeIndex;
    std::optional<std::size_t> nodeIndex;
    std::optional<std::size_t> polygonIndex;
    std::optional<std::size_t> portalWorldRoomIndex;

    // Dynamic-object provenance.
    std::optional<std::size_t> dynamicObjectIndex;
    std::optional<std::size_t> dynamicMeshIndex;
    std::optional<std::uint32_t> sourceTriangleIndex;
    std::optional<std::uint32_t> sourceMaterialReference;
    std::uint32_t actorObjectId{};
    bool reverseFacing{};
    bool withinRequestedSegment{};

    [[nodiscard]] friend constexpr bool operator==(
        const MissionWorldRuntimeCombinedLineHit&,
        const MissionWorldRuntimeCombinedLineHit&) noexcept = default;
};

enum class MissionWorldRuntimeCombinedLineTraceStatus : std::uint8_t {
    noHit,
    hit,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    invalidBasis,
    invalidDynamicMesh,
    invalidDynamicObject,
    dynamicObjectLimitExceeded,
    traversalDepthExceeded,
    outOfSegmentHit,
};

struct MissionWorldRuntimeCombinedLineTraceResult final {
    MissionWorldRuntimeCombinedLineTraceStatus status{
        MissionWorldRuntimeCombinedLineTraceStatus::noHit};
    std::optional<MissionWorldRuntimeCombinedLineHit> hit;

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                MissionWorldRuntimeCombinedLineTraceStatus::noHit ||
            status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit;
    }
};

struct MissionWorldRuntimeCombinedLineTraceOptions final {
    assets::MissionWorldSpatialLineTraceOptions staticTrace{};
    std::size_t maximumDynamicObjects{65'536U};
};

// Reconstructs the non-portal portion of PhLine::GetBspCollision: room-static
// trees first, then caller-supplied active room objects in native list order,
// all competing through one strict-nearest fraction. Exact ties retain the
// static hit or earlier dynamic object. The caller still owns room membership,
// actor lifetime, and bounded portal continuation.
//
// This runtime function is read-only, allocation-free, and noexcept.
[[nodiscard]] MissionWorldRuntimeCombinedLineTraceResult
traceMissionWorldRuntimeCombinedLine(
    const assets::MissionWorldSpatialArena& staticArena,
    const BasisTransform& runtimeBasis,
    std::size_t worldRoomIndex,
    const Vec3& runtimeStart,
    const Vec3& runtimeEnd,
    std::span<const LegacyDynamicBspMesh> dynamicMeshes,
    std::span<const LegacyDynamicBspLineObject> dynamicObjects,
    const MissionWorldRuntimeCombinedLineTraceOptions& options = {}) noexcept;

} // namespace airfix::render
