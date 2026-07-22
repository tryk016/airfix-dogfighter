#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace airfix::assets {

class ParseError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::uint32_t fourCC(
    const char a,
    const char b,
    const char c,
    const char d) noexcept {
    return static_cast<std::uint8_t>(a) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
}

inline constexpr std::uint32_t kGtiMagic = fourCC('G', 't', 'I', 'm');
inline constexpr std::uint32_t kGtiVersion = 4U;
inline constexpr std::uint32_t kGtiChecksumChunk = fourCC('C', 'R', 'C', '0');
inline constexpr std::uint32_t kGtiImageChunk = fourCC('I', 'm', 'a', 'g');
inline constexpr std::uint32_t kCcfMagic = fourCC('C', 'c', 'F', 'f');
inline constexpr std::uint32_t kCcfVersion = 0x98092901U;

struct GtiChunk {
    std::uint32_t id{};
    std::uint32_t payloadSize{};
    std::uint64_t payloadOffset{};
};

struct GtiVariant {
    std::uint32_t format{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t paletteEntries{};
    std::uint32_t mipmapLevels{};
    std::uint64_t pixelDataOffset{};
    std::uint32_t pixelDataSize{};
    std::uint64_t expectedPixelDataSize{};
    std::uint64_t trailingBytes{};
};

struct GtiMetadata {
    std::optional<std::uint32_t> checksum;
    std::size_t checksumChunkCount{};
    bool terminalDeclaredOverrun{};
    std::vector<GtiChunk> chunks;
    std::vector<GtiVariant> variants;
};

struct CcfChunk {
    std::uint16_t id{};
    std::uint32_t totalSize{};
    std::uint64_t offset{};
    std::vector<CcfChunk> directChildren;
};

struct CcfMetadata {
    std::uint16_t rootId{};
    std::uint32_t rootSize{};
    std::vector<CcfChunk> topLevelChunks;
};

struct RgbaImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

[[nodiscard]] GtiMetadata parseGti(std::span<const std::uint8_t> bytes);
[[nodiscard]] CcfMetadata parseCcf(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::uint32_t gtiBitsPerPixel(std::uint32_t format) noexcept;
[[nodiscard]] std::uint64_t expectedGtiPixelBytes(
    std::uint32_t format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mipmapLevels);
[[nodiscard]] RgbaImage decodeGtiBaseRgba(
    std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    std::size_t outputLimit);

} // namespace airfix::assets
