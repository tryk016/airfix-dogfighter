#include "airfix/crypto/Sha256.hpp"
#include "airfix/texture/TextureHdPngPreparation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using airfix::texture::PrivateTextureFileReadResult;
using airfix::texture::PrivateTextureFileStatus;
using airfix::texture::TextureHdPngPreparationIssueKind;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void appendBigEndian(Bytes &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

[[nodiscard]] std::uint32_t crc32(const std::span<const std::uint8_t> bytes) {
  std::uint32_t value = 0xFFFF'FFFFU;
  for (const auto byte : bytes) {
    value ^= byte;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
      const auto mask =
          static_cast<std::uint32_t>(-static_cast<std::int32_t>(value & 1U));
      value = (value >> 1U) ^ (0xEDB8'8320U & mask);
    }
  }
  return value ^ 0xFFFF'FFFFU;
}

[[nodiscard]] std::uint32_t adler32(const std::span<const std::uint8_t> bytes) {
  constexpr std::uint32_t modulus = 65'521U;
  std::uint32_t first = 1U;
  std::uint32_t second = 0U;
  for (const auto byte : bytes) {
    first = (first + byte) % modulus;
    second = (second + first) % modulus;
  }
  return (second << 16U) | first;
}

void appendChunk(Bytes &png, const std::array<char, 4U> &type,
                 const std::span<const std::uint8_t> data) {
  appendBigEndian(png, static_cast<std::uint32_t>(data.size()));
  const auto typeOffset = png.size();
  for (const auto character : type) {
    png.push_back(static_cast<std::uint8_t>(character));
  }
  png.insert(png.end(), data.begin(), data.end());
  appendBigEndian(png, crc32(std::span(png).subspan(
                           typeOffset, type.size() + data.size())));
}

[[nodiscard]] Bytes makePng(const std::uint32_t width,
                            const std::uint32_t height, const Bytes &rgba) {
  const auto rowBytes = static_cast<std::size_t>(width) * 4U;
  require(rgba.size() == rowBytes * height,
          "synthetic PNG pixel count is invalid");

  Bytes filtered;
  filtered.reserve((rowBytes + 1U) * height);
  for (std::uint32_t row = 0U; row < height; ++row) {
    filtered.push_back(0U);
    const auto begin =
        rgba.begin() + static_cast<std::ptrdiff_t>(row * rowBytes);
    filtered.insert(filtered.end(), begin,
                    begin + static_cast<std::ptrdiff_t>(rowBytes));
  }
  require(filtered.size() <= std::numeric_limits<std::uint16_t>::max(),
          "synthetic PNG is too large for one stored block");

  Bytes zlib{0x78U, 0x01U, 0x01U};
  const auto length = static_cast<std::uint16_t>(filtered.size());
  const auto inverse = static_cast<std::uint16_t>(~length);
  zlib.push_back(static_cast<std::uint8_t>(length));
  zlib.push_back(static_cast<std::uint8_t>(length >> 8U));
  zlib.push_back(static_cast<std::uint8_t>(inverse));
  zlib.push_back(static_cast<std::uint8_t>(inverse >> 8U));
  zlib.insert(zlib.end(), filtered.begin(), filtered.end());
  appendBigEndian(zlib, adler32(filtered));

  Bytes ihdr;
  appendBigEndian(ihdr, width);
  appendBigEndian(ihdr, height);
  ihdr.insert(ihdr.end(), {8U, 6U, 0U, 0U, 0U});

  Bytes png{0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  appendChunk(png, {'I', 'H', 'D', 'R'}, ihdr);
  appendChunk(png, {'I', 'D', 'A', 'T'}, zlib);
  appendChunk(png, {'I', 'E', 'N', 'D'}, {});
  return png;
}

[[nodiscard]] Bytes solidRgba(const std::uint32_t width,
                              const std::uint32_t height,
                              const std::array<std::uint8_t, 4U> color) {
  Bytes pixels(static_cast<std::size_t>(width) * height * 4U);
  for (std::size_t offset = 0U; offset < pixels.size(); offset += 4U) {
    std::copy(color.begin(), color.end(),
              pixels.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  return pixels;
}

class MockFileStore final : public airfix::texture::PrivateTextureFileStore {
public:
  explicit MockFileStore(const std::uint64_t generation) noexcept
      : generation_(generation) {}

  [[nodiscard]] std::uint64_t generation() const noexcept override {
    return generation_;
  }

  [[nodiscard]] PrivateTextureFileReadResult
  readFile(const std::string_view relativePath, const std::size_t maximumBytes,
           const std::uint64_t expectedGeneration) const noexcept override {
    ++readCount_;
    if (expectedGeneration != generation_) {
      return {.status = PrivateTextureFileStatus::staleGeneration};
    }
    if (forcedStatus_.has_value()) {
      return {.status = *forcedStatus_};
    }
    const auto found = files_.find(std::string(relativePath));
    if (found == files_.end()) {
      return {.status = PrivateTextureFileStatus::notFound};
    }
    if (found->second.size() > maximumBytes) {
      return {.status = PrivateTextureFileStatus::sizeLimitExceeded};
    }
    return {
        .status = PrivateTextureFileStatus::ready,
        .bytes = found->second,
    };
  }

  void add(std::string path, Bytes bytes) {
    files_.emplace(std::move(path), std::move(bytes));
  }

  void force(const PrivateTextureFileStatus status) noexcept {
    forcedStatus_ = status;
  }

  [[nodiscard]] std::size_t readCount() const noexcept { return readCount_; }

private:
  std::uint64_t generation_{};
  std::map<std::string, Bytes, std::less<>> files_;
  std::optional<PrivateTextureFileStatus> forcedStatus_;
  mutable std::size_t readCount_{};
};

struct Fixture final {
  static constexpr std::uint64_t generation = 41U;

  Bytes level0Pixels = solidRgba(4U, 4U, {10U, 20U, 30U, 255U});
  Bytes level1Pixels = solidRgba(2U, 2U, {40U, 50U, 60U, 255U});
  Bytes level2Pixels = solidRgba(1U, 1U, {70U, 80U, 90U, 255U});
  Bytes level0 = makePng(4U, 4U, level0Pixels);
  Bytes level1 = makePng(2U, 2U, level1Pixels);
  Bytes level2 = makePng(1U, 1U, level2Pixels);
  airfix::texture::TextureHdManifestRecord record;
  airfix::texture::TextureReplacementCandidate candidate;
  MockFileStore files{generation};

  Fixture() {
    const Bytes source{1U, 3U, 3U, 7U};
    record.sourceGtiSha256 = airfix::crypto::sha256(source);
    record.outputPngSha256 = airfix::crypto::sha256(level0);
    record.logicalPaths = {"textures/synthetic.gti"};
    record.source = {1U, 1U};
    record.result = {4U, 4U};
    record.alphaUsage = airfix::texture::TextureHdAlphaUsage::opaque;
    record.sourceMipCount = 1U;
    record.generatedMipCount = 3U;
    record.baseTextureRelativePath = "results/base.png";
    record.mipmapDirectoryRelativePath = "mips/synthetic";
    record.parameters.scale = 4U;
    record.parameters.sampleSpace =
        airfix::texture::TextureHdSampleSpace::encodedUnclassified;

    candidate.record = &record;
    candidate.generation = generation;
    candidate.sourceGtiSha256 = record.sourceGtiSha256;
    candidate.basePngBytes = level0;

    files.add("mips/synthetic/mip-00.png", level0);
    files.add("mips/synthetic/mip-01.png", level1);
    files.add("mips/synthetic/mip-02.png", level2);
  }
};

void requireIssue(const airfix::texture::TextureHdPngPreparation &result,
                  const TextureHdPngPreparationIssueKind kind,
                  const std::optional<std::uint32_t> level,
                  const std::string_view message) {
  require(!result.success() && !result.asset.has_value(), message);
  if (result.issue.kind != kind || result.issue.level != level) {
    throw std::runtime_error(
        std::string(message) + ": expected issue " +
        std::to_string(static_cast<unsigned>(kind)) + "/" +
        (level.has_value() ? std::to_string(*level) : "none") + ", received " +
        std::to_string(static_cast<unsigned>(result.issue.kind)) + "/" +
        (result.issue.level.has_value() ? std::to_string(*result.issue.level)
                                        : "none"));
  }
}

void testCompleteChainPreparation() {
  Fixture fixture;
  const auto result =
      airfix::texture::prepareTextureHdPng(fixture.candidate, fixture.files);
  require(result.success(), "valid synthetic RGBA8 mip chain was rejected");
  const auto &asset = *result.asset;
  require(asset.generation == Fixture::generation &&
              asset.alphaUsage ==
                  airfix::texture::TextureHdAlphaUsage::opaque &&
              asset.colorClassification ==
                  airfix::texture::TextureHdColorClassification::
                      technicalUnclassified,
          "prepared asset changed generation, alpha, or color policy");
  require(asset.levels.size() == 3U && asset.decodedRgbaBytes == 84U &&
              asset.encodedChainBytes == fixture.level0.size() +
                                             fixture.level1.size() +
                                             fixture.level2.size(),
          "prepared asset totals differ from the complete chain");
  require(asset.levels[0U].level == 0U && asset.levels[0U].width == 4U &&
              asset.levels[0U].height == 4U &&
              asset.levels[0U].bytesPerRow == 16U &&
              asset.levels[0U].integrity ==
                  airfix::texture::TextureHdMipIntegrity::authenticatedBase &&
              asset.levels[0U].rgba8 == fixture.level0Pixels &&
              asset.levels[1U].rgba8 == fixture.level1Pixels &&
              asset.levels[2U].rgba8 == fixture.level2Pixels,
          "decoded RGBA8 chain or integrity labels changed");
  require(fixture.files.readCount() == 4U,
          "preparation did not check every mip and the next numbered level");
}

void testHeaderAndDecodeFailures() {
  {
    Fixture fixture;
    fixture.level1[0U] = 0U;
    fixture.files.add("unused", {});
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-01.png", fixture.level1);
    files.add("mips/synthetic/mip-02.png", fixture.level2);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::invalidPngSignature, 1U,
                 "invalid PNG signature was accepted");
  }
  {
    Fixture fixture;
    fixture.level1[25U] = 2U;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-01.png", fixture.level1);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::unsupportedPngFormat, 1U,
                 "non-RGBA PNG was accepted");
  }
  {
    Fixture fixture;
    fixture.level1[28U] = 1U;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-01.png", fixture.level1);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::unsupportedPngFormat, 1U,
                 "interlaced PNG was accepted");
  }
  {
    Fixture fixture;
    fixture.level1[11U] = 12U;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-01.png", fixture.level1);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::invalidIhdr, 1U,
                 "invalid IHDR length was accepted");
  }
  {
    Fixture fixture;
    fixture.level1[23U] = 3U;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-01.png", fixture.level1);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::dimensionMismatch, 1U,
                 "wrong IHDR dimensions were accepted");
  }
  {
    Fixture fixture;
    fixture.level1[45U] ^= 0x40U;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-01.png", fixture.level1);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::decodeFailure, 1U,
                 "PNG with corrupt CRC was accepted");
  }
}

