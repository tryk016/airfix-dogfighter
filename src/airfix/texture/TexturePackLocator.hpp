#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace airfix::texture {

inline constexpr std::uint16_t texturePackLocatorSchemaVersion = 1U;
inline constexpr std::size_t maximumTexturePackLocatorBytes = 8U * 1024U;
inline constexpr std::size_t maximumTexturePackManifestRelativePathBytes =
    4U * 1024U;

struct TexturePackLocatorRecord final {
    std::string packageDirectoryName;
    std::string manifestRelativePath;

    [[nodiscard]] friend bool operator==(
        const TexturePackLocatorRecord&,
        const TexturePackLocatorRecord&) = default;
};

struct OpaqueFutureTexturePackLocatorRecord final {
    std::uint16_t schemaVersion{};
    std::vector<std::uint8_t> exactBytes;
};

using DecodedTexturePackLocator =
    std::variant<TexturePackLocatorRecord,
                 OpaqueFutureTexturePackLocatorRecord>;

enum class TexturePackLocatorCodecIssue : std::uint8_t {
    tooLarge,
    malformed,
    unsupportedPastSchema,
    invalidDirectoryName,
    invalidManifestRelativePath,
    checksumMismatch,
};

class TexturePackLocatorCodecError final : public std::runtime_error {
  public:
    TexturePackLocatorCodecError(TexturePackLocatorCodecIssue issue,
                                 std::uint16_t schemaVersion,
                                 const char* message);

    [[nodiscard]] TexturePackLocatorCodecIssue issue() const noexcept {
        return issue_;
    }

    [[nodiscard]] std::uint16_t schemaVersion() const noexcept {
        return schemaVersion_;
    }

  private:
    TexturePackLocatorCodecIssue issue_;
    std::uint16_t schemaVersion_;
};

// Product-generated immutable package directory identity. It is deliberately
// narrower than a generic path so cleanup and recovery never claim an entry
// that was not created by the iOS importer.
[[nodiscard]] bool validTexturePackDirectoryName(
    std::string_view value) noexcept;

// Canonical AFTL v1 stores only an opaque package-directory identity and a
// root-relative manifest path. It never stores the configured source root,
// logical GTI paths, checksums from the private manifest, or host paths.
[[nodiscard]] std::vector<std::uint8_t> encodeTexturePackLocator(
    const TexturePackLocatorRecord& record);

// A well-framed future schema is retained byte-for-byte so an older build
// cannot silently downgrade owner content configuration.
[[nodiscard]] DecodedTexturePackLocator decodeTexturePackLocator(
    std::span<const std::uint8_t> bytes);

} // namespace airfix::texture
