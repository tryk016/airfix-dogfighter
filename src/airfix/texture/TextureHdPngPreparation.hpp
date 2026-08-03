#pragma once

#include "airfix/texture/PrivateTextureFileStore.hpp"
#include "airfix/texture/TextureHdManifestIndex.hpp"
#include "airfix/texture/TextureReplacementResolver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::texture {

enum class TextureHdColorClassification : std::uint8_t {
  colorSrgb,
  colorLinear,
  normal,
  mask,
  technicalUnclassified,
};

enum class TextureHdMipIntegrity : std::uint8_t {
  authenticatedBase,
  structurallyValidated,
};

struct PreparedTextureHdMipLevel final {
  std::uint32_t level{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t bytesPerRow{};
  TextureHdMipIntegrity integrity{TextureHdMipIntegrity::structurallyValidated};
  std::vector<std::uint8_t> rgba8;
};

struct PreparedTextureAsset final {
  std::uint64_t generation{};
  TextureHdAlphaUsage alphaUsage{TextureHdAlphaUsage::opaque};
  TextureHdColorClassification colorClassification{
      TextureHdColorClassification::technicalUnclassified};
  std::uint64_t encodedChainBytes{};
  std::uint64_t decodedRgbaBytes{};
  std::vector<PreparedTextureHdMipLevel> levels;
};

enum class TextureHdPngPreparationIssueKind : std::uint8_t {
  none,
  invalidCandidate,
  invalidLimits,
  staleGeneration,
  unsafeRelativePath,
  fileNotFound,
  unsafeIndirection,
  unsafeFileType,
  multipleLinks,
  encodedByteLimitExceeded,
  changedDuringRead,
  fileIoFailure,
  invalidPngSignature,
  invalidIhdr,
  unsupportedPngFormat,
  dimensionLimitExceeded,
  dimensionMismatch,
  mipZeroMismatch,
  unexpectedNumberedMip,
  decodedByteLimitExceeded,
  integerOverflow,
  decodeFailure,
  decodedLayoutMismatch,
  alphaUsageMismatch,
  preparationFailure,
};

struct TextureHdPngPreparationIssue final {
  TextureHdPngPreparationIssueKind kind{TextureHdPngPreparationIssueKind::none};
  // A level number is safe diagnostic metadata. No path, checksum, decoder
  // message, or private manifest value is retained.
  std::optional<std::uint32_t> level;
};

struct TextureHdPngPreparationLimits final {
  std::uint32_t maximumDimension{16'384U};
  std::uint32_t maximumMipLevels{32U};
  std::size_t maximumRelativePathBytes{4'096U};
  std::size_t maximumEncodedBytesPerLevel{std::size_t{64U} * 1024U * 1024U};
  std::size_t maximumEncodedChainBytes{std::size_t{256U} * 1024U * 1024U};
  std::size_t maximumEncodedBytesInFlight{std::size_t{128U} * 1024U * 1024U};
  std::size_t maximumDecodedRgbaBytes{std::size_t{512U} * 1024U * 1024U};
};

struct TextureHdPngPreparation final {
  std::optional<PreparedTextureAsset> asset;
  TextureHdPngPreparationIssue issue;

  [[nodiscard]] bool success() const noexcept {
    return asset.has_value() &&
           issue.kind == TextureHdPngPreparationIssueKind::none;
  }
};

// Prepares one complete reviewed HD PNG chain without native GPU work. The
// candidate's authenticated base is checked again, mip-00 must be byte-equal
// to it, every declared mip must be an 8-bit RGBA non-interlaced PNG with the
// exact natural dimensions, and the first undeclared numbered mip must be
// absent. Non-zero mip content is structurally validated but cannot be called
// cryptographically authenticated until a future manifest carries its digest.
// Any failure returns one fixed, path-free issue and no partial asset.
[[nodiscard]] TextureHdPngPreparation
prepareTextureHdPng(const TextureReplacementCandidate &candidate,
                    const PrivateTextureFileStore &files,
                    const TextureHdPngPreparationLimits &limits = {}) noexcept;

} // namespace airfix::texture
