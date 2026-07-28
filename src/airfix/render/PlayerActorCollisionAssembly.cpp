#include "airfix/render/PlayerActorCollisionAssembly.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace airfix::render {
namespace {

constexpr double orthonormalTolerance = 1.0e-4;

void addIssue(
    PlayerActorCollisionAssembly& result,
    const PlayerActorCollisionIssueKind kind,
    const std::optional<PlayerActorVisualDrawIssueKind> actorVisualIssue =
        std::nullopt,
    const std::optional<LegacyDynamicBspBuildIssueKind> dynamicBspIssue =
        std::nullopt,
    const std::optional<GeometryErrorCode> geometryError = std::nullopt,
    const std::optional<std::size_t> meshIndex = std::nullopt,
    const std::optional<std::size_t> instanceIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt,
    const std::optional<std::uint32_t> materialReference = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .actorVisualIssue = actorVisualIssue,
        .dynamicBspIssue = dynamicBspIssue,
        .geometryError = geometryError,
        .meshIndex = meshIndex,
        .instanceIndex = instanceIndex,
        .physicalMeshIndex = physicalMeshIndex,
        .materialReference = materialReference,
    });
}

[[nodiscard]] bool checkedAdd(
    std::uint64_t& total,
    const std::uint64_t addition) noexcept {
    if (addition >
        std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += addition;
    return true;
}

[[nodiscard]] bool checkedBytes(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t elementSize) noexcept {
    if (count != 0U &&
        elementSize >
            std::numeric_limits<std::uint64_t>::max() / count) {
        return false;
    }
    return checkedAdd(
        total,
        static_cast<std::uint64_t>(count) * elementSize);
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] double dotDouble(
    const Vec3& left,
    const Vec3& right) noexcept {
    return static_cast<double>(left.x) * right.x +
        static_cast<double>(left.y) * right.y +
        static_cast<double>(left.z) * right.z;
}

[[nodiscard]] bool orthonormal(const Mat3& value) noexcept {
    if (!finite(value.columns[0]) ||
        !finite(value.columns[1]) ||
        !finite(value.columns[2])) {
        return false;
    }
    for (std::size_t index = 0U; index < 3U; ++index) {
        if (std::abs(
                dotDouble(
                    value.columns[index],
                    value.columns[index]) -
                1.0) > orthonormalTolerance) {
            return false;
        }
    }
    return
        std::abs(dotDouble(value.columns[0], value.columns[1])) <=
            orthonormalTolerance &&
        std::abs(dotDouble(value.columns[0], value.columns[2])) <=
            orthonormalTolerance &&
        std::abs(dotDouble(value.columns[1], value.columns[2])) <=
            orthonormalTolerance;
}

[[nodiscard]] ConvertedNodeTransform transformOf(
    const DrawMeshInstance& instance) noexcept {
    return {
        .linear = instance.modelLinear,
        .translation = instance.modelTranslation,
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] bool validRetainedBytes(
    const LegacyDynamicBspMesh& mesh) noexcept {
    std::uint64_t arenaBytes = 0U;
    if (!checkedBytes(
            arenaBytes,
            mesh.localArena.rooms.size(),
            sizeof(assets::MissionWorldSpatialRoom)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.treeReferences.size(),
            sizeof(std::size_t)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.trees.size(),
            sizeof(assets::MissionWorldSpatialTree)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.nodes.size(),
            sizeof(assets::MissionWorldSpatialNode)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.polygons.size(),
            sizeof(assets::MissionWorldSpatialPolygon)) ||
        arenaBytes != mesh.localArena.retainedPayloadBytes) {
        return false;
    }
    auto meshBytes = arenaBytes;
    return checkedBytes(
               meshBytes,
               mesh.polygonMaterialReferences.size(),
               sizeof(std::uint32_t)) &&
        meshBytes == mesh.retainedPayloadBytes;
}

[[nodiscard]] PlayerActorCollisionIssueKind mapBuildIssue(
    const LegacyDynamicBspBuildIssueKind kind) noexcept {
    switch (kind) {
    case LegacyDynamicBspBuildIssueKind::limitExceeded:
        return PlayerActorCollisionIssueKind::limitExceeded;
    case LegacyDynamicBspBuildIssueKind::retainedByteLimitExceeded:
        return PlayerActorCollisionIssueKind::retainedByteLimitExceeded;
    case LegacyDynamicBspBuildIssueKind::integerOverflow:
        return PlayerActorCollisionIssueKind::integerOverflow;
    case LegacyDynamicBspBuildIssueKind::allocationFailure:
        return PlayerActorCollisionIssueKind::allocationFailure;
    case LegacyDynamicBspBuildIssueKind::invalidGeometry:
    case LegacyDynamicBspBuildIssueKind::duplicateMaterialBinding:
        return PlayerActorCollisionIssueKind::dynamicBspFailure;
    }
    return PlayerActorCollisionIssueKind::dynamicBspFailure;
}

void clearPublishable(PlayerActorCollisionAssembly& result) noexcept {
    result.meshes.clear();
    result.meshProvenance.clear();
    result.instances.clear();
    result.retainedPayloadBytes = 0U;
}

[[nodiscard]] PlayerActorCollisionAssembly build(
    const assets::CcfMetadata& ccf,
    const PlayerActorVisualDrawAssembly& actorVisual,
    const BasisTransform& basis,
    const PlayerActorCollisionLimits& limits) {
    PlayerActorCollisionAssembly result;

    if (!actorVisual.issues.empty()) {
        addIssue(
            result,
            PlayerActorCollisionIssueKind::actorVisualFailure,
            actorVisual.issues.front().kind);
        return result;
    }
    if (actorVisual.model.meshes.empty() ||
        actorVisual.model.instances.empty() ||
        actorVisual.model.meshes.size() !=
            actorVisual.meshProvenance.size() ||
        actorVisual.model.instances.size() !=
            actorVisual.instanceProvenance.size()) {
        addIssue(
            result,
            PlayerActorCollisionIssueKind::
                invalidActorVisualAssembly);
        return result;
    }
    if (actorVisual.model.meshes.size() > limits.maximumMeshes ||
        actorVisual.model.instances.size() > limits.maximumInstances ||
        ccf.materials.size() > limits.maximumMaterialBindings) {
        addIssue(result, PlayerActorCollisionIssueKind::limitExceeded);
        return result;
    }

    std::uint64_t retained = 0U;
    if (!checkedBytes(
            retained,
            actorVisual.model.meshes.size(),
            sizeof(LegacyDynamicBspMesh)) ||
        !checkedBytes(
            retained,
            actorVisual.meshProvenance.size(),
            sizeof(PlayerActorCollisionMeshProvenance)) ||
        !checkedBytes(
            retained,
            actorVisual.model.instances.size(),
            sizeof(PlayerActorCollisionInstance))) {
        addIssue(
            result,
            PlayerActorCollisionIssueKind::integerOverflow);
        return result;
    }
    if (retained > limits.maximumRetainedBytes) {
        addIssue(
            result,
            PlayerActorCollisionIssueKind::
                retainedByteLimitExceeded);
        return result;
    }

    std::unordered_map<
        std::uint32_t,
        const assets::CcfMaterialMetadata*>
        materialByReference;
    materialByReference.reserve(ccf.materials.size());
    for (const auto& material : ccf.materials) {
        const auto [unused, inserted] =
            materialByReference.emplace(
                material.reference, &material);
        (void)unused;
        if (!inserted) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::
                    duplicateMaterialBinding,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                material.reference);
            return result;
        }
    }

    result.meshes.reserve(actorVisual.model.meshes.size());
    result.meshProvenance.reserve(
        actorVisual.meshProvenance.size());
    std::unordered_set<std::size_t> seenPhysicalMeshes;
    seenPhysicalMeshes.reserve(actorVisual.meshProvenance.size());
    for (std::size_t meshIndex = 0U;
         meshIndex < actorVisual.meshProvenance.size();
         ++meshIndex) {
        const auto& provenance =
            actorVisual.meshProvenance[meshIndex];
        if (provenance.legacySkinSlot != 0U ||
            provenance.blueprintIndex >= ccf.blueprints.size() ||
            provenance.physicalMeshIndex >= ccf.meshes.size() ||
            ccf.blueprints[provenance.blueprintIndex].reference !=
                provenance.blueprintReference ||
            !ccf.blueprints[provenance.blueprintIndex]
                 .meshIndex.has_value() ||
            *ccf.blueprints[provenance.blueprintIndex].meshIndex !=
                provenance.physicalMeshIndex ||
            !seenPhysicalMeshes.insert(
                provenance.physicalMeshIndex).second) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::invalidMeshProvenance,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                meshIndex,
                std::nullopt,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }

        const auto& sourceMesh =
            ccf.meshes[provenance.physicalMeshIndex];
        if (sourceMesh.vertices.size() >
                limits.geometryPerMesh.maxVertices ||
            sourceMesh.triangles.size() >
                limits.geometryPerMesh.maxTriangles ||
            sourceMesh.vertices.size() >
                limits.dynamicBspPerMesh.maximumVertices ||
            sourceMesh.triangles.size() >
                limits.dynamicBspPerMesh.maximumTriangles) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::limitExceeded,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                meshIndex,
                std::nullopt,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }
        const auto maximumMeshMaterialBindings = std::min(
            limits.maximumMaterialBindings,
            limits.dynamicBspPerMesh.maximumMaterialBindings);
        std::vector<LegacyDynamicBspMaterialBinding> bindings;
        bindings.reserve(std::min(
            sourceMesh.triangles.size(),
            maximumMeshMaterialBindings));
        std::unordered_set<std::uint32_t> seenMaterials;
        seenMaterials.reserve(bindings.capacity());
        for (const auto& triangle : sourceMesh.triangles) {
            if (!seenMaterials.insert(
                    triangle.materialReference).second) {
                continue;
            }
            if (bindings.size() >= maximumMeshMaterialBindings) {
                addIssue(
                    result,
                    PlayerActorCollisionIssueKind::limitExceeded,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    meshIndex,
                    std::nullopt,
                    provenance.physicalMeshIndex,
                    triangle.materialReference);
                clearPublishable(result);
                return result;
            }
            const auto material =
                materialByReference.find(
                    triangle.materialReference);
            if (material == materialByReference.end()) {
                addIssue(
                    result,
                    PlayerActorCollisionIssueKind::
                        missingMaterialBinding,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    meshIndex,
                    std::nullopt,
                    provenance.physicalMeshIndex,
                    triangle.materialReference);
                clearPublishable(result);
                return result;
            }
            bindings.push_back({
                .sourceReference =
                    triangle.materialReference,
                .collisionMode2152 =
                    material->second->collisionMode2152,
            });
        }

        ConvertedMeshGeometry converted;
        try {
            converted = convertLegacyGeometry(
                sourceMesh,
                basis,
                UvPolicy::preserveRaw,
                limits.geometryPerMesh);
        }
        catch (const GeometryError& error) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::geometryFailure,
                std::nullopt,
                std::nullopt,
                error.code(),
                meshIndex,
                std::nullopt,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }

        auto meshLimits = limits.dynamicBspPerMesh;
        meshLimits.maximumRetainedBytes = std::min(
            meshLimits.maximumRetainedBytes,
            limits.maximumRetainedBytes - retained);
        auto mesh = buildLegacyDynamicBsp(
            converted, bindings, meshLimits);
        if (!mesh.complete()) {
            const auto upstream = mesh.issues.empty()
                ? std::optional<LegacyDynamicBspBuildIssueKind>{}
                : std::optional{mesh.issues.front().kind};
            addIssue(
                result,
                upstream.has_value()
                    ? mapBuildIssue(*upstream)
                    : PlayerActorCollisionIssueKind::dynamicBspFailure,
                std::nullopt,
                upstream,
                std::nullopt,
                meshIndex,
                std::nullopt,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }
        if (!checkedAdd(retained, mesh.retainedPayloadBytes)) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::integerOverflow,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                meshIndex,
                std::nullopt,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }
        if (retained > limits.maximumRetainedBytes) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::
                    retainedByteLimitExceeded,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                meshIndex,
                std::nullopt,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }
        result.meshProvenance.push_back({
            .actor = provenance,
            .collisionMeshIndex = meshIndex,
            .sourceMeshReference = sourceMesh.reference,
        });
        result.meshes.push_back(std::move(mesh));
    }

    result.instances.reserve(actorVisual.model.instances.size());
    for (std::size_t instanceIndex = 0U;
         instanceIndex < actorVisual.model.instances.size();
         ++instanceIndex) {
        const auto& instance =
            actorVisual.model.instances[instanceIndex];
        const auto& provenance =
            actorVisual.instanceProvenance[instanceIndex];
        const auto meshIndex =
            static_cast<std::size_t>(instance.meshSlot);
        if (meshIndex >= result.meshes.size() ||
            provenance.legacySkinSlot != 0U ||
            provenance.blueprintIndex >= ccf.blueprints.size() ||
            provenance.physicalMeshIndex >= ccf.meshes.size() ||
            instance.sourceNodeReference !=
                provenance.blueprintReference ||
            ccf.blueprints[provenance.blueprintIndex].reference !=
                provenance.blueprintReference ||
            !ccf.blueprints[provenance.blueprintIndex]
                 .meshIndex.has_value() ||
            *ccf.blueprints[provenance.blueprintIndex].meshIndex !=
                provenance.physicalMeshIndex ||
            result.meshProvenance[meshIndex]
                    .actor.physicalMeshIndex !=
                provenance.physicalMeshIndex) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::
                    invalidInstanceProvenance,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                instanceIndex,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }
        const auto actorLocal = transformOf(instance);
        if (!orthonormal(actorLocal.linear) ||
            !finite(actorLocal.translation)) {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::
                    invalidActorLocalTransform,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                instanceIndex,
                provenance.physicalMeshIndex);
            clearPublishable(result);
            return result;
        }
        result.instances.push_back({
            .collisionMeshIndex = meshIndex,
            .actor = provenance,
            .actorLocal = actorLocal,
        });
    }

    result.retainedPayloadBytes = retained;
    return result;
}

} // namespace

