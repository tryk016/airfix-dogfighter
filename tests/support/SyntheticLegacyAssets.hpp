#pragma once

#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/LegacyFormats.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace airfix::testing {

using LegacyAssetBytes = std::vector<std::uint8_t>;

inline constexpr std::string_view kSyntheticWorldLogicalPath =
    "Game/Worlds/Test.world";
inline constexpr std::string_view kSyntheticCcfLogicalPath =
    "Graphics/World.ccf";
inline constexpr std::string_view kSyntheticAlternateCcfLogicalPath =
    "Graphics/Alternate.ccf";
inline constexpr std::string_view kSyntheticTextureRoot =
    "Graphics/Textures";
inline constexpr std::string_view kSyntheticWallGtiLogicalPath =
    "Graphics/Textures/Wall.gti";
inline constexpr std::string_view kSyntheticDetailGtiLogicalPath =
    "Graphics/Textures/Detail.gti";

inline constexpr std::array<std::uint8_t, 16U> kSyntheticWallRgba{{
    0xFFU, 0x00U, 0x00U, 0xFFU,
    0x00U, 0xFFU, 0x00U, 0xFFU,
    0x00U, 0x00U, 0xFFU, 0xFFU,
    0xFFU, 0xFFU, 0xFFU, 0x80U,
}};

inline constexpr std::array<std::uint8_t, 16U> kSyntheticDetailRgba{{
    0x10U, 0x20U, 0x30U, 0x40U,
    0x50U, 0x60U, 0x70U, 0x80U,
    0x90U, 0xA0U, 0xB0U, 0xC0U,
    0xD0U, 0xE0U, 0xF0U, 0xFFU,
}};

struct SyntheticLegacyCcfOptions {
    std::string primaryTexture{"Wall"};
    std::optional<std::string> secondaryTexture;
    std::array<float, 3U> placedTranslation{4.0F, 5.0F, 6.0F};
};

struct SyntheticLegacyAssetOptions {
    std::string ccfLogicalPath{std::string(kSyntheticCcfLogicalPath)};
    std::string textureRoot{std::string(kSyntheticTextureRoot)};
    bool includeSecondaryTexture{};
};

struct SyntheticLegacyAssets {
    LegacyAssetBytes world;
    LegacyAssetBytes ccf;
    LegacyAssetBytes wallGti;
    std::optional<LegacyAssetBytes> detailGti;
};

