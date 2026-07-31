#include "airfix/render/DrawSubmissionPlan.hpp"

#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

void addIssue(
    DrawSubmissionDescription& result,
    const DrawSubmissionIssueKind kind,
    const std::optional<std::size_t> meshSlot = std::nullopt,
    const std::optional<std::size_t> instanceIndex = std::nullopt,
    const std::optional<std::size_t> vertexIndex = std::nullopt,
    const std::optional<std::size_t> indexPosition = std::nullopt,
    const std::optional<std::size_t> rangeIndex = std::nullopt,
    const std::optional<std::size_t> materialSlot = std::nullopt,
    const std::optional<DrawSubmissionTextureRole> textureRole = std::nullopt,
    const std::optional<TextureAssetId> textureAssetId = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .meshSlot = meshSlot,
        .instanceIndex = instanceIndex,
        .vertexIndex = vertexIndex,
        .indexPosition = indexPosition,
        .rangeIndex = rangeIndex,
        .materialSlot = materialSlot,
        .textureRole = textureRole,
        .textureAssetId = textureAssetId,
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
    DrawSubmissionDescription& result) {
    std::size_t next = 0U;
    if (!checkedAdd(total, addition, next)) {
        addIssue(result, DrawSubmissionIssueKind::integerOverflow);
        return false;
    }
    if (next > limit) {
        addIssue(result, DrawSubmissionIssueKind::limitExceeded);
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
    DrawSubmissionDescription& result) {
    std::size_t bytes = 0U;
    if (!checkedMultiply(count, elementSize, bytes)) {
        addIssue(result, DrawSubmissionIssueKind::integerOverflow);
        return false;
    }
    return accountCount(total, bytes, limit, result);
}

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value.u) && std::isfinite(value.v);
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] bool finite(const DrawMaterialState& value) noexcept {
    return std::isfinite(value.scalar2140) &&
        finite(value.firstVector2140) &&
        finite(value.secondVector2140);
}

[[nodiscard]] bool ordered(const Bounds3& bounds) noexcept {
    return bounds.minimum.x <= bounds.maximum.x &&
        bounds.minimum.y <= bounds.maximum.y &&
        bounds.minimum.z <= bounds.maximum.z;
}

[[nodiscard]] bool contains(
    const Bounds3& bounds,
    const Vec3& point) noexcept {
    return point.x >= bounds.minimum.x &&
        point.x <= bounds.maximum.x &&
        point.y >= bounds.minimum.y &&
        point.y <= bounds.maximum.y &&
        point.z >= bounds.minimum.z &&
        point.z <= bounds.maximum.z;
}

[[nodiscard]] bool validTexcoordMode(const TexcoordMode mode) noexcept {
    return mode == TexcoordMode::none || mode == TexcoordMode::uv0;
}

[[nodiscard]] bool validateTexture(
    DrawSubmissionDescription& result,
    const std::optional<TextureAssetId> texture,
    const DrawSubmissionTextureRole role,
    const std::size_t availableTextureAssetCount,
    const std::size_t meshSlot,
    const std::size_t materialSlot) {
    if (!texture.has_value() ||
        static_cast<std::size_t>(texture->value) <
            availableTextureAssetCount) {
        return true;
    }
    addIssue(
        result,
        DrawSubmissionIssueKind::textureAssetOutOfRange,
        meshSlot,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        materialSlot,
        role,
        texture);
    return false;
}

} // namespace

