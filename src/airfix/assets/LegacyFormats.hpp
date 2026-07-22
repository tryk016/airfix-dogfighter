#pragma once

#include "airfix/assets/AssetPrimitives.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace airfix::assets {

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

struct CcfMaterialMetadata {
    std::string name;
    std::string prefix;
    std::uint32_t reference{};
    std::optional<std::string> primaryTexture;
    std::optional<std::string> secondaryTexture;
    std::optional<std::string> environmentTexture;
    std::uint64_t offset{};
};

using CcfVector3 = std::array<float, 3>;

struct CcfMeshVertexMetadata {
    CcfVector3 position{};
    std::optional<CcfVector3> optionalVector;
    std::optional<CcfVector3> loaderVector;
    std::optional<std::uint32_t> value4500;
    std::uint64_t offset{};
};

struct CcfMeshPaintMetadata {
    std::uint32_t type{};
    std::vector<CcfVector3> colors;
};

struct CcfMeshTriangleMetadata {
    std::array<std::uint32_t, 3> vertexIndices{};
    std::uint32_t materialReference{};
    std::optional<std::array<float, 6>> textureCoordinates;
    std::optional<CcfMeshPaintMetadata> paint;
    std::uint64_t offset{};
};

struct CcfMeshRangeMetadata {
    std::uint32_t enabled{};
    float first{};
    float second{};
};

struct CcfMeshMetadata {
    std::string name;
    std::string prefix;
    std::uint32_t reference{};
    std::uint8_t selectionFlagA{};
    std::uint8_t selectionFlagB{};
    std::uint32_t linkReference{};
    CcfVector3 position{};
    float scalar{};
    std::array<CcfVector3, 3> orientation{};
    std::vector<CcfMeshVertexMetadata> vertices;
    std::vector<CcfMeshTriangleMetadata> triangles;
    std::optional<std::uint32_t> vampireMode;
    std::optional<std::uint32_t> value4501;
    std::optional<std::uint8_t> propertyF0B2;
    std::optional<CcfMeshRangeMetadata> range;
    std::uint64_t offset{};
};

struct CcfMetadata {
    std::uint16_t rootId{};
    std::uint32_t rootSize{};
    std::vector<CcfChunk> topLevelChunks;
    std::vector<CcfMaterialMetadata> materials;
    std::vector<CcfMeshMetadata> meshes;
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
