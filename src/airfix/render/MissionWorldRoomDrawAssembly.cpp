#include "airfix/render/MissionWorldRoomDrawAssembly.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace airfix::render {
namespace {

void addIssue(
    MissionWorldRoomDrawAssembly& result,
    const MissionWorldRoomDrawIssueKind kind,
    const std::optional<std::size_t> sourceIndex = std::nullopt,
    const std::optional<std::size_t> placedNodeIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt,
    const std::optional<std::uint32_t> materialReference = std::nullopt,
    const std::optional<
        assets::MissionWorldRoomDrawPlanIssueKind> planIssue =
            std::nullopt,
    const std::optional<GeometryErrorCode> geometryError =
        std::nullopt,
    const std::optional<DrawMeshErrorCode> drawMeshError =
        std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .sourceIndex = sourceIndex,
        .placedNodeIndex = placedNodeIndex,
        .physicalMeshIndex = physicalMeshIndex,
        .materialReference = materialReference,
        .planIssue = planIssue,
        .geometryError = geometryError,
        .drawMeshError = drawMeshError,
    });
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool accountCount(
    std::size_t& current,
    const std::size_t added,
    const std::size_t maximum,
    MissionWorldRoomDrawAssembly& result,
    const std::size_t sourceIndex,
    const std::optional<std::size_t> placedNodeIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt) {
    std::size_t next = 0U;
    if (!checkedAdd(current, added, next)) {
        addIssue(
            result,
            MissionWorldRoomDrawIssueKind::integerOverflow,
            sourceIndex,
            placedNodeIndex,
            physicalMeshIndex);
        return false;
    }
    if (next > maximum) {
        addIssue(
            result,
            MissionWorldRoomDrawIssueKind::limitExceeded,
            sourceIndex,
            placedNodeIndex,
            physicalMeshIndex);
        return false;
    }
    current = next;
    return true;
}

[[nodiscard]] bool accountBytes(
    std::size_t& current,
    const std::size_t count,
    const std::size_t elementSize,
    const std::size_t maximum,
    MissionWorldRoomDrawAssembly& result,
    const std::size_t sourceIndex,
    const std::optional<std::size_t> placedNodeIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt) {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        addIssue(
            result,
            MissionWorldRoomDrawIssueKind::integerOverflow,
            sourceIndex,
            placedNodeIndex,
            physicalMeshIndex);
        return false;
    }
    return accountCount(
        current,
        count * elementSize,
        maximum,
        result,
        sourceIndex,
        placedNodeIndex,
        physicalMeshIndex);
}

[[nodiscard]] MissionWorldRoomDrawIssueKind geometryIssueKind(
    const GeometryErrorCode code,
    const bool transform) noexcept {
    if (code == GeometryErrorCode::limitExceeded) {
        return MissionWorldRoomDrawIssueKind::limitExceeded;
    }
    if (code == GeometryErrorCode::unsupportedOrientation) {
        return MissionWorldRoomDrawIssueKind::
            unsupportedPlacedOrientation;
    }
    if (transform) {
        return MissionWorldRoomDrawIssueKind::invalidTransform;
    }
    return MissionWorldRoomDrawIssueKind::geometryFailure;
}

[[nodiscard]] MissionWorldRoomDrawIssueKind drawMeshIssueKind(
    const DrawMeshErrorCode code) noexcept {
    if (code == DrawMeshErrorCode::limitExceeded) {
        return MissionWorldRoomDrawIssueKind::limitExceeded;
    }
    if (code == DrawMeshErrorCode::integerOverflow) {
        return MissionWorldRoomDrawIssueKind::integerOverflow;
    }
    return MissionWorldRoomDrawIssueKind::drawMeshFailure;
}

} // namespace

