#include "airfix/assets/LegacyFormats.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendU16(Bytes& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(Bytes& bytes, const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8U)));
    }
}

void appendBytes(Bytes& destination, const Bytes& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

[[nodiscard]] Bytes ccfChunk(const std::uint16_t id, const Bytes& payload) {
    Bytes bytes;
    appendU16(bytes, id);
    appendU32(bytes, static_cast<std::uint32_t>(6U + payload.size()));
    appendBytes(bytes, payload);
    return bytes;
}

[[nodiscard]] Bytes ccfString(
    const std::string& value,
    const bool validTerminator = true) {
    Bytes payload;
    appendU32(payload, static_cast<std::uint32_t>(value.size() + 1U));
    payload.insert(payload.end(), value.begin(), value.end());
    payload.push_back(validTerminator ? 0U : static_cast<std::uint8_t>('X'));
    return ccfChunk(0xF020U, payload);
}

[[nodiscard]] Bytes ccfName(
    const bool validTerminator = true,
    const bool zeroLengthPrefix = false) {
    Bytes payload = ccfString("Material", validTerminator);
    if (zeroLengthPrefix) {
        appendBytes(payload, ccfChunk(0xF020U, Bytes(4U, 0U)));
    }
    else {
        appendBytes(payload, ccfString("House"));
    }
    return ccfChunk(0xF010U, payload);
}

struct MaterialCcfOptions {
    bool includeScalars{true};
    bool duplicatePrimaryTexture{};
    bool validNameTerminator{true};
    bool zeroLengthPrefix{};
    bool includeSecondaryTexture{};
    bool includeEnvironmentTexture{};
    bool includeUnknownProperty{};
    bool includeUnknownSectionChild{};
};

[[nodiscard]] Bytes makeMaterialCcf(const MaterialCcfOptions& options = {}) {
    Bytes materialPayload = ccfName(
        options.validNameTerminator, options.zeroLengthPrefix);
    appendU32(materialPayload, 42U);

    Bytes texturePayload = ccfString("Wall.gti");
    const auto primaryTexture = ccfChunk(0x2110U, texturePayload);
    appendBytes(materialPayload, primaryTexture);
    if (options.duplicatePrimaryTexture) {
        appendBytes(materialPayload, primaryTexture);
    }
    if (options.includeSecondaryTexture) {
        appendBytes(materialPayload, ccfChunk(0x2111U, ccfString("Detail.gti")));
    }
    if (options.includeEnvironmentTexture) {
        appendBytes(materialPayload, ccfChunk(0x2120U, ccfString("Env.gti")));
    }
    if (options.includeUnknownProperty) {
        appendBytes(materialPayload, ccfChunk(0x2153U, Bytes(16U, 0U)));
    }

    Bytes vectorsPayload;
    appendBytes(vectorsPayload, ccfChunk(0xF030U, Bytes(12U, 0U)));
    appendBytes(vectorsPayload, ccfChunk(0xF030U, Bytes(12U, 0U)));
    appendU32(vectorsPayload, 0U);
    appendBytes(materialPayload, ccfChunk(0x2140U, vectorsPayload));
    appendBytes(materialPayload, ccfChunk(0x2150U, Bytes(6U, 0U)));
    appendBytes(materialPayload, ccfChunk(0x2151U, Bytes(1U, 0U)));
    if (options.includeScalars) {
        appendBytes(materialPayload, ccfChunk(0x2152U, Bytes(12U, 0U)));
    }

    const auto material = ccfChunk(0x2100U, materialPayload);
    auto sectionPayload = material;
    if (options.includeUnknownSectionChild) {
        appendBytes(sectionPayload, ccfChunk(0x2200U, {}));
    }
    const auto section = ccfChunk(0x2000U, sectionPayload);
    Bytes bytes;
    appendU32(bytes, airfix::assets::kCcfMagic);
    appendU32(bytes, airfix::assets::kCcfVersion);
    appendU16(bytes, 1U);
    appendU32(bytes, static_cast<std::uint32_t>(6U + section.size()));
    appendBytes(bytes, section);
    return bytes;
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireParseError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const airfix::assets::ParseError&) {
        return;
    }
    throw std::runtime_error("expected asset ParseError");
}

[[nodiscard]] Bytes makeGti() {
    Bytes bytes;
    appendU32(bytes, airfix::assets::kGtiMagic);
    appendU32(bytes, airfix::assets::kGtiVersion);
    appendU32(bytes, airfix::assets::kGtiChecksumChunk);
    appendU32(bytes, 4U);
    appendU32(bytes, 0x12345678U);
    appendU32(bytes, airfix::assets::kGtiImageChunk);
    appendU32(bytes, 36U);
    appendU32(bytes, 8U);
    appendU32(bytes, 2U);
    appendU32(bytes, 2U);
    appendU32(bytes, 0U);
    appendU32(bytes, 1U);
    bytes.insert(bytes.end(), 16U, 0xAAU);
    return bytes;
}

