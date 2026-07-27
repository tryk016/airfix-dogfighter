#include "airfix/render/TextureRuntimeData.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    GtiUploadPreparation& result,
    const GtiUploadDataIssueKind kind,
    const std::optional<GtiUploadIssueKind> planIssue = std::nullopt,
    const std::optional<std::size_t> variantIndex = std::nullopt,
    const std::optional<std::uint32_t> level = std::nullopt) {
    result.plan.reset();
    result.uploadLevels.clear();
    result.issues.push_back({
        .kind = kind,
        .planIssue = planIssue,
        .variantIndex = variantIndex,
        .level = level,
    });
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

[[nodiscard]] GtiUploadDataIssueKind planIssueKind(
    const GtiUploadIssueKind kind) noexcept {
    switch (kind) {
    case GtiUploadIssueKind::limitExceeded:
        return GtiUploadDataIssueKind::limitExceeded;
    case GtiUploadIssueKind::integerOverflow:
        return GtiUploadDataIssueKind::integerOverflow;
    default:
        return GtiUploadDataIssueKind::planFailure;
    }
}

[[nodiscard]] bool planShapeIsConsistent(const GtiUploadPlan& plan) noexcept {
    if (plan.uploadLevels.size() != plan.uploadedMipCount ||
        plan.allocatedMipCount < plan.uploadedMipCount) {
        return false;
    }
    switch (plan.mipPolicy) {
    case GtiMipPolicy::authoredChain:
        return plan.allocatedMipCount == plan.uploadedMipCount;
    case GtiMipPolicy::generateFromBase:
        return plan.uploadedMipCount == 1U;
    }
    return false;
}

} // namespace