namespace legacy_detail {

inline void appendU16(
    LegacyAssetBytes& bytes,
    const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

inline void appendU32(
    LegacyAssetBytes& bytes,
    const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < sizeof(value); ++byte) {
        bytes.push_back(static_cast<std::uint8_t>(
            value >> static_cast<unsigned>(byte * 8U)));
    }
}

inline void appendFloat(
    LegacyAssetBytes& bytes,
    const float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

inline void appendBytes(
    LegacyAssetBytes& destination,
    const std::span<const std::uint8_t> source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

[[nodiscard]] inline LegacyAssetBytes ccfChunk(
    const std::uint16_t id,
    const std::span<const std::uint8_t> payload = {}) {
    LegacyAssetBytes bytes;
    appendU16(bytes, id);
    if (payload.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max() - 6U)) {
        throw std::runtime_error("synthetic CCF chunk exceeds its 32-bit size");
    }
    appendU32(bytes, static_cast<std::uint32_t>(6U + payload.size()));
    appendBytes(bytes, payload);
    return bytes;
}

[[nodiscard]] inline LegacyAssetBytes ccfVector3(
    const std::uint16_t id,
    const float x,
    const float y,
    const float z) {
    LegacyAssetBytes payload;
    appendFloat(payload, x);
    appendFloat(payload, y);
    appendFloat(payload, z);
    return ccfChunk(id, payload);
}

[[nodiscard]] inline LegacyAssetBytes ccfString(
    const std::string_view value) {
    if (value.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("synthetic CCF string exceeds its 32-bit size");
    }
    LegacyAssetBytes payload;
    appendU32(payload, static_cast<std::uint32_t>(value.size() + 1U));
    payload.insert(payload.end(), value.begin(), value.end());
    payload.push_back(0U);
    return ccfChunk(0xF020U, payload);
}

[[nodiscard]] inline LegacyAssetBytes ccfName(
    const std::string_view name,
    const std::string_view prefix) {
    auto payload = ccfString(name);
    const auto prefixChunk = ccfString(prefix);
    appendBytes(payload, prefixChunk);
    return ccfChunk(0xF010U, payload);
}

[[nodiscard]] inline LegacyAssetBytes ccfOrientation() {
    auto matrix = ccfVector3(0xF040U, 1.0F, 0.0F, 0.0F);
    const auto y = ccfVector3(0xF040U, 0.0F, 1.0F, 0.0F);
    const auto z = ccfVector3(0xF040U, 0.0F, 0.0F, 1.0F);
    appendBytes(matrix, y);
    appendBytes(matrix, z);
    return ccfChunk(0xF070U, ccfChunk(0xF050U, matrix));
}

[[nodiscard]] inline LegacyAssetBytes material(
    const SyntheticLegacyCcfOptions& options) {
    auto payload = ccfName("Material", "Synthetic");
    appendU32(payload, 42U);
    appendBytes(payload, ccfChunk(0x2110U, ccfString(options.primaryTexture)));
    if (options.secondaryTexture.has_value()) {
        appendBytes(
            payload,
            ccfChunk(0x2111U, ccfString(*options.secondaryTexture)));
    }
    return ccfChunk(0x2100U, payload);
}

[[nodiscard]] inline LegacyAssetBytes meshVertex(
    const float x,
    const float y,
    const float z) {
    auto payload = ccfVector3(0xF040U, x, y, z);
    LegacyAssetBytes zero;
    appendU32(zero, 0U);
    appendBytes(payload, ccfChunk(0x4500U, zero));
    return ccfChunk(0x3110U, payload);
}

[[nodiscard]] inline LegacyAssetBytes mesh() {
    auto payload = ccfName("Mesh", "Synthetic");
    appendU32(payload, 7U);
    payload.push_back(1U);
    payload.push_back(0U);
    appendU32(payload, 0U);
    appendBytes(payload, ccfVector3(0xF040U, 0.0F, 0.0F, 0.0F));
    appendFloat(payload, 1.0F);
    appendBytes(payload, ccfOrientation());
    appendBytes(payload, meshVertex(0.0F, 0.0F, 0.0F));
    appendBytes(payload, meshVertex(1.0F, 0.0F, 0.0F));
    appendBytes(payload, meshVertex(0.0F, 1.0F, 0.0F));

    LegacyAssetBytes triangle;
    appendU32(triangle, 0U);
    appendU32(triangle, 1U);
    appendU32(triangle, 2U);
    appendU32(triangle, 42U);
    LegacyAssetBytes textureCoordinates;
    for (const auto coordinate :
         {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}) {
        appendFloat(textureCoordinates, coordinate);
    }
    appendBytes(triangle, ccfChunk(0xF060U, textureCoordinates));
    appendBytes(payload, ccfChunk(0x3120U, triangle));
    return ccfChunk(0x3100U, payload);
}

[[nodiscard]] inline LegacyAssetBytes room(
    const std::string_view name,
    const std::uint32_t reference) {
    auto payload = ccfName(name, "Synthetic");
    appendU32(payload, reference);
    return ccfChunk(0x1100U, payload);
}

[[nodiscard]] inline LegacyAssetBytes placedObject(
    const SyntheticLegacyCcfOptions& options) {
    auto payload = ccfName("PlacedMesh", "Synthetic");
    appendU32(payload, 100U);
    appendU32(payload, 7U);
    appendU32(payload, 20U);
    appendU32(payload, 0U);
    payload.push_back(0U);
    appendU32(payload, 0U);
    appendU32(payload, 0U);
    appendBytes(
        payload,
        ccfVector3(
            0xF040U,
            options.placedTranslation[0],
            options.placedTranslation[1],
            options.placedTranslation[2]));
    appendFloat(payload, 1.0F);
    appendBytes(payload, ccfOrientation());
    return ccfChunk(0x4100U, payload);
}

inline void appendAfChunk(
    LegacyAssetBytes& bytes,
    const std::uint32_t id,
    const std::string_view value) {
    appendU32(bytes, id);
    if (value.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("synthetic World string exceeds its 32-bit size");
    }
    appendU32(bytes, static_cast<std::uint32_t>(value.size() + 1U));
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0U);
}

inline void validateCcf(
    const LegacyAssetBytes& bytes,
    const SyntheticLegacyCcfOptions& options) {
    const auto parsed = assets::parseCcf(bytes);
    if (parsed.rooms.size() != 2U ||
        !parsed.rooms[0].primaryBinding ||
        parsed.rooms[0].reference != 10U ||
        parsed.rooms[1].reference != 20U ||
        parsed.materials.size() != 1U ||
        parsed.materials[0].reference != 42U ||
        parsed.materials[0].primaryTexture !=
            std::optional<std::string>{options.primaryTexture} ||
        parsed.materials[0].secondaryTexture != options.secondaryTexture ||
        parsed.meshes.size() != 1U ||
        parsed.meshes[0].reference != 7U ||
        parsed.meshes[0].vertices.size() != 3U ||
        parsed.meshes[0].triangles.size() != 1U ||
        parsed.placedNodes.size() != 1U ||
        parsed.placedNodes[0].roomReference != 20U) {
        throw std::runtime_error(
            "synthetic CCF failed its semantic self-check");
    }
}

} // namespace legacy_detail

