#include "airfix/texture/TextureHdManifestIndex.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace airfix::texture;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::string digestText(const char digit) {
    return std::string(64U, digit);
}

[[nodiscard]] airfix::crypto::Sha256Digest digestBytes(
    const std::uint8_t value) {
    airfix::crypto::Sha256Digest digest{};
    digest.fill(value);
    return digest;
}

[[nodiscard]] std::span<const std::uint8_t> bytesOf(
    const std::string& text) {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size(),
    };
}

[[nodiscard]] std::string header(
    const std::size_t results,
    const std::size_t logicalPaths,
    const std::size_t accepted,
    const std::size_t rejected = 0U,
    const std::size_t manualReview = 0U,
    const std::string_view reviewRun = "synthetic-review") {
    std::ostringstream json;
    json << '{'
         << "\"record_type\":\"hd-reviewed-corpus\","
         << "\"schema_version\":1,"
         << "\"review_run_id\":\"" << reviewRun << "\","
         << "\"source_corpus_id\":\"" << digestText('a') << "\","
         << "\"base_manifest_sha256\":\"" << digestText('b') << "\","
         << "\"base_pipeline_version\":\"synthetic-v1\","
         << "\"override_sources\":[{"
         << "\"sha256\":\"" << digestText('c') << "\","
         << "\"pipeline_version\":\"synthetic-v1\","
         << "\"result_count\":1}],"
         << "\"unique_result_count\":" << results << ','
         << "\"logical_texture_count\":" << logicalPaths << ','
         << "\"status_counts\":{"
         << "\"accepted\":" << accepted << ','
         << "\"rejected\":" << rejected << ','
         << "\"manual-review\":" << manualReview << "},"
         << "\"review_basis\":\"synthetic test fixture\","
         << "\"local_only\":true}"
         << '\n';
    return json.str();
}

struct RecordOptions {
    char sourceDigest{'1'};
    char inputDigest{'2'};
    char outputDigest{'3'};
    std::string logicalPaths{"\"Textures/UI/Hud.gti\""};
    std::string categories{"\"ui-fonts\""};
    std::string category{"ui-fonts"};
    std::string sourceCorpus{digestText('a')};
    std::string status{"accepted"};
    std::string reviewStatus{"accepted"};
    std::string reviewRun{"synthetic-review"};
    std::string sampleSpace{"encoded-unclassified"};
    std::string resultPath{"hd/results/base.png"};
    std::string mipDirectory{"hd/mips/source"};
    std::string comparisonBoard{"hd/boards/source.png"};
    std::uint32_t scale{4U};
    std::uint32_t sourceWidth{4U};
    std::uint32_t sourceHeight{2U};
    std::uint32_t sourceMips{3U};
    std::uint32_t resultWidth{16U};
    std::uint32_t resultHeight{8U};
    std::uint32_t generatedMips{5U};
    std::string alphaUsage{"translucent"};
    std::string model{"null"};
    std::string modelProvenance{"null"};
    std::string qa{"{\"score\":1.25,\"optional\":null}"};
    std::string extraTopLevel;
    bool includeReview{true};
};

