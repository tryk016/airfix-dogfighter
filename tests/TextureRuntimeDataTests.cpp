#include "airfix/render/TextureRuntimeData.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using namespace airfix::assets;
using namespace airfix::render;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

[[nodiscard]] Bytes makeGti(
    const std::uint32_t format,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t mipLevels,
    const Bytes& palette,
    const Bytes& pixels,
    const std::uint32_t checksum = 0x12345678U) {
    Bytes bytes;
    appendU32(bytes, kGtiMagic);
    appendU32(bytes, kGtiVersion);
    appendU32(bytes, kGtiChecksumChunk);
    appendU32(bytes, 4U);
    appendU32(bytes, checksum);
    appendU32(bytes, kGtiImageChunk);
    appendU32(
        bytes,
        static_cast<std::uint32_t>(20U + palette.size() + pixels.size()));
    appendU32(bytes, format);
    appendU32(bytes, width);
    appendU32(bytes, height);
    appendU32(
        bytes, static_cast<std::uint32_t>(palette.size() / 4U));
    appendU32(bytes, mipLevels);
    bytes.insert(bytes.end(), palette.begin(), palette.end());
    bytes.insert(bytes.end(), pixels.begin(), pixels.end());
    return bytes;
}

[[nodiscard]] bool hasIssue(
    const GtiUploadPreparation& result,
    const GtiUploadDataIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

[[nodiscard]] Bytes withUnknownChunks(
    const Bytes& gti,
    const std::size_t chunkCount) {
    Bytes result(gti.begin(), gti.begin() + 8);
    for (std::size_t index = 0U; index < chunkCount; ++index) {
        appendU32(result, fourCC('U', 'n', 'k', 'n'));
        appendU32(result, 0U);
    }
    result.insert(result.end(), gti.begin() + 8, gti.end());
    return result;
}

void requireAtomicFailure(
    const GtiUploadPreparation& result,
    const GtiUploadDataIssueKind kind,
    const std::string& message) {
    require(
        !result.success() && !result.plan.has_value() &&
            result.uploadLevels.empty() && hasIssue(result, kind),
        message);
}

void testAuthoredMipChainMaterializesExactly() {
    Bytes pixels;
    for (std::uint32_t index = 0U; index < 21U; ++index) {
        pixels.push_back(static_cast<std::uint8_t>(index));
        pixels.push_back(static_cast<std::uint8_t>(index + 1U));
        pixels.push_back(static_cast<std::uint8_t>(index + 2U));
        pixels.push_back(0xFFU);
    }
    const auto bytes = makeGti(8U, 4U, 4U, 3U, {}, pixels);
    const TextureImportRequest request{TextureAssetId{0U}, 91U};
    const auto result = prepareGtiUpload(request, bytes);

    require(result.success() && result.plan.has_value(),
        "valid authored GTI was rejected");
    const auto& plan = *result.plan;
    require(plan.request == request &&
                plan.request.assetId == TextureAssetId{0U},
        "texture import identity, including ID zero, was not retained");
    require(plan.mipPolicy == GtiMipPolicy::authoredChain &&
                result.uploadLevels.size() == 3U,
        "authored mip chain was not materialized completely");
    require(
        result.uploadLevels[0].width == 4U &&
            result.uploadLevels[0].height == 4U &&
            result.uploadLevels[0].pixels.size() == 64U &&
            result.uploadLevels[1].width == 2U &&
            result.uploadLevels[1].height == 2U &&
            result.uploadLevels[1].pixels.size() == 16U &&
            result.uploadLevels[2].width == 1U &&
            result.uploadLevels[2].height == 1U &&
            result.uploadLevels[2].pixels.size() == 4U,
        "decoded authored levels do not match upload metadata");
    require(plan.decodedRgbaBytes == 84U &&
                plan.uploadRgbaBytes == 84U &&
                plan.residentRgbaBytes == 84U,
        "authored materialization byte totals changed");
}

void testLegacyAnomalyMaterializesOnlyBase() {
    Bytes pixels(42U, 0U);
    for (std::size_t index = 0U; index < 32U; index += 4U) {
        pixels[index] = 0x30U;
        pixels[index + 1U] = 0x20U;
        pixels[index + 2U] = 0x10U;
        pixels[index + 3U] = 0x40U;
    }
    const auto bytes = makeGti(8U, 4U, 2U, 3U, {}, pixels);
    const auto result = prepareGtiUpload(
        TextureImportRequest{TextureAssetId{7U}, 12U}, bytes);

    require(result.success() && result.plan.has_value(),
        "valid legacy mip anomaly was rejected");
    require(result.plan->mipPolicy == GtiMipPolicy::generateFromBase &&
                result.plan->allocatedMipCount == 3U &&
                result.plan->uploadedMipCount == 1U &&
                result.uploadLevels.size() == 1U,
        "legacy mip anomaly decoded data below the base level");
    require(result.uploadLevels[0].width == 4U &&
                result.uploadLevels[0].height == 2U &&
                result.uploadLevels[0].pixels.size() == 32U &&
                result.plan->decodedRgbaBytes == 32U &&
                result.plan->uploadRgbaBytes == 32U &&
                result.plan->residentRgbaBytes == 44U,
        "base-only materialization does not match its plan");
}

void testLaterMipDecodeFailureIsAtomic() {
    const Bytes palette{
        0x03U, 0x02U, 0x01U, 0xFFU,
        0x30U, 0x20U, 0x10U, 0xFFU,
    };
    // The base indices are valid, while the second mip refers past the
    // two-entry palette. Parsing and planning succeed before decoding fails.
    const Bytes pixels{0U, 1U, 0U, 1U, 2U};
    const auto bytes = makeGti(3U, 2U, 2U, 2U, palette, pixels);
    const auto result = prepareGtiUpload(
        TextureImportRequest{TextureAssetId{0U}, 3U}, bytes);
    requireAtomicFailure(
        result,
        GtiUploadDataIssueKind::decodeFailure,
        "later authored-mip corruption leaked partial decoded output");
}

void testParseAndAllMaterializationLimitsAreTypedAndAtomic() {
    const auto authored = makeGti(8U, 4U, 4U, 3U, {}, Bytes(84U, 0U));
    const auto generated = makeGti(8U, 4U, 2U, 3U, {}, Bytes(42U, 0U));
    const TextureImportRequest request{TextureAssetId{0U}, 5U};

    {
        auto invalid = authored;
        invalid[0] = 'X';
        requireAtomicFailure(
            prepareGtiUpload(request, invalid),
            GtiUploadDataIssueKind::parseFailure,
            "malformed GTI did not produce an atomic parse issue");
    }
    {
        const auto unsupported =
            makeGti(1U, 2U, 2U, 1U, {}, Bytes(2U, 0U));
        requireAtomicFailure(
            prepareGtiUpload(request, unsupported),
            GtiUploadDataIssueKind::planFailure,
            "unsupported but parseable GTI did not produce a plan issue");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.maximumSourceBytes = 0U;
        const Bytes malformedAndOversized{0U};
        const auto result =
            prepareGtiUpload(request, malformedAndOversized, limits);
        requireAtomicFailure(
            result,
            GtiUploadDataIssueKind::limitExceeded,
            "source-byte limit was not enforced before materialization");
        require(!hasIssue(result, GtiUploadDataIssueKind::parseFailure),
            "oversized source reached the GTI parser");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumDecodedRgbaBytes = 83U;
        requireAtomicFailure(
            prepareGtiUpload(request, authored, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "decoded-byte limit was not forwarded");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumUploadRgbaBytes = 83U;
        requireAtomicFailure(
            prepareGtiUpload(request, authored, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "upload-byte limit was not forwarded");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumResidentRgbaBytes = 43U;
        requireAtomicFailure(
            prepareGtiUpload(request, generated, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "resident-byte limit was not forwarded");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumMetadataBytes = 0U;
        requireAtomicFailure(
            prepareGtiUpload(request, authored, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "metadata-byte limit was not forwarded");
    }
    {
        const auto chunkHeavy = withUnknownChunks(authored, 128U);
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumMetadataBytes =
            3U * sizeof(GtiChunk) + sizeof(GtiVariant);
        requireAtomicFailure(
            prepareGtiUpload(request, chunkHeavy, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "chunk metadata was allocated past its pre-parse budget");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumVariants = 0U;
        requireAtomicFailure(
            prepareGtiUpload(request, authored, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "variant count was not bounded before parser allocation");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumUploadLevels = 2U;
        requireAtomicFailure(
            prepareGtiUpload(request, authored, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "upload-level limit was not forwarded");
    }
    {
        auto limits = GtiUploadDataLimits{};
        limits.upload.maximumAllocatedMipCount = 2U;
        requireAtomicFailure(
            prepareGtiUpload(request, generated, limits),
            GtiUploadDataIssueKind::limitExceeded,
            "allocated-level limit was not forwarded");
    }
}

} // namespace

int main() {
    try {
        testAuthoredMipChainMaterializesExactly();
        testLegacyAnomalyMaterializesOnlyBase();
        testLaterMipDecodeFailureIsAtomic();
        testParseAndAllMaterializationLimitsAreTypedAndAtomic();
        std::cout << "texture runtime data tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "texture runtime data tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
