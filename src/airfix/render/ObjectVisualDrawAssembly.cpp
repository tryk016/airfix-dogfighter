#include "airfix/render/ObjectVisualDrawAssembly.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    ObjectVisualDrawAssembly& result,
    const ObjectVisualDrawIssueKind kind,
    const std::optional<std::size_t> blueprintIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt,
    const std::optional<std::uint32_t> sourceReference = std::nullopt,
    const std::optional<assets::DependencyIssueKind> dependencyIssue =
        std::nullopt,
    const std::optional<assets::BlueprintGraphIssueKind> graphIssue =
        std::nullopt,
    const std::optional<GeometryErrorCode> geometryError = std::nullopt,
    const std::optional<DrawMeshErrorCode> drawMeshError = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .blueprintIndex = blueprintIndex,
        .physicalMeshIndex = physicalMeshIndex,
        .sourceReference = sourceReference,
        .dependencyIssue = dependencyIssue,
        .graphIssue = graphIssue,
        .geometryError = geometryError,
        .drawMeshError = drawMeshError,
    });
}

bool accountCount(
    std::size_t& total,
    const std::size_t addition,
    const std::size_t limit,
    ObjectVisualDrawAssembly& result) {
    if (addition > std::numeric_limits<std::size_t>::max() - total) {
        addIssue(result, ObjectVisualDrawIssueKind::integerOverflow);
        return false;
    }
    total += addition;
    if (total > limit) {
        addIssue(result, ObjectVisualDrawIssueKind::limitExceeded);
        return false;
    }
    return true;
}

bool accountBytes(
    std::size_t& total,
    const std::size_t count,
    const std::size_t elementSize,
    const std::size_t limit,
    ObjectVisualDrawAssembly& result) {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        addIssue(result, ObjectVisualDrawIssueKind::integerOverflow);
        return false;
    }
    return accountCount(total, count * elementSize, limit, result);
}

ObjectVisualDrawIssueKind dependencyIssueKind(
    const assets::DependencyIssueKind kind) noexcept {
    if (kind == assets::DependencyIssueKind::limitExceeded) {
        return ObjectVisualDrawIssueKind::limitExceeded;
    }
    return ObjectVisualDrawIssueKind::dependencyFailure;
}

ObjectVisualDrawIssueKind graphIssueKind(
    const assets::BlueprintGraphIssueKind kind) noexcept {
    if (kind == assets::BlueprintGraphIssueKind::limitExceeded) {
        return ObjectVisualDrawIssueKind::limitExceeded;
    }
    return ObjectVisualDrawIssueKind::graphFailure;
}

ObjectVisualDrawIssueKind geometryIssueKind(
    const GeometryErrorCode code,
    const bool transform) noexcept {
    if (code == GeometryErrorCode::limitExceeded) {
        return ObjectVisualDrawIssueKind::limitExceeded;
    }
    return transform
        ? ObjectVisualDrawIssueKind::invalidTransform
        : ObjectVisualDrawIssueKind::geometryFailure;
}

ObjectVisualDrawIssueKind drawMeshIssueKind(
    const DrawMeshErrorCode code) noexcept {
    if (code == DrawMeshErrorCode::limitExceeded) {
        return ObjectVisualDrawIssueKind::limitExceeded;
    }
    if (code == DrawMeshErrorCode::integerOverflow) {
        return ObjectVisualDrawIssueKind::integerOverflow;
    }
    return ObjectVisualDrawIssueKind::drawMeshFailure;
}

} // namespace

