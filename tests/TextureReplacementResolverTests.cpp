#include "airfix/crypto/Sha256.hpp"
#include "airfix/render/TextureRuntimeData.hpp"
#include "airfix/texture/TextureHdManifestIndex.hpp"
#include "airfix/texture/TextureReplacementResolver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using namespace airfix;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

[[nodiscard]] Bytes makeGti() {
    Bytes bytes;
    appendU32(bytes, assets::kGtiMagic);
    appendU32(bytes, assets::kGtiVersion);
    appendU32(bytes, assets::kGtiChecksumChunk);
    appendU32(bytes, 4U);
    appendU32(bytes, 0x12345678U);
    appendU32(bytes, assets::kGtiImageChunk);
    appendU32(bytes, 24U);
    appendU32(bytes, 8U);
    appendU32(bytes, 1U);
    appendU32(bytes, 1U);
    appendU32(bytes, 0U);
    appendU32(bytes, 1U);
    bytes.insert(bytes.end(), {0x10U, 0x20U, 0x30U, 0xFFU});
    return bytes;
}

[[nodiscard]] std::span<const std::uint8_t>
bytesOf(const std::string& text) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

[[nodiscard]] std::string
makeManifest(const crypto::Sha256Digest& sourceDigest,
             const crypto::Sha256Digest& outputDigest) {
    const std::string sixtyFourA(64U, 'a');
    const std::string sixtyFourB(64U, 'b');
    const std::string sixtyFourC(64U, 'c');
    std::ostringstream json;
    json << '{' << "\"record_type\":\"hd-reviewed-corpus\","
         << "\"schema_version\":1,"
         << "\"review_run_id\":\"synthetic-review\","
         << "\"source_corpus_id\":\"" << sixtyFourA << "\","
         << "\"base_manifest_sha256\":\"" << sixtyFourB << "\","
         << "\"base_pipeline_version\":\"synthetic-v1\","
         << "\"override_sources\":[{\"sha256\":\"" << sixtyFourC
         << "\",\"pipeline_version\":\"synthetic-v1\","
         << "\"result_count\":1}],"
         << "\"unique_result_count\":1,"
         << "\"logical_texture_count\":1,"
         << "\"status_counts\":{\"accepted\":1,\"rejected\":0,"
         << "\"manual-review\":0},"
         << "\"review_basis\":\"synthetic fixture\","
         << "\"local_only\":true}\n";
    json << '{' << "\"schema_version\":1,"
         << "\"pipeline_version\":\"synthetic-v1\","
         << "\"run_id\":\"synthetic-run\","
         << "\"source_corpus_id\":\"" << sixtyFourA << "\","
         << "\"source_gti_sha256\":\"" << crypto::toHex(sourceDigest) << "\","
         << "\"input_png_sha256\":\"" << sixtyFourB << "\","
         << "\"output_png_sha256\":\"" << crypto::toHex(outputDigest) << "\","
         << "\"logical_paths\":[\"Textures/UI/Hud.gti\"],"
         << "\"categories\":[\"ui-fonts\"],"
         << "\"category\":\"ui-fonts\","
         << "\"role\":\"synthetic-role\","
         << "\"method\":\"synthetic-method\","
         << "\"model\":null,\"model_provenance\":null,"
         << "\"parameters\":{\"scale\":4,\"edge_mode\":\"clamp\","
         << "\"rgb_resampler\":\"lanczos\","
         << "\"alpha_resampler\":\"bicubic\","
         << "\"transparent_rgb_extension\":true,"
         << "\"sample_space\":\"encoded-unclassified\"},"
         << "\"source\":{\"width\":1,\"height\":1,"
         << "\"alpha_usage\":\"opaque\",\"mipmaps\":1},"
         << "\"result\":{\"width\":4,\"height\":4,"
         << "\"generated_mipmaps\":3,"
         << "\"path\":\"layers/result/base.png\","
         << "\"mipmap_directory\":\"layers/result/mips\","
         << "\"comparison_board\":\"layers/result/board.png\"},"
         << "\"qa\":{},\"status\":\"accepted\","
         << "\"automated_status\":\"manual-review\","
         << "\"review\":{\"status\":\"accepted\","
         << "\"basis\":\"synthetic explicit review\","
         << "\"review_run_id\":\"synthetic-review\","
         << "\"used_override\":false}}\n";
    return json.str();
}