DrawSubmissionDescription buildDrawSubmissionPlan(
    const DrawModelPayload& model,
    const std::size_t availableTextureAssetCount,
    const DrawSubmissionLimits& limits) {
    DrawSubmissionDescription result;

    if (model.meshes.size() > limits.maximumMeshes ||
        model.instances.size() > limits.maximumInstances) {
        addIssue(result, DrawSubmissionIssueKind::limitExceeded);
        return result;
    }
    // meshSlot is a uint32_t in both source instances and public metadata.
    if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t)) {
        constexpr auto maximumAddressableMeshes =
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()) +
            std::size_t{1U};
        if (model.meshes.size() > maximumAddressableMeshes) {
            addIssue(result, DrawSubmissionIssueKind::integerOverflow);
            return result;
        }
    }

    std::size_t totalVertices = 0U;
    std::size_t totalIndices = 0U;
    std::size_t totalMaterials = 0U;
    std::size_t totalRanges = 0U;
    std::size_t sourceBytes = 0U;
    if (!accountBytes(
            sourceBytes,
            model.meshes.size(),
            sizeof(DrawMeshPayload),
            limits.maximumSourceBytes,
            result) ||
        !accountBytes(
            sourceBytes,
            model.instances.size(),
            sizeof(DrawMeshInstance),
            limits.maximumSourceBytes,
            result)) {
        return result;
    }

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
                sourceBytes,
                mesh.vertices.size(),
                sizeof(DrawVertex),
                limits.maximumSourceBytes,
                result) ||
            !accountBytes(
                sourceBytes,
                mesh.indices.size(),
                sizeof(std::uint32_t),
                limits.maximumSourceBytes,
                result) ||
            !accountBytes(
                sourceBytes,
                mesh.materials.size(),
                sizeof(DrawMaterial),
                limits.maximumSourceBytes,
                result) ||
            !accountBytes(
                sourceBytes,
                mesh.ranges.size(),
                sizeof(DrawRange),
                limits.maximumSourceBytes,
                result)) {
            return result;
        }
    }

    std::size_t commandCount = 0U;
    for (std::size_t instanceIndex = 0U;
         instanceIndex < model.instances.size();
         ++instanceIndex) {
        const auto& instance = model.instances[instanceIndex];
        if (static_cast<std::size_t>(instance.meshSlot) >=
            model.meshes.size()) {
            addIssue(
                result,
                DrawSubmissionIssueKind::invalidInstanceMeshSlot,
                instance.meshSlot,
                instanceIndex);
            return result;
        }
        if (!accountCount(
                commandCount,
                model.meshes[instance.meshSlot].ranges.size(),
                limits.maximumCommands,
                result)) {
            return result;
        }
    }

    for (std::size_t meshSlot = 0U;
         meshSlot < model.meshes.size();
         ++meshSlot) {
        const auto& mesh = model.meshes[meshSlot];
        for (std::size_t vertexIndex = 0U;
             vertexIndex < mesh.vertices.size();
             ++vertexIndex) {
            const auto& vertex = mesh.vertices[vertexIndex];
            if (!finite(vertex.position) || !finite(vertex.normal) ||
                !finite(vertex.uv)) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::nonFiniteVertex,
                    meshSlot,
                    std::nullopt,
                    vertexIndex);
                return result;
            }
        }
        if (!finite(mesh.localBounds.minimum) ||
            !finite(mesh.localBounds.maximum)) {
            addIssue(
                result,
                DrawSubmissionIssueKind::nonFiniteBounds,
                meshSlot);
            return result;
        }
        if (!ordered(mesh.localBounds) ||
            (mesh.vertices.empty() &&
             (mesh.localBounds.minimum != Vec3{} ||
              mesh.localBounds.maximum != Vec3{}))) {
            addIssue(
                result,
                DrawSubmissionIssueKind::invalidBounds,
                meshSlot);
            return result;
        }
        for (std::size_t vertexIndex = 0U;
             vertexIndex < mesh.vertices.size();
             ++vertexIndex) {
            if (!contains(
                    mesh.localBounds,
                    mesh.vertices[vertexIndex].position)) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::invalidBounds,
                    meshSlot,
                    std::nullopt,
                    vertexIndex);
                return result;
            }
        }
        for (std::size_t indexPosition = 0U;
             indexPosition < mesh.indices.size();
             ++indexPosition) {
            if (static_cast<std::size_t>(mesh.indices[indexPosition]) >=
                mesh.vertices.size()) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::indexOutOfRange,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    indexPosition);
                return result;
            }
        }
        for (std::size_t materialSlot = 0U;
             materialSlot < mesh.materials.size();
             ++materialSlot) {
            const auto& material = mesh.materials[materialSlot];
            if (!finite(material.state)) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::nonFiniteMaterialState,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    materialSlot);
                return result;
            }
            if (!validateTexture(
                    result,
                    material.primary,
                    DrawSubmissionTextureRole::primary,
                    availableTextureAssetCount,
                    meshSlot,
                    materialSlot) ||
                !validateTexture(
                    result,
                    material.secondary,
                    DrawSubmissionTextureRole::secondary,
                    availableTextureAssetCount,
                    meshSlot,
                    materialSlot) ||
                !validateTexture(
                    result,
                    material.environment,
                    DrawSubmissionTextureRole::environment,
                    availableTextureAssetCount,
                    meshSlot,
                    materialSlot)) {
                return result;
            }
        }

        std::size_t expectedFirstIndex = 0U;
        for (std::size_t rangeIndex = 0U;
             rangeIndex < mesh.ranges.size();
             ++rangeIndex) {
            const auto& range = mesh.ranges[rangeIndex];
            if (range.indexCount == 0U ||
                range.indexCount % 3U != 0U) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::invalidRange,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    rangeIndex);
                return result;
            }
            if (!validTexcoordMode(range.texcoordMode)) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::invalidTexcoordMode,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    rangeIndex);
                return result;
            }
            if (range.materialSlot >= mesh.materials.size()) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::invalidMaterialSlot,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    rangeIndex,
                    range.materialSlot);
                return result;
            }
            if (static_cast<std::size_t>(range.firstIndex) !=
                expectedFirstIndex) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::rangeCoverageMismatch,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    rangeIndex);
                return result;
            }
            std::size_t rangeEnd = 0U;
            if (!checkedAdd(
                    expectedFirstIndex,
                    static_cast<std::size_t>(range.indexCount),
                    rangeEnd)) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::integerOverflow,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    rangeIndex);
                return result;
            }
            if (rangeEnd > mesh.indices.size()) {
                addIssue(
                    result,
                    DrawSubmissionIssueKind::rangeCoverageMismatch,
                    meshSlot,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    rangeIndex);
                return result;
            }
            expectedFirstIndex = rangeEnd;
        }
        if (expectedFirstIndex != mesh.indices.size()) {
            addIssue(
                result,
                DrawSubmissionIssueKind::rangeCoverageMismatch,
                meshSlot);
            return result;
        }
    }

    for (std::size_t instanceIndex = 0U;
         instanceIndex < model.instances.size();
         ++instanceIndex) {
        const auto& instance = model.instances[instanceIndex];
        if (!finite(instance.modelLinear) ||
            !finite(instance.modelTranslation)) {
            addIssue(
                result,
                DrawSubmissionIssueKind::nonFiniteTransform,
                instance.meshSlot,
                instanceIndex);
            return result;
        }
    }

    DrawSubmissionPlan candidate;
    candidate.meshUploads.reserve(model.meshes.size());
    candidate.commands.reserve(commandCount);
    for (std::size_t meshSlot = 0U;
         meshSlot < model.meshes.size();
         ++meshSlot) {
        candidate.meshUploads.push_back({
            .meshSlot = static_cast<std::uint32_t>(meshSlot),
            .vertexCount = model.meshes[meshSlot].vertices.size(),
            .indexCount = model.meshes[meshSlot].indices.size(),
        });
    }
    for (std::size_t instanceIndex = 0U;
         instanceIndex < model.instances.size();
         ++instanceIndex) {
        const auto& instance = model.instances[instanceIndex];
        const auto& mesh = model.meshes[instance.meshSlot];
        for (std::size_t rangeIndex = 0U;
             rangeIndex < mesh.ranges.size();
             ++rangeIndex) {
            const auto& range = mesh.ranges[rangeIndex];
            const auto& material = mesh.materials[range.materialSlot];
            candidate.commands.push_back({
                .instanceIndex = instanceIndex,
                .meshSlot = instance.meshSlot,
                .rangeIndex = rangeIndex,
                .firstIndex = range.firstIndex,
                .indexCount = range.indexCount,
                .materialSlot = range.materialSlot,
                .texcoordMode = range.texcoordMode,
                .primary = material.primary,
                .secondary = material.secondary,
                .environment = material.environment,
                .materialState = material.state,
            });
        }
    }
    result.plan = std::move(candidate);
    return result;
}

} // namespace airfix::render
