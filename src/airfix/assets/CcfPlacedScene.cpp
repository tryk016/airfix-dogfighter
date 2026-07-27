#include "airfix/assets/CcfPlacedScene.hpp"

#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace airfix::assets {
namespace {

template <typename Target>
struct ReferenceMatch {
    Target first{};
    std::size_t count{};
};

template <typename Target>
bool addReference(
    std::unordered_map<std::uint32_t, ReferenceMatch<Target>>& references,
    const std::uint32_t reference,
    const Target& target) {
    const auto [iterator, inserted] = references.emplace(
        reference, ReferenceMatch<Target>{.first = target, .count = 1U});
    if (!inserted) {
        ++iterator->second.count;
        return iterator->second.count == 2U;
    }
    return false;
}

void addIssue(
    std::vector<PlacedSceneIssue>& issues,
    const PlacedSceneIssueKind kind,
    const std::optional<std::size_t> index = std::nullopt,
    const std::optional<std::uint32_t> reference = std::nullopt) {
    issues.push_back({
        .kind = kind,
        .placedNodeIndex = index,
        .reference = reference,
    });
}

void clearResolvedGraph(ResolvedPlacedScene& scene) {
    scene.rootIndices.clear();
    for (auto& node : scene.nodes) {
        node.parentTarget.reset();
        node.childIndices.clear();
    }
    for (auto& children : scene.meshChildIndices) {
        children.clear();
    }
}

template <typename Metadata>
auto buildCatalogReferences(
    const std::vector<Metadata>& records,
    std::vector<PlacedSceneIssue>& issues,
    const PlacedSceneIssueKind duplicateKind) {
    std::unordered_map<std::uint32_t, ReferenceMatch<std::size_t>> references;
    references.reserve(records.size());
    for (std::size_t index = 0U; index < records.size(); ++index) {
        const auto reference = records[index].reference;
        const auto found = references.find(reference);
        if (found == references.end()) {
            references.emplace(
                reference,
                ReferenceMatch<std::size_t>{.first = index, .count = 1U});
            continue;
        }
        if (found->second.count == 1U) {
            addIssue(issues, duplicateKind, std::nullopt, reference);
        }
        ++found->second.count;
    }
    return references;
}

using CatalogReferences =
    std::unordered_map<std::uint32_t, ReferenceMatch<std::size_t>>;

PlacedRoomTarget resolveRoom(
    const CatalogReferences& rooms,
    const std::uint32_t reference,
    const std::size_t nodeIndex,
    std::vector<PlacedSceneIssue>& issues) {
    const auto match = rooms.find(reference);
    if (match == rooms.end()) {
        return {
            .kind = PlacedRoomTargetKind::externalReceiverFallback,
            .roomIndex = std::nullopt,
        };
    }
    if (match->second.count != 1U) {
        addIssue(
            issues,
            PlacedSceneIssueKind::ambiguousRoomReference,
            nodeIndex,
            reference);
        return {
            .kind = PlacedRoomTargetKind::unresolvedAmbiguous,
            .roomIndex = std::nullopt,
        };
    }
    return {
        .kind = PlacedRoomTargetKind::parsedRoom,
        .roomIndex = match->second.first,
    };
}

PlacedMeshTarget resolveMesh(
    const CatalogReferences& meshes,
    const std::uint32_t reference,
    const std::size_t nodeIndex,
    std::vector<PlacedSceneIssue>& issues) {
    const auto match = meshes.find(reference);
    if (match == meshes.end()) {
        addIssue(
            issues,
            PlacedSceneIssueKind::missingMesh,
            nodeIndex,
            reference);
        return {
            .kind = PlacedMeshTargetKind::unresolvedMissing,
            .meshIndex = std::nullopt,
        };
    }
    if (match->second.count != 1U) {
        addIssue(
            issues,
            PlacedSceneIssueKind::ambiguousMeshReference,
            nodeIndex,
            reference);
        return {
            .kind = PlacedMeshTargetKind::unresolvedAmbiguous,
            .meshIndex = std::nullopt,
        };
    }
    return {
        .kind = PlacedMeshTargetKind::parsedMesh,
        .meshIndex = match->second.first,
    };
}

PlacedPortalRoomTarget resolvePortalRoom(
    const CatalogReferences& rooms,
    const std::uint32_t reference,
    const std::size_t nodeIndex,
    std::vector<PlacedSceneIssue>& issues) {
    const auto match = rooms.find(reference);
    if (match == rooms.end()) {
        return {
            .kind = PlacedPortalRoomTargetKind::notSet,
            .roomIndex = std::nullopt,
        };
    }
    if (match->second.count != 1U) {
        addIssue(
            issues,
            PlacedSceneIssueKind::ambiguousPortalRoomReference,
            nodeIndex,
            reference);
        return {
            .kind = PlacedPortalRoomTargetKind::unresolvedAmbiguous,
            .roomIndex = std::nullopt,
        };
    }
    return {
        .kind = PlacedPortalRoomTargetKind::parsedRoom,
        .roomIndex = match->second.first,
    };
}

} // namespace