GtiUploadPreparation prepareGtiUpload(
    const TextureImportRequest& request,
    const std::span<const std::uint8_t> gtiBytes,
    const GtiUploadDataLimits& limits) {
    GtiUploadPreparation result;
    if (gtiBytes.size() > limits.maximumSourceBytes) {
        addIssue(result, GtiUploadDataIssueKind::limitExceeded);
        return result;
    }

    assets::GtiMetadata metadata;
    try {
        metadata = assets::parseGti(
            gtiBytes,
            assets::GtiParseLimits{
                .maximumVariants = limits.upload.maximumVariants,
                .maximumMetadataBytes =
                    limits.upload.maximumMetadataBytes,
            });
    }
    catch (const assets::GtiParseLimitError&) {
        addIssue(result, GtiUploadDataIssueKind::limitExceeded);
        return result;
    }
    catch (const assets::ParseError&) {
        addIssue(result, GtiUploadDataIssueKind::parseFailure);
        return result;
    }

    const auto description =
        describeGtiUpload(request, metadata, limits.upload);
    if (!description.issues.empty() || !description.plan.has_value()) {
        if (description.issues.empty()) {
            addIssue(result, GtiUploadDataIssueKind::planFailure);
        }
        else {
            for (const auto& issue : description.issues) {
                addIssue(
                    result,
                    planIssueKind(issue.kind),
                    issue.kind,
                    issue.variantIndex,
                    issue.level);
            }
        }
        return result;
    }

    const auto& plan = *description.plan;
    if (plan.request != request ||
        plan.variantIndex >= metadata.variants.size() ||
        plan.checksum != metadata.checksum ||
        !planShapeIsConsistent(plan)) {
        addIssue(result, GtiUploadDataIssueKind::planMismatch);
        return result;
    }
    const auto& variant = metadata.variants[plan.variantIndex];
    if (plan.format != variant.format ||
        plan.uploadedMipCount >
            std::numeric_limits<std::size_t>::max() ||
        plan.decodedRgbaBytes >
            std::numeric_limits<std::size_t>::max()) {
        addIssue(result, GtiUploadDataIssueKind::integerOverflow);
        return result;
    }

    std::vector<assets::RgbaImage> decoded;
    try {
        if (plan.mipPolicy == GtiMipPolicy::authoredChain) {
            decoded = assets::decodeGtiMipChainRgba(
                gtiBytes,
                variant,
                static_cast<std::size_t>(plan.decodedRgbaBytes))
                          .levels;
        }
        else {
            decoded.push_back(assets::decodeGtiBaseRgba(
                gtiBytes,
                variant,
                static_cast<std::size_t>(plan.decodedRgbaBytes)));
        }
    }
    catch (const assets::ParseError&) {
        addIssue(
            result,
            GtiUploadDataIssueKind::decodeFailure,
            std::nullopt,
            plan.variantIndex);
        return result;
    }

    if (decoded.size() != plan.uploadLevels.size() ||
        decoded.size() != plan.uploadedMipCount) {
        addIssue(
            result,
            GtiUploadDataIssueKind::planMismatch,
            std::nullopt,
            plan.variantIndex);
        return result;
    }

    std::uint64_t decodedBytes = 0U;
    std::uint64_t plannedUploadBytes = 0U;
    for (std::size_t index = 0U; index < decoded.size(); ++index) {
        const auto& image = decoded[index];
        const auto& level = plan.uploadLevels[index];
        std::uint64_t rowBytes = 0U;
        std::uint64_t imageBytes = 0U;
        if (!checkedMultiply(image.width, 4U, rowBytes) ||
            !checkedMultiply(rowBytes, image.height, imageBytes) ||
            !checkedAdd(decodedBytes, imageBytes, decodedBytes) ||
            !checkedAdd(
                plannedUploadBytes,
                level.rgbaBytes,
                plannedUploadBytes)) {
            addIssue(
                result,
                GtiUploadDataIssueKind::integerOverflow,
                std::nullopt,
                plan.variantIndex,
                static_cast<std::uint32_t>(index));
            return result;
        }
        if (index > std::numeric_limits<std::uint32_t>::max() ||
            level.level != static_cast<std::uint32_t>(index) ||
            image.width != level.width ||
            image.height != level.height ||
            rowBytes != level.bytesPerRow ||
            imageBytes != level.rgbaBytes ||
            imageBytes != image.pixels.size()) {
            addIssue(
                result,
                GtiUploadDataIssueKind::planMismatch,
                std::nullopt,
                plan.variantIndex,
                index <= std::numeric_limits<std::uint32_t>::max()
                    ? std::optional<std::uint32_t>(
                          static_cast<std::uint32_t>(index))
                    : std::nullopt);
            return result;
        }
    }

    std::uint64_t residentBytes = 0U;
    auto residentWidth = variant.width;
    auto residentHeight = variant.height;
    for (std::uint32_t level = 0U;
         level < plan.allocatedMipCount;
         ++level) {
        std::uint64_t rowBytes = 0U;
        std::uint64_t levelBytes = 0U;
        if (!checkedMultiply(residentWidth, 4U, rowBytes) ||
            !checkedMultiply(rowBytes, residentHeight, levelBytes) ||
            !checkedAdd(residentBytes, levelBytes, residentBytes)) {
            addIssue(
                result,
                GtiUploadDataIssueKind::integerOverflow,
                std::nullopt,
                plan.variantIndex,
                level);
            return result;
        }
        residentWidth = std::max(1U, residentWidth >> 1U);
        residentHeight = std::max(1U, residentHeight >> 1U);
    }

    if (decodedBytes != plan.decodedRgbaBytes ||
        decodedBytes != plan.uploadRgbaBytes ||
        plannedUploadBytes != plan.uploadRgbaBytes ||
        residentBytes != plan.residentRgbaBytes) {
        addIssue(
            result,
            GtiUploadDataIssueKind::planMismatch,
            std::nullopt,
            plan.variantIndex);
        return result;
    }

    result.plan = plan;
    result.uploadLevels = std::move(decoded);
    return result;
}

} // namespace airfix::render