[[nodiscard]] std::string record(const RecordOptions& options = {}) {
    std::ostringstream json;
    json << '{'
         << "\"schema_version\":1,"
         << "\"pipeline_version\":\"synthetic-v1\","
         << "\"run_id\":\"synthetic-run\","
         << "\"source_corpus_id\":\"" << options.sourceCorpus << "\","
         << "\"source_gti_sha256\":\""
         << digestText(options.sourceDigest) << "\","
         << "\"input_png_sha256\":\""
         << digestText(options.inputDigest) << "\","
         << "\"output_png_sha256\":\""
         << digestText(options.outputDigest) << "\","
         << "\"logical_paths\":[" << options.logicalPaths << "],"
         << "\"categories\":[" << options.categories << "],"
         << "\"category\":\"" << options.category << "\","
         << "\"role\":\"synthetic-role\","
         << "\"method\":\"synthetic-method\","
         << "\"model\":" << options.model << ','
         << "\"model_provenance\":" << options.modelProvenance << ','
         << "\"parameters\":{"
         << "\"scale\":" << options.scale << ','
         << "\"edge_mode\":\"clamp\","
         << "\"rgb_resampler\":\"lanczos\","
         << "\"alpha_resampler\":\"bicubic\","
         << "\"transparent_rgb_extension\":true,"
         << "\"sample_space\":\"" << options.sampleSpace << "\","
         << "\"future_parameter\":null},"
         << "\"source\":{"
         << "\"width\":" << options.sourceWidth << ','
         << "\"height\":" << options.sourceHeight << ','
         << "\"alpha_usage\":\"" << options.alphaUsage << "\","
         << "\"mipmaps\":" << options.sourceMips << "},"
         << "\"result\":{"
         << "\"width\":" << options.resultWidth << ','
         << "\"height\":" << options.resultHeight << ','
         << "\"generated_mipmaps\":" << options.generatedMips << ','
         << "\"path\":\"" << options.resultPath << "\","
         << "\"mipmap_directory\":\"" << options.mipDirectory << "\","
         << "\"comparison_board\":\"" << options.comparisonBoard << "\"},"
         << "\"qa\":" << options.qa << ','
         << "\"status\":\"" << options.status << "\","
         << "\"automated_status\":\"manual-review\"";
    if (options.includeReview) {
        json << ",\"review\":{"
             << "\"status\":\"" << options.reviewStatus << "\","
             << "\"basis\":\"synthetic explicit review\","
             << "\"review_run_id\":\"" << options.reviewRun << "\","
             << "\"used_override\":false}";
    }
    json << options.extraTopLevel << '}' << '\n';
    return json.str();
}

[[nodiscard]] std::string replaceOnce(
    std::string text,
    const std::string_view from,
    const std::string_view to) {
    const auto offset = text.find(from);
    if (offset == std::string::npos) {
        throw std::runtime_error("synthetic replacement source not found");
    }
    text.replace(offset, from.size(), to);
    return text;
}

void requireIssue(
    const std::string& document,
    const TextureHdManifestIssueKind kind,
    const std::optional<std::size_t> lineNumber,
    const TextureHdManifestLimits& limits = {}) {
    const auto parsed = parseTextureHdManifest(bytesOf(document), limits);
    require(!parsed.index.has_value() && parsed.issues.size() == 1U,
        "invalid synthetic manifest did not fail atomically");
    if (parsed.issues[0U].kind != kind) {
        std::ostringstream message;
        message << "synthetic manifest issue mismatch: expected "
                << static_cast<int>(kind) << ", actual "
                << static_cast<int>(parsed.issues[0U].kind);
        throw std::runtime_error(message.str());
    }
    if (parsed.issues[0U].lineNumber != lineNumber) {
        std::ostringstream message;
        message << "synthetic manifest line mismatch: expected ";
        if (lineNumber.has_value()) {
            message << *lineNumber;
        } else {
            message << "none";
        }
        message << ", actual ";
        if (parsed.issues[0U].lineNumber.has_value()) {
            message << *parsed.issues[0U].lineNumber;
        } else {
            message << "none";
        }
        throw std::runtime_error(message.str());
    }
}