ResolvedPlacedScene resolvePlacedScene(
    const CcfMetadata& ccf,
    const PlacedSceneLimits& limits) {
    ResolvedPlacedScene result;
    if (ccf.placedNodes.size() > limits.maximumPlacedNodes ||
        ccf.rooms.size() > limits.maximumRooms ||
        ccf.meshes.size() > limits.maximumMeshes ||
        ccf.meshes.size() >
            std::numeric_limits<std::size_t>::max() -
                ccf.placedNodes.size()) {
        addIssue(result.issues, PlacedSceneIssueKind::limitExceeded);
        return result;
    }

    const auto rooms = buildCatalogReferences(
        ccf.rooms,
        result.issues,
        PlacedSceneIssueKind::duplicateRoomReference);
    const auto meshes = buildCatalogReferences(
        ccf.meshes,
        result.issues,
        PlacedSceneIssueKind::duplicateMeshReference);

    result.nodes.reserve(ccf.placedNodes.size());
    result.rootIndices.reserve(ccf.placedNodes.size());
    result.meshChildIndices.resize(ccf.meshes.size());
    for (std::size_t index = 0U; index < ccf.placedNodes.size(); ++index) {
        const auto& source = ccf.placedNodes[index];
        ResolvedPlacedNode node{
            .placedNodeIndex = index,
            .instantiated = false,
            .parentTarget = std::nullopt,
            .childIndices = {},
            .roomTarget = {},
            .meshTarget = {},
            .portalRoomTarget = {},
        };

        switch (source.kind) {
        case CcfPlacedNodeKind::object: {
            const auto* object = std::get_if<CcfPlacedObjectMetadata>(
                &source.data);
            if (object == nullptr) {
                addIssue(
                    result.issues,
                    PlacedSceneIssueKind::invalidNodeData,
                    index,
                    source.currentReference);
                break;
            }
            node.meshTarget = resolveMesh(
                meshes, object->meshReference, index, result.issues);
            if (node.meshTarget.kind != PlacedMeshTargetKind::parsedMesh) {
                break;
            }
            node.instantiated = true;
            node.roomTarget = resolveRoom(
                rooms, source.roomReference, index, result.issues);
            node.portalRoomTarget = resolvePortalRoom(
                rooms,
                object->portalRoomReference,
                index,
                result.issues);
            break;
        }
        case CcfPlacedNodeKind::nullNode:
            if (!std::holds_alternative<CcfPlacedNullMetadata>(source.data)) {
                addIssue(
                    result.issues,
                    PlacedSceneIssueKind::invalidNodeData,
                    index,
                    source.currentReference);
                break;
            }
            node.instantiated = true;
            node.roomTarget = resolveRoom(
                rooms, source.roomReference, index, result.issues);
            break;
        case CcfPlacedNodeKind::light:
            if (!std::holds_alternative<CcfPlacedLightMetadata>(source.data)) {
                addIssue(
                    result.issues,
                    PlacedSceneIssueKind::invalidNodeData,
                    index,
                    source.currentReference);
                break;
            }
            node.instantiated = true;
            node.roomTarget = resolveRoom(
                rooms, source.roomReference, index, result.issues);
            break;
        default:
            addIssue(
                result.issues,
                PlacedSceneIssueKind::invalidNodeData,
                index,
                source.currentReference);
            break;
        }
        result.nodes.push_back(std::move(node));
    }

    // The original lookup covers all SRT node domains. Objects rejected above
    // are deliberately absent because the loader never instantiates them or
    // records their pending parent attachment.
    std::unordered_map<std::uint32_t, ReferenceMatch<PlacedParentTarget>>
        activeReferences;
    activeReferences.reserve(ccf.placedNodes.size() + ccf.meshes.size());
    bool graphInvalid = false;
    for (std::size_t index = 0U; index < result.nodes.size(); ++index) {
        const auto reference = ccf.placedNodes[index].currentReference;
        if (result.nodes[index].instantiated) {
            if (addReference(
                activeReferences,
                reference,
                PlacedParentTarget{
                    .kind = PlacedParentTargetKind::placedNode,
                    .index = index,
                })) {
                addIssue(
                    result.issues,
                    PlacedSceneIssueKind::duplicateSrtReference,
                    std::nullopt,
                    reference);
                graphInvalid = true;
            }
        }
    }
    for (std::size_t index = 0U; index < ccf.meshes.size(); ++index) {
        const auto reference = ccf.meshes[index].reference;
        if (addReference(
                activeReferences,
                reference,
                PlacedParentTarget{
                    .kind = PlacedParentTargetKind::meshPrototype,
                    .index = index,
                })) {
            addIssue(
                result.issues,
                PlacedSceneIssueKind::duplicateSrtReference,
                std::nullopt,
                reference);
            graphInvalid = true;
        }
    }

    std::size_t edgeCount = 0U;
    for (std::size_t index = 0U; index < result.nodes.size(); ++index) {
        auto& node = result.nodes[index];
        const auto& source = ccf.placedNodes[index];
        if (!node.instantiated) {
            continue;
        }
        if (source.parentReference == 0U) {
            result.rootIndices.push_back(index);
            continue;
        }
        if (source.parentReference == source.currentReference) {
            addIssue(
                result.issues,
                PlacedSceneIssueKind::selfParent,
                index,
                source.parentReference);
            graphInvalid = true;
            continue;
        }
        const auto current = activeReferences.find(source.currentReference);
        if (current == activeReferences.end() ||
            current->second.count != 1U ||
            current->second.first.kind != PlacedParentTargetKind::placedNode ||
            current->second.first.index != index) {
            addIssue(
                result.issues,
                PlacedSceneIssueKind::ambiguousChildReference,
                index,
                source.currentReference);
            graphInvalid = true;
            continue;
        }

        const auto parent = activeReferences.find(source.parentReference);
        if (parent == activeReferences.end()) {
            addIssue(
                result.issues,
                PlacedSceneIssueKind::missingParent,
                index,
                source.parentReference);
            graphInvalid = true;
            continue;
        }
        if (parent->second.count != 1U) {
            addIssue(
                result.issues,
                PlacedSceneIssueKind::ambiguousParentReference,
                index,
                source.parentReference);
            graphInvalid = true;
            continue;
        }
        if (edgeCount >= limits.maximumEdges) {
            addIssue(
                result.issues,
                PlacedSceneIssueKind::limitExceeded,
                index,
                source.parentReference);
            graphInvalid = true;
            continue;
        }
        ++edgeCount;
        node.parentTarget = parent->second.first;
        if (node.parentTarget->kind == PlacedParentTargetKind::placedNode) {
            result.nodes[node.parentTarget->index].childIndices.push_back(index);
        }
        else {
            result.meshChildIndices[node.parentTarget->index].push_back(index);
        }
    }

    // Parent chains are walked iteratively. Resolved depth is memoized so a
    // child-before-parent layout remains linear rather than quadratic.
    std::vector<std::uint8_t> color(result.nodes.size(), 0U);
    std::vector<std::optional<std::size_t>> depths(result.nodes.size());
    std::vector<std::size_t> chain;
    chain.reserve(result.nodes.size());
    bool depthLimitReported = false;
    for (std::size_t start = 0U; start < result.nodes.size(); ++start) {
        if (!result.nodes[start].instantiated || color[start] != 0U) {
            continue;
        }

        chain.clear();
        auto current = start;
        bool cycleFound = false;
        bool terminalInChain = false;
        std::size_t terminalDepth = 0U;
        std::optional<std::size_t> inheritedDepth;
        while (true) {
            if (color[current] == 0U) {
                color[current] = 1U;
                chain.push_back(current);
                const auto& parent = result.nodes[current].parentTarget;
                if (!parent.has_value() ||
                    parent->kind != PlacedParentTargetKind::placedNode) {
                    terminalInChain = true;
                    terminalDepth = parent.has_value() ? 1U : 0U;
                    break;
                }
                current = parent->index;
                continue;
            }
            if (color[current] == 1U) {
                addIssue(
                    result.issues,
                    PlacedSceneIssueKind::cycle,
                    current,
                    ccf.placedNodes[current].currentReference);
                graphInvalid = true;
                cycleFound = true;
                break;
            }
            if (depths[current].has_value()) {
                inheritedDepth = *depths[current] + 1U;
            }
            break;
        }

        if (!cycleFound &&
            (terminalInChain || inheritedDepth.has_value())) {
            auto nextDepth =
                terminalInChain ? terminalDepth : *inheritedDepth;
            for (auto iterator = chain.rbegin(); iterator != chain.rend();
                 ++iterator) {
                depths[*iterator] = nextDepth;
                if (!depthLimitReported && nextDepth > limits.maximumDepth) {
                    addIssue(
                        result.issues,
                        PlacedSceneIssueKind::limitExceeded,
                        *iterator,
                        ccf.placedNodes[*iterator].currentReference);
                    graphInvalid = true;
                    depthLimitReported = true;
                }
                ++nextDepth;
            }
        }
        for (const auto index : chain) {
            color[index] = 2U;
        }
    }

    if (graphInvalid) {
        clearResolvedGraph(result);
    }

    return result;
}

} // namespace airfix::assets