class MockFileStore final : public texture::PrivateTextureFileStore {
  public:
    MockFileStore(const std::uint64_t generation, Bytes bytes,
                  const texture::PrivateTextureFileStatus status =
                      texture::PrivateTextureFileStatus::ready)
        : generation_(generation), bytes_(std::move(bytes)), status_(status) {}

    [[nodiscard]] std::uint64_t generation() const noexcept override {
        return generation_;
    }

    [[nodiscard]] texture::PrivateTextureFileReadResult
    readFile(const std::string_view relativePath,
             const std::size_t maximumBytes,
             const std::uint64_t expectedGeneration) const noexcept override {
        ++readCount_;
        if (expectedGeneration != generation_) {
            return {.status =
                        texture::PrivateTextureFileStatus::staleGeneration};
        }
        if (relativePath != "layers/result/base.png") {
            return {.status = texture::PrivateTextureFileStatus::notFound};
        }
        if (status_ != texture::PrivateTextureFileStatus::ready) {
            return {.status = status_};
        }
        if (bytes_.size() > maximumBytes) {
            return {.status =
                        texture::PrivateTextureFileStatus::sizeLimitExceeded};
        }
        try {
            return {
                .status = texture::PrivateTextureFileStatus::ready,
                .bytes = bytes_,
            };
        } catch (...) {
            return {.status = texture::PrivateTextureFileStatus::ioFailure};
        }
    }

    [[nodiscard]] std::size_t readCount() const noexcept { return readCount_; }

  private:
    std::uint64_t generation_{};
    Bytes bytes_;
    texture::PrivateTextureFileStatus status_{};
    mutable std::size_t readCount_{};
};

[[nodiscard]] texture::TextureHdManifestIndex makeIndex(const Bytes& source,
                                                        const Bytes& output) {
    const auto manifest =
        makeManifest(crypto::sha256(source), crypto::sha256(output));
    auto parsed = texture::parseTextureHdManifest(bytesOf(manifest));
    require(parsed.success(), "synthetic reviewed manifest was rejected");
    return std::move(*parsed.index);
}

[[nodiscard]] bool
sameClassicPreparation(const render::GtiUploadPreparation& left,
                       const render::GtiUploadPreparation& right) {
    if (!left.success() || !right.success() || !left.plan.has_value() ||
        !right.plan.has_value()) {
        return false;
    }
    const auto& a = *left.plan;
    const auto& b = *right.plan;
    if (a.request != b.request || a.variantIndex != b.variantIndex ||
        a.format != b.format || a.checksum != b.checksum ||
        a.sampleSpace != b.sampleSpace || a.mipPolicy != b.mipPolicy ||
        a.uploadLevels != b.uploadLevels ||
        a.allocatedMipCount != b.allocatedMipCount ||
        a.uploadedMipCount != b.uploadedMipCount ||
        a.decodedRgbaBytes != b.decodedRgbaBytes ||
        a.uploadRgbaBytes != b.uploadRgbaBytes ||
        a.residentRgbaBytes != b.residentRgbaBytes ||
        left.uploadLevels.size() != right.uploadLevels.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.uploadLevels.size(); ++index) {
        const auto& x = left.uploadLevels[index];
        const auto& y = right.uploadLevels[index];
        if (x.width != y.width || x.height != y.height ||
            x.pixels != y.pixels) {
            return false;
        }
    }
    return true;
}