void testAcceptedOnlyIndexAndLookups() {
    RecordOptions rejected;
    rejected.sourceDigest = '4';
    rejected.inputDigest = '5';
    rejected.outputDigest = '6';
    rejected.logicalPaths = "\"Textures/Effects/Smoke.gti\"";
    rejected.categories = "\"effects\"";
    rejected.category = "effects";
    rejected.status = "rejected";
    rejected.reviewStatus = "rejected";

    const auto document = header(2U, 2U, 1U, 1U) + record() + record(rejected);
    const auto parsed = parseTextureHdManifest(bytesOf(document));
    require(parsed.success(), "valid reviewed synthetic corpus was rejected");
    const auto& index = *parsed.index;
    require(index.summary().schemaVersion == 1U &&
                index.summary().scale == 4U &&
                index.summary().declaredResultCount == 2U &&
                index.summary().declaredLogicalTextureCount == 2U &&
                index.summary().acceptedResultCount == 1U &&
                index.summary().manifestSha256 ==
                    airfix::crypto::sha256(bytesOf(document)),
        "reviewed-corpus summary is incorrect");
    require(index.records().size() == 1U,
        "non-accepted record entered the runtime index");

    const auto& accepted = index.records()[0U];
    require(accepted.sourceGtiSha256 == digestBytes(0x11U) &&
                accepted.outputPngSha256 == digestBytes(0x33U) &&
                accepted.logicalPaths ==
                    std::vector<std::string>{"textures\\ui\\hud.gti"} &&
                accepted.source == TextureHdDimensions{4U, 2U} &&
                accepted.result == TextureHdDimensions{16U, 8U} &&
                accepted.alphaUsage == TextureHdAlphaUsage::translucent &&
                accepted.sourceMipCount == 3U &&
                accepted.generatedMipCount == 5U &&
                accepted.baseTextureRelativePath == "hd/results/base.png" &&
                accepted.mipmapDirectoryRelativePath == "hd/mips/source" &&
                accepted.method == "synthetic-method" &&
                accepted.parameters.scale == 4U &&
                accepted.parameters.edgeMode == TextureHdEdgeMode::clamp &&
                accepted.parameters.rgbResampler == "lanczos" &&
                accepted.parameters.alphaResampler == "bicubic" &&
                accepted.parameters.transparentRgbExtension,
        "accepted replacement metadata was not retained exactly");

    require(index.findByLogicalPath("TEXTURES\\UI/HUD.GTI") == &accepted,
        "logical path lookup did not use normalized legacy identity");
    require(index.findBySourceGtiSha256(digestBytes(0x11U)) == &accepted,
        "source digest lookup did not find the accepted record");
    require(index.findByLogicalPath("Textures/Effects/Smoke.gti") == nullptr &&
                index.findBySourceGtiSha256(digestBytes(0x44U)) == nullptr,
        "rejected record remained queryable");
    require(index.findByLogicalPath("../escape.gti") == nullptr,
        "invalid lookup path did not fail closed");
}

