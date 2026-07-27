#include "airfix/render/PlayerActorSceneAssembly.hpp"

#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    PlayerActorSceneAssembly& result,
    const PlayerActorSceneIssueKind kind,
    const std::optional<PlayerActorVisualDrawIssueKind> actorVisualIssue =
        std::nullopt,
    const std::optional<std::size_t> staticInstanceIndex = std::nullopt,
    const std::optional<std::size_t> actorInstanceIndex = std::nullopt,
    const std::optional<std::size_t> actorMeshSlot = std::nullopt,
    const std::optional<PlayerActorVisualProvenance> actorProvenance =
        std::nullopt,
    const std::optional<GeometryErrorCode> geometryError = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .actorVisualIssue = actorVisualIssue,
        .staticInstanceIndex = staticInstanceIndex,
        .actorInstanceIndex = actorInstanceIndex,
        .actorMeshSlot = actorMeshSlot,
        .actorProvenance = actorProvenance,
        .geometryError = geometryError,
    });
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] bool accountCount(
    std::size_t& total,
    const std::size_t addition,
    const std::size_t limit,
    PlayerActorSceneAssembly& result) {
    std::size_t next = 0U;
    if (!checkedAdd(total, addition, next)) {
        addIssue(result, PlayerActorSceneIssueKind::integerOverflow);
        return false;
    }
    if (next > limit) {
        addIssue(result, PlayerActorSceneIssueKind::limitExceeded);
        return false;
    }
    total = next;
    return true;
}

[[nodiscard]] bool accountBytes(
    std::size_t& total,
    const std::size_t count,
    const std::size_t elementSize,
    const std::size_t limit,
    PlayerActorSceneAssembly& result) {
    std::size_t bytes = 0U;
    if (!checkedMultiply(count, elementSize, bytes)) {
        addIssue(result, PlayerActorSceneIssueKind::integerOverflow);
        return false;
    }
    return accountCount(total, bytes, limit, result);
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] std::optional<GeometryErrorCode> transformError(
    const ConvertedNodeTransform& transform) noexcept {
    if (!finite(transform.linear) || !finite(transform.translation) ||
        !std::isfinite(transform.rawScalar)) {
        return GeometryErrorCode::nonFiniteValue;
    }
    const auto linearDeterminant = determinant(transform.linear);
    if (!std::isfinite(linearDeterminant)) {
        return GeometryErrorCode::nonFiniteValue;
    }
    if (linearDeterminant == 0.0F) {
        return GeometryErrorCode::singularTransform;
    }
    return std::nullopt;
}