void testLogicalPathCandidateAndDigestAlternative() {
    const auto gti = makeGti();
    const Bytes encoded{0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU};
    const auto index = makeIndex(gti, encoded);
    MockFileStore files(7U, encoded);
    const texture::TextureReplacementResolver resolver(index, files, 7U);

    const auto direct = resolver.resolve("textures\\ui\\HUD.GTI", gti);
    require(direct.replacementReady(), "path match did not resolve");
    require(direct.fallbackReason ==
                    texture::TextureReplacementFallbackReason::none &&
                direct.candidate.matchKind ==
                    texture::TextureReplacementMatchKind::logicalPath &&
                direct.candidate.basePngBytes == encoded &&
                direct.candidate.generation == 7U &&
                direct.candidate.record != nullptr,
            "path candidate lost identity or bytes");

    const auto conservative = resolver.resolve("Textures/Alias/Hud.gti", gti);
    require(!conservative.replacementReady() &&
                conservative.fallbackReason ==
                    texture::TextureReplacementFallbackReason::
                        sourceDigestAlternativeNotAllowed,
            "digest alternative was not conservative by default");
    const auto alternative = resolver.resolve(
        "Textures/Alias/Hud.gti", gti, {.allowSourceDigestAlternative = true});
    require(
        alternative.replacementReady() &&
            alternative.candidate.matchKind ==
                texture::TextureReplacementMatchKind::sourceDigestAlternative,
        "explicit compatible digest alternative did not resolve");
}

void testIdentityAndGenerationFailuresDoNotRead() {
    const auto gti = makeGti();
    const Bytes encoded{1U, 2U, 3U};
    const auto index = makeIndex(gti, encoded);
    MockFileStore files(9U, encoded);
    const texture::TextureReplacementResolver resolver(index, files, 9U);

    const auto invalid = resolver.resolve("../escape.gti", gti);
    require(
        !invalid.replacementReady() && !invalid.sourceDigestComputed &&
            invalid.fallbackReason ==
                texture::TextureReplacementFallbackReason::invalidLogicalPath &&
            files.readCount() == 0U,
        "invalid logical path reached hashing or file access");

    auto changed = gti;
    changed.back() ^= 0x7FU;
    const auto mismatch = resolver.resolve("Textures/UI/Hud.gti", changed);
    require(!mismatch.replacementReady() && mismatch.sourceDigestComputed &&
                mismatch.fallbackReason ==
                    texture::TextureReplacementFallbackReason::
                        sourceDigestMismatch &&
                files.readCount() == 0U,
            "source checksum mismatch reached the private file");

    MockFileStore newerFiles(10U, encoded);
    const texture::TextureReplacementResolver stale(index, newerFiles, 9U);
    const auto staleResult = stale.resolve("Textures/UI/Hud.gti", gti);
    require(
        !staleResult.replacementReady() &&
            staleResult.fallbackReason ==
                texture::TextureReplacementFallbackReason::staleGeneration &&
            !staleResult.sourceDigestComputed && newerFiles.readCount() == 0U,
        "stale generation reached source hashing or file access");
}