[[nodiscard]] Bytes makeCcf() {
    Bytes bytes;
    appendU32(bytes, airfix::assets::kCcfMagic);
    appendU32(bytes, airfix::assets::kCcfVersion);
    appendU16(bytes, 1U);
    appendU32(bytes, 18U);
    appendU16(bytes, 0x3000U);
    appendU32(bytes, 12U);
    appendU16(bytes, 0x3100U);
    appendU32(bytes, 6U);
    return bytes;
}

void testGti() {
    const auto bytes = makeGti();
    const auto metadata = airfix::assets::parseGti(bytes);
    require(metadata.checksum == 0x12345678U, "GTI checksum mismatch");
    require(metadata.chunks.size() == 2U, "GTI chunk count mismatch");
    require(metadata.variants.size() == 1U, "GTI variant count mismatch");
    require(metadata.variants[0].format == 8U, "GTI format mismatch");
    require(metadata.variants[0].width == 2U && metadata.variants[0].height == 2U,
        "GTI dimensions mismatch");
    require(metadata.variants[0].pixelDataSize == 16U, "GTI pixel bytes mismatch");
    require(metadata.variants[0].expectedPixelDataSize == 16U,
        "GTI computed pixel bytes mismatch");

    auto invalid = bytes;
    invalid[0] = 'X';
    requireParseError([&] { (void)airfix::assets::parseGti(invalid); });
    invalid = bytes;
    invalid[12] = 0x05U;
    requireParseError([&] { (void)airfix::assets::parseGti(invalid); });

    auto repeatedChecksum = bytes;
    Bytes checksumChunk;
    appendU32(checksumChunk, airfix::assets::kGtiChecksumChunk);
    appendU32(checksumChunk, 4U);
    appendU32(checksumChunk, 0xAABBCCDDU);
    repeatedChecksum.insert(
        repeatedChecksum.begin() + 20, checksumChunk.begin(), checksumChunk.end());
    const auto repeatedMetadata = airfix::assets::parseGti(repeatedChecksum);
    require(repeatedMetadata.checksumChunkCount == 2U,
        "GTI repeated checksum count mismatch");
    require(repeatedMetadata.checksum == 0xAABBCCDDU,
        "GTI must keep the final checksum");

    auto terminalOverrun = bytes;
    terminalOverrun[24] = 40U;
    const auto overrunMetadata = airfix::assets::parseGti(terminalOverrun);
    require(overrunMetadata.terminalDeclaredOverrun,
        "GTI terminal declared-size quirk was not recorded");
    require(overrunMetadata.variants[0].expectedPixelDataSize == 16U,
        "GTI terminal overrun changed the computed pixel size");

    requireParseError([] {
        (void)airfix::assets::expectedGtiPixelBytes(
            8U,
            std::numeric_limits<std::uint32_t>::max(),
            std::numeric_limits<std::uint32_t>::max(),
            1U);
    });
}

