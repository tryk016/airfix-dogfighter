#include "airfix/texture/TextureHdPngPreparation.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <lodepng.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace airfix::texture {
namespace {

constexpr std::array<std::uint8_t, 8U> pngSignature{0x89U, 0x50U, 0x4EU, 0x47U,
                                                    0x0DU, 0x0AU, 0x1AU, 0x0AU};
constexpr std::size_t pngIhdrBytes = 33U;

struct PngHeader final {
  std::uint32_t width{};
  std::uint32_t height{};
};

[[nodiscard]] std::uint32_t
readBigEndianU32(const std::span<const std::uint8_t> bytes,
                 const std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] bool checkedAdd(const std::uint64_t left,
                              const std::uint64_t right,
                              std::uint64_t &result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool checkedMultiply(const std::uint64_t left,
                                   const std::uint64_t right,
                                   std::uint64_t &result) noexcept {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] TextureHdPngPreparation
failure(const TextureHdPngPreparationIssueKind kind,
        const std::optional<std::uint32_t> level = std::nullopt) noexcept {
  return {
      .asset = std::nullopt,
      .issue = {.kind = kind, .level = level},
  };
}

[[nodiscard]] TextureHdPngPreparationIssueKind
mapFileFailure(const PrivateTextureFileStatus status) noexcept {
  switch (status) {
  case PrivateTextureFileStatus::invalidArgument:
    return TextureHdPngPreparationIssueKind::invalidLimits;
  case PrivateTextureFileStatus::invalidRelativePath:
    return TextureHdPngPreparationIssueKind::unsafeRelativePath;
  case PrivateTextureFileStatus::staleGeneration:
    return TextureHdPngPreparationIssueKind::staleGeneration;
  case PrivateTextureFileStatus::notFound:
    return TextureHdPngPreparationIssueKind::fileNotFound;
  case PrivateTextureFileStatus::unsafeIndirection:
    return TextureHdPngPreparationIssueKind::unsafeIndirection;
  case PrivateTextureFileStatus::unsafeType:
    return TextureHdPngPreparationIssueKind::unsafeFileType;
  case PrivateTextureFileStatus::multipleLinks:
    return TextureHdPngPreparationIssueKind::multipleLinks;
  case PrivateTextureFileStatus::sizeLimitExceeded:
    return TextureHdPngPreparationIssueKind::encodedByteLimitExceeded;
  case PrivateTextureFileStatus::changedDuringRead:
    return TextureHdPngPreparationIssueKind::changedDuringRead;
  case PrivateTextureFileStatus::ioFailure:
    return TextureHdPngPreparationIssueKind::fileIoFailure;
  case PrivateTextureFileStatus::ready:
    break;
  }
  return TextureHdPngPreparationIssueKind::preparationFailure;
}

[[nodiscard]] std::optional<TextureHdPngPreparationIssueKind> inspectHeader(
    const std::span<const std::uint8_t> bytes,
    const std::uint32_t expectedWidth, const std::uint32_t expectedHeight,
    const TextureHdPngPreparationLimits &limits, PngHeader &header) noexcept {
  if (bytes.size() < pngSignature.size() ||
      !std::equal(pngSignature.begin(), pngSignature.end(), bytes.begin())) {
    return TextureHdPngPreparationIssueKind::invalidPngSignature;
  }
  if (bytes.size() < pngIhdrBytes || readBigEndianU32(bytes, 8U) != 13U ||
      bytes[12U] != 'I' || bytes[13U] != 'H' || bytes[14U] != 'D' ||
      bytes[15U] != 'R') {
    return TextureHdPngPreparationIssueKind::invalidIhdr;
  }

  header.width = readBigEndianU32(bytes, 16U);
  header.height = readBigEndianU32(bytes, 20U);
  if (header.width == 0U || header.height == 0U ||
      header.width > limits.maximumDimension ||
      header.height > limits.maximumDimension) {
    return TextureHdPngPreparationIssueKind::dimensionLimitExceeded;
  }
  if (bytes[24U] != 8U || bytes[25U] != 6U || bytes[26U] != 0U ||
      bytes[27U] != 0U || bytes[28U] != 0U) {
    return TextureHdPngPreparationIssueKind::unsupportedPngFormat;
  }
  if (header.width != expectedWidth || header.height != expectedHeight) {
    return TextureHdPngPreparationIssueKind::dimensionMismatch;
  }
  return std::nullopt;
}

[[nodiscard]] bool alphaMatches(const std::span<const std::uint8_t> pixels,
                                const TextureHdAlphaUsage usage) noexcept {
  for (std::size_t offset = 3U; offset < pixels.size(); offset += 4U) {
    const auto alpha = pixels[offset];
    if (usage == TextureHdAlphaUsage::opaque && alpha != 0xFFU) {
      return false;
    }
    if (usage == TextureHdAlphaUsage::binary && alpha != 0U && alpha != 0xFFU) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<TextureHdPngPreparationIssueKind>
decodeLevel(const std::span<const std::uint8_t> encoded,
            const PngHeader &header, const TextureHdAlphaUsage alphaUsage,
            const std::size_t decodedLimit, PreparedTextureHdMipLevel &level) {
  std::uint64_t rowBytes = 0U;
  std::uint64_t decodedBytes = 0U;
  if (!checkedMultiply(header.width, 4U, rowBytes) ||
      !checkedMultiply(rowBytes, header.height, decodedBytes) ||
      decodedBytes > decodedLimit ||
      decodedBytes > std::numeric_limits<std::size_t>::max()) {
    return TextureHdPngPreparationIssueKind::decodedByteLimitExceeded;
  }

  std::vector<std::uint8_t> decoded;
  unsigned width = 0U;
  unsigned height = 0U;
  const auto error = lodepng::decode(decoded, width, height, encoded.data(),
                                     encoded.size(), LCT_RGBA, 8U);
  if (error != 0U) {
    return TextureHdPngPreparationIssueKind::decodeFailure;
  }
  if (width != header.width || height != header.height) {
    return TextureHdPngPreparationIssueKind::decodedLayoutMismatch;
  }

  level.width = header.width;
  level.height = header.height;
  level.bytesPerRow = rowBytes;
  if (decoded.size() != decodedBytes) {
    return TextureHdPngPreparationIssueKind::decodedLayoutMismatch;
  }
  level.rgba8 = std::move(decoded);
  if (!alphaMatches(level.rgba8, alphaUsage)) {
    level.rgba8.clear();
    return TextureHdPngPreparationIssueKind::alphaUsageMismatch;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
mipRelativePath(const std::string &directory,
                const TextureHdPngPreparationLimits &limits,
                const std::uint32_t level) {
  if (level > 99U || directory.empty()) {
    return std::nullopt;
  }
  std::array<char, 11U> filename{};
  const auto written =
      std::snprintf(filename.data(), filename.size(), "mip-%02u.png", level);
  if (written != 10) {
    return std::nullopt;
  }
  const std::size_t separatorBytes =
      directory.back() == '/' || directory.back() == '\\' ? 0U : 1U;
  if (directory.size() > limits.maximumRelativePathBytes ||
      separatorBytes + 10U >
          limits.maximumRelativePathBytes - directory.size()) {
    return std::nullopt;
  }
  std::string result = directory;
  if (separatorBytes != 0U) {
    result.push_back('/');
  }
  result.append(filename.data(), 10U);
  return result;
}

[[nodiscard]] std::uint32_t naturalMipCount(std::uint32_t width,
                                            std::uint32_t height) noexcept {
  std::uint32_t count = 1U;
  while (width != 1U || height != 1U) {
    ++count;
    width = std::max(1U, width >> 1U);
    height = std::max(1U, height >> 1U);
  }
  return count;
}

[[nodiscard]] bool
validLimits(const TextureHdPngPreparationLimits &limits) noexcept {
  return limits.maximumDimension > 0U && limits.maximumMipLevels > 0U &&
         limits.maximumMipLevels <= 99U &&
         limits.maximumRelativePathBytes > 0U &&
         limits.maximumEncodedBytesPerLevel > 0U &&
         limits.maximumEncodedChainBytes > 0U &&
         limits.maximumEncodedBytesInFlight > 0U &&
         limits.maximumDecodedRgbaBytes > 0U;
}

[[nodiscard]] bool validAlphaUsage(const TextureHdAlphaUsage usage) noexcept {
  switch (usage) {
  case TextureHdAlphaUsage::opaque:
  case TextureHdAlphaUsage::binary:
  case TextureHdAlphaUsage::translucent:
    return true;
  }
  return false;
}

} // namespace

TextureHdPngPreparation
prepareTextureHdPng(const TextureReplacementCandidate &candidate,
                    const PrivateTextureFileStore &files,
                    const TextureHdPngPreparationLimits &limits) noexcept {
  if (!validLimits(limits)) {
    return failure(TextureHdPngPreparationIssueKind::invalidLimits);
  }
  const auto *const record = candidate.record;
  if (record == nullptr || candidate.generation == 0U ||
      candidate.basePngBytes.empty() ||
      candidate.sourceGtiSha256 != record->sourceGtiSha256 ||
      !validAlphaUsage(record->alphaUsage) ||
      record->parameters.sampleSpace !=
          TextureHdSampleSpace::encodedUnclassified ||
      record->parameters.scale != 4U || record->source.width == 0U ||
      record->source.height == 0U ||
      record->source.width > std::numeric_limits<std::uint32_t>::max() / 4U ||
      record->source.height > std::numeric_limits<std::uint32_t>::max() / 4U ||
      record->result.width == 0U || record->result.height == 0U ||
      record->result.width != record->source.width * 4U ||
      record->result.height != record->source.height * 4U ||
      record->generatedMipCount == 0U ||
      record->generatedMipCount > limits.maximumMipLevels ||
      record->generatedMipCount !=
          naturalMipCount(record->result.width, record->result.height)) {
    return failure(TextureHdPngPreparationIssueKind::invalidCandidate);
  }
  if (record->result.width > limits.maximumDimension ||
      record->result.height > limits.maximumDimension) {
    return failure(TextureHdPngPreparationIssueKind::dimensionLimitExceeded);
  }
  if (candidate.basePngBytes.size() > limits.maximumEncodedBytesPerLevel ||
      candidate.basePngBytes.size() > limits.maximumEncodedChainBytes ||
      candidate.basePngBytes.size() > limits.maximumEncodedBytesInFlight) {
    return failure(TextureHdPngPreparationIssueKind::encodedByteLimitExceeded);
  }
  if (files.generation() != candidate.generation) {
    return failure(TextureHdPngPreparationIssueKind::staleGeneration);
  }

  try {
    if (crypto::sha256(candidate.basePngBytes) != record->outputPngSha256) {
      return failure(TextureHdPngPreparationIssueKind::invalidCandidate);
    }

    PreparedTextureAsset asset{
        .generation = candidate.generation,
        .alphaUsage = record->alphaUsage,
        .colorClassification =
            TextureHdColorClassification::technicalUnclassified,
        .encodedChainBytes = 0U,
        .decodedRgbaBytes = 0U,
        .levels = {},
    };
    asset.levels.reserve(record->generatedMipCount);

    auto expectedWidth = record->result.width;
    auto expectedHeight = record->result.height;
    for (std::uint32_t index = 0U; index < record->generatedMipCount; ++index) {
      const auto relativePath =
          mipRelativePath(record->mipmapDirectoryRelativePath, limits, index);
      if (!relativePath.has_value()) {
        return failure(TextureHdPngPreparationIssueKind::unsafeRelativePath,
                       index);
      }

      const auto read =
          files.readFile(*relativePath, limits.maximumEncodedBytesPerLevel,
                         candidate.generation);
      if (!read.success()) {
        return failure(mapFileFailure(read.status), index);
      }
      std::uint64_t nextEncodedChainBytes = 0U;
      std::uint64_t encodedInFlight = 0U;
      if (!checkedAdd(asset.encodedChainBytes, read.bytes.size(),
                      nextEncodedChainBytes) ||
          !checkedAdd(candidate.basePngBytes.size(), read.bytes.size(),
                      encodedInFlight)) {
        return failure(TextureHdPngPreparationIssueKind::integerOverflow,
                       index);
      }
      if (nextEncodedChainBytes > limits.maximumEncodedChainBytes ||
          encodedInFlight > limits.maximumEncodedBytesInFlight) {
        return failure(
            TextureHdPngPreparationIssueKind::encodedByteLimitExceeded, index);
      }
      asset.encodedChainBytes = nextEncodedChainBytes;

      if (index == 0U && read.bytes != candidate.basePngBytes) {
        return failure(TextureHdPngPreparationIssueKind::mipZeroMismatch,
                       index);
      }

      PngHeader header;
      if (const auto headerIssue = inspectHeader(
              read.bytes, expectedWidth, expectedHeight, limits, header);
          headerIssue.has_value()) {
        return failure(*headerIssue, index);
      }

      std::uint64_t expectedRowBytes = 0U;
      std::uint64_t expectedDecodedBytes = 0U;
      std::uint64_t totalDecodedBytes = 0U;
      if (!checkedMultiply(expectedWidth, 4U, expectedRowBytes) ||
          !checkedMultiply(expectedRowBytes, expectedHeight,
                           expectedDecodedBytes) ||
          !checkedAdd(asset.decodedRgbaBytes, expectedDecodedBytes,
                      totalDecodedBytes)) {
        return failure(TextureHdPngPreparationIssueKind::integerOverflow,
                       index);
      }
      if (totalDecodedBytes > limits.maximumDecodedRgbaBytes ||
          expectedDecodedBytes > std::numeric_limits<std::size_t>::max()) {
        return failure(
            TextureHdPngPreparationIssueKind::decodedByteLimitExceeded, index);
      }

      PreparedTextureHdMipLevel level{
          .level = index,
          .integrity = index == 0U
                           ? TextureHdMipIntegrity::authenticatedBase
                           : TextureHdMipIntegrity::structurallyValidated,
          .rgba8 = {},
      };
      if (const auto decodeIssue = decodeLevel(
              read.bytes, header, record->alphaUsage,
              static_cast<std::size_t>(expectedDecodedBytes), level);
          decodeIssue.has_value()) {
        return failure(*decodeIssue, index);
      }
      if (level.bytesPerRow != expectedRowBytes ||
          level.rgba8.size() != expectedDecodedBytes) {
        return failure(TextureHdPngPreparationIssueKind::decodedLayoutMismatch,
                       index);
      }
      asset.decodedRgbaBytes = totalDecodedBytes;
      asset.levels.push_back(std::move(level));
      expectedWidth = std::max(1U, expectedWidth >> 1U);
      expectedHeight = std::max(1U, expectedHeight >> 1U);
    }

    const auto unexpectedPath = mipRelativePath(
        record->mipmapDirectoryRelativePath, limits, record->generatedMipCount);
    if (!unexpectedPath.has_value()) {
      return failure(TextureHdPngPreparationIssueKind::unsafeRelativePath,
                     record->generatedMipCount);
    }
    const auto unexpected =
        files.readFile(*unexpectedPath, 1U, candidate.generation);
    if (unexpected.status != PrivateTextureFileStatus::notFound) {
      if (unexpected.success() ||
          unexpected.status == PrivateTextureFileStatus::sizeLimitExceeded) {
        return failure(TextureHdPngPreparationIssueKind::unexpectedNumberedMip,
                       record->generatedMipCount);
      }
      return failure(mapFileFailure(unexpected.status),
                     record->generatedMipCount);
    }

    return {
        .asset = std::move(asset),
        .issue = {},
    };
  } catch (...) {
    return failure(TextureHdPngPreparationIssueKind::preparationFailure);
  }
}

} // namespace airfix::texture