void testFileFailuresAreFixedAndByteFree() {
    const auto gti = makeGti();
    const Bytes encoded{4U, 5U, 6U, 7U};
    const auto index = makeIndex(gti, encoded);
    using FileStatus = texture::PrivateTextureFileStatus;
    using Reason = texture::TextureReplacementFallbackReason;
    constexpr std::array mappings{
        std::pair{FileStatus::invalidArgument,
                  Reason::invalidResolverConfiguration},
        std::pair{FileStatus::invalidRelativePath, Reason::unsafeRelativePath},
        std::pair{FileStatus::staleGeneration, Reason::staleGeneration},
        std::pair{FileStatus::notFound, Reason::fileNotFound},
        std::pair{FileStatus::unsafeIndirection, Reason::unsafeIndirection},
        std::pair{FileStatus::unsafeType, Reason::unsafeFileType},
        std::pair{FileStatus::multipleLinks, Reason::multipleLinks},
        std::pair{FileStatus::sizeLimitExceeded,
                  Reason::encodedByteLimitExceeded},
        std::pair{FileStatus::changedDuringRead, Reason::changedDuringRead},
        std::pair{FileStatus::ioFailure, Reason::fileIoFailure},
    };
    for (const auto& [status, reason] : mappings) {
        MockFileStore files(11U, encoded, status);
        const texture::TextureReplacementResolver resolver(index, files, 11U);
        const auto result = resolver.resolve("Textures/UI/Hud.gti", gti);
        require(!result.replacementReady() && result.fallbackReason == reason &&
                    result.candidate.basePngBytes.empty(),
                "private file failure leaked bytes or changed reason");
    }

    MockFileStore corrupt(12U, Bytes{9U, 9U, 9U});
    const texture::TextureReplacementResolver corruptResolver(index, corrupt,
                                                              12U);
    const auto checksum = corruptResolver.resolve("Textures/UI/Hud.gti", gti);
    require(!checksum.replacementReady() &&
                checksum.fallbackReason == Reason::baseChecksumMismatch &&
                checksum.candidate.basePngBytes.empty(),
            "base checksum mismatch retained untrusted bytes");

    MockFileStore limitedFiles(13U, encoded);
    const texture::TextureReplacementResolver limited(
        index, limitedFiles, 13U, {.maximumBasePngBytes = 2U});
    const auto limit = limited.resolve("Textures/UI/Hud.gti", gti);
    require(!limit.replacementReady() &&
                limit.fallbackReason == Reason::encodedByteLimitExceeded,
            "encoded byte limit did not fail closed");

    const texture::TextureReplacementResolver zeroGeneration(index,
                                                             limitedFiles, 0U);
    const auto invalidConfiguration =
        zeroGeneration.resolve("Textures/UI/Hud.gti", gti);
    require(!invalidConfiguration.replacementReady() &&
                !invalidConfiguration.sourceDigestComputed &&
                invalidConfiguration.fallbackReason ==
                    Reason::invalidResolverConfiguration,
            "zero resolver generation did not fail before hashing or I/O");
}

void testEveryResolverFailureUsesTheExactClassicPath() {
    const auto gti = makeGti();
    const Bytes encoded{8U, 7U, 6U, 5U};
    const auto index = makeIndex(gti, encoded);
    const render::TextureImportRequest request{
        .assetId = render::TextureAssetId{3U},
        .archiveFileIndex = 41U,
        .logicalPath = "Textures/UI/Hud.gti",
    };
    const auto direct = render::prepareGtiUpload(request, gti);
    require(direct.success(), "synthetic Classic GTI was rejected");

    const auto classic = render::prepareTextureSource(request, gti);
    require(classic.classicFallbackUsed() && classic.sourceSelected() &&
                classic.replacement.fallbackReason ==
                    texture::TextureReplacementFallbackReason::notConfigured &&
                sameClassicPreparation(direct, classic.classicGti),
            "unconfigured resolver changed Classic preparation");

    MockFileStore missing(14U, encoded,
                          texture::PrivateTextureFileStatus::notFound);
    const texture::TextureReplacementResolver missingResolver(index, missing,
                                                              14U);
    const auto fallback =
        render::prepareTextureSource(request, gti, &missingResolver, {}, {});
    require(
        fallback.classicFallbackUsed() && fallback.sourceSelected() &&
            fallback.replacement.fallbackReason ==
                texture::TextureReplacementFallbackReason::fileNotFound &&
            sameClassicPreparation(direct, fallback.classicGti),
        "replacement failure changed GTI request, bytes, limits, or output");

    MockFileStore ready(15U, encoded);
    const texture::TextureReplacementResolver readyResolver(index, ready, 15U);
    const auto candidate =
        render::prepareTextureSource(request, gti, &readyResolver, {}, {});
    require(candidate.replacementReady() && candidate.sourceSelected() &&
                !candidate.classicGti.plan.has_value() &&
                candidate.classicGti.uploadLevels.empty() &&
                candidate.classicGti.issues.empty(),
            "resolved candidate also decoded or retained a Classic payload");
}

} // namespace

int main() {
    try {
        testLogicalPathCandidateAndDigestAlternative();
        testIdentityAndGenerationFailuresDoNotRead();
        testFileFailuresAreFixedAndByteFree();
        testEveryResolverFailureUsesTheExactClassicPath();
        std::cout << "texture replacement resolver tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "texture replacement resolver tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
