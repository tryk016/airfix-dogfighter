#pragma once

#include "airfix/assets/CcfPlacedScene.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::assets {

enum class RoomSceneIssueKind : std::uint8_t {
    placedSceneDependency,
    invalidTreeStructure,
    ambiguousPlacedObjectReference,
    missingPlacedObject,
    inactivePlacedObject,
    nonObjectPlacedNode,
    missingMeshTarget,
    unresolvedRoomTarget,
    roomMismatch,
    polygonIndexOutOfRange,
    missingPortalRoomTarget,
    limitExceeded,
};

struct RoomSceneIssue {
    RoomSceneIssueKind kind{RoomSceneIssueKind::invalidTreeStructure};
    std::optional<std::size_t> roomIndex;
    std::optional<CcfBspTreeKind> treeKind;
    std::optional<std::size_t> treeIndex;
    std::optional<std::size_t> nodeIndex;
    std::optional<std::size_t> polygonMetadataIndex;
    std::optional<std::uint32_t> reference;
};

struct ResolvedBspPolygonBinding {
    std::size_t roomIndex{};
    CcfBspTreeKind treeKind{CcfBspTreeKind::staticTree};
    // Index in the room's static or portal tree vector, selected by treeKind.
    std::size_t treeIndex{};
    std::size_t nodeIndex{};
    std::size_t polygonMetadataIndex{};
    std::size_t placedNodeIndex{};
    std::size_t meshIndex{};
    std::size_t triangleIndex{};
    // Present only for portal-tree bindings.
    std::optional<std::size_t> portalRoomIndex;

    friend bool operator==(
        const ResolvedBspPolygonBinding&,
        const ResolvedBspPolygonBinding&) = default;
};

struct RoomSceneLimits {
    std::size_t maximumRooms{100'000U};
    std::size_t maximumTrees{100'000U};
    std::size_t maximumNodes{100'000U};
    std::size_t maximumBindings{250'000U};
    std::size_t maximumDepth{1'024U};
};

struct ResolvedCcfRoomScene {
    // Physical room, static/portal tree, node-arena, and node-polygon order.
    // Bindings refer to existing metadata and never copy proprietary geometry.
    std::vector<ResolvedBspPolygonBinding> bindings;
    std::vector<RoomSceneIssue> issues;
};

// Resolves the room BSP's deferred polygon -> placed object -> mesh triangle
// links. The placed scene is resolved internally from the same immutable CCF,
// preventing a stale or foreign resolution from publishing incorrect indices.
// Any dependency, structure, ambiguity, or limit failure clears all bindings
// atomically.
[[nodiscard]] ResolvedCcfRoomScene resolveCcfRoomScene(
    const CcfMetadata& ccf,
    const RoomSceneLimits& limits = {});

} // namespace airfix::assets