[[nodiscard]] ConvertedNodeTransform transformOf(
    const DrawMeshInstance& instance) noexcept {
    return {
        .linear = instance.modelLinear,
        .translation = instance.modelTranslation,
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] bool validateFinalLimits(
    const DrawModelPayload& staticModel,
    const DrawModelPayload& actorModel,
    const std::size_t finalMeshCount,
    const std::size_t finalInstanceCount,
    const PlayerActorSceneLimits& limits,
    PlayerActorSceneAssembly& result) {
    if (finalMeshCount > limits.maximumMeshes ||
        finalInstanceCount > limits.maximumInstances) {
        addIssue(result, PlayerActorSceneIssueKind::limitExceeded);
        return false;
    }

    std::size_t totalVertices = 0U;
    std::size_t totalIndices = 0U;
    std::size_t totalMaterials = 0U;
    std::size_t totalRanges = 0U;
    std::size_t totalBytes = 0U;

    if (!accountBytes(
            totalBytes,
            finalMeshCount,
            sizeof(DrawMeshPayload),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            finalInstanceCount,
            sizeof(DrawMeshInstance),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            actorModel.meshes.size(),
            sizeof(PlayerActorSceneMeshProvenance),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            actorModel.instances.size(),
            sizeof(PlayerActorSceneInstanceProvenance),
            limits.maximumTotalBytes,
            result) ||
        !accountBytes(
            totalBytes,
            1U,
            sizeof(PlayerActorSceneBinding),
            limits.maximumTotalBytes,
            result)) {
        return false;
    }

    const auto accountModel = [&](const DrawModelPayload& model) {
        for (const auto& mesh : model.meshes) {
            if (!accountCount(
                    totalVertices,
                    mesh.vertices.size(),
                    limits.maximumTotalVertices,
                    result) ||
                !accountCount(
                    totalIndices,
                    mesh.indices.size(),
                    limits.maximumTotalIndices,
                    result) ||
                !accountCount(
                    totalMaterials,
                    mesh.materials.size(),
                    limits.maximumTotalMaterials,
                    result) ||
                !accountCount(
                    totalRanges,
                    mesh.ranges.size(),
                    limits.maximumTotalRanges,
                    result) ||
                !accountBytes(
                    totalBytes,
                    mesh.vertices.size(),
                    sizeof(DrawVertex),
                    limits.maximumTotalBytes,
                    result) ||
                !accountBytes(
                    totalBytes,
                    mesh.indices.size(),
                    sizeof(std::uint32_t),
                    limits.maximumTotalBytes,
                    result) ||
                !accountBytes(
                    totalBytes,
                    mesh.materials.size(),
                    sizeof(DrawMaterial),
                    limits.maximumTotalBytes,
                    result) ||
                !accountBytes(
                    totalBytes,
                    mesh.ranges.size(),
                    sizeof(DrawRange),
                    limits.maximumTotalBytes,
                    result)) {
                return false;
            }
        }
        return true;
    };

    return accountModel(staticModel) && accountModel(actorModel);
}

[[nodiscard]] bool validateAddressableCounts(
    const std::size_t finalMeshCount,
    const std::size_t finalInstanceCount,
    PlayerActorSceneAssembly& result) {
    if constexpr (
        std::numeric_limits<std::size_t>::max() >
        std::numeric_limits<std::uint32_t>::max()) {
        constexpr auto maximumUint32 =
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max());
        constexpr auto maximumUint32Count =
            maximumUint32 + std::size_t{1U};
        if (finalMeshCount > maximumUint32Count) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::meshSlotOverflow);
            return false;
        }
        if (finalInstanceCount > maximumUint32Count) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::instanceIndexOverflow);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validateStaticModel(
    const DrawModelPayload& model,
    PlayerActorSceneAssembly& result) {
    for (std::size_t instanceIndex = 0U;
         instanceIndex < model.instances.size();
         ++instanceIndex) {
        const auto& instance = model.instances[instanceIndex];
        if (static_cast<std::size_t>(instance.meshSlot) >=
            model.meshes.size()) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::invalidStaticMeshSlot,
                std::nullopt,
                instanceIndex);
            return false;
        }
        const auto error = transformError(transformOf(instance));
        if (error.has_value()) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::invalidStaticTransform,
                std::nullopt,
                instanceIndex,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                error);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validateActorVisualEnvelope(
    const PlayerActorVisualDrawAssembly& actorVisual,
    PlayerActorSceneAssembly& result) {
    if (!actorVisual.issues.empty()) {
        const auto& issue = actorVisual.issues.front();
        addIssue(
            result,
            PlayerActorSceneIssueKind::actorVisualFailure,
            issue.kind,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            issue.geometryError);
        return false;
    }

    if (actorVisual.model.meshes.empty() ||
        actorVisual.model.instances.empty()) {
        addIssue(result, PlayerActorSceneIssueKind::emptyActorVisual);
        return false;
    }
    if (actorVisual.model.meshes.size() !=
            actorVisual.meshProvenance.size() ||
        actorVisual.model.instances.size() !=
            actorVisual.instanceProvenance.size()) {
        addIssue(
            result,
            PlayerActorSceneIssueKind::invalidActorVisualAssembly);
        return false;
    }
    return true;
}