void testChainIdentityAndCompleteness() {
  {
    Fixture fixture;
    fixture.files.add("mips/synthetic/mip-03.png", fixture.level2);
    requireIssue(
        airfix::texture::prepareTextureHdPng(fixture.candidate, fixture.files),
        TextureHdPngPreparationIssueKind::unexpectedNumberedMip, 3U,
        "undeclared numbered mip was accepted");
  }
  {
    Fixture fixture;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-02.png", fixture.level2);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::fileNotFound, 1U,
                 "missing middle mip was accepted");
  }
  {
    Fixture fixture;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level1);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::mipZeroMismatch, 0U,
                 "mip zero differing from authenticated base was accepted");
  }
  {
    Fixture fixture;
    fixture.candidate.sourceGtiSha256[0U] ^= 0xFFU;
    requireIssue(
        airfix::texture::prepareTextureHdPng(fixture.candidate, fixture.files),
        TextureHdPngPreparationIssueKind::invalidCandidate, std::nullopt,
        "candidate identity mismatch was accepted");
    require(fixture.files.readCount() == 0U,
            "invalid candidate reached private file I/O");
  }
  {
    Fixture fixture;
    fixture.record.alphaUsage =
        static_cast<airfix::texture::TextureHdAlphaUsage>(0xFFU);
    requireIssue(
        airfix::texture::prepareTextureHdPng(fixture.candidate, fixture.files),
        TextureHdPngPreparationIssueKind::invalidCandidate, std::nullopt,
        "invalid alpha-usage metadata was accepted");
    require(fixture.files.readCount() == 0U,
            "invalid alpha metadata reached private file I/O");
  }
  {
    Fixture fixture;
    fixture.record.parameters.scale = 2U;
    requireIssue(
        airfix::texture::prepareTextureHdPng(fixture.candidate, fixture.files),
        TextureHdPngPreparationIssueKind::invalidCandidate, std::nullopt,
        "invalid 4x metadata was accepted");
    require(fixture.files.readCount() == 0U,
            "invalid scale metadata reached private file I/O");
  }
}

