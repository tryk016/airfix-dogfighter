#include "airfix/render/CcfRoomDrawAssembly.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    CcfRoomDrawAssembly& result,
    const CcfRoomDrawIssueKind kind,
    const std::optional<std::size_t> placedNodeIndex = std::nullopt,
    const std::optional<std::size_t> meshIndex = std::nullopt,
    const std::optional<std::uint32_t> materialReference = std::nullopt,
    const std::optional<GeometryErrorCode> geometryError = std::nullopt,
    const std::optional<DrawMeshErrorCode> drawMeshError = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .placedNodeIndex = placedNodeIndex,
        .meshIndex = meshIndex,
        .materialReference = materialReference,
        .geometryError = geometryError,
        .drawMeshError = drawMeshError,
    });
}

bool accountCount(
    std::size_t& total,
    const std::size_t addition,
    const std::size_t limit,
    CcfRoomDrawAssembly& result) {
    if (addition > std::numeric_limits<std::size_t>::max() - total) {
        addIssue(result, CcfRoomDrawIssueKind::integerOverflow);
        return false;
    }
    total += addition;
    if (total > limit) {
        addIssue(result, CcfRoomDrawIssueKind::limitExceeded);
        return false;
    }
    return true;
}

bool accountBytes(
    std::size_t& total,
    const std::size_t count,
    const std::size_t elementSize,
    const std::size_t limit,
    CcfRoomDrawAssembly& result) {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        addIssue(result, CcfRoomDrawIssueKind::integerOverflow);
        return false;
    }
    return accountCount(total, count * elementSize, limit, result);
}

CcfRoomDrawIssueKind geometryIssueKind(
    const GeometryErrorCode code,
    const bool placedTransform) noexcept {
    if (code == GeometryErrorCode::limitExceeded) {
        return CcfRoomDrawIssueKind::limitExceeded;
    }
    if (placedTransform &&
        code == GeometryErrorCode::unsupportedOrientation) {
        return CcfRoomDrawIssueKind::unsupportedPlacedOrientation;
    }
    if (placedTransform) {
        return CcfRoomDrawIssueKind::invalidTransform;
    }
    return CcfRoomDrawIssueKind::geometryFailure;
}

CcfRoomDrawIssueKind drawMeshIssueKind(
    const DrawMeshErrorCode code) noexcept {
    if (code == DrawMeshErrorCode::limitExceeded) {
        return CcfRoomDrawIssueKind::limitExceeded;
    }
    if (code == DrawMeshErrorCode::integerOverflow) {
        return CcfRoomDrawIssueKind::integerOverflow;
    }
    return CcfRoomDrawIssueKind::drawMeshFailure;
}

} // namespace

