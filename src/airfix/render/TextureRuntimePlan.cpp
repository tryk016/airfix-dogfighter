#include "airfix/render/TextureRuntimePlan.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace airfix::render {
namespace {

struct RoleState {
    bool primary{};
    bool secondary{};
    bool environment{};
};

[[nodiscard]] bool validRole(const assets::TextureDependencyRole role) noexcept {
    switch (role) {
    case assets::TextureDependencyRole::primary:
    case assets::TextureDependencyRole::secondary:
    case assets::TextureDependencyRole::environment:
        return true;
    }
    return false;
}

[[nodiscard]] bool roleWasSeen(
    RoleState& state,
    const assets::TextureDependencyRole role) noexcept {
    bool* seen = nullptr;
    switch (role) {
    case assets::TextureDependencyRole::primary: seen = &state.primary; break;
    case assets::TextureDependencyRole::secondary: seen = &state.secondary; break;
    case assets::TextureDependencyRole::environment:
        seen = &state.environment;
        break;
    }
    if (seen == nullptr) {
        return false;
    }
    const bool result = *seen;
    *seen = true;
    return result;
}

void assignRole(
    DrawMaterial& material,
    const assets::TextureDependencyRole role,
    const TextureAssetId assetId) {
    switch (role) {
    case assets::TextureDependencyRole::primary:
        material.primary = assetId;
        break;
    case assets::TextureDependencyRole::secondary:
        material.secondary = assetId;
        break;
    case assets::TextureDependencyRole::environment:
        material.environment = assetId;
        break;
    }
}

[[nodiscard]] bool checkedAdd(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checkedMultiply(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checkedMetadataAdd(
    const std::size_t count,
    const std::size_t elementSize,
    std::size_t& total) noexcept {
    if (count != 0U &&
        elementSize > std::numeric_limits<std::size_t>::max() / count) {
        return false;
    }
    const auto bytes = count * elementSize;
    if (bytes > std::numeric_limits<std::size_t>::max() - total) {
        return false;
    }
    total += bytes;
    return true;
}

void addUploadIssue(
    GtiUploadDescription& result,
    const GtiUploadIssueKind kind,
    const std::optional<std::size_t> variantIndex = std::nullopt,
    const std::optional<std::uint32_t> level = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .variantIndex = variantIndex,
        .level = level,
    });
}

} // namespace