[[nodiscard]] bool validateActorVisualDetails(
    const PlayerActorVisualDrawAssembly& actorVisual,
    PlayerActorSceneAssembly& result) {
    std::vector<bool> seen(actorVisual.model.meshes.size(), false);
    std::size_t nextFirstUseSlot = 0U;
    for (std::size_t instanceIndex = 0U;
         instanceIndex < actorVisual.model.instances.size();
         ++instanceIndex) {
        const auto& instance = actorVisual.model.instances[instanceIndex];
        const auto& provenance =
            actorVisual.instanceProvenance[instanceIndex];
        const auto meshSlot = static_cast<std::size_t>(instance.meshSlot);
        if (meshSlot >= actorVisual.model.meshes.size()) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::invalidActorMeshSlot,
                std::nullopt,
                std::nullopt,
                instanceIndex,
                meshSlot,
                provenance);
            return false;
        }
        if (instance.sourceNodeReference !=
                provenance.blueprintReference ||
            provenance.legacySkinSlot != 0U ||
            actorVisual.meshProvenance[meshSlot].legacySkinSlot != 0U ||
            provenance.physicalMeshIndex !=
                actorVisual.meshProvenance[meshSlot].physicalMeshIndex) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::invalidActorProvenance,
                std::nullopt,
                std::nullopt,
                instanceIndex,
                meshSlot,
                provenance);
            return false;
        }
        if (!seen[meshSlot]) {
            if (meshSlot != nextFirstUseSlot ||
                actorVisual.meshProvenance[meshSlot] != provenance) {
                addIssue(
                    result,
                    PlayerActorSceneIssueKind::invalidActorProvenance,
                    std::nullopt,
                    std::nullopt,
                    instanceIndex,
                    meshSlot,
                    provenance);
                return false;
            }
            seen[meshSlot] = true;
            ++nextFirstUseSlot;
        }
        const auto error = transformError(transformOf(instance));
        if (error.has_value()) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::invalidActorLocalTransform,
                std::nullopt,
                std::nullopt,
                instanceIndex,
                meshSlot,
                provenance,
                error);
            return false;
        }
    }
    if (nextFirstUseSlot != actorVisual.model.meshes.size()) {
        addIssue(
            result,
            PlayerActorSceneIssueKind::invalidActorProvenance);
        return false;
    }
    return true;
}

} // namespace