[[nodiscard]] inline LegacyAssetBytes makeSyntheticWorld(
    const std::string_view ccfLogicalPath = kSyntheticCcfLogicalPath,
    const std::string_view textureRoot = kSyntheticTextureRoot) {
    LegacyAssetBytes chunks;
    legacy_detail::appendAfChunk(
        chunks, assets::fourCC('T', 'E', 'X', 'U'), textureRoot);
    legacy_detail::appendAfChunk(
        chunks, assets::fourCC('C', 'C', 'F', 'F'), ccfLogicalPath);

    LegacyAssetBytes bytes;
    legacy_detail::appendU32(bytes, assets::kAfHouseRoot);
    legacy_detail::appendU32(
        bytes, static_cast<std::uint32_t>(chunks.size()));
    legacy_detail::appendBytes(bytes, chunks);

    const auto parsed = assets::parseWorldDefinition(bytes);
    if (parsed.textureRoot != std::optional<std::string>{textureRoot} ||
        parsed.ccfPath != std::optional<std::string>{ccfLogicalPath}) {
        throw std::runtime_error(
            "synthetic World failed its semantic self-check");
    }
    return bytes;
}

[[nodiscard]] inline LegacyAssetBytes makeSyntheticLegacyCcf(
    const SyntheticLegacyCcfOptions& options = {}) {
    auto rooms = legacy_detail::room("Receiver", 10U);
    const auto ordinaryRoom = legacy_detail::room("Room", 20U);
    legacy_detail::appendBytes(rooms, ordinaryRoom);

    LegacyAssetBytes sections =
        legacy_detail::ccfChunk(0x1000U, rooms);
    legacy_detail::appendBytes(
        sections,
        legacy_detail::ccfChunk(
            0x2000U, legacy_detail::material(options)));
    legacy_detail::appendBytes(
        sections,
        legacy_detail::ccfChunk(0x3000U, legacy_detail::mesh()));
    legacy_detail::appendBytes(
        sections,
        legacy_detail::ccfChunk(
            0x4000U, legacy_detail::placedObject(options)));

    LegacyAssetBytes bytes;
    legacy_detail::appendU32(bytes, assets::kCcfMagic);
    legacy_detail::appendU32(bytes, assets::kCcfVersion);
    legacy_detail::appendU16(bytes, 1U);
    legacy_detail::appendU32(
        bytes, static_cast<std::uint32_t>(6U + sections.size()));
    legacy_detail::appendBytes(bytes, sections);
    legacy_detail::validateCcf(bytes, options);
    return bytes;
}

[[nodiscard]] inline LegacyAssetBytes makeSyntheticRgba8Gti(
    const std::span<const std::uint8_t, 16U> rgba = kSyntheticWallRgba,
    const std::uint32_t checksum = 0x12345678U) {
    LegacyAssetBytes bytes;
    legacy_detail::appendU32(bytes, assets::kGtiMagic);
    legacy_detail::appendU32(bytes, assets::kGtiVersion);
    legacy_detail::appendU32(bytes, assets::kGtiChecksumChunk);
    legacy_detail::appendU32(bytes, 4U);
    legacy_detail::appendU32(bytes, checksum);
    legacy_detail::appendU32(bytes, assets::kGtiImageChunk);
    legacy_detail::appendU32(bytes, 36U);
    legacy_detail::appendU32(bytes, 8U);
    legacy_detail::appendU32(bytes, 2U);
    legacy_detail::appendU32(bytes, 2U);
    legacy_detail::appendU32(bytes, 0U);
    legacy_detail::appendU32(bytes, 1U);
    for (std::size_t pixel = 0U; pixel < 4U; ++pixel) {
        const auto offset = pixel * 4U;
        bytes.push_back(rgba[offset + 2U]);
        bytes.push_back(rgba[offset + 1U]);
        bytes.push_back(rgba[offset]);
        bytes.push_back(rgba[offset + 3U]);
    }

    const auto parsed = assets::parseGti(bytes);
    const auto decoded = assets::decodeGtiBaseRgba(
        bytes, parsed.variants.at(0U), rgba.size());
    if (parsed.checksum != checksum ||
        decoded.width != 2U ||
        decoded.height != 2U ||
        decoded.pixels.size() != rgba.size() ||
        !std::equal(decoded.pixels.begin(), decoded.pixels.end(), rgba.begin())) {
        throw std::runtime_error(
            "synthetic RGBA8 GTI failed its semantic self-check");
    }
    return bytes;
}

[[nodiscard]] inline SyntheticLegacyAssets makeSyntheticLegacyAssets(
    const SyntheticLegacyAssetOptions& options = {}) {
    SyntheticLegacyCcfOptions ccfOptions;
    if (options.includeSecondaryTexture) {
        ccfOptions.secondaryTexture = "Detail";
    }
    SyntheticLegacyAssets result{
        .world = makeSyntheticWorld(
            options.ccfLogicalPath, options.textureRoot),
        .ccf = makeSyntheticLegacyCcf(ccfOptions),
        .wallGti = makeSyntheticRgba8Gti(kSyntheticWallRgba),
        .detailGti = std::nullopt,
    };
    if (options.includeSecondaryTexture) {
        result.detailGti =
            makeSyntheticRgba8Gti(kSyntheticDetailRgba, 0x87654321U);
    }
    return result;
}

} // namespace airfix::testing
