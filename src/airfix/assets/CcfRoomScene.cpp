#include "airfix/assets/CcfRoomScene.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace airfix::assets {
namespace {

struct ReferenceMatch {
    std::size_t first{};
    std::size_t count{};
};

void addIssue(
    ResolvedCcfRoomScene& scene,
    const RoomSceneIssueKind kind,
    const std::optional<std::size_t> roomIndex = std::nullopt,
    const std::optional<CcfBspTreeKind> treeKind = std::nullopt,
    const std::optional<std::size_t> treeIndex = std::nullopt,
    const std::optional<std::size_t> nodeIndex = std::nullopt,
    const std::optional<std::size_t> polygonMetadataIndex = std::nullopt,
    const std::optional<std::uint32_t> reference = std::nullopt) {
    scene.issues.push_back({
        .kind = kind,
        .roomIndex = roomIndex,
        .treeKind = treeKind,
        .treeIndex = treeIndex,
        .nodeIndex = nodeIndex,
        .polygonMetadataIndex = polygonMetadataIndex,
        .reference = reference,
    });
}

bool addWithinLimit(
    std::size_t& total,
    const std::size_t addition,
    const std::size_t limit) {
    if (total > limit || addition > limit - total) {
        return false;
    }
    total += addition;
    return true;
}

bool validateTreeStructure(
    const CcfBspTreeMetadata& tree,
    const std::size_t roomIndex,
    const std::size_t treeIndex,
    const RoomSceneLimits& limits,
    ResolvedCcfRoomScene& result) {
    if (tree.nodes.empty() || tree.rootNodeIndex >= tree.nodes.size()) {
        addIssue(
            result,
            RoomSceneIssueKind::invalidTreeStructure,
            roomIndex,
            tree.kind,
            treeIndex);
        return false;
    }

    std::vector<std::uint8_t> visited(tree.nodes.size(), 0U);
    std::vector<std::uint8_t> polygonOwners(tree.polygons.size(), 0U);
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    stack.reserve(tree.nodes.size());
    stack.emplace_back(tree.rootNodeIndex, 1U);
    std::size_t visitedCount = 0U;
    bool valid = true;

    while (!stack.empty()) {
        const auto [nodeIndex, depth] = stack.back();
        stack.pop_back();
        if (nodeIndex >= tree.nodes.size() || visited[nodeIndex] != 0U ||
            depth > limits.maximumDepth) {
            addIssue(
                result,
                depth > limits.maximumDepth
                    ? RoomSceneIssueKind::limitExceeded
                    : RoomSceneIssueKind::invalidTreeStructure,
                roomIndex,
                tree.kind,
                treeIndex,
                nodeIndex);
            valid = false;
            continue;
        }
        visited[nodeIndex] = 1U;
        ++visitedCount;

        const auto& node = tree.nodes[nodeIndex];
        const auto childAExpected = node.childAPresenceRaw != 0U;
        const auto childBExpected = node.childBPresenceRaw != 0U;
        if (childAExpected != node.childAIndex.has_value() ||
            childBExpected != node.childBIndex.has_value()) {
            addIssue(
                result,
                RoomSceneIssueKind::invalidTreeStructure,
                roomIndex,
                tree.kind,
                treeIndex,
                nodeIndex);
            valid = false;
        }

        for (const auto polygonIndex : node.polygonIndices) {
            if (polygonIndex >= tree.polygons.size()) {
                addIssue(
                    result,
                    RoomSceneIssueKind::invalidTreeStructure,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonIndex);
                valid = false;
            }
            else if (polygonOwners[polygonIndex] != 0U) {
                addIssue(
                    result,
                    RoomSceneIssueKind::invalidTreeStructure,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonIndex);
                valid = false;
            }
            else {
                polygonOwners[polygonIndex] = 1U;
            }
        }

        // Push B before A so the bounded validation walk follows physical
        // child-A then child-B preorder. Binding output itself follows arenas.
        if (node.childBIndex.has_value()) {
            stack.emplace_back(*node.childBIndex, depth + 1U);
        }
        if (node.childAIndex.has_value()) {
            stack.emplace_back(*node.childAIndex, depth + 1U);
        }
    }

    if (visitedCount != tree.nodes.size()) {
        addIssue(
            result,
            RoomSceneIssueKind::invalidTreeStructure,
            roomIndex,
            tree.kind,
            treeIndex);
        valid = false;
    }
    if (std::ranges::any_of(
            polygonOwners,
            [](const auto ownerCount) { return ownerCount != 1U; })) {
        addIssue(
            result,
            RoomSceneIssueKind::invalidTreeStructure,
            roomIndex,
            tree.kind,
            treeIndex);
        valid = false;
    }
    return valid;
}

using References =
    std::unordered_map<std::uint32_t, ReferenceMatch>;

template <typename Range, typename Reference>
[[nodiscard]] References buildReferences(
    const Range& values,
    Reference referenceOf) {
    References references;
    references.reserve(values.size());
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const auto reference = referenceOf(values[index]);
        const auto [iterator, inserted] =
            references.emplace(reference, ReferenceMatch{index, 1U});
        if (!inserted) {
            ++iterator->second.count;
        }
    }
    return references;
}