bool PlayerActorCollisionAssembly::complete() const noexcept {
    if (!issues.empty() ||
        meshes.empty() ||
        instances.empty() ||
        meshes.size() != meshProvenance.size() ||
        retainedPayloadBytes == 0U) {
        return false;
    }

    std::uint64_t retained = 0U;
    if (!checkedBytes(
            retained, meshes.size(), sizeof(LegacyDynamicBspMesh)) ||
        !checkedBytes(
            retained,
            meshProvenance.size(),
            sizeof(PlayerActorCollisionMeshProvenance)) ||
        !checkedBytes(
            retained,
            instances.size(),
            sizeof(PlayerActorCollisionInstance))) {
        return false;
    }
    for (std::size_t meshIndex = 0U;
         meshIndex < meshes.size();
         ++meshIndex) {
        const auto& provenance = meshProvenance[meshIndex];
        if (!meshes[meshIndex].complete() ||
            !validRetainedBytes(meshes[meshIndex]) ||
            provenance.collisionMeshIndex != meshIndex ||
            provenance.actor.legacySkinSlot != 0U ||
            !checkedAdd(
                retained,
                meshes[meshIndex].retainedPayloadBytes)) {
            return false;
        }
    }
    for (const auto& instance : instances) {
        if (instance.collisionMeshIndex >= meshes.size() ||
            instance.actor.legacySkinSlot != 0U ||
            instance.actor.physicalMeshIndex !=
                meshProvenance[instance.collisionMeshIndex]
                    .actor.physicalMeshIndex ||
            !orthonormal(instance.actorLocal.linear) ||
            !finite(instance.actorLocal.translation) ||
            instance.actorLocal.rawScalar != 1.0F) {
            return false;
        }
    }
    return retained == retainedPayloadBytes;
}

