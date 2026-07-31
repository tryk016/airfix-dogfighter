#include "airfix/render/TextureRuntimePlan.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace airfix::assets;
using namespace airfix::render;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] ResolvedTextureEntry entry(
    const std::uint32_t materialReference,
    const TextureDependencyRole role,
    const std::size_t archiveFileIndex,
    const TextureEntryStatus status = TextureEntryStatus::unique) {
    return {
        .role = role,
        .materialReference = materialReference,
        .materialIndex = 0U,
        .sourceText = {},
        .status = status,
        .logicalPath = {},
        .archiveDirectoryIndex = std::nullopt,
        .archiveFileIndex = archiveFileIndex,
        .archiveLogicalPath = std::nullopt,
    };
}

[[nodiscard]] bool hasBindingIssue(
    const TextureBindingPlan& result,
    const TextureBindingIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

[[nodiscard]] TextureBindingPlan bindingPlan(
    const std::span<const std::uint32_t> references,
    std::vector<ResolvedTextureEntry> entries,
    const TextureBindingPlanLimits& limits = {}) {
    std::vector<TextureDependency> expected;
    expected.reserve(entries.size());
    for (const auto& texture : entries) {
        expected.push_back({
            .role = texture.role,
            .materialReference = texture.materialReference,
            .materialIndex = texture.materialIndex,
            .sourceText = texture.sourceText,
        });
    }
    TextureEntryResolution resolution;
    resolution.entries = std::move(entries);
    return buildTextureBindingPlan(references, expected, resolution, limits);
}

void testDenseIdsAndCrossRoleDeduplication() {
    const std::vector<std::uint32_t> references{20U, 10U, 30U};
    const std::vector<ResolvedTextureEntry> entries{
        entry(10U, TextureDependencyRole::environment, 9000U),
        entry(20U, TextureDependencyRole::primary, 17U),
        entry(10U, TextureDependencyRole::primary, 17U),
        entry(30U, TextureDependencyRole::secondary, 9000U),
    };
    const auto result = bindingPlan(references, entries);
    require(result.issues.empty(), "valid texture bindings were rejected");
    require(result.materials.size() == 3U && result.imports.size() == 2U,
        "texture binding output has the wrong cardinality");
    require(
        result.imports[0] ==
            TextureImportRequest{TextureAssetId{0U}, 9000U} &&
            result.imports[1] ==
                TextureImportRequest{TextureAssetId{1U}, 17U},
        "asset IDs are not dense in first-use order");
    require(
        result.materials[0].sourceReference == 20U &&
            result.materials[0].primary == TextureAssetId{1U} &&
            result.materials[1].sourceReference == 10U &&
            result.materials[1].environment == TextureAssetId{0U} &&
            result.materials[1].primary == TextureAssetId{1U} &&
            result.materials[2].sourceReference == 30U &&
            result.materials[2].secondary == TextureAssetId{0U},
        "material order, roles, or cross-material deduplication changed");
}

void testEmptyTexturesAndMaterialsAreRetained() {
    const std::vector<std::uint32_t> references{8U, 9U};
    auto result = bindingPlan(references, {});
    require(result.issues.empty() && result.materials.size() == 2U &&
                result.imports.empty(),
        "untextured materials were discarded");
    require(!result.materials[0].primary.has_value() &&
                !result.materials[0].secondary.has_value() &&
                !result.materials[0].environment.has_value(),
        "untextured material acquired a binding");

    result = bindingPlan({}, {});
    require(result.issues.empty() && result.materials.empty() &&
                result.imports.empty(),
        "empty input did not produce an empty successful plan");
}

void testRecoveredMaterialStateAndDefaults() {
    const CcfMaterialMetadata recoveredMetadata{
        .properties2140 = CcfMaterialProperties2140{
            .firstVector = {1.25F, 2.5F, 3.75F},
            .secondVector = {-4.5F, 5.25F, -6.75F},
            .scalar = 0.625F,
        },
        .properties2150 = CcfMaterialProperties2150{
            .lightingMode = 2U,
            .gouraudShading = true,
            .blendMode = 3U,
        },
        .flag2151 = true,
    };
    const DrawMaterialState expected{
        .lightingMode = 2U,
        .gouraudShading = true,
        .blendMode = 3U,
        .flag2151 = true,
        .scalar2140 = 0.625F,
        .firstVector2140 = {1.25F, 2.5F, 3.75F},
        .secondVector2140 = {-4.5F, 5.25F, -6.75F},
    };
    require(
        makeDrawMaterialState(recoveredMetadata) == expected,
        "recovered CCF material fields did not map to the draw contract");
    require(
        makeDrawMaterialState(CcfMaterialMetadata{}) == DrawMaterialState{},
        "absent CCF material chunks did not retain native reset defaults");

    const std::vector<std::uint32_t> references{8U};
    const std::vector<DrawMaterialState> states{expected};
    const TextureEntryResolution resolution;
    auto result = buildTextureBindingPlan(
        references,
        states,
        std::span<const TextureDependency>{},
        resolution);
    require(
        result.issues.empty() && result.materials.size() == 1U &&
            result.materials[0].state == expected,
        "texture binding discarded the recovered material state");

    const std::vector<DrawMaterialState> mismatched{expected, expected};
    result = buildTextureBindingPlan(
        references,
        mismatched,
        std::span<const TextureDependency>{},
        resolution);
    require(
        hasBindingIssue(
            result,
            TextureBindingIssueKind::materialStateMismatch) &&
            result.materials.empty() && result.imports.empty(),
        "material-state cardinality mismatch was not fail-closed");

    result = buildTextureBindingPlan(
        references,
        std::span<const DrawMaterialState>{},
        std::span<const TextureDependency>{},
        resolution);
    require(
        hasBindingIssue(
            result,
            TextureBindingIssueKind::materialStateMismatch) &&
            result.materials.empty() && result.imports.empty(),
        "explicit empty material-state span silently selected defaults");
}

void testBindingFailuresAreTypedAndAtomic() {
    {
        const std::vector<std::uint32_t> references{1U, 1U};
        const auto result = bindingPlan(references, {});
        require(hasBindingIssue(
                    result,
                    TextureBindingIssueKind::duplicateMaterialReference) &&
                    result.materials.empty() && result.imports.empty(),
            "duplicate material reference was not fail-closed");
    }
    {
        const std::vector<std::uint32_t> references{1U};
        const std::vector<ResolvedTextureEntry> entries{
            entry(
                2U,
                TextureDependencyRole::primary,
                4U,
                TextureEntryStatus::notFound),
        };
        const auto result = bindingPlan(references, entries);
        require(hasBindingIssue(
                    result,
                    TextureBindingIssueKind::unknownMaterialReference) &&
                    hasBindingIssue(
                        result,
                        TextureBindingIssueKind::unresolvedTexture) &&
                    result.materials.empty() && result.imports.empty(),
            "unknown/unresolved entry was not typed and fail-closed");
    }
    {
        const std::vector<std::uint32_t> references{1U};
        auto ambiguous = entry(
            1U,
            TextureDependencyRole::primary,
            4U,
            TextureEntryStatus::ambiguous);
        const std::vector<ResolvedTextureEntry> entries{ambiguous};
        const auto result = bindingPlan(references, entries);
        require(hasBindingIssue(
                    result,
                    TextureBindingIssueKind::nonUniqueTexture) &&
                    result.materials.empty() && result.imports.empty(),
            "ambiguous texture entry was not typed and fail-closed");
    }
    {
        const std::vector<std::uint32_t> references{1U};
        auto noIndex = entry(1U, TextureDependencyRole::primary, 4U);
        noIndex.archiveFileIndex.reset();
        const std::vector<ResolvedTextureEntry> entries{noIndex};
        const auto result = bindingPlan(references, entries);
        require(hasBindingIssue(
                    result,
                    TextureBindingIssueKind::missingArchiveFileIndex) &&
                    result.materials.empty() && result.imports.empty(),
            "missing archive file index was not typed and fail-closed");
    }
    {
        const std::vector<std::uint32_t> references{1U};
        const std::vector<ResolvedTextureEntry> entries{
            entry(1U, TextureDependencyRole::primary, 4U),
            entry(1U, TextureDependencyRole::primary, 5U),
        };
        const auto result = bindingPlan(references, entries);
        require(hasBindingIssue(
                    result,
                    TextureBindingIssueKind::duplicateTextureRole) &&
                    result.materials.empty() && result.imports.empty(),
            "duplicate material role was not typed and fail-closed");
    }
}

void testUpstreamResolutionContractIsFailClosed() {
    const std::vector<std::uint32_t> references{1U};
    {
        TextureEntryResolution resolution;
        resolution.issues.push_back({
            .kind = TextureEntryIssueKind::limitExceeded,
            .dependencyIndex = std::nullopt,
        });
        const auto result =
            buildTextureBindingPlan(references, {}, resolution);
        require(
            hasBindingIssue(
                result,
                TextureBindingIssueKind::upstreamResolutionIssue) &&
                result.issues[0].upstreamIssue ==
                    TextureEntryIssueKind::limitExceeded &&
                result.materials.empty() && result.imports.empty(),
            "empty upstream limit failure was treated as untextured success");
    }
    {
        const std::vector<TextureDependency> expected{{
            .role = TextureDependencyRole::primary,
            .materialReference = 1U,
            .materialIndex = 3U,
            .sourceText = "expected",
        }};
        const TextureEntryResolution truncated;
        const auto result =
            buildTextureBindingPlan(references, expected, truncated);
        require(
            hasBindingIssue(
                result,
                TextureBindingIssueKind::dependencyMismatch) &&
                result.materials.empty() && result.imports.empty(),
            "truncated resolution was not rejected atomically");
    }
    {
        const std::vector<TextureDependency> expected{{
            .role = TextureDependencyRole::primary,
            .materialReference = 1U,
            .materialIndex = 3U,
            .sourceText = "expected",
        }};
        TextureEntryResolution mismatched;
        mismatched.entries = {
            entry(1U, TextureDependencyRole::secondary, 7U),
        };
        mismatched.entries[0].materialIndex = 4U;
        mismatched.entries[0].sourceText = "different";
        const auto result =
            buildTextureBindingPlan(references, expected, mismatched);
        require(
            hasBindingIssue(
                result,
                TextureBindingIssueKind::dependencyMismatch) &&
                result.issues[0].entryIndex == 0U &&
                result.materials.empty() && result.imports.empty(),
            "dependency identity mismatch was not rejected atomically");
    }
    {
        const auto unresolved =
            entry(
                1U,
                TextureDependencyRole::primary,
                7U,
                TextureEntryStatus::notFound);
        const auto result = bindingPlan(references, {unresolved});
        require(
            hasBindingIssue(
                result,
                TextureBindingIssueKind::unresolvedTexture) &&
                result.materials.empty() && result.imports.empty(),
            "forged successful resolution with unresolved status was accepted");
    }
}

void testBindingLimitsAreAtomic() {
    const std::vector<std::uint32_t> references{1U};
    const std::vector<ResolvedTextureEntry> entries{
        entry(1U, TextureDependencyRole::primary, 4U),
    };
    auto limits = TextureBindingPlanLimits{};
    limits.maximumImports = 0U;
    auto result = bindingPlan(references, entries, limits);
    require(hasBindingIssue(result, TextureBindingIssueKind::limitExceeded) &&
                result.materials.empty() && result.imports.empty(),
        "import limit did not fail atomically");

    limits = {};
    limits.maximumMaterials = 0U;
    result = bindingPlan(references, entries, limits);
    require(hasBindingIssue(result, TextureBindingIssueKind::limitExceeded) &&
                result.materials.empty() && result.imports.empty(),
        "material limit did not fail atomically");

    limits = {};
    limits.maximumTextureEntries = 0U;
    result = bindingPlan(references, entries, limits);
    require(hasBindingIssue(result, TextureBindingIssueKind::limitExceeded) &&
                result.materials.empty() && result.imports.empty(),
        "texture-entry limit did not fail atomically");

    TextureEntryResolution oversizedDiagnostics;
    oversizedDiagnostics.issues.push_back({
        .kind = TextureEntryIssueKind::notFound,
        .dependencyIndex = 0U,
    });
    result = buildTextureBindingPlan(
        std::span<const std::uint32_t>{},
        std::span<const TextureDependency>{},
        oversizedDiagnostics,
        limits);
    require(hasBindingIssue(result, TextureBindingIssueKind::limitExceeded) &&
                !hasBindingIssue(
                    result,
                    TextureBindingIssueKind::upstreamResolutionIssue) &&
                result.materials.empty() && result.imports.empty(),
        "upstream diagnostic count was traversed beyond the configured limit");
}

[[nodiscard]] std::uint32_t bits(const std::uint32_t format) {
    switch (format) {
    case 3U: return 8U;
    case 4U:
    case 6U: return 16U;
    case 7U: return 24U;
    case 8U: return 32U;
    default: return 4U;
    }
}

[[nodiscard]] GtiVariant variant(
    const std::uint32_t format,
    const std::uint32_t width = 1U,
    const std::uint32_t height = 1U,
    const std::uint32_t mipCount = 1U) {
    const auto baseBytes =
        static_cast<std::uint64_t>(width) * height * bits(format) / 8U;
    std::uint64_t expected = 0U;
    for (std::uint32_t level = 0U; level < mipCount && level < 32U; ++level) {
        expected += baseBytes >> (level * 2U);
    }
    return {
        .format = format,
        .width = width,
        .height = height,
        .paletteEntries = format == 3U || format == 4U ? 256U : 0U,
        .mipmapLevels = mipCount,
        .pixelDataOffset = format == 3U || format == 4U ? 1024U : 0U,
        .pixelDataSize = static_cast<std::uint32_t>(expected),
        .expectedPixelDataSize = expected,
        .trailingBytes = 0U,
    };
}

[[nodiscard]] bool hasUploadIssue(
    const GtiUploadDescription& result,
    const GtiUploadIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

[[nodiscard]] GtiMetadata metadataOf(
    std::vector<GtiVariant> variants,
    const std::optional<std::uint32_t> checksum = std::nullopt) {
    GtiMetadata metadata;
    metadata.checksum = checksum;
    metadata.variants = std::move(variants);
    return metadata;
}

void testVariantPriorityAndExactAuthoredChain() {
    auto metadata = metadataOf(
        {
            variant(6U, 4U, 4U, 3U),
            variant(3U, 4U, 4U, 3U),
            variant(4U, 4U, 4U, 3U),
            variant(7U, 4U, 4U, 3U),
            variant(8U, 4U, 4U, 3U),
        },
        0xAABBCCDDU);
    const TextureImportRequest request{TextureAssetId{2U}, 9001U};
    const auto result = describeGtiUpload(request, metadata);
    require(result.issues.empty() && result.plan.has_value(),
        "valid exact GTI chain was rejected");
    const auto& plan = *result.plan;
    require(plan.request == request && plan.variantIndex == 4U &&
                plan.format == 8U && plan.checksum == 0xAABBCCDDU,
        "variant priority or upload identity is incorrect");
    require(plan.mipPolicy == GtiMipPolicy::authoredChain &&
                plan.allocatedMipCount == 3U &&
                plan.uploadedMipCount == 3U &&
                plan.uploadLevels.size() == 3U,
        "exact authored chain did not retain all declared levels");
    require(plan.uploadLevels[0] == GtiUploadLevel{0U, 4U, 4U, 16U, 64U} &&
                plan.uploadLevels[1] ==
                    GtiUploadLevel{1U, 2U, 2U, 8U, 16U} &&
                plan.uploadLevels[2] ==
                    GtiUploadLevel{2U, 1U, 1U, 4U, 4U},
        "upload-level RGBA metadata is incorrect");
    require(plan.decodedRgbaBytes == 84U &&
                plan.uploadRgbaBytes == 84U &&
                plan.residentRgbaBytes == 84U,
        "exact authored-chain byte totals are incorrect");
}

void testAnomalousChainUsesBaseAndNaturalResidentChain() {
    const auto metadata = metadataOf({variant(8U, 4U, 2U, 3U)});
    const auto result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 12U}, metadata);
    require(result.issues.empty() && result.plan.has_value(),
        "valid anomalous GTI chain was rejected");
    const auto& plan = *result.plan;
    require(plan.mipPolicy == GtiMipPolicy::generateFromBase &&
                plan.allocatedMipCount == 3U &&
                plan.uploadedMipCount == 1U &&
                plan.uploadLevels ==
                    std::vector<GtiUploadLevel>{
                        GtiUploadLevel{0U, 4U, 2U, 16U, 32U}},
        "anomalous chain did not select base-only upload plus generation");
    require(plan.decodedRgbaBytes == 32U &&
                plan.uploadRgbaBytes == 32U &&
                plan.residentRgbaBytes == 44U,
        "4x2 natural three-level RGBA totals are incorrect");
}