ObjectVisualDrawAssembly buildObjectVisualDrawAssembly(
    const assets::ObjectDefinition& object,
    const assets::CcfMetadata& ccf,
    const std::span<const DrawMaterial> materialBindings,
    const BasisTransform& basis,
    const UvPolicy uvPolicy,
    const ObjectVisualDrawLimits& limits) {
    ObjectVisualDrawAssembly result;
    result.resolution = assets::resolveObjectSceneDependencies(
        object, ccf, limits.dependencies);

    if (!object.meshName.has_value()) {
        addIssue(
            result,
            ObjectVisualDrawIssueKind::missingBlueprintSelector);
        return result;
    }
    for (const auto& issue : result.resolution.issues) {
        addIssue(
            result,
            dependencyIssueKind(issue.kind),
            std::nullopt,
            std::nullopt,
            issue.reference,
            issue.kind);
    }
    for (const auto& issue : result.resolution.graphIssues) {
        addIssue(
            result,
            graphIssueKind(issue.kind),
            issue.blueprintIndex,
            std::nullopt,
            issue.reference,
            std::nullopt,
            issue.kind);
    }
    if (!result.issues.empty()) {
        return result;
    }
    if (result.resolution.selectorStatus !=
            assets::BlueprintSelectorStatus::unique ||
        !result.resolution.rootBlueprintIndex.has_value() ||
        *result.resolution.rootBlueprintIndex >= ccf.blueprints.size() ||
        result.resolution.blueprintIndices.empty() ||
        result.resolution.blueprintIndices.front() !=
            *result.resolution.rootBlueprintIndex) {
        addIssue(result, ObjectVisualDrawIssueKind::invalidResolution);
        return result;
    }
    if (result.resolution.meshes.empty()) {
        addIssue(
            result,
            ObjectVisualDrawIssueKind::invalidResolution,
            result.resolution.rootBlueprintIndex);
        return result;
    }
    if (result.resolution.meshes.size() > limits.maximumInstances ||
        materialBindings.size() > limits.maximumMaterialBindings) {
        addIssue(result, ObjectVisualDrawIssueKind::limitExceeded);
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
                ObjectVisualDrawIssueKind::invalidMaterialBinding,
                std::nullopt,
                std::nullopt,
                binding.sourceReference);
            return result;
        }
    }

    std::vector<assets::SceneMeshDependency> firstUses;
    firstUses.reserve(result.resolution.meshes.size());
    std::unordered_set<std::size_t> seenPhysicalMeshes;
    seenPhysicalMeshes.reserve(result.resolution.meshes.size());
    for (const auto& dependency : result.resolution.meshes) {
        if (dependency.blueprintIndex >= ccf.blueprints.size() ||
            dependency.meshIndex >= ccf.meshes.size()) {
            addIssue(
                result,
                ObjectVisualDrawIssueKind::invalidResolution,
                dependency.blueprintIndex,
                dependency.meshIndex);
            return result;
        }
        const auto& blueprint = ccf.blueprints[dependency.blueprintIndex];
        if (blueprint.kind != assets::CcfBlueprintKind::mesh ||
            !blueprint.meshIndex.has_value() ||
            *blueprint.meshIndex != dependency.meshIndex) {
            addIssue(
                result,
                ObjectVisualDrawIssueKind::invalidResolution,
                dependency.blueprintIndex,
                dependency.meshIndex,
                blueprint.reference);
            return result;
        }
        if (seenPhysicalMeshes.insert(dependency.meshIndex).second) {
            firstUses.push_back(dependency);
        }
    }
    if (firstUses.size() > limits.maximumMeshes) {
        addIssue(result, ObjectVisualDrawIssueKind::limitExceeded);
        return result;
    }

    std::size_t totalBytes = 0U;
    if (!accountBytes(
            totalBytes,
            firstUses.size(),
            sizeof(DrawMeshPayload),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            result.resolution.meshes.size(),
            sizeof(DrawMeshInstance),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            firstUses.size(),
            sizeof(ObjectVisualProvenance),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            result.resolution.meshes.size(),
            sizeof(ObjectVisualProvenance),
            limits.maximumTotalBytes,
            result)) {
        return result;
    }

    DrawModelPayload candidate;
    candidate.meshes.reserve(firstUses.size());
    candidate.instances.reserve(result.resolution.meshes.size());
    std::vector<ObjectVisualProvenance> meshProvenance;
    meshProvenance.reserve(firstUses.size());
    std::vector<ObjectVisualProvenance> instanceProvenance;
    instanceProvenance.reserve(result.resolution.meshes.size());
    std::unordered_map<std::size_t, std::uint32_t> meshSlotByIndex;
    meshSlotByIndex.reserve(firstUses.size());
    std::size_t totalVertices = 0U;
    std::size_t totalIndices = 0U;
    std::size_t totalMaterials = 0U;
    std::size_t totalRanges = 0U;

    for (const auto& firstUse : firstUses) {
        if (candidate.meshes.size() >
            std::numeric_limits<std::uint32_t>::max()) {
            addIssue(
                result,
                ObjectVisualDrawIssueKind::integerOverflow,
                firstUse.blueprintIndex,
                firstUse.meshIndex);
            return result;
        }
        const auto& sourceMesh = ccf.meshes[firstUse.meshIndex];
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
                    ObjectVisualDrawIssueKind::missingMaterialBinding,
                    firstUse.blueprintIndex,
                    firstUse.meshIndex,
                    triangle.materialReference);
                return result;
            }
            if (meshBindings.size() >=
                    limits.drawMeshPerMesh.maximumMaterials ||
                totalMaterials >= limits.maximumTotalMaterials ||
                meshBindings.size() >=
                    limits.maximumTotalMaterials - totalMaterials) {
                addIssue(result, ObjectVisualDrawIssueKind::limitExceeded);
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
                firstUse.blueprintIndex,
                firstUse.meshIndex,
                sourceMesh.reference,
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
                firstUse.blueprintIndex,
                firstUse.meshIndex,
                sourceMesh.reference,
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
        meshSlotByIndex.emplace(firstUse.meshIndex, meshSlot);
        candidate.meshes.push_back(std::move(drawMesh));
        meshProvenance.push_back({
            .blueprintIndex = firstUse.blueprintIndex,
            .physicalMeshIndex = firstUse.meshIndex,
        });
    }

    for (const auto& dependency : result.resolution.meshes) {
        const auto meshSlot = meshSlotByIndex.find(dependency.meshIndex);
        if (meshSlot == meshSlotByIndex.end()) {
            addIssue(
                result,
                ObjectVisualDrawIssueKind::invalidResolution,
                dependency.blueprintIndex,
                dependency.meshIndex);
            return result;
        }
        const auto& blueprint = ccf.blueprints[dependency.blueprintIndex];
        ConvertedNodeTransform authoredWorld;
        try {
            authoredWorld = convertLegacyTransform(
                blueprint.authoredTransform, basis);
        }
        catch (const GeometryError& error) {
            addIssue(
                result,
                geometryIssueKind(error.code(), true),
                dependency.blueprintIndex,
                dependency.meshIndex,
                blueprint.reference,
                std::nullopt,
                std::nullopt,
                error.code());
            return result;
        }
        candidate.instances.push_back({
            .meshSlot = meshSlot->second,
            .sourceNodeReference = blueprint.reference,
            .modelLinear = authoredWorld.linear,
            .modelTranslation = authoredWorld.translation,
        });
        instanceProvenance.push_back({
            .blueprintIndex = dependency.blueprintIndex,
            .physicalMeshIndex = dependency.meshIndex,
        });
    }

    result.model = std::move(candidate);
    result.meshProvenance = std::move(meshProvenance);
    result.instanceProvenance = std::move(instanceProvenance);
    return result;
}

} // namespace airfix::render