void testCcf() {
    const auto bytes = makeCcf();
    const auto metadata = airfix::assets::parseCcf(bytes);
    require(metadata.rootId == 1U, "CCF root id mismatch");
    require(metadata.topLevelChunks.size() == 1U, "CCF top-level count mismatch");
    require(metadata.topLevelChunks[0].id == 0x3000U, "CCF child id mismatch");
    require(metadata.topLevelChunks[0].directChildren.size() == 1U,
        "CCF direct child count mismatch");
    require(metadata.topLevelChunks[0].directChildren[0].id == 0x3100U,
        "CCF direct child id mismatch");

    auto invalid = bytes;
    invalid[10] = 17U;
    requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
    invalid = bytes;
    invalid[16] = 5U;
    requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });

    const auto materialBytes = makeMaterialCcf();
    const auto materialMetadata = airfix::assets::parseCcf(materialBytes);
    require(materialMetadata.materials.size() == 1U, "CCF material count mismatch");
    const auto& material = materialMetadata.materials[0];
    require(material.name == "Material", "CCF material name mismatch");
    require(material.prefix == "House", "CCF material prefix mismatch");
    require(material.reference == 42U, "CCF material reference mismatch");
    require(material.primaryTexture == "Wall.gti", "CCF primary texture mismatch");
    require(!material.secondaryTexture.has_value(), "unexpected CCF secondary texture");
    require(!material.environmentTexture.has_value(), "unexpected CCF environment texture");
    require(materialMetadata.topLevelChunks[0].directChildren[0].directChildren.size() == 5U,
        "CCF material property count mismatch");

    const auto missingProperty = makeMaterialCcf({.includeScalars = false});
    require(airfix::assets::parseCcf(missingProperty).materials.size() == 1U,
        "CCF loader-compatible missing property was rejected");
    const auto duplicateTexture = makeMaterialCcf({.duplicatePrimaryTexture = true});
    requireParseError([&] { (void)airfix::assets::parseCcf(duplicateTexture); });
    const auto unterminatedName = makeMaterialCcf({.validNameTerminator = false});
    requireParseError([&] { (void)airfix::assets::parseCcf(unterminatedName); });

    const auto emptyPrefix = makeMaterialCcf({.zeroLengthPrefix = true});
    const auto emptyPrefixMetadata = airfix::assets::parseCcf(emptyPrefix);
    require(emptyPrefixMetadata.materials[0].prefix.empty(),
        "CCF zero-length prefix mismatch");

    const auto allTextures = makeMaterialCcf({
        .includeSecondaryTexture = true,
        .includeEnvironmentTexture = true,
    });
    const auto allTextureMetadata = airfix::assets::parseCcf(allTextures);
    require(allTextureMetadata.materials[0].secondaryTexture == "Detail.gti",
        "CCF secondary texture mismatch");
    require(allTextureMetadata.materials[0].environmentTexture == "Env.gti",
        "CCF environment texture mismatch");

    const auto forwardCompatible = makeMaterialCcf({
        .includeUnknownProperty = true,
        .includeUnknownSectionChild = true,
    });
    const auto forwardMetadata = airfix::assets::parseCcf(forwardCompatible);
    require(forwardMetadata.materials.size() == 1U,
        "CCF unknown material extension changed material count");
    require(forwardMetadata.topLevelChunks[0].directChildren.size() == 2U,
        "CCF unknown section child was not preserved");
}

[[nodiscard]] airfix::assets::GtiVariant onePixelVariant(
    const std::uint32_t format,
    const std::uint32_t paletteEntries,
    const std::uint64_t pixelOffset,
    const std::uint32_t pixelBytes) {
    return {
        .format = format,
        .width = 1U,
        .height = 1U,
        .paletteEntries = paletteEntries,
        .mipmapLevels = 1U,
        .pixelDataOffset = pixelOffset,
        .pixelDataSize = pixelBytes,
        .expectedPixelDataSize = pixelBytes,
        .trailingBytes = 0U,
    };
}

void testGtiBaseDecoding() {
    {
        const Bytes data{0x03U, 0x02U, 0x01U, 0x99U, 0x00U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(3U, 1U, 4U, 1U), 4U);
        require(image.pixels == Bytes{0x01U, 0x02U, 0x03U, 0xFFU},
            "GTI P8 conversion mismatch");
    }
    {
        const Bytes data{0x30U, 0x20U, 0x10U, 0x99U, 0x00U, 0x80U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(4U, 1U, 4U, 2U), 4U);
        require(image.pixels == Bytes{0x10U, 0x20U, 0x30U, 0x80U},
            "GTI P8A8 conversion mismatch");
    }
    {
        const Bytes data{0x23U, 0xF1U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(6U, 0U, 0U, 2U), 4U);
        require(image.pixels == Bytes{0x10U, 0x20U, 0x30U, 0xF0U},
            "GTI ARGB4444 conversion mismatch");
    }
    {
        const Bytes data{0x11U, 0x22U, 0x33U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(7U, 0U, 0U, 3U), 4U);
        require(image.pixels == Bytes{0x11U, 0x22U, 0x33U, 0xFFU},
            "GTI RGB888 conversion mismatch");
    }
    {
        const Bytes data{0x33U, 0x22U, 0x11U, 0x44U};
        const auto image = airfix::assets::decodeGtiBaseRgba(
            data, onePixelVariant(8U, 0U, 0U, 4U), 4U);
        require(image.pixels == Bytes{0x11U, 0x22U, 0x33U, 0x44U},
            "GTI ARGB8888 conversion mismatch");
    }
    {
        const Bytes data{0x33U, 0x22U, 0x11U, 0x44U};
        requireParseError([&] {
            (void)airfix::assets::decodeGtiBaseRgba(
                data, onePixelVariant(8U, 0U, 0U, 4U), 3U);
        });
    }
}

} // namespace

int main() {
    try {
        testGti();
        testCcf();
        testGtiBaseDecoding();
        std::cout << "all legacy asset tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "legacy asset test failure: " << error.what() << '\n';
        return 1;
    }
}
