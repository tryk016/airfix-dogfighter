#include "airfix/assets/LegacyFormats.hpp"

#include <array>
#include <bit>
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

void appendFloat(Bytes& bytes, const float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
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

[[nodiscard]] Bytes ccfVector3(
    const std::uint16_t id,
    const float x,
    const float y,
    const float z) {
    Bytes payload;
    appendFloat(payload, x);
    appendFloat(payload, y);
    appendFloat(payload, z);
    return ccfChunk(id, payload);
}

[[nodiscard]] Bytes ccfDocument(const Bytes& section) {
    Bytes bytes;
    appendU32(bytes, airfix::assets::kCcfMagic);
    appendU32(bytes, airfix::assets::kCcfVersion);
    appendU16(bytes, 1U);
    appendU32(bytes, static_cast<std::uint32_t>(6U + section.size()));
    appendBytes(bytes, section);
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

[[nodiscard]] Bytes ccfNamed(
    const std::string& name,
    const std::string& prefix) {
    Bytes payload = ccfString(name);
    appendBytes(payload, ccfString(prefix));
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
    return ccfDocument(section);
}

[[nodiscard]] Bytes makeMeshVertex(
    const float x,
    const float y,
    const float z,
    const bool includeOptionalVector) {
    Bytes payload = ccfVector3(0xF040U, x, y, z);
    if (includeOptionalVector) {
        appendBytes(payload, ccfVector3(0xF040U, 0.0F, 1.0F, 0.0F));
    }
    Bytes value;
    appendU32(value, 0U);
    appendBytes(payload, ccfChunk(0x4500U, value));
    return ccfChunk(0x3110U, payload);
}

struct MeshCcfOptions {
    bool invalidTriangleIndex{};
    bool includeUnknownChild{};
    bool unknownPaintType{};
    bool includePaintExtensionTail{};
};

[[nodiscard]] Bytes makeMeshCcf(const MeshCcfOptions& options = {}) {
    Bytes meshPayload = ccfNamed("Mesh", "House");
    appendU32(meshPayload, 7U);
    meshPayload.push_back(1U);
    meshPayload.push_back(0U);
    appendU32(meshPayload, 9U);
    appendBytes(meshPayload, ccfVector3(0xF040U, 1.0F, 2.0F, 3.0F));
    appendFloat(meshPayload, 2.0F);

    Bytes matrixPayload = ccfVector3(0xF040U, 1.0F, 0.0F, 0.0F);
    appendBytes(matrixPayload, ccfVector3(0xF040U, 0.0F, 1.0F, 0.0F));
    appendBytes(matrixPayload, ccfVector3(0xF040U, 0.0F, 0.0F, 1.0F));
    appendBytes(meshPayload, ccfChunk(0xF070U, ccfChunk(0xF050U, matrixPayload)));

    appendBytes(meshPayload, makeMeshVertex(0.0F, 0.0F, 0.0F, true));
    appendBytes(meshPayload, makeMeshVertex(1.0F, 0.0F, 0.0F, false));
    appendBytes(meshPayload, makeMeshVertex(0.0F, 1.0F, 0.0F, false));

    Bytes trianglePayload;
    appendU32(trianglePayload, 0U);
    appendU32(trianglePayload, 1U);
    appendU32(trianglePayload, options.invalidTriangleIndex ? 3U : 2U);
    appendU32(trianglePayload, 42U);
    Bytes paintPayload;
    appendU32(paintPayload, options.unknownPaintType ? 99U : 3U);
    if (options.unknownPaintType) {
        paintPayload.insert(paintPayload.end(), {0xDEU, 0xADU, 0xBEU, 0xEFU});
    }
    else {
        appendBytes(paintPayload, ccfVector3(0xF030U, 1.0F, 0.0F, 0.0F));
        appendBytes(paintPayload, ccfVector3(0xF030U, 0.0F, 1.0F, 0.0F));
        appendBytes(paintPayload, ccfVector3(0xF030U, 0.0F, 0.0F, 1.0F));
    }
    if (options.includePaintExtensionTail) {
        paintPayload.insert(paintPayload.end(), {0xAAU, 0x55U});
    }
    appendBytes(trianglePayload, ccfChunk(0xF090U, paintPayload));
    Bytes textureCoordinates;
    for (const auto value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}) {
        appendFloat(textureCoordinates, value);
    }
    appendBytes(trianglePayload, ccfChunk(0xF060U, textureCoordinates));
    appendBytes(meshPayload, ccfChunk(0x3120U, trianglePayload));

    Bytes vampireMode;
    appendU32(vampireMode, 0U);
    appendBytes(meshPayload, ccfChunk(0xF0A0U, vampireMode));
    Bytes range;
    appendU32(range, 1U);
    appendFloat(range, -1.0F);
    appendFloat(range, 1.0F);
    appendBytes(meshPayload, ccfChunk(0xF0D0U, range));
    appendBytes(meshPayload, ccfChunk(0xF0B2U, Bytes{1U}));
    if (options.includeUnknownChild) {
        appendBytes(meshPayload, ccfChunk(0x4999U, {}));
    }
    return ccfDocument(ccfChunk(0x3000U, ccfChunk(0x3100U, meshPayload)));
}

[[nodiscard]] Bytes makeNonMeshBlueprint(
    const std::uint16_t id,
    const std::string& name,
    const std::string& prefix,
    const std::uint32_t reference) {
    Bytes payload = ccfNamed(name, prefix);
    appendU32(payload, reference);
    appendU32(payload, 0U);
    appendU32(payload, 0U);
    appendBytes(payload, ccfVector3(0xF040U, 0.0F, 0.0F, 0.0F));
    appendFloat(payload, 1.0F);
    Bytes matrixPayload = ccfVector3(0xF040U, 1.0F, 0.0F, 0.0F);
    appendBytes(matrixPayload, ccfVector3(0xF040U, 0.0F, 1.0F, 0.0F));
    appendBytes(matrixPayload, ccfVector3(0xF040U, 0.0F, 0.0F, 1.0F));
    appendBytes(payload, ccfChunk(0xF070U, ccfChunk(0xF050U, matrixPayload)));
    return ccfChunk(id, payload);
}

[[nodiscard]] Bytes makeNonMeshBlueprintCcf() {
    Bytes sectionPayload = makeNonMeshBlueprint(0x4200U, "Null", "House", 11U);
    appendBytes(sectionPayload, makeNonMeshBlueprint(0x4300U, "Light", "House", 12U));
    return ccfDocument(ccfChunk(0x3000U, sectionPayload));
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
    return ccfDocument(ccfChunk(0x1000U, ccfChunk(0x1100U, {})));
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
    require(metadata.topLevelChunks[0].id == 0x1000U, "CCF child id mismatch");
    require(metadata.topLevelChunks[0].directChildren.size() == 1U,
        "CCF direct child count mismatch");
    require(metadata.topLevelChunks[0].directChildren[0].id == 0x1100U,
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

    const auto meshBytes = makeMeshCcf({.includeUnknownChild = true});
    const auto meshMetadata = airfix::assets::parseCcf(meshBytes);
    require(meshMetadata.meshes.size() == 1U, "CCF mesh count mismatch");
    const auto& mesh = meshMetadata.meshes[0];
    require(mesh.name == "Mesh" && mesh.prefix == "House", "CCF mesh name mismatch");
    require(mesh.reference == 7U && mesh.linkReference == 9U,
        "CCF mesh references mismatch");
    require(mesh.selectionFlagA == 1U && mesh.selectionFlagB == 0U,
        "CCF mesh flags mismatch");
    require(mesh.position == airfix::assets::CcfVector3{1.0F, 2.0F, 3.0F},
        "CCF mesh position mismatch");
    require(mesh.scalar == 2.0F, "CCF mesh scalar mismatch");
    require(mesh.vertices.size() == 3U, "CCF mesh vertex count mismatch");
    require(mesh.vertices[0].optionalVector.has_value(),
        "CCF mesh optional vertex vector missing");
    require(mesh.vertices[0].value4500 == 0U, "CCF mesh 0x4500 mismatch");
    require(mesh.triangles.size() == 1U, "CCF mesh triangle count mismatch");
    require(mesh.triangles[0].vertexIndices == std::array<std::uint32_t, 3>{0U, 1U, 2U},
        "CCF mesh triangle indices mismatch");
    require(mesh.triangles[0].materialReference == 42U,
        "CCF mesh triangle material mismatch");
    require(mesh.triangles[0].textureCoordinates.has_value(),
        "CCF mesh texture coordinates missing");
    require(mesh.triangles[0].paint.has_value() &&
        mesh.triangles[0].paint->type == 3U &&
        mesh.triangles[0].paint->colors.size() == 3U,
        "CCF mesh paint mismatch");
    require(mesh.vampireMode == 0U && mesh.propertyF0B2 == 1U && mesh.range.has_value(),
        "CCF mesh control properties mismatch");
    const auto& indexedMesh = meshMetadata.topLevelChunks[0].directChildren[0];
    require(!indexedMesh.directChildren.empty() &&
        indexedMesh.directChildren.back().id == 0x4999U,
        "CCF mesh unknown child was not preserved in the chunk index");

    const auto invalidTriangle = makeMeshCcf({.invalidTriangleIndex = true});
    requireParseError([&] { (void)airfix::assets::parseCcf(invalidTriangle); });

    const auto unknownPaint = airfix::assets::parseCcf(
        makeMeshCcf({.unknownPaintType = true}));
    require(unknownPaint.meshes[0].triangles[0].paint->type == 99U &&
        unknownPaint.meshes[0].triangles[0].paint->colors.empty(),
        "CCF unknown paint payload was not preserved opaquely");
    const auto extendedPaint = airfix::assets::parseCcf(
        makeMeshCcf({.includePaintExtensionTail = true}));
    require(extendedPaint.meshes[0].triangles[0].paint->colors.size() == 3U,
        "CCF known paint extension tail was rejected");

    require(meshMetadata.blueprints.size() == 1U &&
        meshMetadata.blueprints[0].kind == airfix::assets::CcfBlueprintKind::mesh &&
        meshMetadata.blueprints[0].meshIndex == 0U,
        "CCF mesh blueprint index mismatch");
    const auto nonMeshMetadata = airfix::assets::parseCcf(makeNonMeshBlueprintCcf());
    require(nonMeshMetadata.meshes.empty(), "unexpected geometry for non-mesh blueprints");
    require(nonMeshMetadata.blueprints.size() == 2U,
        "CCF non-mesh blueprint count mismatch");
    require(nonMeshMetadata.blueprints[0].kind ==
        airfix::assets::CcfBlueprintKind::nullNode &&
        nonMeshMetadata.blueprints[0].name == "Null" &&
        nonMeshMetadata.blueprints[0].reference == 11U &&
        !nonMeshMetadata.blueprints[0].meshIndex.has_value(),
        "CCF null blueprint mismatch");
    require(nonMeshMetadata.blueprints[1].kind ==
        airfix::assets::CcfBlueprintKind::light &&
        nonMeshMetadata.blueprints[1].name == "Light" &&
        nonMeshMetadata.blueprints[1].reference == 12U,
        "CCF light blueprint mismatch");

    Bytes descriptorBombPayload;
    descriptorBombPayload.reserve(250'000U * 6U);
    const auto emptyUnknown = ccfChunk(0x1200U, {});
    for (std::size_t index = 0U; index < 250'000U; ++index) {
        appendBytes(descriptorBombPayload, emptyUnknown);
    }
    const auto descriptorBomb = ccfDocument(
        ccfChunk(0x1000U, descriptorBombPayload));
    requireParseError([&] { (void)airfix::assets::parseCcf(descriptorBomb); });
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

[[nodiscard]] airfix::assets::GtiVariant mipVariant(
    const std::uint32_t format,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t paletteEntries,
    const std::uint32_t mipmapLevels,
    const std::uint64_t pixelOffset,
    const std::uint32_t pixelBytes) {
    return {
        .format = format,
        .width = width,
        .height = height,
        .paletteEntries = paletteEntries,
        .mipmapLevels = mipmapLevels,
        .pixelDataOffset = pixelOffset,
        .pixelDataSize = pixelBytes,
        .expectedPixelDataSize = airfix::assets::expectedGtiPixelBytes(
            format, width, height, mipmapLevels),
        .trailingBytes = pixelBytes - airfix::assets::expectedGtiPixelBytes(
            format, width, height, mipmapLevels),
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

void testGtiMipLayouts() {
    const auto variant = mipVariant(8U, 4U, 4U, 0U, 3U, 100U, 84U);
    const auto layouts = airfix::assets::describeGtiMipLevels(variant);
    require(layouts.size() == 3U, "GTI mip layout count mismatch");
    require(layouts[0].level == 0U && layouts[0].width == 4U &&
        layouts[0].height == 4U && layouts[0].sourceOffset == 100U &&
        layouts[0].sourceSize == 64U && layouts[0].requiredTexelBytes == 64U &&
        layouts[0].exactTexelLayout,
        "GTI base mip layout mismatch");
    require(layouts[1].level == 1U && layouts[1].width == 2U &&
        layouts[1].height == 2U && layouts[1].sourceOffset == 164U &&
        layouts[1].sourceSize == 16U && layouts[1].requiredTexelBytes == 16U &&
        layouts[1].exactTexelLayout,
        "GTI second mip layout mismatch");
    require(layouts[2].level == 2U && layouts[2].width == 1U &&
        layouts[2].height == 1U && layouts[2].sourceOffset == 180U &&
        layouts[2].sourceSize == 4U && layouts[2].requiredTexelBytes == 4U &&
        layouts[2].exactTexelLayout,
        "GTI final mip layout mismatch");

    const auto uneven = mipVariant(8U, 1U, 4U, 0U, 4U, 0U, 21U);
    const auto unevenLayouts = airfix::assets::describeGtiMipLevels(uneven);
    require(unevenLayouts.size() == 4U && unevenLayouts[0].exactTexelLayout,
        "GTI uneven base layout mismatch");
    require(unevenLayouts[1].width == 1U && unevenLayouts[1].height == 2U &&
        unevenLayouts[1].sourceSize == 4U &&
        unevenLayouts[1].requiredTexelBytes == 8U &&
        !unevenLayouts[1].exactTexelLayout,
        "GTI uneven mip mismatch was not described");
    require(unevenLayouts[3].width == 1U && unevenLayouts[3].height == 1U &&
        unevenLayouts[3].sourceOffset == 21U && unevenLayouts[3].sourceSize == 0U &&
        unevenLayouts[3].requiredTexelBytes == 4U &&
        !unevenLayouts[3].exactTexelLayout,
        "GTI zero-byte tail mip mismatch");

    const Bytes data(21U, 0U);
    const auto base = airfix::assets::decodeGtiBaseRgba(data, uneven, 16U);
    require(base.width == 1U && base.height == 4U && base.pixels.size() == 16U,
        "GTI uneven image base level did not decode independently");
    requireParseError([&] {
        (void)airfix::assets::decodeGtiMipLevelRgba(data, uneven, 1U, 8U);
    });
    requireParseError([&] {
        (void)airfix::assets::decodeGtiMipChainRgba(data, uneven, 64U);
    });
}

void testGtiMipDecoding() {
    {
        const Bytes data{
            0x03U, 0x02U, 0x01U, 0xAAU,
            0x30U, 0x20U, 0x10U, 0xBBU,
            0U, 1U, 0U, 1U,
            1U,
        };
        const auto variant = mipVariant(3U, 2U, 2U, 2U, 2U, 8U, 5U);
        const auto chain = airfix::assets::decodeGtiMipChainRgba(data, variant, 20U);
        require(chain.levels.size() == 2U &&
            chain.levels[1].pixels == Bytes{0x10U, 0x20U, 0x30U, 0xFFU},
            "GTI P8 mip conversion mismatch");
    }
    {
        const Bytes data{
            0x03U, 0x02U, 0x01U, 0xAAU,
            0x30U, 0x20U, 0x10U, 0xBBU,
            0U, 0x10U, 1U, 0x20U, 0U, 0x30U, 1U, 0x40U,
            1U, 0x80U,
        };
        const auto variant = mipVariant(4U, 2U, 2U, 2U, 2U, 8U, 10U);
        const auto chain = airfix::assets::decodeGtiMipChainRgba(data, variant, 20U);
        require(chain.levels.size() == 2U &&
            chain.levels[1].pixels == Bytes{0x10U, 0x20U, 0x30U, 0x80U},
            "GTI P8A8 shared-palette mip conversion mismatch");
    }
    {
        const Bytes data{
            0x23U, 0xF1U, 0x23U, 0xF1U, 0x23U, 0xF1U, 0x23U, 0xF1U,
            0x56U, 0xA4U,
        };
        const auto variant = mipVariant(6U, 2U, 2U, 0U, 2U, 0U, 10U);
        const auto mip = airfix::assets::decodeGtiMipLevelRgba(data, variant, 1U, 4U);
        require(mip.pixels == Bytes{0x40U, 0x50U, 0x60U, 0xA0U},
            "GTI ARGB4444 mip conversion mismatch");
    }
    {
        const Bytes data{
            1U, 2U, 3U, 1U, 2U, 3U, 1U, 2U, 3U, 1U, 2U, 3U,
            0x11U, 0x22U, 0x33U,
        };
        const auto variant = mipVariant(7U, 2U, 2U, 0U, 2U, 0U, 15U);
        const auto mip = airfix::assets::decodeGtiMipLevelRgba(data, variant, 1U, 4U);
        require(mip.pixels == Bytes{0x11U, 0x22U, 0x33U, 0xFFU},
            "GTI RGB888 mip conversion mismatch");
    }
    {
        Bytes data{
            3U, 2U, 1U, 4U, 3U, 2U, 1U, 4U,
            3U, 2U, 1U, 4U, 3U, 2U, 1U, 4U,
            0x33U, 0x22U, 0x11U, 0x44U,
        };
        data.insert(data.end(), {0xDEU, 0xADU, 0xBEU});
        const auto variant = mipVariant(8U, 2U, 2U, 0U, 2U, 0U, 23U);
        const auto chain = airfix::assets::decodeGtiMipChainRgba(data, variant, 20U);
        require(chain.levels.size() == 2U &&
            chain.levels[1].pixels == Bytes{0x11U, 0x22U, 0x33U, 0x44U},
            "GTI ARGB8888 mip conversion or trailing-byte handling mismatch");
    }
}

void testGtiMipDecodeFailures() {
    {
        const Bytes truncated(19U, 0U);
        const auto variant = mipVariant(8U, 2U, 2U, 0U, 2U, 0U, 20U);
        const auto base = airfix::assets::decodeGtiBaseRgba(truncated, variant, 16U);
        require(base.pixels.size() == 16U,
            "GTI truncated later mip affected base decode");
        requireParseError([&] {
            (void)airfix::assets::decodeGtiMipLevelRgba(
                truncated, variant, 1U, 4U);
        });
        requireParseError([&] {
            (void)airfix::assets::decodeGtiMipChainRgba(truncated, variant, 20U);
        });
    }
    {
        const Bytes data{
            3U, 2U, 1U, 0U,
            0x30U, 0x20U, 0x10U, 0U,
            0U, 1U, 0U, 1U,
            2U,
        };
        const auto variant = mipVariant(3U, 2U, 2U, 2U, 2U, 8U, 5U);
        const auto base = airfix::assets::decodeGtiBaseRgba(data, variant, 16U);
        require(base.pixels.size() == 16U,
            "GTI invalid later palette index affected base decode");
        requireParseError([&] {
            (void)airfix::assets::decodeGtiMipLevelRgba(data, variant, 1U, 4U);
        });
    }
    {
        const Bytes data(20U, 0U);
        const auto variant = mipVariant(8U, 2U, 2U, 0U, 2U, 0U, 20U);
        requireParseError([&] {
            (void)airfix::assets::decodeGtiMipLevelRgba(data, variant, 1U, 3U);
        });
        requireParseError([&] {
            (void)airfix::assets::decodeGtiMipChainRgba(data, variant, 19U);
        });
        requireParseError([&] {
            (void)airfix::assets::decodeGtiMipLevelRgba(data, variant, 2U, 20U);
        });
    }
    requireParseError([] {
        const auto invalid = mipVariant(3U, 1U, 1U, 0U, 1U, 0U, 1U);
        (void)airfix::assets::describeGtiMipLevels(invalid);
    });
    requireParseError([] {
        const auto invalid = mipVariant(3U, 1U, 1U, 257U, 1U, 1028U, 1U);
        (void)airfix::assets::describeGtiMipLevels(invalid);
    });
    requireParseError([] {
        const auto invalid = mipVariant(8U, 1U, 1U, 1U, 1U, 4U, 4U);
        (void)airfix::assets::describeGtiMipLevels(invalid);
    });
    requireParseError([] {
        airfix::assets::GtiVariant invalid{
            .format = 8U,
            .width = std::numeric_limits<std::uint32_t>::max(),
            .height = std::numeric_limits<std::uint32_t>::max(),
            .paletteEntries = 0U,
            .mipmapLevels = 1U,
        };
        (void)airfix::assets::describeGtiMipLevels(invalid);
    });
    requireParseError([] {
        const Bytes physicallyLargeEnough(16U, 0U);
        const airfix::assets::GtiVariant declaredTooSmall{
            .format = 8U,
            .width = 2U,
            .height = 2U,
            .paletteEntries = 0U,
            .mipmapLevels = 1U,
            .pixelDataOffset = 0U,
            .pixelDataSize = 15U,
            .expectedPixelDataSize = 16U,
            .trailingBytes = 0U,
        };
        (void)airfix::assets::decodeGtiBaseRgba(
            physicallyLargeEnough, declaredTooSmall, 16U);
    });
}

} // namespace

int main() {
    try {
        testGti();
        testCcf();
        testGtiBaseDecoding();
        testGtiMipLayouts();
        testGtiMipDecoding();
        testGtiMipDecodeFailures();
        std::cout << "all legacy asset tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "legacy asset test failure: " << error.what() << '\n';
        return 1;
    }
}
