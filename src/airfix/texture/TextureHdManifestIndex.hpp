#pragma once

#include "airfix/crypto/Sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace airfix::texture {

enum class TextureHdAlphaUsage : std::uint8_t {
    opaque,
    binary,
    translucent,
};

enum class TextureHdEdgeMode : std::uint8_t {
    wrap,
    clamp,
};

enum class TextureHdSampleSpace : std::uint8_t {
    encodedUnclassified,
};

enum class TextureHdManifestIssueKind : std::uint8_t {
    inputLimitExceeded,
    lineLimitExceeded,
    lineCountLimitExceeded,
    recordLimitExceeded,
    containerLimitExceeded,
    malformedJson,
    unsupportedSchema,
    invalidHeader,
    invalidRecord,
    invalidDigest,
    invalidLogicalPath,
    invalidRelativePath,
    duplicateLogicalPath,
    duplicateSourceDigest,
    countMismatch,
};

struct TextureHdManifestIssue {
    TextureHdManifestIssueKind kind{TextureHdManifestIssueKind::malformedJson};
    // One-based JSONL line only. No private field, path, checksum, or value is
    // retained in diagnostics.
    std::optional<std::size_t> lineNumber;
};

struct TextureHdManifestLimits {
    std::size_t maximumInputBytes{std::size_t{64U} * 1024U * 1024U};
    std::size_t maximumLineBytes{std::size_t{1024U} * 1024U};
    std::size_t maximumLines{4'097U};
    std::size_t maximumRecords{4'096U};
    std::size_t maximumItemsPerLine{32'768U};
    std::size_t maximumStringBytes{std::size_t{64U} * 1024U};
    std::size_t maximumTotalStringBytesPerLine{
        std::size_t{512U} * 1024U};
    std::size_t maximumDepth{32U};
    std::size_t maximumLogicalPathsPerRecord{4'096U};
    std::size_t maximumTotalLogicalPaths{262'144U};
    std::size_t maximumLogicalPathBytes{4'096U};
    std::size_t maximumRelativePathBytes{4'096U};
    std::uint32_t maximumDimension{65'536U};
    std::uint32_t maximumMipLevels{32U};
};

struct TextureHdDimensions {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] friend constexpr bool operator==(
        const TextureHdDimensions&,
        const TextureHdDimensions&) = default;
};

struct TextureHdManifestParameters {
    std::uint32_t scale{};
    TextureHdEdgeMode edgeMode{TextureHdEdgeMode::clamp};
    std::string rgbResampler;
    std::string alphaResampler;
    bool transparentRgbExtension{};
    TextureHdSampleSpace sampleSpace{
        TextureHdSampleSpace::encodedUnclassified};
};

struct TextureHdManifestRecord {
    crypto::Sha256Digest sourceGtiSha256{};
    crypto::Sha256Digest outputPngSha256{};
    std::vector<std::string> logicalPaths;
    TextureHdDimensions source;
    TextureHdDimensions result;
    TextureHdAlphaUsage alphaUsage{TextureHdAlphaUsage::opaque};
    std::uint32_t sourceMipCount{};
    std::uint32_t generatedMipCount{};
    std::string baseTextureRelativePath;
    std::string mipmapDirectoryRelativePath;
    std::string method;
    TextureHdManifestParameters parameters;
};

struct TextureHdManifestSummary {
    std::uint32_t schemaVersion{};
    std::uint32_t scale{};
    crypto::Sha256Digest manifestSha256{};
    crypto::Sha256Digest sourceCorpusId{};
    crypto::Sha256Digest baseManifestSha256{};
    std::size_t declaredResultCount{};
    std::size_t declaredLogicalTextureCount{};
    std::size_t acceptedResultCount{};
};

struct TextureHdManifestParseResult;

class TextureHdManifestIndex final {
public:
    [[nodiscard]] const TextureHdManifestSummary& summary() const noexcept {
        return summary_;
    }

    [[nodiscard]] std::span<const TextureHdManifestRecord> records() const
        noexcept {
        return records_;
    }

    // Query normalization is the same slash and ASCII-case policy used while
    // indexing. Invalid logical paths return no candidate. No query value is
    // logged or copied into a diagnostic.
    [[nodiscard]] const TextureHdManifestRecord* findByLogicalPath(
        std::string_view logicalPath) const;

    [[nodiscard]] const TextureHdManifestRecord* findBySourceGtiSha256(
        const crypto::Sha256Digest& digest) const noexcept;

private:
    struct LogicalPathEntry {
        std::string normalizedPath;
        std::size_t recordIndex{};
    };

    struct DigestEntry {
        crypto::Sha256Digest digest{};
        std::size_t recordIndex{};
    };

    friend TextureHdManifestParseResult parseTextureHdManifest(
        std::span<const std::uint8_t>, const TextureHdManifestLimits&);

    TextureHdManifestSummary summary_;
    std::vector<TextureHdManifestRecord> records_;
    std::vector<LogicalPathEntry> logicalPaths_;
    std::vector<DigestEntry> sourceDigests_;
    std::size_t logicalPathLimit_{};
};

struct TextureHdManifestParseResult {
    std::optional<TextureHdManifestIndex> index;
    std::vector<TextureHdManifestIssue> issues;

    [[nodiscard]] bool success() const noexcept {
        return index.has_value() && issues.empty();
    }
};

// Parses one reviewed-corpus header followed by bounded result records. Only
// records whose public status and explicit review status are both accepted are
// retained. The parser performs no filesystem I/O and exposes no diagnostic
// text derived from private manifest values.
[[nodiscard]] TextureHdManifestParseResult parseTextureHdManifest(
    std::span<const std::uint8_t> bytes,
    const TextureHdManifestLimits& limits = {});

} // namespace airfix::texture
