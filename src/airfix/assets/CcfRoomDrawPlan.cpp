#include "airfix/assets/CcfRoomDrawPlan.hpp"

#include "airfix/assets/CcfPlacedScene.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace airfix::assets {
namespace {

void addIssue(
    CcfRoomDrawPlan& result,
    const CcfRoomDrawPlanIssueKind kind,
    const std::optional<std::size_t> placedNodeIndex = std::nullopt,
    const std::optional<std::size_t> meshIndex = std::nullopt,
    const std::optional<std::uint32_t> reference = std::nullopt,
    const std::optional<std::size_t> requestedRoomIndex = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .placedNodeIndex = placedNodeIndex,
        .meshIndex = meshIndex,
        .reference = reference,
        .requestedRoomIndex = requestedRoomIndex,
    });
}

void failClosed(CcfRoomDrawPlan& result) {
    result.roomIndex.reset();
    result.placedNodeIndices.clear();
    result.meshIndices.clear();
    result.materialIndices.clear();
    result.textures.clear();
}

struct MaterialMatch {
    std::size_t index{};
    bool ambiguous{};
};

} // namespace

CcfRoomDrawPlan resolveRoomDrawPlan(
    const CcfMetadata& ccf,
    const std::size_t ccfRoomIndex,
    const CcfRoomDrawPlanLimits& limits) {
    CcfRoomDrawPlan result;
    if (ccfRoomIndex >= ccf.rooms.size()) {
        addIssue(
            result,
            CcfRoomDrawPlanIssueKind::roomIndexOutOfRange,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            ccfRoomIndex);
        return result;
    }
    if (!ccf.roomSections.empty()) {
        const auto& section = ccf.roomSections.front();
        if (ccf.roomSections.size() != 1U ||
            section.firstPhysicalRoomIndex != 0U ||
            section.physicalRoomCount != ccf.rooms.size() ||
            !section.firstDirectChildIsRoom) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::
                    unsupportedRoomSectionLayout);
            return result;
        }
    }
    if (!ccf.rooms.front().primaryBinding) {
        addIssue(result, CcfRoomDrawPlanIssueKind::firstRoomNotPrimary);
        return result;
    }
    if (ccf.placedNodes.size() > limits.maximumPlacedNodes ||
        ccf.materials.size() > limits.maximumMaterials) {
        addIssue(result, CcfRoomDrawPlanIssueKind::limitExceeded);
        return result;
    }

    result.roomIndex = ccfRoomIndex;
    const auto selectedRoomIsReceiver = ccfRoomIndex == 0U;
    const auto placedScene = resolvePlacedScene(ccf);
    if (!placedScene.issues.empty() ||
        placedScene.nodes.size() != ccf.placedNodes.size()) {
        addIssue(result, CcfRoomDrawPlanIssueKind::placedSceneDependency);
        failClosed(result);
        return result;
    }

    result.placedNodeIndices.reserve(std::min(
        ccf.placedNodes.size(), limits.maximumInstances));
    result.meshIndices.reserve(std::min(
        ccf.meshes.size(), limits.maximumUniqueMeshes));
    std::unordered_set<std::size_t> seenMeshes;
    seenMeshes.reserve(std::min(
        ccf.meshes.size(), limits.maximumUniqueMeshes));

    for (std::size_t index = 0U; index < ccf.placedNodes.size(); ++index) {
        const auto& source = ccf.placedNodes[index];
        const auto& node = placedScene.nodes[index];
        if (node.placedNodeIndex != index) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::invalidPlacedNode,
                index);
            failClosed(result);
            return result;
        }

        switch (source.kind) {
        case CcfPlacedNodeKind::nullNode:
            if (!std::holds_alternative<CcfPlacedNullMetadata>(source.data)) {
                addIssue(
                    result,
                    CcfRoomDrawPlanIssueKind::invalidPlacedNode,
                    index);
                failClosed(result);
                return result;
            }
            continue;
        case CcfPlacedNodeKind::light:
            if (!std::holds_alternative<CcfPlacedLightMetadata>(source.data)) {
                addIssue(
                    result,
                    CcfRoomDrawPlanIssueKind::invalidPlacedNode,
                    index);
                failClosed(result);
                return result;
            }
            continue;
        case CcfPlacedNodeKind::object:
            if (!std::holds_alternative<CcfPlacedObjectMetadata>(source.data) ||
                !node.instantiated) {
                addIssue(
                    result,
                    CcfRoomDrawPlanIssueKind::invalidPlacedNode,
                    index);
                failClosed(result);
                return result;
            }
            break;
        default:
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::invalidPlacedNode,
                index);
            failClosed(result);
            return result;
        }

        if (node.roomTarget.kind == PlacedRoomTargetKind::parsedRoom) {
            if (!node.roomTarget.roomIndex.has_value() ||
                *node.roomTarget.roomIndex >= ccf.rooms.size()) {
                addIssue(
                    result,
                    CcfRoomDrawPlanIssueKind::invalidPlacedNode,
                    index);
                failClosed(result);
                return result;
            }
            if (*node.roomTarget.roomIndex != ccfRoomIndex) {
                continue;
            }
        }
        else if (
            node.roomTarget.kind ==
            PlacedRoomTargetKind::externalReceiverFallback) {
            if (!selectedRoomIsReceiver) {
                continue;
            }
        }
        else {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::invalidPlacedNode,
                index);
            failClosed(result);
            return result;
        }

        if (node.meshTarget.kind != PlacedMeshTargetKind::parsedMesh ||
            !node.meshTarget.meshIndex.has_value() ||
            *node.meshTarget.meshIndex >= ccf.meshes.size()) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::invalidMeshIndex,
                index,
                node.meshTarget.meshIndex);
            failClosed(result);
            return result;
        }

        if (result.placedNodeIndices.size() >= limits.maximumInstances) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::limitExceeded,
                index);
            failClosed(result);
            return result;
        }
        const auto meshIndex = *node.meshTarget.meshIndex;
        if (!seenMeshes.contains(meshIndex)) {
            if (result.meshIndices.size() >= limits.maximumUniqueMeshes) {
                addIssue(
                    result,
                    CcfRoomDrawPlanIssueKind::limitExceeded,
                    index,
                    meshIndex);
                failClosed(result);
                return result;
            }
            seenMeshes.insert(meshIndex);
            result.meshIndices.push_back(meshIndex);
        }
        result.placedNodeIndices.push_back(index);
    }

    std::vector<std::uint32_t> materialReferences;
    materialReferences.reserve(std::min(
        limits.maximumMaterialReferences, ccf.materials.size()));
    std::unordered_set<std::uint32_t> seenMaterialReferences;
    seenMaterialReferences.reserve(std::min(
        limits.maximumMaterialReferences, ccf.materials.size()));
    for (const auto meshIndex : result.meshIndices) {
        if (meshIndex >= ccf.meshes.size()) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::invalidMeshIndex,
                std::nullopt,
                meshIndex);
            failClosed(result);
            return result;
        }
        for (const auto& triangle : ccf.meshes[meshIndex].triangles) {
            if (seenMaterialReferences.contains(
                    triangle.materialReference)) {
                continue;
            }
            if (materialReferences.size() >=
                limits.maximumMaterialReferences) {
                addIssue(
                    result,
                    CcfRoomDrawPlanIssueKind::limitExceeded,
                    std::nullopt,
                    meshIndex,
                    triangle.materialReference);
                failClosed(result);
                return result;
            }
            seenMaterialReferences.insert(triangle.materialReference);
            materialReferences.push_back(triangle.materialReference);
        }
    }

    std::unordered_map<std::uint32_t, MaterialMatch> materialsByReference;
    materialsByReference.reserve(ccf.materials.size());
    for (std::size_t index = 0U; index < ccf.materials.size(); ++index) {
        const auto [iterator, inserted] = materialsByReference.emplace(
            ccf.materials[index].reference,
            MaterialMatch{.index = index});
        if (!inserted) {
            iterator->second.ambiguous = true;
        }
    }

    const auto addTexture = [&result, &limits](
        const TextureDependencyRole role,
        const std::uint32_t reference,
        const std::size_t materialIndex,
        const std::optional<std::string>& source) {
        if (!source.has_value()) {
            return true;
        }
        if (result.textures.size() >= limits.maximumTextureEdges) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::limitExceeded,
                std::nullopt,
                std::nullopt,
                reference);
            return false;
        }
        result.textures.push_back({
            .role = role,
            .materialReference = reference,
            .materialIndex = materialIndex,
            .sourceText = *source,
        });
        return true;
    };

    for (const auto reference : materialReferences) {
        const auto match = materialsByReference.find(reference);
        if (match == materialsByReference.end()) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::materialNotFound,
                std::nullopt,
                std::nullopt,
                reference);
            continue;
        }
        if (match->second.ambiguous) {
            addIssue(
                result,
                CcfRoomDrawPlanIssueKind::materialAmbiguous,
                std::nullopt,
                std::nullopt,
                reference);
            continue;
        }

        const auto materialIndex = match->second.index;
        const auto& material = ccf.materials[materialIndex];
        result.materialIndices.push_back(materialIndex);
        if (!addTexture(
                TextureDependencyRole::primary,
                reference,
                materialIndex,
                material.primaryTexture) ||
            !addTexture(
                TextureDependencyRole::secondary,
                reference,
                materialIndex,
                material.secondaryTexture) ||
            !addTexture(
                TextureDependencyRole::environment,
                reference,
                materialIndex,
                material.environmentTexture)) {
            failClosed(result);
            return result;
        }
    }

    if (!result.issues.empty()) {
        failClosed(result);
    }
    return result;
}

CcfRoomDrawPlan resolveFirstRoomDrawPlan(
    const CcfMetadata& ccf,
    const CcfRoomDrawPlanLimits& limits) {
    return resolveRoomDrawPlan(ccf, 0U, limits);
}

} // namespace airfix::assets
