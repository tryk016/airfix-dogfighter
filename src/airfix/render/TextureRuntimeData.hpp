#pragma once

#include "airfix/render/TextureRuntimePlan.hpp"
#include "airfix/texture/TextureHdPngPreparation.hpp"
#include "airfix/texture/TextureMode.hpp"
#include "airfix/texture/TextureReplacementResolver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class GtiUploadDataIssueKind : std::uint8_t {
    parseFailure,
    planFailure,
    decodeFailure,
    planMismatch,
    limitExceeded,
    integerOverflow,
};

struct GtiUploadDataIssue {
    GtiUploadDataIssueKind kind{GtiUploadDataIssueKind::parseFailure};
    std::optional<GtiUploadIssueKind> planIssue;
    std::optional<std::size_t> variantIndex;
    std::optional<std::uint32_t> level;
};

struct GtiUploadDataLimits {
    // Checked before parsing so an oversized untrusted source cannot cause
    // parser work or metadata allocation.
    std::size_t maximumSourceBytes{512U * 1024U * 1024U};
    // Forwarded unchanged to describeGtiUpload. Parser hard bounds remain
    // independently enforced by parseGti and describeGtiUpload.
    GtiUploadLimits upload{};
};

struct GtiUploadPreparation {
    // Both payload fields are published together only after the decoded data
    // has been checked against every field and byte total in the plan. This
    // stage retains encodedUnclassified and makes no colour-classification
    // decision.
    std::optional<GtiUploadPlan> plan;
    std::vector<assets::RgbaImage> uploadLevels;
    std::vector<GtiUploadDataIssue> issues;

    [[nodiscard]] bool success() const noexcept {
        return plan.has_value() && issues.empty();
    }
};

struct TextureSourcePreparation final {
    texture::TextureReplacementResolution replacement;
    // Populated exactly when replacement resolution falls back. The existing
    // GTI preparation function remains the sole Classic implementation.
    GtiUploadPreparation classicGti;

    [[nodiscard]] bool replacementReady() const noexcept {
        return replacement.replacementReady();
    }

    [[nodiscard]] bool classicFallbackUsed() const noexcept {
        return !replacementReady();
    }

    // Source selection only. A replacement candidate has not yet passed PNG
    // decoding or upload preparation; those are deliberately a later stage.
    [[nodiscard]] bool sourceSelected() const noexcept {
        return replacementReady() || classicGti.success();
    }
};

struct TextureUploadSourcePreparation final {
    texture::TextureMode selectedMode{texture::TextureMode::classic};
    texture::TextureReplacementFallbackReason resolverFallback{
        texture::TextureReplacementFallbackReason::notConfigured};
    std::optional<texture::TextureHdPngPreparationIssueKind>
        pngPreparationFallback;
    bool sourceDigestComputed{};
    crypto::Sha256Digest sourceGtiSha256{};
    std::uint64_t replacementGeneration{};
    GtiUploadPreparation upload;

    [[nodiscard]] bool success() const noexcept { return upload.success(); }

    [[nodiscard]] bool enhancedSelected() const noexcept {
        return selectedMode == texture::TextureMode::enhanced && success();
    }
};

// Parses, plans, and decodes one complete GTI source transaction. Authored
// chains decode every selected mip. Legacy dimension anomalies decode only the
// base level and leave lower-level generation to the eventual render backend.
// This function makes no sRGB, premultiplication, vertical-orientation, or
// blending decision.
[[nodiscard]] GtiUploadPreparation prepareGtiUpload(
    const TextureImportRequest& request,
    std::span<const std::uint8_t> gtiBytes,
    const GtiUploadDataLimits& limits = {});

// Resolves one optional private base-image candidate from the actual immutable
// GTI bytes. No configured resolver means permanent Classic mode. Every
// fixed-reason replacement failure calls prepareGtiUpload with the identical
// request, byte span, and limits; candidate success deliberately stops before
// PNG decoding, which belongs to the next reviewed stage.
[[nodiscard]] TextureSourcePreparation prepareTextureSource(
    const TextureImportRequest& request,
    std::span<const std::uint8_t> gtiBytes,
    const texture::TextureReplacementResolver* resolver = nullptr,
    texture::TextureReplacementLookupPolicy replacementPolicy = {},
    const GtiUploadDataLimits& limits = {});

// Complete per-texture source transaction used by opt-in product pilots. A
// reviewed candidate is decoded and converted to the same backend-neutral
// authored RGBA8 upload contract used by legacy GTI. Every resolver, PNG,
// validation, or budget failure prepares the byte-identical GTI source
// instead. No private path, checksum, or decoder text enters the result.
[[nodiscard]] TextureUploadSourcePreparation prepareTextureUploadSource(
    const TextureImportRequest& request,
    std::span<const std::uint8_t> gtiBytes,
    const texture::TextureReplacementResolver* resolver,
    const texture::PrivateTextureFileStore* files,
    texture::TextureReplacementLookupPolicy replacementPolicy = {},
    const GtiUploadDataLimits& gtiLimits = {},
    const texture::TextureHdPngPreparationLimits& pngLimits = {});

} // namespace airfix::render