TextureBindingPlan buildTextureBindingPlan(
    const std::span<const std::uint32_t> materialReferences,
    const std::span<const assets::TextureDependency> expectedDependencies,
    const assets::TextureEntryResolution& textureResolution,
    const TextureBindingPlanLimits& limits) {
    TextureBindingPlan result;
    if (materialReferences.size() > limits.maximumMaterials ||
        expectedDependencies.size() > limits.maximumTextureEntries ||
        textureResolution.entries.size() > limits.maximumTextureEntries ||
        textureResolution.issues.size() > limits.maximumTextureEntries) {
        result.issues.push_back({
            .kind = TextureBindingIssueKind::limitExceeded,
            .entryIndex = std::nullopt,
            .materialReference = std::nullopt,
            .role = std::nullopt,
            .upstreamIssue = std::nullopt,
        });
        return result;
    }

    for (const auto& upstream : textureResolution.issues) {
        result.issues.push_back({
            .kind = TextureBindingIssueKind::upstreamResolutionIssue,
            .entryIndex = upstream.dependencyIndex,
            .materialReference = std::nullopt,
            .role = std::nullopt,
            .upstreamIssue = upstream.kind,
        });
    }
    if (!result.issues.empty()) {
        return result;
    }

    const auto& textureEntries = textureResolution.entries;
    if (textureEntries.size() != expectedDependencies.size()) {
        result.issues.push_back({
            .kind = TextureBindingIssueKind::dependencyMismatch,
            .entryIndex = std::nullopt,
            .materialReference = std::nullopt,
            .role = std::nullopt,
            .upstreamIssue = std::nullopt,
        });
        return result;
    }
    for (std::size_t index = 0U; index < textureEntries.size(); ++index) {
        const auto& entry = textureEntries[index];
        const auto& dependency = expectedDependencies[index];
        if (entry.role != dependency.role ||
            entry.materialReference != dependency.materialReference ||
            entry.materialIndex != dependency.materialIndex ||
            entry.sourceText != dependency.sourceText) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::dependencyMismatch,
                .entryIndex = index,
                .materialReference = dependency.materialReference,
                .role = dependency.role,
                .upstreamIssue = std::nullopt,
            });
        }
    }
    if (!result.issues.empty()) {
        return result;
    }

    std::unordered_map<std::uint32_t, std::size_t> materialSlots;
    materialSlots.reserve(materialReferences.size());
    result.materials.reserve(materialReferences.size());
    for (std::size_t index = 0U; index < materialReferences.size(); ++index) {
        const auto reference = materialReferences[index];
        const auto [unused, inserted] = materialSlots.emplace(
            reference, result.materials.size());
        if (!inserted) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::duplicateMaterialReference,
                .entryIndex = std::nullopt,
                .materialReference = reference,
                .role = std::nullopt,
                .upstreamIssue = std::nullopt,
            });
        }
        result.materials.push_back({
            .sourceReference = reference,
            .primary = std::nullopt,
            .secondary = std::nullopt,
            .environment = std::nullopt,
        });
    }

    std::vector<RoleState> roles(result.materials.size());
    std::unordered_map<std::size_t, TextureAssetId> assetIds;
    assetIds.reserve(
        textureEntries.size() < limits.maximumImports
            ? textureEntries.size()
            : limits.maximumImports);
    result.imports.reserve(
        textureEntries.size() < limits.maximumImports
            ? textureEntries.size()
            : limits.maximumImports);

    for (std::size_t entryIndex = 0U;
         entryIndex < textureEntries.size();
         ++entryIndex) {
        const auto& entry = textureEntries[entryIndex];
        bool valid = true;
        const auto materialIt = materialSlots.find(entry.materialReference);
        if (materialIt == materialSlots.end()) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::unknownMaterialReference,
                .entryIndex = entryIndex,
                .materialReference = entry.materialReference,
                .role = entry.role,
                .upstreamIssue = std::nullopt,
            });
            valid = false;
        }
        if (!validRole(entry.role)) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::invalidTextureRole,
                .entryIndex = entryIndex,
                .materialReference = entry.materialReference,
                .role = std::nullopt,
                .upstreamIssue = std::nullopt,
            });
            valid = false;
        }
        if (entry.status == assets::TextureEntryStatus::ambiguous) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::nonUniqueTexture,
                .entryIndex = entryIndex,
                .materialReference = entry.materialReference,
                .role = entry.role,
                .upstreamIssue = std::nullopt,
            });
            valid = false;
        }
        else if (entry.status != assets::TextureEntryStatus::unique) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::unresolvedTexture,
                .entryIndex = entryIndex,
                .materialReference = entry.materialReference,
                .role = entry.role,
                .upstreamIssue = std::nullopt,
            });
            valid = false;
        }
        else if (!entry.archiveFileIndex.has_value()) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::missingArchiveFileIndex,
                .entryIndex = entryIndex,
                .materialReference = entry.materialReference,
                .role = entry.role,
                .upstreamIssue = std::nullopt,
            });
            valid = false;
        }

        if (materialIt != materialSlots.end() && validRole(entry.role) &&
            roleWasSeen(roles[materialIt->second], entry.role)) {
            result.issues.push_back({
                .kind = TextureBindingIssueKind::duplicateTextureRole,
                .entryIndex = entryIndex,
                .materialReference = entry.materialReference,
                .role = entry.role,
                .upstreamIssue = std::nullopt,
            });
            valid = false;
        }
        if (!valid) {
            continue;
        }

        const auto archiveFileIndex = *entry.archiveFileIndex;
        auto assetIt = assetIds.find(archiveFileIndex);
        if (assetIt == assetIds.end()) {
            if (result.imports.size() >= limits.maximumImports) {
                result.issues.push_back({
                    .kind = TextureBindingIssueKind::limitExceeded,
                    .entryIndex = entryIndex,
                    .materialReference = entry.materialReference,
                    .role = entry.role,
                    .upstreamIssue = std::nullopt,
                });
                continue;
            }
            if (result.imports.size() >
                std::numeric_limits<std::uint32_t>::max()) {
                result.issues.push_back({
                    .kind = TextureBindingIssueKind::integerOverflow,
                    .entryIndex = entryIndex,
                    .materialReference = entry.materialReference,
                    .role = entry.role,
                    .upstreamIssue = std::nullopt,
                });
                continue;
            }
            const TextureAssetId assetId{
                static_cast<std::uint32_t>(result.imports.size())};
            result.imports.push_back({
                .assetId = assetId,
                .archiveFileIndex = archiveFileIndex,
            });
            assetIt = assetIds.emplace(archiveFileIndex, assetId).first;
        }
        assignRole(
            result.materials[materialIt->second], entry.role, assetIt->second);
    }

    if (!result.issues.empty()) {
        result.materials.clear();
        result.imports.clear();
    }
    return result;
}

