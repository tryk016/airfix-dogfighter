#pragma once

#include "airfix/assets/LegacyFormats.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::assets {

enum class PlacedSceneIssueKind : std::uint8_t {
    duplicateSrtReference,
    ambiguousChildReference,
    missingParent,
    ambiguousParentReference,
    selfParent,
    cycle,
    duplicateRoomReference,
    ambiguousRoomReference,
    duplicateMeshReference,
    missingMesh,
    ambiguousMeshReference,
    ambiguousPortalRoomReference,
    invalidNodeData,
    limitExceeded,
};

struct PlacedSceneIssue {
    PlacedSceneIssueKind kind{PlacedSceneIssueKind::missingParent};
    std::optional<std::size_t> placedNodeIndex;
    std::optional<std::uint32_t> reference;
};

enum class PlacedParentTargetKind : std::uint8_t {
    placedNode,
    meshPrototype,
};

struct PlacedParentTarget {
    PlacedParentTargetKind kind{PlacedParentTargetKind::placedNode};
    std::size_t index{};

    friend bool operator==(
        const PlacedParentTarget&,
        const PlacedParentTarget&) = default;
};

enum class PlacedRoomTargetKind : std::uint8_t {
    parsedRoom,
    externalReceiverFallback,
    unresolvedAmbiguous,
};

struct PlacedRoomTarget {
    PlacedRoomTargetKind kind{PlacedRoomTargetKind::externalReceiverFallback};
    std::optional<std::size_t> roomIndex;
};

enum class PlacedMeshTargetKind : std::uint8_t {
    notApplicable,
    parsedMesh,
    unresolvedMissing,
    unresolvedAmbiguous,
};

struct PlacedMeshTarget {
    PlacedMeshTargetKind kind{PlacedMeshTargetKind::notApplicable};
    std::optional<std::size_t> meshIndex;
};

enum class PlacedPortalRoomTargetKind : std::uint8_t {
    notApplicable,
    notSet,
    parsedRoom,
    unresolvedAmbiguous,
};

struct PlacedPortalRoomTarget {
    PlacedPortalRoomTargetKind kind{
        PlacedPortalRoomTargetKind::notApplicable};
    std::optional<std::size_t> roomIndex;
};

struct ResolvedPlacedNode {
    std::size_t placedNodeIndex{};
    // The legacy loader creates nulls and lights immediately, but skips an
    // object whose mesh reference cannot be resolved uniquely.
    bool instantiated{};
    std::optional<PlacedParentTarget> parentTarget;
    // Only placed-node parents own entries here. Order remains physical CCF
    // order even when a child precedes its parent in the file.
    std::vector<std::size_t> childIndices;
    PlacedRoomTarget roomTarget;
    PlacedMeshTarget meshTarget;
    PlacedPortalRoomTarget portalRoomTarget;
};

struct PlacedSceneLimits {
    std::size_t maximumPlacedNodes{100'000U};
    std::size_t maximumRooms{100'000U};
    std::size_t maximumMeshes{100'000U};
    std::size_t maximumEdges{100'000U};
    std::size_t maximumDepth{1'024U};
};

struct ResolvedPlacedScene {
    // Nodes remain parallel to CcfMetadata::placedNodes.
    std::vector<ResolvedPlacedNode> nodes;
    // Instantiated nodes authored with parentReference == 0, in physical order.
    std::vector<std::size_t> rootIndices;
    // Lists are parallel to CcfMetadata::meshes. Each list retains physical
    // placed-node order for children attached to that loaded mesh prototype.
    std::vector<std::vector<std::size_t>> meshChildIndices;
    std::vector<PlacedSceneIssue> issues;
};

// Resolves the independent 0x4000 scene after the complete CCF is available.
// Parent lookup intentionally spans instantiated placed SRT nodes and the
// loaded mesh prototypes in CcfMetadata::meshes, matching the legacy global
// SRT reference lookup. Other 0x3000 blueprint kinds are not loaded SRT nodes.
[[nodiscard]] ResolvedPlacedScene resolvePlacedScene(
    const CcfMetadata& ccf,
    const PlacedSceneLimits& limits = {});

} // namespace airfix::assets