PlayerActorSceneAssembly buildPlayerActorSceneAssembly(
    DrawModelPayload staticModel,
    PlayerActorVisualDrawAssembly actorVisual,
    const ConvertedNodeTransform& actorWorld,
    const PlayerActorSceneLimits& limits) {
    PlayerActorSceneAssembly result;

    if (const auto error = transformError(actorWorld);
        error.has_value()) {
        addIssue(
            result,
            PlayerActorSceneIssueKind::invalidActorWorldTransform,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            error);
        return result;
    }
    if (!validateActorVisualEnvelope(actorVisual, result)) {
        return result;
    }

    std::size_t finalMeshCount = 0U;
    std::size_t finalInstanceCount = 0U;
    if (!checkedAdd(
            staticModel.meshes.size(),
            actorVisual.model.meshes.size(),
            finalMeshCount) ||
        !checkedAdd(
            staticModel.instances.size(),
            actorVisual.model.instances.size(),
            finalInstanceCount)) {
        addIssue(result, PlayerActorSceneIssueKind::integerOverflow);
        return result;
    }
    if (!validateAddressableCounts(
            finalMeshCount,
            finalInstanceCount,
            result) ||
        !validateFinalLimits(
            staticModel,
            actorVisual.model,
            finalMeshCount,
            finalInstanceCount,
            limits,
            result)) {
        return result;
    }
    // Global count/byte preflight deliberately precedes the first-use bitmap
    // allocation in validateActorVisualDetails. Forged oversized input thus
    // fails under the caller's bounds rather than requesting proportional
    // unchecked working storage.
    if (!validateStaticModel(staticModel, result) ||
        !validateActorVisualDetails(actorVisual, result)) {
        return result;
    }

    const auto firstMeshSlot = staticModel.meshes.size();
    const auto firstInstanceIndex = staticModel.instances.size();

    DrawModelPayload candidate = std::move(staticModel);
    candidate.meshes.reserve(finalMeshCount);
    candidate.instances.reserve(finalInstanceCount);

    std::vector<PlayerActorSceneMeshProvenance> meshProvenance;
    meshProvenance.reserve(actorVisual.model.meshes.size());
    for (std::size_t actorMeshSlot = 0U;
         actorMeshSlot < actorVisual.model.meshes.size();
         ++actorMeshSlot) {
        std::size_t finalMeshSlot = 0U;
        if (!checkedAdd(
                firstMeshSlot,
                actorMeshSlot,
                finalMeshSlot) ||
            finalMeshSlot >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::meshSlotOverflow,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                actorMeshSlot,
                actorVisual.meshProvenance[actorMeshSlot]);
            return result;
        }
        meshProvenance.push_back({
            .actor = actorVisual.meshProvenance[actorMeshSlot],
            .finalMeshSlot =
                static_cast<std::uint32_t>(finalMeshSlot),
        });
    }
    candidate.meshes.insert(
        candidate.meshes.end(),
        std::make_move_iterator(actorVisual.model.meshes.begin()),
        std::make_move_iterator(actorVisual.model.meshes.end()));

    std::vector<PlayerActorSceneInstanceProvenance> instanceProvenance;
    instanceProvenance.reserve(actorVisual.model.instances.size());
    for (std::size_t actorInstanceIndex = 0U;
         actorInstanceIndex < actorVisual.model.instances.size();
         ++actorInstanceIndex) {
        const auto& actorInstance =
            actorVisual.model.instances[actorInstanceIndex];
        const auto& actorProvenance =
            actorVisual.instanceProvenance[actorInstanceIndex];

        std::size_t finalMeshSlot = 0U;
        std::size_t finalInstanceIndex = 0U;
        if (!checkedAdd(
                firstMeshSlot,
                static_cast<std::size_t>(actorInstance.meshSlot),
                finalMeshSlot) ||
            finalMeshSlot >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::meshSlotOverflow,
                std::nullopt,
                std::nullopt,
                actorInstanceIndex,
                actorInstance.meshSlot,
                actorProvenance);
            return result;
        }
        if (!checkedAdd(
                firstInstanceIndex,
                actorInstanceIndex,
                finalInstanceIndex) ||
            finalInstanceIndex >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::instanceIndexOverflow,
                std::nullopt,
                std::nullopt,
                actorInstanceIndex,
                actorInstance.meshSlot,
                actorProvenance);
            return result;
        }

        const auto actorLocal = transformOf(actorInstance);
        ConvertedNodeTransform absolute;
        try {
            absolute = composeNodeTransforms(
                actorWorld, actorLocal);
        }
        catch (const GeometryError& error) {
            addIssue(
                result,
                PlayerActorSceneIssueKind::invalidComposedTransform,
                std::nullopt,
                std::nullopt,
                actorInstanceIndex,
                actorInstance.meshSlot,
                actorProvenance,
                error.code());
            return result;
        }
        candidate.instances.push_back({
            .meshSlot = static_cast<std::uint32_t>(finalMeshSlot),
            .sourceNodeReference =
                actorInstance.sourceNodeReference,
            .modelLinear = absolute.linear,
            .modelTranslation = absolute.translation,
        });
        instanceProvenance.push_back({
            .actor = actorProvenance,
            .finalInstanceIndex =
                static_cast<std::uint32_t>(finalInstanceIndex),
            .actorLocal = actorLocal,
        });
    }

    result.model = std::move(candidate);
    result.actorMeshProvenance = std::move(meshProvenance);
    result.actorInstanceProvenance =
        std::move(instanceProvenance);
    result.actorBinding = PlayerActorSceneBinding{
        .firstMeshSlot = firstMeshSlot,
        .meshCount = result.actorMeshProvenance.size(),
        .firstInstanceIndex = firstInstanceIndex,
        .instanceCount = result.actorInstanceProvenance.size(),
    };
    return result;
}

} // namespace airfix::render