CcfRoomDrawAssembly buildRoomDrawAssembly(
    const assets::CcfMetadata& ccf,
    const std::size_t ccfRoomIndex,
    const std::span<const DrawMaterial> materialBindings,
    const BasisTransform& basis,
    const UvPolicy uvPolicy,
    const CcfRoomDrawLimits& limits) {
    CcfRoomDrawAssembly result;
    result.plan = assets::resolveRoomDrawPlan(
        ccf, ccfRoomIndex, limits.plan);
    if (!result.plan.issues.empty()) {
        addIssue(result, CcfRoomDrawIssueKind::planDependency);
        return result;
    }
    if (result.plan.meshIndices.size() > limits.maximumMeshes ||
        result.plan.placedNodeIndices.size() > limits.maximumInstances ||
        materialBindings.size() > limits.maximumMaterialBindings) {
        addIssue(result, CcfRoomDrawIssueKind::limitExceeded);
        return result;
    }

    std::unordered_map<std::uint32_t, const DrawMaterial*>
        materialByReference;
    materialByReference.reserve(materialBindings.size());
    for (const auto& binding : materialBindings) {
        const auto [unused, inserted] = materialByReference.emplace(
            binding.sourceReference, &binding);
        (void)unused;
        if (!inserted) {
            addIssue(
                result,
                CcfRoomDrawIssueKind::invalidMaterialBinding,
                std::nullopt,
                std::nullopt,
                binding.sourceReference);
            return result;
        }
    }

    std::size_t totalBytes = 0U;
    if (!accountBytes(
            totalBytes,
            result.plan.meshIndices.size(),
            sizeof(DrawMeshPayload),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            result.plan.placedNodeIndices.size(),
            sizeof(DrawMeshInstance),
            limits.maximumTotalBytes,
            result)) {
        return result;
    }

    DrawModelPayload candidate;
    candidate.meshes.reserve(result.plan.meshIndices.size());
    candidate.instances.reserve(result.plan.placedNodeIndices.size());
    std::unordered_map<std::size_t, std::uint32_t> meshSlotByIndex;
    meshSlotByIndex.reserve(result.plan.meshIndices.size());
    std::unordered_map<std::uint32_t, std::size_t> meshIndexByReference;
    meshIndexByReference.reserve(result.plan.meshIndices.size());
    std::size_t totalVertices = 0U;
    std::size_t totalIndices = 0U;
    std::size_t totalMaterials = 0U;
    std::size_t totalRanges = 0U;

    for (const auto meshIndex : result.plan.meshIndices) {
        if (meshIndex >= ccf.meshes.size() ||
            candidate.meshes.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            addIssue(
                result,
                meshIndex >= ccf.meshes.size()
                    ? CcfRoomDrawIssueKind::invalidPlacedNode
                    : CcfRoomDrawIssueKind::integerOverflow,
                std::nullopt,
                meshIndex);
            return result;
        }
        const auto& sourceMesh = ccf.meshes[meshIndex];
        if (!meshIndexByReference.emplace(
                sourceMesh.reference, meshIndex).second) {
            addIssue(
                result,
                CcfRoomDrawIssueKind::invalidPlacedNode,
                std::nullopt,
                meshIndex);
            return result;
        }
        std::vector<DrawMaterial> meshBindings;
        meshBindings.reserve(std::min(
            sourceMesh.triangles.size(),
            limits.drawMeshPerMesh.maximumMaterials));
        std::unordered_set<std::uint32_t> seenMaterials;
        seenMaterials.reserve(meshBindings.capacity());
        for (const auto& triangle : sourceMesh.triangles) {
            if (!seenMaterials.insert(triangle.materialReference).second) {
                continue;
            }
            const auto binding =
                materialByReference.find(triangle.materialReference);
            if (binding == materialByReference.end()) {
                addIssue(
                    result,
                    CcfRoomDrawIssueKind::missingMaterialBinding,
                    std::nullopt,
                    meshIndex,
                    triangle.materialReference);
                return result;
            }
            if (meshBindings.size() >=
                    limits.drawMeshPerMesh.maximumMaterials ||
                meshBindings.size() >=
                    limits.maximumTotalMaterials - totalMaterials) {
                addIssue(result, CcfRoomDrawIssueKind::limitExceeded);
                return result;
            }
            meshBindings.push_back(*binding->second);
        }

        const auto remainingVertices =
            limits.maximumTotalVertices - totalVertices;
        const auto remainingIndices =
            limits.maximumTotalIndices - totalIndices;
        const auto remainingMaterials =
            limits.maximumTotalMaterials - totalMaterials;
        const auto remainingRanges =
            limits.maximumTotalRanges - totalRanges;
        const auto remainingBytes =
            limits.maximumTotalBytes - totalBytes;
        const GeometryLimits geometryLimits{
            .maxVertices = std::min(
                limits.geometryPerMesh.maxVertices,
                remainingVertices),
            .maxTriangles = std::min(
                limits.geometryPerMesh.maxTriangles,
                remainingIndices / 3U),
        };
        const DrawMeshLimits meshLimits{
            .maximumVertices = std::min(
                limits.drawMeshPerMesh.maximumVertices,
                remainingVertices),
            .maximumIndices = std::min(
                limits.drawMeshPerMesh.maximumIndices,
                remainingIndices),
            .maximumMaterials = std::min(
                limits.drawMeshPerMesh.maximumMaterials,
                remainingMaterials),
            .maximumRanges = std::min(
                limits.drawMeshPerMesh.maximumRanges,
                remainingRanges),
            .maximumTotalBytes = std::min(
                limits.drawMeshPerMesh.maximumTotalBytes,
                remainingBytes),
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
                std::nullopt,
                meshIndex,
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
                std::nullopt,
                meshIndex,
                std::nullopt,
                std::nullopt,
                error.code());
            return result;
        }

        if (!accountCount(
                totalVertices,
                drawMesh.vertices.size(),
                limits.maximumTotalVertices,
                result) ||
            !accountCount(
                totalIndices,
                drawMesh.indices.size(),
                limits.maximumTotalIndices,
                result) ||
            !accountCount(
                totalMaterials,
                drawMesh.materials.size(),
                limits.maximumTotalMaterials,
                result) ||
            !accountCount(
                totalRanges,
                drawMesh.ranges.size(),
                limits.maximumTotalRanges,
                result) ||
            !accountBytes(
                totalBytes,
                drawMesh.vertices.size(),
                sizeof(DrawVertex),
                limits.maximumTotalBytes,
                result) ||
            !accountBytes(
                totalBytes,
                drawMesh.indices.size(),
                sizeof(std::uint32_t),
                limits.maximumTotalBytes,
                result) ||
            !accountBytes(
                totalBytes,
                drawMesh.materials.size(),
                sizeof(DrawMaterial),
                limits.maximumTotalBytes,
                result) ||
            !accountBytes(
                totalBytes,
                drawMesh.ranges.size(),
                sizeof(DrawRange),
                limits.maximumTotalBytes,
                result)) {
            return result;
        }

        const auto meshSlot =
            static_cast<std::uint32_t>(candidate.meshes.size());
        meshSlotByIndex.emplace(meshIndex, meshSlot);
        candidate.meshes.push_back(std::move(drawMesh));
    }

    for (const auto placedNodeIndex : result.plan.placedNodeIndices) {
        if (placedNodeIndex >= ccf.placedNodes.size()) {
            addIssue(
                result,
                CcfRoomDrawIssueKind::invalidPlacedNode,
                placedNodeIndex);
            return result;
        }
        const auto& placed = ccf.placedNodes[placedNodeIndex];
        const auto* object =
            std::get_if<assets::CcfPlacedObjectMetadata>(&placed.data);
        if (placed.kind != assets::CcfPlacedNodeKind::object ||
            object == nullptr) {
            addIssue(
                result,
                CcfRoomDrawIssueKind::invalidPlacedNode,
                placedNodeIndex);
            return result;
        }

        const auto meshMatch =
            meshIndexByReference.find(object->meshReference);
        if (meshMatch == meshIndexByReference.end() ||
            !meshSlotByIndex.contains(meshMatch->second)) {
            addIssue(
                result,
                CcfRoomDrawIssueKind::invalidPlacedNode,
                placedNodeIndex);
            return result;
        }
        const auto meshIndex = meshMatch->second;

        ConvertedNodeTransform world;
        try {
            world = convertLegacyTransform(placed.transform, basis);
        }
        catch (const GeometryError& error) {
            addIssue(
                result,
                geometryIssueKind(error.code(), true),
                placedNodeIndex,
                meshIndex,
                std::nullopt,
                error.code());
            return result;
        }
        candidate.instances.push_back({
            .meshSlot = meshSlotByIndex.at(meshIndex),
            .sourceNodeReference = placed.currentReference,
            .modelLinear = world.linear,
            .modelTranslation = world.translation,
        });
    }

    result.model = std::move(candidate);
    return result;
}

CcfRoomDrawAssembly buildFirstRoomDrawAssembly(
    const assets::CcfMetadata& ccf,
    const std::span<const DrawMaterial> materialBindings,
    const BasisTransform& basis,
    const UvPolicy uvPolicy,
    const CcfRoomDrawLimits& limits) {
    return buildRoomDrawAssembly(
        ccf, 0U, materialBindings, basis, uvPolicy, limits);
}

} // namespace airfix::render