void resolveTreeBindings(
    const CcfMetadata& ccf,
    const ResolvedPlacedScene& placedScene,
    const References& placedReferences,
    const References& materialReferences,
    const std::size_t roomIndex,
    const std::size_t treeIndex,
    const CcfBspTreeMetadata& tree,
    ResolvedCcfRoomScene& result) {
    for (std::size_t nodeIndex = 0U; nodeIndex < tree.nodes.size();
         ++nodeIndex) {
        const auto& node = tree.nodes[nodeIndex];
        for (const auto polygonMetadataIndex : node.polygonIndices) {
            if (polygonMetadataIndex >= tree.polygons.size()) {
                continue;
            }
            const auto& polygon = tree.polygons[polygonMetadataIndex];
            const auto match =
                placedReferences.find(polygon.placedObjectReference);
            if (match == placedReferences.end()) {
                addIssue(
                    result,
                    RoomSceneIssueKind::missingPlacedObject,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }
            if (match->second.count != 1U) {
                addIssue(
                    result,
                    RoomSceneIssueKind::ambiguousPlacedObjectReference,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }

            const auto placedNodeIndex = match->second.first;
            if (placedNodeIndex >= placedScene.nodes.size() ||
                !placedScene.nodes[placedNodeIndex].instantiated) {
                addIssue(
                    result,
                    RoomSceneIssueKind::inactivePlacedObject,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }
            const auto& source = ccf.placedNodes[placedNodeIndex];
            const auto* object =
                std::get_if<CcfPlacedObjectMetadata>(&source.data);
            if (source.kind != CcfPlacedNodeKind::object ||
                object == nullptr) {
                addIssue(
                    result,
                    RoomSceneIssueKind::nonObjectPlacedNode,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }

            const auto& resolvedNode = placedScene.nodes[placedNodeIndex];
            if (resolvedNode.meshTarget.kind !=
                    PlacedMeshTargetKind::parsedMesh ||
                !resolvedNode.meshTarget.meshIndex.has_value() ||
                *resolvedNode.meshTarget.meshIndex >= ccf.meshes.size()) {
                addIssue(
                    result,
                    RoomSceneIssueKind::missingMeshTarget,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }
            if (resolvedNode.roomTarget.kind !=
                    PlacedRoomTargetKind::parsedRoom ||
                !resolvedNode.roomTarget.roomIndex.has_value()) {
                addIssue(
                    result,
                    RoomSceneIssueKind::unresolvedRoomTarget,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }
            if (*resolvedNode.roomTarget.roomIndex != roomIndex) {
                addIssue(
                    result,
                    RoomSceneIssueKind::roomMismatch,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }

            const auto meshIndex = *resolvedNode.meshTarget.meshIndex;
            if (polygon.polygonIndex >=
                ccf.meshes[meshIndex].triangles.size()) {
                addIssue(
                    result,
                    RoomSceneIssueKind::polygonIndexOutOfRange,
                    roomIndex,
                    tree.kind,
                    treeIndex,
                    nodeIndex,
                    polygonMetadataIndex,
                    polygon.placedObjectReference);
                continue;
            }
            const auto triangleIndex =
                static_cast<std::size_t>(polygon.polygonIndex);
            const auto materialReference =
                ccf.meshes[meshIndex].triangles[triangleIndex]
                    .materialReference;
            std::optional<std::size_t> materialIndex;
            const auto materialMatch =
                materialReferences.find(materialReference);
            if (materialMatch != materialReferences.end() &&
                materialMatch->second.count == 1U) {
                materialIndex = materialMatch->second.first;
            }

            std::optional<std::size_t> portalRoomIndex;
            if (tree.kind == CcfBspTreeKind::portalTree) {
                if (resolvedNode.portalRoomTarget.kind !=
                        PlacedPortalRoomTargetKind::parsedRoom ||
                    !resolvedNode.portalRoomTarget.roomIndex.has_value() ||
                    *resolvedNode.portalRoomTarget.roomIndex >=
                        ccf.rooms.size()) {
                    addIssue(
                        result,
                        RoomSceneIssueKind::missingPortalRoomTarget,
                        roomIndex,
                        tree.kind,
                        treeIndex,
                        nodeIndex,
                        polygonMetadataIndex,
                        polygon.placedObjectReference);
                    continue;
                }
                portalRoomIndex =
                    *resolvedNode.portalRoomTarget.roomIndex;
            }

            result.bindings.push_back({
                .roomIndex = roomIndex,
                .treeKind = tree.kind,
                .treeIndex = treeIndex,
                .nodeIndex = nodeIndex,
                .polygonMetadataIndex = polygonMetadataIndex,
                .placedNodeIndex = placedNodeIndex,
                .meshIndex = meshIndex,
                .triangleIndex = triangleIndex,
                .materialIndex = materialIndex,
                .portalRoomIndex = portalRoomIndex,
            });
        }
    }
}

} // namespace

ResolvedCcfRoomScene resolveCcfRoomScene(
    const CcfMetadata& ccf,
    const RoomSceneLimits& limits) {
    ResolvedCcfRoomScene result;
    if (ccf.rooms.size() > limits.maximumRooms) {
        addIssue(result, RoomSceneIssueKind::limitExceeded);
        return result;
    }
    const auto placedScene = resolvePlacedScene(ccf);
    if (!placedScene.issues.empty() ||
        placedScene.nodes.size() != ccf.placedNodes.size()) {
        addIssue(result, RoomSceneIssueKind::placedSceneDependency);
        return result;
    }

    std::size_t treeCount = 0U;
    std::size_t nodeCount = 0U;
    std::size_t polygonCount = 0U;
    std::size_t bindingCount = 0U;
    bool shapeWithinLimits = true;
    for (const auto& room : ccf.rooms) {
        if (!addWithinLimit(
                treeCount,
                room.staticBspTrees.size(),
                limits.maximumTrees) ||
            !addWithinLimit(
                treeCount,
                room.portalBspTrees.size(),
                limits.maximumTrees)) {
            shapeWithinLimits = false;
            break;
        }
        for (const auto* trees :
             {&room.staticBspTrees, &room.portalBspTrees}) {
            for (const auto& tree : *trees) {
                if (!addWithinLimit(
                        nodeCount,
                        tree.nodes.size(),
                        limits.maximumNodes) ||
                    !addWithinLimit(
                        polygonCount,
                        tree.polygons.size(),
                        limits.maximumBindings)) {
                    shapeWithinLimits = false;
                    break;
                }
                for (const auto& node : tree.nodes) {
                    if (!addWithinLimit(
                            bindingCount,
                            node.polygonIndices.size(),
                            limits.maximumBindings)) {
                        shapeWithinLimits = false;
                        break;
                    }
                }
                if (!shapeWithinLimits) {
                    break;
                }
            }
            if (!shapeWithinLimits) {
                break;
            }
        }
        if (!shapeWithinLimits) {
            break;
        }
    }
    if (!shapeWithinLimits) {
        addIssue(result, RoomSceneIssueKind::limitExceeded);
        return result;
    }

    result.bindings.reserve(bindingCount);
    const auto placedReferences = buildReferences(
        ccf.placedNodes,
        [](const auto& node) { return node.currentReference; });
    const auto materialReferences = buildReferences(
        ccf.materials,
        [](const auto& material) { return material.reference; });
    for (std::size_t roomIndex = 0U; roomIndex < ccf.rooms.size();
         ++roomIndex) {
        const auto& room = ccf.rooms[roomIndex];
        for (std::size_t treeIndex = 0U;
             treeIndex < room.staticBspTrees.size();
             ++treeIndex) {
            const auto& tree = room.staticBspTrees[treeIndex];
            if (tree.kind != CcfBspTreeKind::staticTree) {
                addIssue(
                    result,
                    RoomSceneIssueKind::invalidTreeStructure,
                    roomIndex,
                    tree.kind,
                    treeIndex);
                continue;
            }
            if (validateTreeStructure(
                    tree, roomIndex, treeIndex, limits, result)) {
                resolveTreeBindings(
                    ccf,
                    placedScene,
                    placedReferences,
                    materialReferences,
                    roomIndex,
                    treeIndex,
                    tree,
                    result);
            }
        }
        for (std::size_t treeIndex = 0U;
             treeIndex < room.portalBspTrees.size();
             ++treeIndex) {
            const auto& tree = room.portalBspTrees[treeIndex];
            if (tree.kind != CcfBspTreeKind::portalTree) {
                addIssue(
                    result,
                    RoomSceneIssueKind::invalidTreeStructure,
                    roomIndex,
                    tree.kind,
                    treeIndex);
                continue;
            }
            if (validateTreeStructure(
                    tree, roomIndex, treeIndex, limits, result)) {
                resolveTreeBindings(
                    ccf,
                    placedScene,
                    placedReferences,
                    materialReferences,
                    roomIndex,
                    treeIndex,
                    tree,
                    result);
            }
        }
    }

    if (!result.issues.empty()) {
        result.bindings.clear();
    }
    return result;
}

} // namespace airfix::assets
