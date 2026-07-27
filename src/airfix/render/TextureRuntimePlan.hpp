#pragma once

#include "airfix/assets/AssetResolver.hpp"
#include "airfix/assets/LegacyFormats.hpp"
#include "airfix/render/DrawMesh.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

struct TextureImportRequest {
    TextureAssetId assetId;
    std::size_t archiveFileIndex{};

    [[nodiscard]] friend constexpr bool operator==(
        const TextureImportRequest&,
        const TextureImportRequest&) = default;
};

enum class TextureBindingIssueKind : std::uint8_t {
    upstreamResolutionIssue,
    dependencyMismatch,
    duplicateMaterialReference,
    unresolvedTexture,
    nonUniqueTexture,
    missingArchiveFileIndex,
    unknownMaterialReference,
    duplicateTextureRole,
    invalidTextureRole,
    limitExceeded,
    integerOverflow,
};

struct TextureBindingIssue {
    TextureBindingIssueKind kind{TextureBindingIssueKind::unresolvedTexture};
    std::optional<std::size_t> entryIndex;
    std::optional<std::uint32_t> materialReference;
    std::optional<assets::TextureDependencyRole> role;
    std::optional<assets::TextureEntryIssueKind> upstreamIssue;
};

struct TextureBindingPlanLimits {
    std::size_t maximumMaterials{65'536U};
    std::size_t maximumTextureEntries{262'144U};
    std::size_t maximumImports{262'144U};
};

struct TextureBindingPlan {
    // Material order is exactly materialReferences order. Any issue clears
    // both public payload vectors atomically.
    std::vector<DrawMaterial> materials;
    // Dense asset IDs are assigned in first resolved-entry use order.
    std::vector<TextureImportRequest> imports;
    std::vector<TextureBindingIssue> issues;
};

[[nodiscard]] TextureBindingPlan buildTextureBindingPlan(
    std::span<const std::uint32_t> materialReferences,
    std::span<const assets::TextureDependency> expectedDependencies,
    const assets::TextureEntryResolution& textureResolution,
    const TextureBindingPlanLimits& limits = {});

enum class GtiMipPolicy : std::uint8_t {
    authoredChain,
    generateFromBase,
};

struct GtiUploadLevel {
    std::uint32_t level{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t bytesPerRow{};
    std::uint64_t rgbaBytes{};

    [[nodiscard]] friend constexpr bool operator==(
        const GtiUploadLevel&,
        const GtiUploadLevel&) = default;
};

struct GtiUploadPlan {
    TextureImportRequest request;
    std::size_t variantIndex{};
    std::uint32_t format{};
    std::optional<std::uint32_t> checksum;
    GtiMipPolicy mipPolicy{GtiMipPolicy::authoredChain};
    // CPU-decoded levels copied to the backend. Generated levels deliberately
    // have no upload record.
    std::vector<GtiUploadLevel> uploadLevels;
    std::uint32_t allocatedMipCount{};
    std::uint32_t uploadedMipCount{};
    std::uint64_t decodedRgbaBytes{};
    std::uint64_t uploadRgbaBytes{};
    // RGBA8 byte footprint of every allocated level, including generated mips.
    std::uint64_t residentRgbaBytes{};
};

enum class GtiUploadIssueKind : std::uint8_t {
    noSupportedVariant,
    duplicateVariantFormat,
    invalidMetadata,
    invalidDimensions,
    invalidMipCount,
    invalidPalette,
    invalidBaseLayout,
    limitExceeded,
    integerOverflow,
};

struct GtiUploadIssue {
    GtiUploadIssueKind kind{GtiUploadIssueKind::invalidMetadata};
    std::optional<std::size_t> variantIndex;
    std::optional<std::uint32_t> level;
};

struct GtiUploadLimits {
    std::size_t maximumVariants{4'096U};
    std::uint32_t maximumDimension{32'768U};
    std::uint32_t maximumDeclaredMipCount{16U};
    std::uint32_t maximumAllocatedMipCount{32U};
    std::size_t maximumUploadLevels{32U};
    std::uint64_t maximumDecodedRgbaBytes{512U * 1024U * 1024U};
    std::uint64_t maximumUploadRgbaBytes{512U * 1024U * 1024U};
    std::uint64_t maximumResidentRgbaBytes{512U * 1024U * 1024U};
    std::size_t maximumMetadataBytes{16U * 1024U * 1024U};
};

struct GtiUploadDescription {
    // The plan is published only when every validation and limit check passes.
    std::optional<GtiUploadPlan> plan;
    std::vector<GtiUploadIssue> issues;
};

// Produces backend-neutral RGBA8 upload metadata only. It performs no archive
// I/O and makes no sRGB, premultiplication, or vertical-orientation decision.
[[nodiscard]] GtiUploadDescription describeGtiUpload(
    const TextureImportRequest& request,
    const assets::GtiMetadata& metadata,
    const GtiUploadLimits& limits = {});

} // namespace airfix::render
