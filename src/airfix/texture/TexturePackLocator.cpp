#include "airfix/texture/TexturePackLocator.hpp"

#include "airfix/crypto/Sha256.hpp"
#include "airfix/texture/PrivateTextureFileStoreInternal.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace airfix::texture {
namespace {

constexpr std::size_t headerBytes = 20U;
constexpr std::size_t checksumBytes = 32U;
constexpr std::size_t minimumRecordBytes = headerBytes + checksumBytes;
constexpr std::size_t maximumDirectoryNameBytes = 41U;

[[nodiscard]] std::uint16_t readU16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t readU32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void writeU16(const std::span<std::uint8_t> bytes,
              const std::size_t offset,
              const std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(const std::span<std::uint8_t> bytes,
              const std::size_t offset,
              const std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

[[noreturn]] void fail(const TexturePackLocatorCodecIssue issue,
                       const std::uint16_t schemaVersion,
                       const char* const message) {
    throw TexturePackLocatorCodecError(issue, schemaVersion, message);
}

[[nodiscard]] bool hasMagic(
    const std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= 4U && bytes[0] == 'A' && bytes[1] == 'F' &&
           bytes[2] == 'T' && bytes[3] == 'L';
}

[[nodiscard]] bool validUuidCharacter(const char value) noexcept {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f');
}

[[nodiscard]] bool validManifestRelativePath(
    const std::string_view path) noexcept {
    PrivateTextureFileStoreLimits limits;
    limits.maximumRelativePathBytes =
        maximumTexturePackManifestRelativePathBytes;
    return detail::parsePrivateTextureRelativePath(path, limits).valid();
}

} // namespace

TexturePackLocatorCodecError::TexturePackLocatorCodecError(
    const TexturePackLocatorCodecIssue issue,
    const std::uint16_t schemaVersion,
    const char* const message)
    : std::runtime_error(message), issue_(issue),
      schemaVersion_(schemaVersion) {}

bool validTexturePackDirectoryName(const std::string_view value) noexcept {
    if (value.size() != maximumDirectoryNameBytes ||
        !value.starts_with("pack-")) {
        return false;
    }
    const auto uuid = value.substr(5U);
    for (std::size_t index = 0U; index < uuid.size(); ++index) {
        const bool separator =
            index == 8U || index == 13U || index == 18U || index == 23U;
        if (separator ? uuid[index] != '-' : !validUuidCharacter(uuid[index])) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> encodeTexturePackLocator(
    const TexturePackLocatorRecord& record) {
    if (!validTexturePackDirectoryName(record.packageDirectoryName)) {
        fail(TexturePackLocatorCodecIssue::invalidDirectoryName,
             texturePackLocatorSchemaVersion,
             "texture package directory identity is invalid");
    }
    if (!validManifestRelativePath(record.manifestRelativePath)) {
        fail(TexturePackLocatorCodecIssue::invalidManifestRelativePath,
             texturePackLocatorSchemaVersion,
             "texture package manifest path is invalid");
    }
    const auto payloadBytes = record.packageDirectoryName.size() +
                              record.manifestRelativePath.size();
    if (payloadBytes > maximumTexturePackLocatorBytes - minimumRecordBytes ||
        record.packageDirectoryName.size() >
            std::numeric_limits<std::uint16_t>::max() ||
        record.manifestRelativePath.size() >
            std::numeric_limits<std::uint16_t>::max()) {
        fail(TexturePackLocatorCodecIssue::tooLarge,
             texturePackLocatorSchemaVersion,
             "texture package locator exceeds its byte limit");
    }
    const auto totalBytes = minimumRecordBytes + payloadBytes;
    std::vector<std::uint8_t> bytes(totalBytes, 0U);
    bytes[0] = 'A';
    bytes[1] = 'F';
    bytes[2] = 'T';
    bytes[3] = 'L';
    writeU16(bytes, 4U, texturePackLocatorSchemaVersion);
    writeU16(bytes, 6U, 0U);
    writeU32(bytes, 8U, static_cast<std::uint32_t>(totalBytes));
    writeU16(bytes, 12U,
             static_cast<std::uint16_t>(record.packageDirectoryName.size()));
    writeU16(bytes, 14U,
             static_cast<std::uint16_t>(record.manifestRelativePath.size()));
    std::copy(record.packageDirectoryName.begin(),
              record.packageDirectoryName.end(), bytes.begin() + headerBytes);
    const auto manifestOffset = headerBytes + record.packageDirectoryName.size();
    std::copy(record.manifestRelativePath.begin(),
              record.manifestRelativePath.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(manifestOffset));
    const auto checksumOffset = totalBytes - checksumBytes;
    const auto checksum = crypto::sha256(
        std::span<const std::uint8_t>(bytes).first(checksumOffset));
    std::copy(checksum.begin(), checksum.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(checksumOffset));
    return bytes;
}

DecodedTexturePackLocator decodeTexturePackLocator(
    const std::span<const std::uint8_t> bytes) {
    if (bytes.size() > maximumTexturePackLocatorBytes) {
        fail(TexturePackLocatorCodecIssue::tooLarge, 0U,
             "texture package locator exceeds its byte limit");
    }
    if (bytes.size() < minimumRecordBytes || !hasMagic(bytes)) {
        fail(TexturePackLocatorCodecIssue::malformed, 0U,
             "texture package locator framing is invalid");
    }
    const auto schemaVersion = readU16(bytes, 4U);
    if (schemaVersion == 0U) {
        fail(TexturePackLocatorCodecIssue::unsupportedPastSchema,
             schemaVersion,
             "texture package locator schema is unsupported");
    }
    if (readU32(bytes, 8U) != bytes.size()) {
        fail(TexturePackLocatorCodecIssue::malformed, schemaVersion,
             "texture package locator size is inconsistent");
    }
    if (schemaVersion > texturePackLocatorSchemaVersion) {
        return OpaqueFutureTexturePackLocatorRecord{
            .schemaVersion = schemaVersion,
            .exactBytes = std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
        };
    }
    const auto directoryBytes = static_cast<std::size_t>(readU16(bytes, 12U));
    const auto manifestBytes = static_cast<std::size_t>(readU16(bytes, 14U));
    if (readU16(bytes, 6U) != 0U || readU32(bytes, 8U) != bytes.size() ||
        readU32(bytes, 16U) != 0U || directoryBytes == 0U ||
        manifestBytes == 0U ||
        directoryBytes > maximumDirectoryNameBytes ||
        manifestBytes > maximumTexturePackManifestRelativePathBytes ||
        directoryBytes + manifestBytes != bytes.size() - minimumRecordBytes) {
        fail(TexturePackLocatorCodecIssue::malformed, schemaVersion,
             "texture package locator fields are inconsistent");
    }
    const auto checksumOffset = bytes.size() - checksumBytes;
    const auto expected = crypto::sha256(bytes.first(checksumOffset));
    if (!std::equal(expected.begin(), expected.end(),
                    bytes.begin() +
                        static_cast<std::ptrdiff_t>(checksumOffset))) {
        fail(TexturePackLocatorCodecIssue::checksumMismatch, schemaVersion,
             "texture package locator checksum does not match");
    }

    const auto directoryBegin = bytes.begin() + headerBytes;
    TexturePackLocatorRecord record{
        .packageDirectoryName = std::string(
            directoryBegin,
            directoryBegin + static_cast<std::ptrdiff_t>(directoryBytes)),
        .manifestRelativePath = std::string(
            directoryBegin + static_cast<std::ptrdiff_t>(directoryBytes),
            bytes.begin() + static_cast<std::ptrdiff_t>(checksumOffset)),
    };
    if (!validTexturePackDirectoryName(record.packageDirectoryName)) {
        fail(TexturePackLocatorCodecIssue::invalidDirectoryName, schemaVersion,
             "texture package directory identity is invalid");
    }
    if (!validManifestRelativePath(record.manifestRelativePath)) {
        fail(TexturePackLocatorCodecIssue::invalidManifestRelativePath,
             schemaVersion, "texture package manifest path is invalid");
    }
    return record;
}

} // namespace airfix::texture
