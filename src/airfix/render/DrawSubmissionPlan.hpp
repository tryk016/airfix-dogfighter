#pragma once

#include "airfix/render/DrawModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::render {

enum class DrawSubmissionTextureRole : std::uint8_t {
    primary,
    secondary,
    environment,
};

enum class DrawSubmissionIssueKind : std::uint8_t {
    limitExceeded,
    integerOverflow,
    nonFiniteVertex,
    nonFiniteBounds,
    invalidBounds,
    nonFiniteTransform,
    indexOutOfRange,
    invalidRange,
    rangeCoverageMismatch,
    invalidMaterialSlot,
    invalidTexcoordMode,
    invalidInstanceMeshSlot,
    textureAssetOutOfRange,
};

struct DrawSubmissionIssue {
    DrawSubmissionIssueKind kind{DrawSubmissionIssueKind::limitExceeded};
    std::optional<std::size_t> meshSlot;
    std::optional<std::size_t> instanceIndex;
    std::optional<std::size_t> vertexIndex;
    std::optional<std::size_t> indexPosition;
    std::optional<std::size_t> rangeIndex;
    std::optional<std::size_t> materialSlot;
    std::optional<DrawSubmissionTextureRole> textureRole;
    std::optional<TextureAssetId> textureAssetId;
};

struct DrawSubmissionLimits {
    std::size_t maximumMeshes{65'536U};
    std::size_t maximumInstances{100'000U};
    std::size_t maximumTotalVertices{3'000'000U};
    std::size_t maximumTotalIndices{3'000'000U};
    std::size_t maximumTotalMaterials{65'536U};
    std::size_t maximumTotalRanges{1'000'000U};
    std::size_t maximumCommands{1'000'000U};
    std::size_t maximumSourceBytes{512U * 1024U * 1024U};
};

struct DrawMeshUploadMetadata {
    std::uint32_t meshSlot{};
    std::size_t vertexCount{};
    std::size_t indexCount{};

    [[nodiscard]] friend constexpr bool operator==(
        const DrawMeshUploadMetadata&,
        const DrawMeshUploadMetadata&) = default;
};

struct DrawSubmissionCommand {
    std::size_t instanceIndex{};
    std::uint32_t meshSlot{};
    std::size_t rangeIndex{};
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::uint32_t materialSlot{};
    TexcoordMode texcoordMode{TexcoordMode::none};
    std::optional<TextureAssetId> primary;
    std::optional<TextureAssetId> secondary;
    std::optional<TextureAssetId> environment;

    [[nodiscard]] friend constexpr bool operator==(
        const DrawSubmissionCommand&,
        const DrawSubmissionCommand&) = default;
};

struct DrawSubmissionPlan {
    // Uploads retain model.meshes order. Commands retain instance order and,
    // within each instance, mesh range order.
    std::vector<DrawMeshUploadMetadata> meshUploads;
    std::vector<DrawSubmissionCommand> commands;
};

struct DrawSubmissionDescription {
    // Any issue suppresses the complete plan atomically.
    std::optional<DrawSubmissionPlan> plan;
    std::vector<DrawSubmissionIssue> issues;
};

// Validates a backend-neutral DrawModelPayload and, only on complete success,
// publishes deterministic upload and indexed-draw metadata. Texture asset ID
// zero is valid whenever availableTextureAssetCount is nonzero.
[[nodiscard]] DrawSubmissionDescription buildDrawSubmissionPlan(
    const DrawModelPayload& model,
    std::size_t availableTextureAssetCount,
    const DrawSubmissionLimits& limits = {});

} // namespace airfix::render
