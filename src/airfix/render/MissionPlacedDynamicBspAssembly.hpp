#pragma once

#include "airfix/assets/CcfPlacedScene.hpp"
#include "airfix/assets/MissionWorldRooms.hpp"
#include "airfix/render/LegacyDynamicBsp.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class MissionPlacedDynamicBspIssueKind : std::uint8_t {
    catalogIncomplete,
    catalogMismatch,
    sourceCountMismatch,
    invalidSourceMetadata,
    unsupportedSourceFlags,
    invalidContributor,
    duplicateContributor,
    missingContributor,
    placedSceneFailure,
    invalidPlacedNode,
    invalidRoomTarget,
    invalidMeshTarget,
    invalidPortalTarget,
    invalidPortalType,
    invalidTransform,
    geometryFailure,
    invalidTreeKind,
    invalidTreeStructure,
    invalidSpatialValue,
    placedObjectReferenceMismatch,
    polygonIndexOutOfRange,
    limitExceeded,
    retainedByteLimitExceeded,
    integerOverflow,
    allocationFailure,
};

struct MissionPlacedDynamicBspIssue final {
    MissionPlacedDynamicBspIssueKind kind{
        MissionPlacedDynamicBspIssueKind::invalidSourceMetadata};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> placedNodeIndex;
    std::optional<std::size_t> physicalMeshIndex;
    std::optional<std::size_t> physicalRoomIndex;
    std::optional<std::size_t> worldRoomIndex;
    std::optional<std::size_t> treeIndex;
    std::optional<std::size_t> nodeIndex;
    std::optional<std::size_t> polygonMetadataIndex;
    std::optional<std::uint32_t> reference;
    std::optional<assets::PlacedSceneIssueKind> placedSceneIssue;
    std::optional<GeometryErrorCode> geometryError;
};

struct MissionPlacedDynamicBspMeshProvenance final {
    std::size_t sourceIndex{};
    std::size_t physicalMeshIndex{};
    std::size_t firstPlacedNodeIndex{};
    std::uint32_t sourceMeshReference{};

    [[nodiscard]] friend constexpr bool operator==(
        const MissionPlacedDynamicBspMeshProvenance&,
        const MissionPlacedDynamicBspMeshProvenance&) noexcept = default;
};

struct MissionPlacedDynamicBspObjectProvenance final {
    std::size_t sourceIndex{};
    std::size_t placedNodeIndex{};
    std::size_t physicalMeshIndex{};
    std::size_t worldRoomIndex{};
    std::uint32_t sourceNodeReference{};

    [[nodiscard]] friend constexpr bool operator==(
        const MissionPlacedDynamicBspObjectProvenance&,
        const MissionPlacedDynamicBspObjectProvenance&) noexcept = default;
};

struct MissionPlacedDynamicBspLimits final {
    std::size_t maximumSources{65'536U};
    std::size_t maximumWorldRooms{100'000U};
    std::size_t maximumPhysicalRooms{262'144U};
    std::size_t maximumScannedMeshes{262'144U};
    std::size_t maximumScannedPlacedNodes{1'000'000U};
    std::size_t maximumScannedMaterials{262'144U};
    std::size_t maximumMeshes{65'536U};
    std::size_t maximumObjects{1'000'000U};
    std::size_t maximumTrees{262'144U};
    std::size_t maximumNodes{1'000'000U};
    std::size_t maximumPolygons{2'000'000U};
    std::size_t maximumDepth{
        assets::kMissionWorldSpatialMaximumTraceDepth};
    // Complete retained output: outer records plus every nested dynamic-mesh
    // arena. Allocator capacity and transient resolver/conversion memory are
    // intentionally excluded.
    std::uint64_t maximumRetainedBytes{512U * 1024U * 1024U};
    assets::MissionWorldRoomBuildLimits catalogAuthentication{};
    assets::PlacedSceneLimits placedScenePerSource{};
    GeometryLimits geometryPerMesh{};
};

struct MissionPlacedDynamicBspAssembly final {
    // One immutable object-local BSP per source-local physical mesh, in the
    // first serialized 0x4101 use order observed by CcRoom::LoadSceneCcf.
    std::vector<LegacyDynamicBspMesh> meshes;
    std::vector<MissionPlacedDynamicBspMeshProvenance> meshProvenance;
    // Flat native room-list order. roomObjectRanges is parallel to the
    // authenticated MissionWorldRoomCatalog::rooms vector.
    std::vector<LegacyDynamicBspLineObject> objects;
    std::vector<MissionPlacedDynamicBspObjectProvenance> objectProvenance;
    std::vector<LegacyDynamicBspRoomObjectRange> roomObjectRanges;
    std::uint64_t retainedPayloadBytes{};
    std::vector<MissionPlacedDynamicBspIssue> issues;

    [[nodiscard]] bool complete() const noexcept;
};

// Material-binds the already parsed F0C0 trees carried by placed-object
// wrapper 0x4101 and publishes exactly the room-list shape observed in the
// native loader. The first object to populate a source-local mesh cache is
// unlinked/relinked and therefore prepended to its room's dynamic list. A
// later 0x4101 object reuses the cached pointer but is not relinked by the
// native load path, so it intentionally contributes no room-list record.
//
// Placed F050 values are authored world transforms. This boundary accepts only
// unit-scale orthonormal transforms and an orthonormal coordinate basis because
// LegacyDynamicBspLineObject deliberately has no scale/shear representation.
// The operation is bounded, may allocate during mission loading, and clears
// every publishable vector atomically on failure.
[[nodiscard]] MissionPlacedDynamicBspAssembly
buildMissionPlacedDynamicBspAssembly(
    std::span<const assets::MissionCcfRoomLoadSource> sources,
    const assets::MissionWorldRoomCatalog& catalog,
    const BasisTransform& basis = {},
    const MissionPlacedDynamicBspLimits& limits = {});

} // namespace airfix::render