void testUnsupportedAndInvalidMetadataAreFailClosed() {
    {
        const auto metadata = metadataOf({variant(0U)});
        const auto result = describeGtiUpload(
            TextureImportRequest{TextureAssetId{0U}, 0U}, metadata);
        require(!result.plan.has_value() &&
                    hasUploadIssue(
                        result,
                        GtiUploadIssueKind::noSupportedVariant),
            "unsupported variants did not fail with a typed issue");
    }
    {
        auto invalid = variant(8U);
        invalid.expectedPixelDataSize += 1U;
        const auto metadata = metadataOf({invalid});
        const auto result = describeGtiUpload(
            TextureImportRequest{TextureAssetId{0U}, 0U}, metadata);
        require(!result.plan.has_value() &&
                    hasUploadIssue(
                        result,
                        GtiUploadIssueKind::invalidMetadata),
            "inconsistent variant byte metadata was accepted");
    }
    {
        auto invalid = variant(8U);
        invalid.width = 0U;
        const auto metadata = metadataOf({invalid});
        const auto result = describeGtiUpload(
            TextureImportRequest{TextureAssetId{0U}, 0U}, metadata);
        require(!result.plan.has_value() &&
                    hasUploadIssue(
                        result,
                        GtiUploadIssueKind::invalidDimensions),
            "zero image dimension was accepted");
    }
    {
        auto invalid = variant(8U);
        invalid.mipmapLevels = 0U;
        const auto metadata = metadataOf({invalid});
        const auto result = describeGtiUpload(
            TextureImportRequest{TextureAssetId{0U}, 0U}, metadata);
        require(!result.plan.has_value() &&
                    hasUploadIssue(
                        result,
                        GtiUploadIssueKind::invalidMipCount),
            "zero mip count was accepted");
    }
    {
        auto invalid = variant(4U);
        invalid.paletteEntries = 0U;
        const auto metadata = metadataOf({invalid});
        const auto result = describeGtiUpload(
            TextureImportRequest{TextureAssetId{0U}, 0U}, metadata);
        require(!result.plan.has_value() &&
                    hasUploadIssue(
                        result,
                        GtiUploadIssueKind::invalidPalette),
            "invalid selected-variant palette was accepted");
    }
    {
        const auto metadata = metadataOf({variant(8U), variant(8U)});
        const auto result = describeGtiUpload(
            TextureImportRequest{TextureAssetId{0U}, 0U}, metadata);
        require(!result.plan.has_value() &&
                    hasUploadIssue(
                        result,
                        GtiUploadIssueKind::duplicateVariantFormat),
            "duplicate variant format was accepted");
    }
}