void testBudgetsAndGeneration() {
  {
    Fixture fixture;
    airfix::texture::TextureHdPngPreparationLimits limits;
    limits.maximumDimension = 2U;
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate,
                                                      fixture.files, limits),
                 TextureHdPngPreparationIssueKind::dimensionLimitExceeded,
                 std::nullopt, "dimension budget was not enforced");
  }
  {
    Fixture fixture;
    airfix::texture::TextureHdPngPreparationLimits limits;
    limits.maximumEncodedBytesPerLevel = fixture.level0.size() - 1U;
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate,
                                                      fixture.files, limits),
                 TextureHdPngPreparationIssueKind::encodedByteLimitExceeded,
                 std::nullopt, "base encoded budget was not enforced");
  }
  {
    Fixture fixture;
    airfix::texture::TextureHdPngPreparationLimits limits;
    limits.maximumEncodedChainBytes =
        fixture.level0.size() + fixture.level1.size() - 1U;
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate,
                                                      fixture.files, limits),
                 TextureHdPngPreparationIssueKind::encodedByteLimitExceeded, 1U,
                 "encoded-chain budget was not enforced");
  }
  {
    Fixture fixture;
    airfix::texture::TextureHdPngPreparationLimits limits;
    limits.maximumEncodedBytesInFlight = fixture.level0.size() * 2U - 1U;
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate,
                                                      fixture.files, limits),
                 TextureHdPngPreparationIssueKind::encodedByteLimitExceeded, 0U,
                 "encoded in-flight budget was not enforced");
  }
  {
    Fixture fixture;
    airfix::texture::TextureHdPngPreparationLimits limits;
    limits.maximumDecodedRgbaBytes = 83U;
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate,
                                                      fixture.files, limits),
                 TextureHdPngPreparationIssueKind::decodedByteLimitExceeded, 2U,
                 "decoded chain budget was not enforced");
  }
  {
    Fixture fixture;
    MockFileStore newer(Fixture::generation + 1U);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, newer),
                 TextureHdPngPreparationIssueKind::staleGeneration,
                 std::nullopt, "stale file-store generation was accepted");
  }
  {
    Fixture fixture;
    airfix::texture::TextureHdPngPreparationLimits limits;
    limits.maximumMipLevels = 0U;
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate,
                                                      fixture.files, limits),
                 TextureHdPngPreparationIssueKind::invalidLimits, std::nullopt,
                 "zero mip-level limit was accepted");
  }
}