GtiUploadDescription describeGtiUpload(
    const TextureImportRequest& request,
    const assets::GtiMetadata& metadata,
    const GtiUploadLimits& limits) {
    GtiUploadDescription result;
    if (metadata.variants.size() > limits.maximumVariants) {
        addUploadIssue(result, GtiUploadIssueKind::limitExceeded);
        return result;
    }

    std::size_t metadataBytes = 0U;
    if (!checkedMetadataAdd(
            metadata.variants.size(),
            sizeof(assets::GtiVariant),
            metadataBytes) ||
        !checkedMetadataAdd(
            metadata.chunks.size(),
            sizeof(assets::GtiChunk),
            metadataBytes)) {
        addUploadIssue(result, GtiUploadIssueKind::integerOverflow);
        return result;
    }
    if (metadataBytes > limits.maximumMetadataBytes) {
        addUploadIssue(result, GtiUploadIssueKind::limitExceeded);
        return result;
    }

    std::unordered_set<std::uint32_t> formats;
    formats.reserve(metadata.variants.size());
    for (std::size_t index = 0U; index < metadata.variants.size(); ++index) {
        const auto& variant = metadata.variants[index];
        if (!formats.insert(variant.format).second) {
            addUploadIssue(
                result, GtiUploadIssueKind::duplicateVariantFormat, index);
        }
        if (variant.width == 0U || variant.height == 0U ||
            variant.width > 32'768U || variant.height > 32'768U) {
            addUploadIssue(
                result, GtiUploadIssueKind::invalidDimensions, index);
        }
        else if (variant.width > limits.maximumDimension ||
                 variant.height > limits.maximumDimension) {
            addUploadIssue(result, GtiUploadIssueKind::limitExceeded, index);
        }
        if (variant.mipmapLevels == 0U || variant.mipmapLevels > 16U) {
            addUploadIssue(
                result, GtiUploadIssueKind::invalidMipCount, index);
        }
        else if (variant.mipmapLevels > limits.maximumDeclaredMipCount) {
            addUploadIssue(result, GtiUploadIssueKind::limitExceeded, index);
        }

        const auto bits = assets::gtiBitsPerPixel(variant.format);
        if (bits == 0U) {
            addUploadIssue(
                result, GtiUploadIssueKind::invalidMetadata, index);
            continue;
        }
        if (variant.width == 0U || variant.height == 0U ||
            variant.width > 32'768U || variant.height > 32'768U ||
            variant.mipmapLevels == 0U || variant.mipmapLevels > 16U) {
            continue;
        }

        std::uint64_t pixels = 0U;
        std::uint64_t baseBits = 0U;
        if (!checkedMultiply(variant.width, variant.height, pixels) ||
            !checkedMultiply(pixels, bits, baseBits)) {
            addUploadIssue(
                result, GtiUploadIssueKind::integerOverflow, index);
            continue;
        }
        const auto baseBytes = baseBits / 8U;
        std::uint64_t expected = 0U;
        bool expectedValid = true;
        for (std::uint32_t level = 0U;
             level < variant.mipmapLevels;
             ++level) {
            const auto sourceBytes = baseBytes >> (level * 2U);
            if (!checkedAdd(expected, sourceBytes, expected)) {
                addUploadIssue(
                    result,
                    GtiUploadIssueKind::integerOverflow,
                    index,
                    level);
                expectedValid = false;
                break;
            }
        }
        if (!expectedValid) {
            continue;
        }
        if (variant.expectedPixelDataSize != expected ||
            variant.pixelDataSize < expected ||
            variant.trailingBytes != variant.pixelDataSize - expected) {
            addUploadIssue(
                result, GtiUploadIssueKind::invalidMetadata, index);
        }
    }

    constexpr std::array<std::uint32_t, 5U> preferredFormats{
        8U, 7U, 4U, 3U, 6U};
    std::optional<std::size_t> selectedIndex;
    for (const auto preferred : preferredFormats) {
        for (std::size_t index = 0U; index < metadata.variants.size(); ++index) {
            if (metadata.variants[index].format == preferred) {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex.has_value()) {
            break;
        }
    }
    if (!selectedIndex.has_value()) {
        addUploadIssue(result, GtiUploadIssueKind::noSupportedVariant);
    }
    if (!result.issues.empty()) {
        return result;
    }

    const auto variantIndex = *selectedIndex;
    const auto& variant = metadata.variants[variantIndex];
    if ((variant.format == 3U || variant.format == 4U) &&
        (variant.paletteEntries == 0U || variant.paletteEntries > 256U)) {
        addUploadIssue(
            result, GtiUploadIssueKind::invalidPalette, variantIndex);
    }
    else if (variant.format != 3U && variant.format != 4U &&
             variant.paletteEntries != 0U) {
        addUploadIssue(
            result, GtiUploadIssueKind::invalidPalette, variantIndex);
    }
    if (!result.issues.empty()) {
        return result;
    }

    const auto bits = assets::gtiBitsPerPixel(variant.format);
    std::uint64_t pixelCount = 0U;
    std::uint64_t baseBits = 0U;
    if (!checkedMultiply(variant.width, variant.height, pixelCount) ||
        !checkedMultiply(pixelCount, bits, baseBits)) {
        addUploadIssue(
            result, GtiUploadIssueKind::integerOverflow, variantIndex);
        return result;
    }
    const auto baseSourceBytes = baseBits / 8U;

    bool allExact = true;
    bool baseExact = false;
    for (std::uint32_t level = 0U; level < variant.mipmapLevels; ++level) {
        const auto width = std::max(1U, variant.width >> level);
        const auto height = std::max(1U, variant.height >> level);
        const auto sourceBytes = baseSourceBytes >> (level * 2U);
        std::uint64_t requiredPixels = 0U;
        std::uint64_t requiredBits = 0U;
        if (!checkedMultiply(width, height, requiredPixels) ||
            !checkedMultiply(requiredPixels, bits, requiredBits)) {
            addUploadIssue(
                result,
                GtiUploadIssueKind::integerOverflow,
                variantIndex,
                level);
            return result;
        }
        const bool exact = sourceBytes == requiredBits / 8U;
        if (level == 0U) {
            baseExact = exact;
        }
        allExact = allExact && exact;
    }
    if (!baseExact) {
        addUploadIssue(
            result, GtiUploadIssueKind::invalidBaseLayout, variantIndex, 0U);
        return result;
    }

    GtiUploadPlan candidate{
        .request = request,
        .variantIndex = variantIndex,
        .format = variant.format,
        .checksum = metadata.checksum,
        .mipPolicy = allExact
            ? GtiMipPolicy::authoredChain
            : GtiMipPolicy::generateFromBase,
        .uploadLevels = {},
        .allocatedMipCount = 0U,
        .uploadedMipCount = 0U,
        .decodedRgbaBytes = 0U,
        .uploadRgbaBytes = 0U,
        .residentRgbaBytes = 0U,
    };

    std::uint32_t naturalMipCount = 1U;
    auto naturalWidth = variant.width;
    auto naturalHeight = variant.height;
    while (naturalWidth > 1U || naturalHeight > 1U) {
        naturalWidth = std::max(1U, naturalWidth >> 1U);
        naturalHeight = std::max(1U, naturalHeight >> 1U);
        ++naturalMipCount;
    }
    candidate.allocatedMipCount =
        allExact ? variant.mipmapLevels : naturalMipCount;
    candidate.uploadedMipCount =
        allExact ? variant.mipmapLevels : 1U;

    if (candidate.allocatedMipCount > limits.maximumAllocatedMipCount ||
        candidate.uploadedMipCount > limits.maximumUploadLevels) {
        addUploadIssue(
            result, GtiUploadIssueKind::limitExceeded, variantIndex);
        return result;
    }
    if (!checkedMetadataAdd(
            candidate.uploadedMipCount,
            sizeof(GtiUploadLevel),
            metadataBytes)) {
        addUploadIssue(
            result, GtiUploadIssueKind::integerOverflow, variantIndex);
        return result;
    }
    if (metadataBytes > limits.maximumMetadataBytes) {
        addUploadIssue(
            result, GtiUploadIssueKind::limitExceeded, variantIndex);
        return result;
    }

    candidate.uploadLevels.reserve(candidate.uploadedMipCount);
    for (std::uint32_t level = 0U;
         level < candidate.uploadedMipCount;
         ++level) {
        const auto width = std::max(1U, variant.width >> level);
        const auto height = std::max(1U, variant.height >> level);
        std::uint64_t bytesPerRow = 0U;
        std::uint64_t rgbaBytes = 0U;
        if (!checkedMultiply(width, 4U, bytesPerRow) ||
            !checkedMultiply(bytesPerRow, height, rgbaBytes)) {
            addUploadIssue(
                result,
                GtiUploadIssueKind::integerOverflow,
                variantIndex,
                level);
            return result;
        }
        if (!checkedAdd(
                candidate.decodedRgbaBytes,
                rgbaBytes,
                candidate.decodedRgbaBytes) ||
            !checkedAdd(
                candidate.uploadRgbaBytes,
                rgbaBytes,
                candidate.uploadRgbaBytes)) {
            addUploadIssue(
                result,
                GtiUploadIssueKind::integerOverflow,
                variantIndex,
                level);
            return result;
        }
        candidate.uploadLevels.push_back({
            .level = level,
            .width = width,
            .height = height,
            .bytesPerRow = bytesPerRow,
            .rgbaBytes = rgbaBytes,
        });
    }

    naturalWidth = variant.width;
    naturalHeight = variant.height;
    for (std::uint32_t level = 0U;
         level < candidate.allocatedMipCount;
         ++level) {
        std::uint64_t bytesPerRow = 0U;
        std::uint64_t rgbaBytes = 0U;
        if (!checkedMultiply(naturalWidth, 4U, bytesPerRow) ||
            !checkedMultiply(bytesPerRow, naturalHeight, rgbaBytes) ||
            !checkedAdd(
                candidate.residentRgbaBytes,
                rgbaBytes,
                candidate.residentRgbaBytes)) {
            addUploadIssue(
                result,
                GtiUploadIssueKind::integerOverflow,
                variantIndex,
                level);
            return result;
        }
        naturalWidth = std::max(1U, naturalWidth >> 1U);
        naturalHeight = std::max(1U, naturalHeight >> 1U);
    }

    if (candidate.decodedRgbaBytes > limits.maximumDecodedRgbaBytes ||
        candidate.uploadRgbaBytes > limits.maximumUploadRgbaBytes ||
        candidate.residentRgbaBytes > limits.maximumResidentRgbaBytes) {
        addUploadIssue(
            result, GtiUploadIssueKind::limitExceeded, variantIndex);
        return result;
    }

    result.plan = std::move(candidate);
    return result;
}

} // namespace airfix::render