MissionWorldRoomDrawAssembly buildMissionWorldRoomDrawAssembly(
    const assets::MissionWorldRoomCatalog& catalog,
    const std::span<const assets::MissionCcfRoomLoadSource>
        loadSources,
    const std::size_t worldRoomIndex,
    const std::span<const MissionWorldRoomDrawSource> drawSources,
    const BasisTransform& basis,
    const UvPolicy uvPolicy,
    const MissionWorldRoomDrawLimits& limits) {
    MissionWorldRoomDrawAssembly result;
    const auto plan = assets::resolveMissionWorldRoomDrawPlan(
        catalog, loadSources, worldRoomIndex, limits.plan);
    if (!plan.complete()) {
        addIssue(
            result,
            MissionWorldRoomDrawIssueKind::planDependency,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            plan.issues.empty()
                ? std::nullopt
                : std::optional{plan.issues.front().kind});
        return result;
    }
    if (plan.sourceCount != drawSources.size() ||
        loadSources.size() != drawSources.size()) {
        addIssue(
            result,
            MissionWorldRoomDrawIssueKind::sourceCountMismatch);
        return result;
    }
    if (drawSources.size() > limits.maximumSources ||
        plan.meshes.size() > limits.maximumMeshes ||
        plan.placedNodes.size() > limits.maximumInstances) {
        addIssue(
            result,
            MissionWorldRoomDrawIssueKind::limitExceeded);
        return result;
    }

    std::size_t totalMaterialBindings = 0U;
    std::vector<std::unordered_map<std::uint32_t, const DrawMaterial*>>
        materialBySource(drawSources.size());
    for (std::size_t sourceIndex = 0U;
         sourceIndex < drawSources.size();
         ++sourceIndex) {
        const auto& source = drawSources[sourceIndex];
        if (source.ccf == nullptr ||
            source.ccf != loadSources[sourceIndex].ccf) {
            addIssue(
                result,
                MissionWorldRoomDrawIssueKind::invalidSource,
                sourceIndex);
            return result;
        }
        std::size_t nextBindingCount = 0U;
        if (!checkedAdd(
                totalMaterialBindings,
                source.materialBindings.size(),
                nextBindingCount)) {
            addIssue(
                result,
                MissionWorldRoomDrawIssueKind::integerOverflow,
                sourceIndex);
            return result;
        }
        if (nextBindingCount > limits.maximumMaterialBindings) {
            addIssue(
                result,
                MissionWorldRoomDrawIssueKind::limitExceeded,
                sourceIndex);
            return result;
        }
        totalMaterialBindings = nextBindingCount;

        auto& bindings = materialBySource[sourceIndex];
        bindings.reserve(source.materialBindings.size());
        for (const auto& binding : source.materialBindings) {
            if (!bindings.emplace(
                    binding.sourceReference, &binding).second) {
                addIssue(
                    result,
                    MissionWorldRoomDrawIssueKind::
                        invalidMaterialBinding,
                    sourceIndex,
                    std::nullopt,
                    std::nullopt,
                    binding.sourceReference);
                return result;
            }
        }
    }

    std::vector<bool> seenMeshSlots(plan.meshes.size(), false);
    std::size_t nextFirstUseMeshSlot = 0U;
    std::optional<std::pair<std::size_t, std::size_t>>
        previousPlaced;
    for (const auto& placed : plan.placedNodes) {
        if (placed.sourceIndex >= drawSources.size() ||
            placed.meshSlot >= plan.meshes.size() ||
            placed.placedNodeIndex >=
                drawSources[placed.sourceIndex]
                    .ccf->placedNodes.size() ||
            plan.meshes[placed.meshSlot].sourceIndex !=
                placed.sourceIndex ||
            placed.contributorIndex.has_value() !=
                placed.physicalRoomIndex.has_value()) {
            addIssue(
                result,
                MissionWorldRoomDrawIssueKind::invalidPlan,
                placed.sourceIndex,
                placed.placedNodeIndex);
            return result;
        }
        const auto order = std::pair{
            placed.sourceIndex, placed.placedNodeIndex};
        if (previousPlaced.has_value() &&
            order <= *previousPlaced) {
            addIssue(
                result,
                MissionWorldRoomDrawIssueKind::invalidPlan,
                placed.sourceIndex,
                placed.placedNodeIndex);
            return result;
        }
        previousPlaced = order;
        if (!seenMeshSlots[placed.meshSlot]) {
            if (placed.meshSlot != nextFirstUseMeshSlot) {
                addIssue(
                    result,
                    MissionWorldRoomDrawIssueKind::invalidPlan,
                    placed.sourceIndex,
                    placed.placedNodeIndex);
                return result;
            }
            seenMeshSlots[placed.meshSlot] = true;
            ++nextFirstUseMeshSlot;
        }
    }
    if (nextFirstUseMeshSlot != plan.meshes.size()) {
        addIssue(
            result,
            MissionWorldRoomDrawIssueKind::invalidPlan);
        return result;
    }

    std::size_t totalBytes = 0U;
    const std::size_t preflightSourceIndex =
        !plan.meshes.empty()
        ? plan.meshes.front().sourceIndex
        : (!plan.placedNodes.empty()
              ? plan.placedNodes.front().sourceIndex
              : 0U);
    if (!accountBytes(
            totalBytes,
            plan.meshes.size(),
            sizeof(DrawMeshPayload),
            limits.maximumTotalBytes,
            result,
            preflightSourceIndex) ||
        !accountBytes(
            totalBytes,
            plan.meshes.size(),
            sizeof(MissionWorldRoomMeshProvenance),
            limits.maximumTotalBytes,
            result,
            preflightSourceIndex) ||
        !accountBytes(
            totalBytes,
            plan.placedNodes.size(),
            sizeof(DrawMeshInstance),
            limits.maximumTotalBytes,
            result,
            preflightSourceIndex) ||
        !accountBytes(
            totalBytes,
            plan.placedNodes.size(),
            sizeof(MissionWorldRoomInstanceProvenance),
            limits.maximumTotalBytes,
            result,
            preflightSourceIndex)) {
        return result;
    }

    DrawModelPayload candidate;
    std::vector<MissionWorldRoomMeshProvenance>
        candidateMeshProvenance;
    std::vector<MissionWorldRoomInstanceProvenance>
        candidateInstanceProvenance;
    candidate.meshes.reserve(plan.meshes.size());
    candidate.instances.reserve(plan.placedNodes.size());
    candidateMeshProvenance.reserve(plan.meshes.size());
    candidateInstanceProvenance.reserve(plan.placedNodes.size());

    std::size_t totalVertices = 0U;
    std::size_t totalIndices = 0U;
    std::size_t totalMaterials = 0U;
    std::size_t totalRanges = 0U;
    for (std::size_t meshSlot = 0U;
         meshSlot < plan.meshes.size();
         ++meshSlot) {
        const auto& plannedMesh = plan.meshes[meshSlot];
        if (plannedMesh.sourceIndex >= drawSources.size() ||
            plannedMesh.physicalMeshIndex >=
                drawSources[plannedMesh.sourceIndex]
                    .ccf->meshes.size() ||
            meshSlot >
                std::numeric_limits<std::uint32_t>::max()) {
            addIssue(
                result,
                meshSlot >
                        std::numeric_limits<std::uint32_t>::max()
                    ? MissionWorldRoomDrawIssueKind::integerOverflow
                    : MissionWorldRoomDrawIssueKind::invalidPlan,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex);
            return result;
        }
        const auto& sourceMesh =
            drawSources[plannedMesh.sourceIndex]
                .ccf->meshes[plannedMesh.physicalMeshIndex];
        std::vector<DrawMaterial> meshBindings;
        meshBindings.reserve(std::min(
            sourceMesh.triangles.size(),
            limits.drawMeshPerMesh.maximumMaterials));
        std::unordered_set<std::uint32_t> seenMaterials;
        seenMaterials.reserve(meshBindings.capacity());
        for (const auto& triangle : sourceMesh.triangles) {
            if (!seenMaterials.insert(
                    triangle.materialReference).second) {
                continue;
            }
            const auto binding =
                materialBySource[plannedMesh.sourceIndex].find(
                    triangle.materialReference);
            if (binding ==
                materialBySource[plannedMesh.sourceIndex].end()) {
                addIssue(
                    result,
                    MissionWorldRoomDrawIssueKind::
                        missingMaterialBinding,
                    plannedMesh.sourceIndex,
                    std::nullopt,
                    plannedMesh.physicalMeshIndex,
                    triangle.materialReference);
                return result;
            }
            if (meshBindings.size() >=
                    limits.drawMeshPerMesh.maximumMaterials ||
                meshBindings.size() >=
                    limits.maximumTotalMaterials - totalMaterials) {
                addIssue(
                    result,
                    MissionWorldRoomDrawIssueKind::limitExceeded,
                    plannedMesh.sourceIndex,
                    std::nullopt,
                    plannedMesh.physicalMeshIndex);
                return result;
            }
            meshBindings.push_back(*binding->second);
        }

        const GeometryLimits geometryLimits{
            .maxVertices = std::min(
                limits.geometryPerMesh.maxVertices,
                limits.maximumTotalVertices - totalVertices),
            .maxTriangles = std::min(
                limits.geometryPerMesh.maxTriangles,
                (limits.maximumTotalIndices - totalIndices) / 3U),
        };
        const DrawMeshLimits meshLimits{
            .maximumVertices = std::min(
                limits.drawMeshPerMesh.maximumVertices,
                limits.maximumTotalVertices - totalVertices),
            .maximumIndices = std::min(
                limits.drawMeshPerMesh.maximumIndices,
                limits.maximumTotalIndices - totalIndices),
            .maximumMaterials = std::min(
                limits.drawMeshPerMesh.maximumMaterials,
                limits.maximumTotalMaterials - totalMaterials),
            .maximumRanges = std::min(
                limits.drawMeshPerMesh.maximumRanges,
                limits.maximumTotalRanges - totalRanges),
            .maximumTotalBytes = std::min(
                limits.drawMeshPerMesh.maximumTotalBytes,
                limits.maximumTotalBytes - totalBytes),
        };

        ConvertedMeshGeometry converted;
        try {
            converted = convertLegacyGeometry(
                sourceMesh, basis, uvPolicy, geometryLimits);
        }
        catch (const GeometryError& error) {
            addIssue(
                result,
                geometryIssueKind(error.code(), false),
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex,
                std::nullopt,
                std::nullopt,
                error.code());
            return result;
        }

        DrawMeshPayload drawMesh;
        try {
            drawMesh = buildDrawMesh(
                converted, meshBindings, meshLimits);
        }
        catch (const DrawMeshError& error) {
            addIssue(
                result,
                drawMeshIssueKind(error.code()),
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                error.code());
            return result;
        }

        if (!accountCount(
                totalVertices,
                drawMesh.vertices.size(),
                limits.maximumTotalVertices,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex) ||
            !accountCount(
                totalIndices,
                drawMesh.indices.size(),
                limits.maximumTotalIndices,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex) ||
            !accountCount(
                totalMaterials,
                drawMesh.materials.size(),
                limits.maximumTotalMaterials,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex) ||
            !accountCount(
                totalRanges,
                drawMesh.ranges.size(),
                limits.maximumTotalRanges,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex) ||
            !accountBytes(
                totalBytes,
                drawMesh.vertices.size(),
                sizeof(DrawVertex),
                limits.maximumTotalBytes,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex) ||
            !accountBytes(
                totalBytes,
                drawMesh.indices.size(),
                sizeof(std::uint32_t),
                limits.maximumTotalBytes,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex) ||
            !accountBytes(
                totalBytes,
                drawMesh.materials.size(),
                sizeof(DrawMaterial),
                limits.maximumTotalBytes,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex) ||
            !accountBytes(
                totalBytes,
                drawMesh.ranges.size(),
                sizeof(DrawRange),
                limits.maximumTotalBytes,
                result,
                plannedMesh.sourceIndex,
                std::nullopt,
                plannedMesh.physicalMeshIndex)) {
            return result;
        }
        candidate.meshes.push_back(std::move(drawMesh));
        candidateMeshProvenance.push_back({
            .sourceIndex = plannedMesh.sourceIndex,
            .physicalMeshIndex =
                plannedMesh.physicalMeshIndex,
        });
    }

    for (const auto& plannedInstance : plan.placedNodes) {
        const auto& source =
            *drawSources[plannedInstance.sourceIndex].ccf;
        const auto& placed =
            source.placedNodes[plannedInstance.placedNodeIndex];
        const auto* object =
            std::get_if<assets::CcfPlacedObjectMetadata>(
                &placed.data);
        if (placed.kind != assets::CcfPlacedNodeKind::object ||
            object == nullptr ||
            plannedInstance.meshSlot >= plan.meshes.size() ||
            plan.meshes[plannedInstance.meshSlot].sourceIndex !=
                plannedInstance.sourceIndex ||
            plan.meshes[plannedInstance.meshSlot]
                    .physicalMeshIndex >= source.meshes.size() ||
            source.meshes[
                plan.meshes[plannedInstance.meshSlot]
                    .physicalMeshIndex].reference !=
                object->meshReference) {
            addIssue(
                result,
                MissionWorldRoomDrawIssueKind::invalidPlacedNode,
                plannedInstance.sourceIndex,
                plannedInstance.placedNodeIndex);
            return result;
        }

        ConvertedNodeTransform world;
        try {
            world = convertLegacyTransform(
                placed.transform, basis);
        }
        catch (const GeometryError& error) {
            addIssue(
                result,
                geometryIssueKind(error.code(), true),
                plannedInstance.sourceIndex,
                plannedInstance.placedNodeIndex,
                plan.meshes[plannedInstance.meshSlot]
                    .physicalMeshIndex,
                std::nullopt,
                std::nullopt,
                error.code());
            return result;
        }
        candidate.instances.push_back({
            .meshSlot = static_cast<std::uint32_t>(
                plannedInstance.meshSlot),
            .sourceNodeReference = placed.currentReference,
            .modelLinear = world.linear,
            .modelTranslation = world.translation,
        });
        candidateInstanceProvenance.push_back({
            .sourceIndex = plannedInstance.sourceIndex,
            .placedNodeIndex = plannedInstance.placedNodeIndex,
            .contributorIndex =
                plannedInstance.contributorIndex,
            .physicalRoomIndex =
                plannedInstance.physicalRoomIndex,
        });
    }

    result.worldRoomIndex = plan.worldRoomIndex;
    result.model = std::move(candidate);
    result.meshProvenance = std::move(candidateMeshProvenance);
    result.instanceProvenance =
        std::move(candidateInstanceProvenance);
    return result;
}

} // namespace airfix::render