void testAlphaContract() {
  {
    Fixture fixture;
    fixture.level0Pixels[3U] = 0U;
    fixture.level0 = makePng(4U, 4U, fixture.level0Pixels);
    fixture.record.outputPngSha256 = airfix::crypto::sha256(fixture.level0);
    fixture.candidate.basePngBytes = fixture.level0;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::alphaUsageMismatch, 0U,
                 "opaque alpha metadata mismatch was accepted");
  }
  {
    Fixture fixture;
    fixture.record.alphaUsage = airfix::texture::TextureHdAlphaUsage::binary;
    fixture.level0Pixels[3U] = 127U;
    fixture.level0 = makePng(4U, 4U, fixture.level0Pixels);
    fixture.record.outputPngSha256 = airfix::crypto::sha256(fixture.level0);
    fixture.candidate.basePngBytes = fixture.level0;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    requireIssue(airfix::texture::prepareTextureHdPng(fixture.candidate, files),
                 TextureHdPngPreparationIssueKind::alphaUsageMismatch, 0U,
                 "non-binary alpha was accepted for binary metadata");
  }
  {
    Fixture fixture;
    fixture.record.alphaUsage =
        airfix::texture::TextureHdAlphaUsage::translucent;
    fixture.level0Pixels[3U] = 127U;
    fixture.level0 = makePng(4U, 4U, fixture.level0Pixels);
    fixture.record.outputPngSha256 = airfix::crypto::sha256(fixture.level0);
    fixture.candidate.basePngBytes = fixture.level0;
    MockFileStore files(Fixture::generation);
    files.add("mips/synthetic/mip-00.png", fixture.level0);
    files.add("mips/synthetic/mip-01.png", fixture.level1);
    files.add("mips/synthetic/mip-02.png", fixture.level2);
    require(airfix::texture::prepareTextureHdPng(fixture.candidate, files)
                .success(),
            "translucent straight alpha was rejected");
  }
}