PlayerActorCollisionAssembly buildPlayerActorCollisionAssembly(
    const assets::CcfMetadata& ccf,
    const PlayerActorVisualDrawAssembly& actorVisual,
    const BasisTransform& basis,
    const PlayerActorCollisionLimits& limits) {
    try {
        return build(ccf, actorVisual, basis, limits);
    }
    catch (const std::bad_alloc&) {
        PlayerActorCollisionAssembly result;
        try {
            addIssue(
                result,
                PlayerActorCollisionIssueKind::allocationFailure);
        }
        catch (...) {
        }
        return result;
    }
}

PlayerActorCollisionPublicationStatus
publishPlayerActorCollisionFrame(
    const PlayerActorCollisionAssembly& assembly,
    const ConvertedNodeTransform& actorWorld,
    const std::uint32_t actorObjectId,
    const bool active,
    const std::size_t worldRoomIndex,
    const std::span<LegacyDynamicBspLineObject> outputObjects,
    const std::span<LegacyDynamicBspRoomObjectRange>
        outputRoomRanges) noexcept {
    if (!assembly.complete()) {
        return PlayerActorCollisionPublicationStatus::invalidAssembly;
    }
    if (outputRoomRanges.empty() ||
        worldRoomIndex >= outputRoomRanges.size()) {
        return PlayerActorCollisionPublicationStatus::invalidInput;
    }
    if (outputObjects.size() != assembly.instances.size()) {
        return PlayerActorCollisionPublicationStatus::
            outputSizeMismatch;
    }
    if (!orthonormal(actorWorld.linear) ||
        !finite(actorWorld.translation) ||
        !std::isfinite(actorWorld.rawScalar)) {
        return PlayerActorCollisionPublicationStatus::invalidTransform;
    }

    // Complete validation pass: no caller-owned output changes before every
    // composition and mesh reference is known to be publishable.
    for (const auto& instance : assembly.instances) {
        if (instance.collisionMeshIndex >= assembly.meshes.size()) {
            return PlayerActorCollisionPublicationStatus::
                invalidAssembly;
        }
        const auto composed = tryComposeNodeTransforms(
            actorWorld, instance.actorLocal);
        if (!composed.has_value() ||
            !orthonormal(composed->linear) ||
            !finite(composed->translation)) {
            return PlayerActorCollisionPublicationStatus::
                invalidTransform;
        }
    }

    for (std::size_t index = 0U;
         index < assembly.instances.size();
         ++index) {
        const auto& instance = assembly.instances[index];
        const auto composed = tryComposeNodeTransforms(
            actorWorld, instance.actorLocal);
        outputObjects[index] = {
            .meshIndex = instance.collisionMeshIndex,
            .actorObjectId = actorObjectId,
            .active = active,
            .objectLocalToRuntime = composed->linear,
            .runtimeTranslation = composed->translation,
            .portalType = -1,
            .portalWorldRoomIndex = std::nullopt,
            .portalObjectVisible = false,
        };
    }
    std::fill(
        outputRoomRanges.begin(),
        outputRoomRanges.end(),
        LegacyDynamicBspRoomObjectRange{});
    outputRoomRanges[worldRoomIndex] = {
        .firstObjectIndex = 0U,
        .objectCount = outputObjects.size(),
    };
    return PlayerActorCollisionPublicationStatus::published;
}

} // namespace airfix::render
