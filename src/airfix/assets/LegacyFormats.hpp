#pragma once

#include "airfix/assets/AssetPrimitives.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
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

class GtiParseLimitError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct GtiParseLimits {
    std::size_t maximumVariants{4'096U};
    // Counts retained GtiChunk and GtiVariant records. The distinct-format
    // set is independently bounded by maximumVariants.
    std::size_t maximumMetadataBytes{16U * 1024U * 1024U};
};

struct CcfChunk {
    std::uint16_t id{};
    std::uint32_t totalSize{};
    std::uint64_t offset{};
    std::vector<CcfChunk> directChildren;
};

using CcfVector3 = std::array<float, 3>;

struct CcfFogMetadata {
    std::uint32_t enabledRaw{};
    float first{};
    float second{};
    CcfVector3 color{};
    std::uint64_t offset{};
};

enum class CcfBspTreeKind : std::uint8_t {
    staticTree,
    portalTree,
};

enum class CcfBspTreeSource : std::uint8_t {
    direct,
    wrapped,
};

struct CcfBspPolygonMetadata {
    CcfVector3 faceCross{};
    CcfVector3 faceNormal{};
    CcfVector3 point0{};
    CcfVector3 edge01{};
    CcfVector3 edge12{};
    std::uint32_t polygonIndex{};
    std::uint32_t placedObjectReference{};
    std::uint64_t offset{};
};

struct CcfBspNodeMetadata {
    std::uint32_t childAPresenceRaw{};
    std::uint32_t childBPresenceRaw{};
    std::optional<std::size_t> childAIndex;
    std::optional<std::size_t> childBIndex;
    CcfVector3 splitNormal{};
    CcfVector3 pointOnPlane{};
    // Indices into CcfBspTreeMetadata::polygons, in physical F0C1 order.
    std::vector<std::size_t> polygonIndices;
    // Includes known polygons and unknown trailing descriptors. Nested child
    // nodes and the two fixed plane vectors are represented by typed fields.
    std::vector<CcfChunk> trailingChildren;
    std::uint64_t offset{};
};

struct CcfBspTreeMetadata {
    CcfBspTreeKind kind{CcfBspTreeKind::staticTree};
    CcfBspTreeSource source{CcfBspTreeSource::direct};
    std::size_t rootNodeIndex{};
    // Flat physical/preorder node arena; child links are indices into it.
    std::vector<CcfBspNodeMetadata> nodes;
    // Flat polygon arena shared by every node in this tree.
    std::vector<CcfBspPolygonMetadata> polygons;
    std::uint64_t offset{};
};

struct CcfRoomMetadata {
    std::string name;
    std::string prefix;
    std::uint32_t reference{};
    // The first direct 0x1100 in its containing 0x1000 section binds the
    // receiver/root room instead of allocating an ordinary child room.
    bool primaryBinding{};
    std::optional<CcfFogMetadata> fog;
    std::vector<CcfBspTreeMetadata> staticBspTrees;
    std::vector<CcfBspTreeMetadata> portalBspTrees;
    std::vector<CcfChunk> directChildren;
    std::uint64_t offset{};
};

struct CcfRoomSectionMetadata {
    std::size_t firstPhysicalRoomIndex{};
    std::size_t physicalRoomCount{};
    // The legacy loader binds root only when the section's first direct child
    // is 0x1100. A later first room is an ordinary named room.
    bool firstDirectChildIsRoom{};
    std::uint64_t offset{};
};

struct CcfMaterialMetadata {
    std::string name;
    std::string prefix;
    std::uint32_t reference{};
    std::optional<std::string> primaryTexture;
    std::optional<std::string> secondaryTexture;
    std::optional<std::string> environmentTexture;
    // First u32 carried by child 0x2152. Native collision paths read this
    // value at CcMaterial + 0x5c; individual numeric meanings remain raw.
    std::optional<std::uint32_t> collisionMode2152;
    std::uint64_t offset{};
};

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

enum class CcfBlueprintKind : std::uint8_t {
    mesh,
    nullNode,
    light,
};

struct CcfSrtMetadata {
    // Transform authored in the 0x3000 blueprint record. Parent attachment may
    // later derive a local transform from this stored/world-space value.
    CcfVector3 position{};
    float rawScalar{};
    std::array<CcfVector3, 3> orientation{};
};

struct CcfBlueprintMetadata {
    CcfBlueprintKind kind{CcfBlueprintKind::mesh};
    std::string name;
    std::string prefix;
    std::uint32_t reference{};
    std::optional<std::uint32_t> auxiliaryReference;
    std::uint32_t parentReference{};
    CcfSrtMetadata authoredTransform;
    std::optional<std::size_t> meshIndex;
    std::uint64_t offset{};
};

enum class CcfPlacedNodeKind : std::uint8_t {
    object,
    nullNode,
    light,
};

using CcfPlacedOrientation = std::variant<
    std::array<CcfVector3, 3>,
    CcfVector3>;

struct CcfPlacedSrtMetadata {
    CcfVector3 position{};
    float rawScalar{};
    // Most records contain an F050 matrix. The original loader also accepts
    // a single F040 vector, retained as the second variant without assigning
    // higher-level rotation semantics to it.
    CcfPlacedOrientation orientation{std::array<CcfVector3, 3>{}};
};

struct CcfOpaqueRange {
    std::uint64_t offset{};
    std::uint32_t length{};
};

struct CcfPlacedObjectMetadata {
    std::uint32_t meshReference{};
    std::uint8_t rawFlag{};
    std::uint32_t portalType{};
    std::uint32_t portalRoomReference{};
    // Boolean lighting-classification gate shared with placed lights; the
    // stable user-facing name remains unproven.
    std::optional<std::uint32_t> propertyF0B0;
    // Boolean dynamic-BSP enable/eligibility gate.
    std::optional<std::uint32_t> propertyF0B1;
    // Serialized 1-based limb-count/ordinal bound, not a parent reference.
    std::optional<std::uint32_t> value4501;
    std::optional<CcfChunk> bsp4101;
};

struct CcfPlacedNullMetadata {
    std::optional<CcfOpaqueRange> block4210;
    // Serialized limb index/ID, not a parent reference.
    std::optional<std::uint32_t> value4500;
};

struct CcfPlacedLight4310Metadata {
    float first{};
    CcfVector3 vector{};
    float second{};
    float third{};
    std::optional<std::string> texture;
};

struct CcfPlacedLight4320Metadata {
    std::array<float, 4> values{};
    std::optional<std::array<std::string, 2>> textures;
};

struct CcfPlacedLight4330Metadata {
    CcfVector3 vector{};
    float first{};
    float second{};
};

struct CcfPlacedLightMetadata {
    std::optional<CcfPlacedLight4310Metadata> property4310;
    std::optional<CcfPlacedLight4320Metadata> property4320;
    std::optional<CcfPlacedLight4330Metadata> property4330;
    // Boolean lighting-classification gate shared with placed objects.
    std::optional<std::uint32_t> propertyF0B0;
};

using CcfPlacedNodeData = std::variant<
    CcfPlacedObjectMetadata,
    CcfPlacedNullMetadata,
    CcfPlacedLightMetadata>;

struct CcfPlacedNodeMetadata {
    CcfPlacedNodeKind kind{CcfPlacedNodeKind::object};
    std::string name;
    std::string prefix;
    std::uint32_t currentReference{};
    std::uint32_t roomReference{};
    std::uint32_t parentReference{};
    CcfPlacedSrtMetadata transform;
    std::vector<CcfChunk> directChildren;
    CcfPlacedNodeData data{CcfPlacedObjectMetadata{}};
    std::uint64_t offset{};
};

struct CcfMetadata {
    std::uint16_t rootId{};
    std::uint32_t rootSize{};
    std::vector<CcfChunk> topLevelChunks;
    // Physical top-level 0x1000 order. Ranges index the flattened rooms vector.
    std::vector<CcfRoomSectionMetadata> roomSections;
    std::vector<CcfRoomMetadata> rooms;
    std::vector<CcfMaterialMetadata> materials;
    std::vector<CcfMeshMetadata> meshes;
    std::vector<CcfBlueprintMetadata> blueprints;
    // Physical order from the independent 0x4000 placed-scene section.
    std::vector<CcfPlacedNodeMetadata> placedNodes;
};

struct RgbaImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

struct GtiMipLevelLayout {
    std::uint32_t level{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sourceOffset{};
    std::uint64_t sourceSize{};
    std::uint64_t requiredTexelBytes{};
    bool exactTexelLayout{};
};

struct RgbaMipChain {
    std::vector<RgbaImage> levels;
};

[[nodiscard]] GtiMetadata parseGti(
    std::span<const std::uint8_t> bytes,
    const GtiParseLimits& limits = {});
[[nodiscard]] CcfMetadata parseCcf(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::uint32_t gtiBitsPerPixel(std::uint32_t format) noexcept;
[[nodiscard]] std::uint64_t expectedGtiPixelBytes(
    std::uint32_t format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mipmapLevels);
[[nodiscard]] std::vector<GtiMipLevelLayout> describeGtiMipLevels(
    const GtiVariant& variant);
[[nodiscard]] RgbaImage decodeGtiMipLevelRgba(
    std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    std::uint32_t level,
    std::size_t outputLimit);
[[nodiscard]] RgbaMipChain decodeGtiMipChainRgba(
    std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    std::size_t outputLimit);
[[nodiscard]] RgbaImage decodeGtiBaseRgba(
    std::span<const std::uint8_t> bytes,
    const GtiVariant& variant,
    std::size_t outputLimit);

} // namespace airfix::assets