void testJsonAndInputLimits() {
    requireIssue({}, TextureHdManifestIssueKind::invalidHeader, std::nullopt);
    requireIssue("{\n", TextureHdManifestIssueKind::malformedJson, 1U);

    auto byteOrderMark = std::string{"\xEF\xBB\xBF"};
    byteOrderMark += header(0U, 0U, 0U);
    requireIssue(
        byteOrderMark,
        TextureHdManifestIssueKind::malformedJson,
        1U);

    auto invalidUtf8 = replaceOnce(
        header(0U, 0U, 0U),
        "synthetic test fixture",
        std::string(1U, static_cast<char>(0xC0U)));
    requireIssue(
        invalidUtf8,
        TextureHdManifestIssueKind::malformedJson,
        1U);

    const auto escapedUnicode = replaceOnce(
        header(0U, 0U, 0U),
        "synthetic test fixture",
        "synthetic \\uD83D\\uDE80");
    require(
        parseTextureHdManifest(bytesOf(escapedUnicode)).success(),
        "valid escaped Unicode was rejected");

    auto duplicateKey = header(0U, 0U, 0U);
    duplicateKey = replaceOnce(
        duplicateKey,
        "\"schema_version\":1,",
        "\"schema_version\":1,\"schema_version\":1,");
    requireIssue(
        duplicateKey,
        TextureHdManifestIssueKind::malformedJson,
        1U);

    const auto valid = header(1U, 1U, 1U) + record();
    auto limits = TextureHdManifestLimits{};
    limits.maximumInputBytes = valid.size() - 1U;
    requireIssue(
        valid,
        TextureHdManifestIssueKind::inputLimitExceeded,
        std::nullopt,
        limits);

    limits = {};
    limits.maximumLineBytes = header(1U, 1U, 1U).size() - 2U;
    requireIssue(
        valid,
        TextureHdManifestIssueKind::lineLimitExceeded,
        1U,
        limits);

    limits = {};
    limits.maximumLines = 1U;
    requireIssue(
        valid,
        TextureHdManifestIssueKind::lineCountLimitExceeded,
        2U,
        limits);

    limits = {};
    limits.maximumItemsPerLine = 1U;
    requireIssue(
        valid,
        TextureHdManifestIssueKind::containerLimitExceeded,
        1U,
        limits);

    limits = {};
    limits.maximumStringBytes = 4U;
    requireIssue(
        valid,
        TextureHdManifestIssueKind::containerLimitExceeded,
        1U,
        limits);

    limits = {};
    limits.maximumTotalStringBytesPerLine = 4U;
    requireIssue(
        valid,
        TextureHdManifestIssueKind::containerLimitExceeded,
        1U,
        limits);

    limits = {};
    limits.maximumDepth = 1U;
    requireIssue(
        valid,
        TextureHdManifestIssueKind::containerLimitExceeded,
        1U,
        limits);
}

void testHeaderValidation() {
    requireIssue(
        record(),
        TextureHdManifestIssueKind::invalidHeader,
        1U);

    auto invalid = replaceOnce(
        header(0U, 0U, 0U),
        "\"schema_version\":1",
        "\"schema_version\":2");
    requireIssue(
        invalid,
        TextureHdManifestIssueKind::unsupportedSchema,
        1U);

    invalid = replaceOnce(
        header(0U, 0U, 0U),
        "\"local_only\":true",
        "\"local_only\":false");
    requireIssue(
        invalid,
        TextureHdManifestIssueKind::invalidHeader,
        1U);

    invalid = replaceOnce(
        header(0U, 0U, 0U),
        "\"manual-review\":0",
        "\"manual-review\":1");
    requireIssue(
        invalid,
        TextureHdManifestIssueKind::countMismatch,
        1U);

    invalid = replaceOnce(
        header(0U, 0U, 0U),
        digestText('a'),
        std::string(64U, 'A'));
    requireIssue(
        invalid,
        TextureHdManifestIssueKind::invalidDigest,
        1U);

    invalid = replaceOnce(
        header(0U, 0U, 0U),
        "\"local_only\":true",
        "\"unexpected\":null,\"local_only\":true");
    requireIssue(
        invalid,
        TextureHdManifestIssueKind::invalidHeader,
        1U);
}

void testRecordSchemaAndIdentityValidation() {
    RecordOptions options;
    options.extraTopLevel = ",\"unexpected\":true";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.includeReview = false;
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.reviewStatus = "rejected";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.reviewRun = "different-review";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.sourceCorpus = digestText('d');
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.sampleSpace = "srgb";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.scale = 2U;
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.resultWidth = 15U;
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.generatedMips = 4U;
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.category = "world";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.model = "true";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);

    options = {};
    options.qa = "[]";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRecord,
        2U);
}