void testUploadLimitsAndOverflowAreFailClosed() {
    const auto metadata = metadataOf({variant(8U, 4U, 2U, 3U)});
    auto limits = GtiUploadLimits{};
    limits.maximumVariants = 0U;
    auto result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "variant-count limit was ignored");

    limits = {};
    limits.maximumDimension = 3U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "dimension limit was ignored");

    limits = {};
    limits.maximumDeclaredMipCount = 2U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "declared-mip limit was ignored");

    limits = {};
    limits.maximumAllocatedMipCount = 2U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "allocated-mip limit was ignored");

    limits = {};
    limits.maximumUploadLevels = 0U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "upload-level limit was ignored");

    limits = {};
    limits.maximumDecodedRgbaBytes = 31U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "decoded-byte limit was ignored");

    limits = {};
    limits.maximumUploadRgbaBytes = 31U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "upload-byte limit was ignored");

    limits = {};
    limits.maximumResidentRgbaBytes = 43U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "resident-byte limit was ignored");

    limits = {};
    limits.maximumMetadataBytes = 0U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U}, metadata, limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::limitExceeded),
        "metadata-byte limit was ignored");

    auto parserOversize = variant(8U, 32'769U, 1U, 1U);
    const auto parserOversizeMetadata = metadataOf({parserOversize});
    limits = {};
    limits.maximumDimension = std::numeric_limits<std::uint32_t>::max();
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U},
        parserOversizeMetadata,
        limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(
                    result,
                    GtiUploadIssueKind::invalidDimensions),
        "dimension above the parser hard bound was accepted");

    const auto tooManyMipsMetadata = metadataOf({variant(8U, 1U, 1U, 17U)});
    limits = {};
    limits.maximumDeclaredMipCount = 32U;
    result = describeGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 0U},
        tooManyMipsMetadata,
        limits);
    require(!result.plan.has_value() &&
                hasUploadIssue(result, GtiUploadIssueKind::invalidMipCount),
        "mip count above the parser hard bound was accepted");

}

} // namespace

int main() {
    try {
        testDenseIdsAndCrossRoleDeduplication();
        testEmptyTexturesAndMaterialsAreRetained();
        testRecoveredMaterialStateAndDefaults();
        testBindingFailuresAreTypedAndAtomic();
        testUpstreamResolutionContractIsFailClosed();
        testBindingLimitsAreAtomic();
        testVariantPriorityAndExactAuthoredChain();
        testAnomalousChainUsesBaseAndNaturalResidentChain();
        testUnsupportedAndInvalidMetadataAreFailClosed();
        testUploadLimitsAndOverflowAreFailClosed();
        std::cout << "texture runtime plan tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "texture runtime plan tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