void testFileFailureMapping() {
  const std::array mappings{
      std::pair{PrivateTextureFileStatus::invalidArgument,
                TextureHdPngPreparationIssueKind::invalidLimits},
      std::pair{PrivateTextureFileStatus::invalidRelativePath,
                TextureHdPngPreparationIssueKind::unsafeRelativePath},
      std::pair{PrivateTextureFileStatus::staleGeneration,
                TextureHdPngPreparationIssueKind::staleGeneration},
      std::pair{PrivateTextureFileStatus::unsafeIndirection,
                TextureHdPngPreparationIssueKind::unsafeIndirection},
      std::pair{PrivateTextureFileStatus::unsafeType,
                TextureHdPngPreparationIssueKind::unsafeFileType},
      std::pair{PrivateTextureFileStatus::multipleLinks,
                TextureHdPngPreparationIssueKind::multipleLinks},
      std::pair{PrivateTextureFileStatus::sizeLimitExceeded,
                TextureHdPngPreparationIssueKind::encodedByteLimitExceeded},
      std::pair{PrivateTextureFileStatus::changedDuringRead,
                TextureHdPngPreparationIssueKind::changedDuringRead},
      std::pair{PrivateTextureFileStatus::ioFailure,
                TextureHdPngPreparationIssueKind::fileIoFailure},
  };
  for (const auto &[status, issue] : mappings) {
    Fixture fixture;
    fixture.files.force(status);
    requireIssue(
        airfix::texture::prepareTextureHdPng(fixture.candidate, fixture.files),
        issue, 0U, "private file failure was not fail-closed");
  }
}

} // namespace

int main() {
  try {
    testCompleteChainPreparation();
    testHeaderAndDecodeFailures();
    testChainIdentityAndCompleteness();
    testBudgetsAndGeneration();
    testAlphaContract();
    testFileFailureMapping();
    std::cout << "texture HD PNG preparation tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "texture HD PNG preparation tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