void testPathsDigestsAndDuplicatesFailClosed() {
    RecordOptions options;
    options.logicalPaths = "\"../escape.gti\"";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidLogicalPath,
        2U);

    options = {};
    options.resultPath = "../escape.png";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRelativePath,
        2U);

    options = {};
    options.logicalPaths = "\"/absolute.gti\"";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidLogicalPath,
        2U);

    options = {};
    options.resultPath = "C:\\\\absolute.png";
    requireIssue(
        header(1U, 1U, 1U) + record(options),
        TextureHdManifestIssueKind::invalidRelativePath,
        2U);

    auto invalidDigest = header(1U, 1U, 1U) + record();
    invalidDigest = replaceOnce(
        invalidDigest,
        digestText('1'),
        std::string(64U, 'G'));
    requireIssue(
        invalidDigest,
        TextureHdManifestIssueKind::invalidDigest,
        2U);

    options = {};
    options.logicalPaths =
        "\"Textures/UI/Hud.gti\",\"textures\\\\ui\\\\HUD.GTI\"";
    requireIssue(
        header(1U, 2U, 1U) + record(options),
        TextureHdManifestIssueKind::duplicateLogicalPath,
        2U);

    RecordOptions second;
    second.sourceDigest = '4';
    second.inputDigest = '5';
    second.outputDigest = '6';
    second.logicalPaths = "\"textures\\\\ui\\\\HUD.GTI\"";
    requireIssue(
        header(2U, 2U, 2U) + record() + record(second),
        TextureHdManifestIssueKind::duplicateLogicalPath,
        3U);

    second.logicalPaths = "\"Textures/World/Wall.gti\"";
    second.sourceDigest = '1';
    requireIssue(
        header(2U, 2U, 2U) + record() + record(second),
        TextureHdManifestIssueKind::duplicateSourceDigest,
        3U);
}

void testCountsAndConfiguredRecordBounds() {
    requireIssue(
        header(2U, 2U, 2U) + record(),
        TextureHdManifestIssueKind::countMismatch,
        std::nullopt);

    requireIssue(
        header(1U, 2U, 1U) + record(),
        TextureHdManifestIssueKind::countMismatch,
        std::nullopt);

    requireIssue(
        header(1U, 1U, 0U, 1U) + record(),
        TextureHdManifestIssueKind::countMismatch,
        std::nullopt);

    auto limits = TextureHdManifestLimits{};
    limits.maximumRecords = 0U;
    requireIssue(
        header(1U, 1U, 1U) + record(),
        TextureHdManifestIssueKind::recordLimitExceeded,
        1U,
        limits);

    limits = {};
    limits.maximumLogicalPathsPerRecord = 1U;
    RecordOptions options;
    options.logicalPaths = "\"A/one.gti\",\"B/two.gti\"";
    requireIssue(
        header(1U, 2U, 1U) + record(options),
        TextureHdManifestIssueKind::containerLimitExceeded,
        2U,
        limits);

    limits = {};
    limits.maximumTotalLogicalPaths = 0U;
    requireIssue(
        header(1U, 1U, 1U) + record(),
        TextureHdManifestIssueKind::containerLimitExceeded,
        1U,
        limits);

    limits = {};
    limits.maximumLogicalPathBytes = 4U;
    requireIssue(
        header(1U, 1U, 1U) + record(),
        TextureHdManifestIssueKind::invalidLogicalPath,
        2U,
        limits);

    limits = {};
    limits.maximumRelativePathBytes = 4U;
    requireIssue(
        header(1U, 1U, 1U) + record(),
        TextureHdManifestIssueKind::invalidRelativePath,
        2U,
        limits);

    limits = {};
    limits.maximumDimension = 15U;
    requireIssue(
        header(1U, 1U, 1U) + record(),
        TextureHdManifestIssueKind::invalidRecord,
        2U,
        limits);

    limits = {};
    limits.maximumMipLevels = 4U;
    requireIssue(
        header(1U, 1U, 1U) + record(),
        TextureHdManifestIssueKind::invalidRecord,
        2U,
        limits);
}

} // namespace

int main() {
    try {
        testAcceptedOnlyIndexAndLookups();
        testJsonAndInputLimits();
        testHeaderValidation();
        testRecordSchemaAndIdentityValidation();
        testPathsDigestsAndDuplicatesFailClosed();
        testCountsAndConfiguredRecordBounds();
        std::cout << "texture HD manifest index tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "texture HD manifest index tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
