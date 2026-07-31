#include "airfix/assets/LegacyFormats.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
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
    bool includeVisualProperties{true};
    bool includeScalars{true};
    std::uint32_t collisionMode2152{8U};
    float scalar2152A{0.5F};
    float scalar2152B{-0.25F};
    std::uint8_t lightingMode{2U};
    bool gouraudShading{true};
    std::uint32_t blendMode{3U};
    bool flag2151{true};
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

    if (options.includeVisualProperties) {
        Bytes vectorsPayload;
        appendBytes(
            vectorsPayload, ccfVector3(0xF030U, 1.25F, 2.5F, -3.75F));
        appendBytes(
            vectorsPayload, ccfVector3(0xF030U, 4.5F, -5.25F, 6.75F));
        appendFloat(vectorsPayload, 0.625F);
        appendBytes(materialPayload, ccfChunk(0x2140U, vectorsPayload));

        Bytes flags;
        flags.push_back(options.lightingMode);
        flags.push_back(options.gouraudShading ? 1U : 0U);
        appendU32(flags, options.blendMode);
        appendBytes(materialPayload, ccfChunk(0x2150U, flags));
        appendBytes(
            materialPayload,
            ccfChunk(
                0x2151U,
                Bytes{static_cast<std::uint8_t>(
                    options.flag2151 ? 1U : 0U)}));
    }
    if (options.includeScalars) {
        Bytes scalars;
        appendU32(scalars, options.collisionMode2152);
        appendFloat(scalars, options.scalar2152A);
        appendFloat(scalars, options.scalar2152B);
        appendBytes(materialPayload, ccfChunk(0x2152U, scalars));
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
    const std::uint32_t reference,
    const std::uint32_t auxiliaryReference,
    const std::uint32_t parentReference,
    const airfix::assets::CcfVector3& position,
    const float rawScalar,
    const std::array<airfix::assets::CcfVector3, 3>& orientation,
    const std::size_t retainedFixedTailBytes =
        std::numeric_limits<std::size_t>::max()) {
    Bytes payload = ccfNamed(name, prefix);
    const auto nameBytes = payload.size();
    appendU32(payload, reference);
    appendU32(payload, auxiliaryReference);
    appendU32(payload, parentReference);
    appendBytes(payload, ccfVector3(
        0xF040U, position[0], position[1], position[2]));
    appendFloat(payload, rawScalar);
    Bytes matrixPayload = ccfVector3(
        0xF040U, orientation[0][0], orientation[0][1], orientation[0][2]);
    appendBytes(matrixPayload, ccfVector3(
        0xF040U, orientation[1][0], orientation[1][1], orientation[1][2]));
    appendBytes(matrixPayload, ccfVector3(
        0xF040U, orientation[2][0], orientation[2][1], orientation[2][2]));
    appendBytes(payload, ccfChunk(0xF070U, ccfChunk(0xF050U, matrixPayload)));
    if (retainedFixedTailBytes < payload.size() - nameBytes) {
        payload.resize(nameBytes + retainedFixedTailBytes);
    }
    return ccfChunk(id, payload);
}

[[nodiscard]] Bytes makeNonMeshBlueprintCcf() {
    const std::array<airfix::assets::CcfVector3, 3> nullOrientation{{
        {0.0F, 1.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, -1.0F},
    }};
    const std::array<airfix::assets::CcfVector3, 3> lightOrientation{{
        {0.5F, 0.0F, 0.0F},
        {0.0F, 0.5F, 0.0F},
        {0.0F, 0.0F, 0.5F},
    }};
    Bytes sectionPayload = makeNonMeshBlueprint(
        0x4200U, "Null", "House", 11U, 101U, 201U,
        {1.0F, 2.0F, 3.0F}, 4.0F, nullOrientation);
    appendBytes(sectionPayload, makeNonMeshBlueprint(
        0x4300U, "Light", "House", 12U, 102U, 202U,
        {-1.0F, -2.0F, -3.0F}, 0.5F, lightOrientation));
    return ccfDocument(ccfChunk(0x3000U, sectionPayload));
}

[[nodiscard]] Bytes makeTruncatedNonMeshBlueprintCcf(
    const std::uint16_t id,
    const std::size_t retainedFixedTailBytes) {
    const std::array<airfix::assets::CcfVector3, 3> orientation{{
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    }};
    return ccfDocument(ccfChunk(0x3000U, makeNonMeshBlueprint(
        id, id == 0x4200U ? "Null" : "Light", "House", 11U, 101U, 201U,
        {1.0F, 2.0F, 3.0F}, 4.0F, orientation,
        retainedFixedTailBytes)));
}

[[nodiscard]] Bytes ccfPlacedOrientation(const bool alternate = false) {
    if (alternate) {
        return ccfChunk(0xF070U, ccfVector3(0xF040U, 0.25F, 0.5F, 0.75F));
    }
    Bytes matrix = ccfVector3(0xF040U, 1.0F, 0.0F, 0.0F);
    appendBytes(matrix, ccfVector3(0xF040U, 0.0F, 1.0F, 0.0F));
    appendBytes(matrix, ccfVector3(0xF040U, 0.0F, 0.0F, 1.0F));
    return ccfChunk(0xF070U, ccfChunk(0xF050U, matrix));
}

[[nodiscard]] Bytes makePlacedRecord(
    const std::uint16_t id,
    const Bytes& children = {},
    const bool alternateOrientation = false,
    const Bytes* orientationOverride = nullptr) {
    Bytes payload = ccfNamed(
        id == 0x4100U ? "Object" : (id == 0x4200U ? "Null" : "Light"),
        "Room");
    appendU32(payload, 100U);
    if (id == 0x4100U) {
        appendU32(payload, 200U);
        appendU32(payload, 300U);
        appendU32(payload, 400U);
        payload.push_back(0x7EU);
        appendU32(payload, 500U);
        appendU32(payload, 600U);
    }
    else {
        appendU32(payload, 300U);
        appendU32(payload, 400U);
    }
    appendBytes(payload, ccfVector3(0xF040U, 1.0F, 2.0F, 3.0F));
    appendFloat(payload, 4.0F);
    appendBytes(payload, orientationOverride != nullptr
        ? *orientationOverride
        : ccfPlacedOrientation(alternateOrientation));
    appendBytes(payload, children);
    return ccfChunk(id, payload);
}

[[nodiscard]] Bytes ccfU32Property(
    const std::uint16_t id,
    const std::uint32_t value) {
    Bytes payload;
    appendU32(payload, value);
    return ccfChunk(id, payload);
}

[[nodiscard]] Bytes ccfPlacedLight4310(const std::optional<std::string>& texture) {
    Bytes payload;
    appendFloat(payload, 1.25F);
    appendBytes(payload, ccfVector3(0xF030U, 0.1F, 0.2F, 0.3F));
    appendFloat(payload, 2.25F);
    appendFloat(payload, 3.25F);
    if (texture.has_value()) {
        appendBytes(payload, ccfString(*texture));
    }
    return ccfChunk(0x4310U, payload);
}

[[nodiscard]] Bytes ccfPlacedLight4320(
    const std::optional<std::array<std::string, 2>>& textures) {
    Bytes payload;
    appendFloat(payload, 4.25F);
    appendFloat(payload, 5.25F);
    appendFloat(payload, 6.25F);
    appendFloat(payload, 7.25F);
    if (textures.has_value()) {
        appendBytes(payload, ccfString((*textures)[0]));
        appendBytes(payload, ccfString((*textures)[1]));
    }
    return ccfChunk(0x4320U, payload);
}

[[nodiscard]] Bytes ccfPlacedLight4330() {
    Bytes payload = ccfVector3(0xF040U, 8.25F, 9.25F, 10.25F);
    appendFloat(payload, 11.25F);
    appendFloat(payload, 12.25F);
    return ccfChunk(0x4330U, payload);
}

[[nodiscard]] Bytes makePlacedCcf(const Bytes& records) {
    return ccfDocument(ccfChunk(0x4000U, records));
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

[[nodiscard]] Bytes ccfFog(
    const std::uint32_t enabledRaw,
    const float first,
    const float second,
    const airfix::assets::CcfVector3& color) {
    Bytes payload;
    appendU32(payload, enabledRaw);
    appendFloat(payload, first);
    appendFloat(payload, second);
    appendBytes(payload, ccfVector3(
        0xF030U, color[0], color[1], color[2]));
    return ccfChunk(0x1101U, payload);
}

[[nodiscard]] Bytes ccfBspPolygon(
    const float seed,
    const std::uint32_t polygonIndex,
    const std::uint32_t objectReference) {
    Bytes payload;
    for (std::size_t vector = 0U; vector < 5U; ++vector) {
        const auto base = seed + static_cast<float>(vector * 3U);
        appendBytes(payload, ccfVector3(
            0xF040U, base, base + 1.0F, base + 2.0F));
    }
    appendU32(payload, polygonIndex);
    appendU32(payload, objectReference);
    return ccfChunk(0xF0C1U, payload);
}

[[nodiscard]] Bytes ccfBspNode(
    const std::uint32_t childAPresenceRaw = 0U,
    const std::optional<Bytes>& childA = std::nullopt,
    const std::uint32_t childBPresenceRaw = 0U,
    const std::optional<Bytes>& childB = std::nullopt,
    const airfix::assets::CcfVector3& splitNormal = {1.0F, 0.0F, 0.0F},
    const airfix::assets::CcfVector3& pointOnPlane = {0.0F, 1.0F, 0.0F},
    const Bytes& trailing = {}) {
    Bytes payload;
    appendU32(payload, childAPresenceRaw);
    if (childA.has_value()) {
        appendBytes(payload, *childA);
    }
    appendU32(payload, childBPresenceRaw);
    if (childB.has_value()) {
        appendBytes(payload, *childB);
    }
    appendBytes(payload, ccfVector3(
        0xF040U, splitNormal[0], splitNormal[1], splitNormal[2]));
    appendBytes(payload, ccfVector3(
        0xF040U, pointOnPlane[0], pointOnPlane[1], pointOnPlane[2]));
    appendBytes(payload, trailing);
    return ccfChunk(0xF0C0U, payload);
}

[[nodiscard]] Bytes ccfRoomDocument(
    const Bytes& children,
    const std::string& name = "World",
    const std::string& prefix = "House",
    const std::uint32_t reference = 77U) {
    Bytes room = ccfNamed(name, prefix);
    appendU32(room, reference);
    appendBytes(room, children);
    return ccfDocument(ccfChunk(0x1000U, ccfChunk(0x1100U, room)));
}

[[nodiscard]] Bytes makeCcf() {
    Bytes room = ccfNamed("World", "House");
    appendU32(room, 77U);
    appendBytes(room, ccfFog(
        1U, 10.0F, 20.0F, {0.25F, 0.5F, 0.75F}));
    return ccfDocument(ccfChunk(0x1000U, ccfChunk(0x1100U, room)));
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
    require(
        metadata.roomSections.size() == 1U &&
        metadata.roomSections[0].firstPhysicalRoomIndex == 0U &&
        metadata.roomSections[0].physicalRoomCount == 1U &&
        metadata.roomSections[0].firstDirectChildIsRoom,
        "CCF room-section metadata mismatch");
    require(metadata.rooms.size() == 1U && metadata.rooms[0].name == "World" &&
            metadata.rooms[0].prefix == "House" &&
            metadata.rooms[0].reference == 77U &&
            metadata.rooms[0].primaryBinding &&
            metadata.rooms[0].fog.has_value() &&
            metadata.rooms[0].fog->enabledRaw == 1U &&
            metadata.rooms[0].fog->first == 10.0F &&
            metadata.rooms[0].fog->second == 20.0F &&
            metadata.rooms[0].fog->color ==
                airfix::assets::CcfVector3{0.25F, 0.5F, 0.75F} &&
            metadata.rooms[0].directChildren.size() == 1U &&
            metadata.rooms[0].directChildren[0].id == 0x1101U,
        "CCF room metadata mismatch");

    Bytes firstRoom = ccfNamed("World", "House");
    appendU32(firstRoom, 77U);
    Bytes secondRoom = ccfNamed("Attic", "House");
    appendU32(secondRoom, 78U);
    Bytes roomRecords;
    appendBytes(roomRecords, ccfChunk(0x1100U, firstRoom));
    appendBytes(roomRecords, ccfChunk(0x1100U, secondRoom));
    const auto twoRooms = airfix::assets::parseCcf(
        ccfDocument(ccfChunk(0x1000U, roomRecords)));
    require(twoRooms.rooms.size() == 2U &&
            twoRooms.rooms[0].primaryBinding &&
            !twoRooms.rooms[1].primaryBinding,
        "CCF primary room binding mismatch");

    Bytes laterRoom = ccfNamed("Later", "House");
    appendU32(laterRoom, 79U);
    Bytes secondSectionRoom = ccfNamed("SecondRoot", "House");
    appendU32(secondSectionRoom, 80U);
    Bytes firstUnusualSection;
    appendBytes(firstUnusualSection, ccfChunk(0x1199U, {}));
    appendBytes(
        firstUnusualSection,
        ccfChunk(0x1100U, laterRoom));
    Bytes unusualSections;
    appendBytes(
        unusualSections,
        ccfChunk(0x1000U, firstUnusualSection));
    appendBytes(
        unusualSections,
        ccfChunk(
            0x1000U,
            ccfChunk(0x1100U, secondSectionRoom)));
    const auto sectioned = airfix::assets::parseCcf(
        ccfDocument(unusualSections));
    require(
        sectioned.roomSections.size() == 2U &&
        sectioned.roomSections[0].firstPhysicalRoomIndex == 0U &&
        sectioned.roomSections[0].physicalRoomCount == 1U &&
        !sectioned.roomSections[0].firstDirectChildIsRoom &&
        sectioned.roomSections[1].firstPhysicalRoomIndex == 1U &&
        sectioned.roomSections[1].physicalRoomCount == 1U &&
        sectioned.roomSections[1].firstDirectChildIsRoom &&
        sectioned.rooms.size() == 2U &&
        !sectioned.rooms[0].primaryBinding &&
        sectioned.rooms[1].primaryBinding,
        "CCF multiple/non-leading room-section binding mismatch");

    auto invalid = bytes;
    invalid[10] = 17U;
    requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
    invalid = bytes;
    invalid[16] = 5U;
    requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
    Bytes truncatedRoom = ccfNamed("World", "House");
    truncatedRoom.push_back(0U);
    const auto malformedRoom = ccfDocument(
        ccfChunk(0x1000U, ccfChunk(0x1100U, truncatedRoom)));
    requireParseError([&] { (void)airfix::assets::parseCcf(malformedRoom); });

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
    require(
        material.properties2140 ==
            airfix::assets::CcfMaterialProperties2140{
                .firstVector = {1.25F, 2.5F, -3.75F},
                .secondVector = {4.5F, -5.25F, 6.75F},
                .scalar = 0.625F,
            },
        "CCF 0x2140 material properties mismatch");
    require(
        material.properties2150 ==
            airfix::assets::CcfMaterialProperties2150{
                .lightingMode = 2U,
                .gouraudShading = true,
                .blendMode = 3U,
            },
        "CCF 0x2150 material properties mismatch");
    require(material.flag2151 == true, "CCF 0x2151 flag mismatch");
    require(material.collisionMode2152 == 8U,
        "CCF 0x2152 collision mode mismatch");
    require(
        material.scalarProperties2152 ==
            std::array<float, 2>{0.5F, -0.25F},
        "CCF 0x2152 scalar properties mismatch");
    require(materialMetadata.topLevelChunks[0].directChildren[0].directChildren.size() == 5U,
        "CCF material property count mismatch");

    const auto missingProperty = makeMaterialCcf({.includeScalars = false});
    const auto missingPropertyMetadata =
        airfix::assets::parseCcf(missingProperty);
    require(missingPropertyMetadata.materials.size() == 1U,
        "CCF loader-compatible missing property was rejected");
    require(!missingPropertyMetadata.materials[0].collisionMode2152.has_value(),
        "absent CCF 0x2152 unexpectedly produced a collision mode");
    require(!missingPropertyMetadata.materials[0].scalarProperties2152.has_value(),
        "absent CCF 0x2152 unexpectedly produced scalar properties");
    const auto missingVisualProperties =
        makeMaterialCcf({.includeVisualProperties = false});
    const auto missingVisualMetadata =
        airfix::assets::parseCcf(missingVisualProperties);
    require(
        !missingVisualMetadata.materials[0].properties2140.has_value() &&
            !missingVisualMetadata.materials[0].properties2150.has_value() &&
            !missingVisualMetadata.materials[0].flag2151.has_value(),
        "absent CCF visual material properties were synthesized by the parser");
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
        meshMetadata.blueprints[0].meshIndex == 0U &&
        !meshMetadata.blueprints[0].auxiliaryReference.has_value() &&
        meshMetadata.blueprints[0].parentReference == mesh.linkReference &&
        meshMetadata.blueprints[0].authoredTransform.position == mesh.position &&
        meshMetadata.blueprints[0].authoredTransform.rawScalar == mesh.scalar &&
        meshMetadata.blueprints[0].authoredTransform.orientation == mesh.orientation,
        "CCF mesh blueprint metadata mismatch");
    const auto nonMeshMetadata = airfix::assets::parseCcf(makeNonMeshBlueprintCcf());
    require(nonMeshMetadata.meshes.empty(), "unexpected geometry for non-mesh blueprints");
    require(nonMeshMetadata.blueprints.size() == 2U,
        "CCF non-mesh blueprint count mismatch");
    require(nonMeshMetadata.blueprints[0].kind ==
        airfix::assets::CcfBlueprintKind::nullNode &&
        nonMeshMetadata.blueprints[0].name == "Null" &&
        nonMeshMetadata.blueprints[0].reference == 11U &&
        nonMeshMetadata.blueprints[0].auxiliaryReference == 101U &&
        nonMeshMetadata.blueprints[0].parentReference == 201U &&
        nonMeshMetadata.blueprints[0].authoredTransform.position ==
            airfix::assets::CcfVector3{1.0F, 2.0F, 3.0F} &&
        nonMeshMetadata.blueprints[0].authoredTransform.rawScalar == 4.0F &&
        nonMeshMetadata.blueprints[0].authoredTransform.orientation ==
            std::array<airfix::assets::CcfVector3, 3>{{
                {0.0F, 1.0F, 0.0F},
                {1.0F, 0.0F, 0.0F},
                {0.0F, 0.0F, -1.0F},
            }} &&
        !nonMeshMetadata.blueprints[0].meshIndex.has_value(),
        "CCF null blueprint mismatch");
    require(nonMeshMetadata.blueprints[1].kind ==
        airfix::assets::CcfBlueprintKind::light &&
        nonMeshMetadata.blueprints[1].name == "Light" &&
        nonMeshMetadata.blueprints[1].reference == 12U &&
        nonMeshMetadata.blueprints[1].auxiliaryReference == 102U &&
        nonMeshMetadata.blueprints[1].parentReference == 202U &&
        nonMeshMetadata.blueprints[1].authoredTransform.position ==
            airfix::assets::CcfVector3{-1.0F, -2.0F, -3.0F} &&
        nonMeshMetadata.blueprints[1].authoredTransform.rawScalar == 0.5F &&
        nonMeshMetadata.blueprints[1].authoredTransform.orientation ==
            std::array<airfix::assets::CcfVector3, 3>{{
                {0.5F, 0.0F, 0.0F},
                {0.0F, 0.5F, 0.0F},
                {0.0F, 0.0F, 0.5F},
            }},
        "CCF light blueprint mismatch");

    for (const auto blueprintId : {0x4200U, 0x4300U}) {
        for (const auto retainedFixedTailBytes : {
                11U,
                12U + 17U,
                12U + 18U + 3U,
                12U + 18U + 4U + 65U,
            }) {
            const auto truncated = makeTruncatedNonMeshBlueprintCcf(
                static_cast<std::uint16_t>(blueprintId), retainedFixedTailBytes);
            requireParseError([&] { (void)airfix::assets::parseCcf(truncated); });
        }
    }

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

void testCcfRoomSpatialMetadata() {
    Bytes rootTrailing = ccfBspPolygon(1.0F, 101U, 201U);
    appendBytes(rootTrailing, ccfChunk(0xF0CFU, Bytes{0xAAU, 0x55U}));
    const auto grandchild = ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {3.0F, 0.0F, 0.0F},
        {0.0F, 3.0F, 0.0F},
        ccfBspPolygon(31.0F, 303U, 403U));
    const auto childA = ccfBspNode(
        7U,
        grandchild,
        0U,
        std::nullopt,
        {2.0F, 0.0F, 0.0F},
        {0.0F, 2.0F, 0.0F});
    const auto childB = ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {4.0F, 0.0F, 0.0F},
        {0.0F, 4.0F, 0.0F});
    const auto root = ccfBspNode(
        2U,
        childA,
        0x80000000U,
        childB,
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        rootTrailing);
    const auto secondRoot = ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {5.0F, 0.0F, 0.0F},
        {0.0F, 5.0F, 0.0F});
    const auto portalRoot = ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {6.0F, 0.0F, 0.0F},
        {0.0F, 6.0F, 0.0F});
    const auto directRoot = ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {7.0F, 0.0F, 0.0F},
        {0.0F, 7.0F, 0.0F});

    Bytes staticWrapperPayload = root;
    appendBytes(staticWrapperPayload, ccfChunk(0x12FFU, Bytes{0x42U}));
    appendBytes(staticWrapperPayload, secondRoot);
    Bytes children = ccfFog(
        0xDEADBEEFU, -5.0F, 42.5F, {0.1F, 0.2F, 0.3F});
    appendBytes(children, ccfChunk(0x1200U, staticWrapperPayload));
    appendBytes(children, ccfChunk(0x1201U, portalRoot));
    appendBytes(children, directRoot);
    appendBytes(children, ccfChunk(0x13FFU, Bytes{0x11U}));

    const auto metadata = airfix::assets::parseCcf(ccfRoomDocument(children));
    require(metadata.rooms.size() == 1U, "CCF spatial room count mismatch");
    const auto& room = metadata.rooms[0];
    require(room.fog.has_value() &&
            room.fog->enabledRaw == 0xDEADBEEFU &&
            room.fog->first == -5.0F &&
            room.fog->second == 42.5F &&
            room.fog->color == airfix::assets::CcfVector3{0.1F, 0.2F, 0.3F},
        "CCF fog fields mismatch");
    require(room.staticBspTrees.size() == 3U &&
            room.portalBspTrees.size() == 1U,
        "CCF BSP tree classification or multi-root count mismatch");
    require(room.directChildren.size() == 5U &&
            room.directChildren[1].id == 0x1200U &&
            room.directChildren[1].directChildren.size() == 3U &&
            room.directChildren[1].directChildren[1].id == 0x12FFU &&
            room.directChildren.back().id == 0x13FFU,
        "CCF room/wrapper unknown children were not retained");

    const auto& tree = room.staticBspTrees[0];
    require(tree.kind == airfix::assets::CcfBspTreeKind::staticTree &&
            tree.source == airfix::assets::CcfBspTreeSource::wrapped &&
            tree.rootNodeIndex == 0U && tree.nodes.size() == 4U,
        "CCF static BSP tree header mismatch");
    require(tree.nodes[0].childAPresenceRaw == 2U &&
            tree.nodes[0].childBPresenceRaw == 0x80000000U &&
            tree.nodes[0].childAIndex == 1U &&
            tree.nodes[0].childBIndex == 3U &&
            tree.nodes[1].childAPresenceRaw == 7U &&
            tree.nodes[1].childAIndex == 2U &&
            !tree.nodes[1].childBIndex.has_value() &&
            !tree.nodes[2].childAIndex.has_value() &&
            !tree.nodes[2].childBIndex.has_value(),
        "CCF BSP non-boolean presence or preorder links mismatch");
    require(tree.nodes[0].offset < tree.nodes[1].offset &&
            tree.nodes[1].offset < tree.nodes[2].offset &&
            tree.nodes[2].offset < tree.nodes[3].offset &&
            tree.nodes[1].splitNormal ==
                airfix::assets::CcfVector3{2.0F, 0.0F, 0.0F} &&
            tree.nodes[3].pointOnPlane ==
                airfix::assets::CcfVector3{0.0F, 4.0F, 0.0F},
        "CCF BSP physical/preorder mapping or plane fields mismatch");
    require(tree.polygons.size() == 2U &&
            tree.nodes[0].polygonIndices == std::vector<std::size_t>{0U} &&
            tree.nodes[2].polygonIndices == std::vector<std::size_t>{1U} &&
            tree.nodes[0].trailingChildren.size() == 2U &&
            tree.nodes[0].trailingChildren[1].id == 0xF0CFU,
        "CCF BSP polygon arena or unknown trailing descriptor mismatch");
    const auto& polygon = tree.polygons[0];
    require(polygon.faceCross ==
                airfix::assets::CcfVector3{1.0F, 2.0F, 3.0F} &&
            polygon.faceNormal ==
                airfix::assets::CcfVector3{4.0F, 5.0F, 6.0F} &&
            polygon.point0 ==
                airfix::assets::CcfVector3{7.0F, 8.0F, 9.0F} &&
            polygon.edge01 ==
                airfix::assets::CcfVector3{10.0F, 11.0F, 12.0F} &&
            polygon.edge12 ==
                airfix::assets::CcfVector3{13.0F, 14.0F, 15.0F} &&
            polygon.polygonIndex == 101U &&
            polygon.placedObjectReference == 201U,
        "CCF BSP polygon fields mismatch");
    require(room.staticBspTrees[1].source ==
                airfix::assets::CcfBspTreeSource::wrapped &&
            room.staticBspTrees[2].source ==
                airfix::assets::CcfBspTreeSource::direct &&
            room.portalBspTrees[0].kind ==
                airfix::assets::CcfBspTreeKind::portalTree &&
            room.portalBspTrees[0].source ==
                airfix::assets::CcfBspTreeSource::wrapped,
        "CCF BSP source classification mismatch");

    Bytes emptyWrappers = ccfChunk(0x1200U, {});
    appendBytes(emptyWrappers, ccfChunk(0x1201U, {}));
    const auto empty = airfix::assets::parseCcf(
        ccfRoomDocument(emptyWrappers));
    require(empty.rooms[0].staticBspTrees.empty() &&
            empty.rooms[0].portalBspTrees.empty() &&
            empty.rooms[0].directChildren[0].directChildren.empty() &&
            empty.rooms[0].directChildren[1].directChildren.empty(),
        "CCF empty BSP wrappers were not accepted and indexed");

    const auto noStaticWrapper = airfix::assets::parseCcf(
        ccfRoomDocument(ccfFog(0U, 0.0F, 0.0F, {0.0F, 0.0F, 0.0F})));
    require(noStaticWrapper.rooms[0].staticBspTrees.empty() &&
            noStaticWrapper.rooms[0].portalBspTrees.empty(),
        "CCF room without a static wrapper was not accepted");
}

void testCcfRoomSpatialFailures() {
    const auto requireRoomError = [](const Bytes& children) {
        const auto invalid = ccfRoomDocument(children);
        requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
    };

    requireRoomError(ccfChunk(0x1101U, Bytes{0x01U, 0x02U}));
    Bytes duplicateFog = ccfFog(1U, 2.0F, 3.0F, {4.0F, 5.0F, 6.0F});
    appendBytes(duplicateFog, ccfFog(
        0U, 7.0F, 8.0F, {9.0F, 10.0F, 11.0F}));
    requireRoomError(duplicateFog);
    auto invalidFogVector = ccfFog(
        1U, 2.0F, 3.0F, {4.0F, 5.0F, 6.0F});
    invalidFogVector[18U] = 0x40U;
    requireRoomError(invalidFogVector);

    requireRoomError(ccfChunk(0x1200U, Bytes(5U, 0U)));
    requireRoomError(ccfBspNode(1U));
    requireRoomError(ccfBspNode(
        1U, ccfChunk(0xF0C2U, {}), 0U, std::nullopt));
    auto invalidSplitVector = ccfBspNode();
    invalidSplitVector[14U] = 0x30U;
    requireRoomError(invalidSplitVector);
    auto truncatedSplitVector = ccfBspNode();
    truncatedSplitVector[16U] = 17U;
    requireRoomError(truncatedSplitVector);

    requireRoomError(ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        ccfChunk(0xF0C1U, Bytes(97U, 0U))));
    auto invalidPolygonVector = ccfBspPolygon(1.0F, 2U, 3U);
    invalidPolygonVector[6U] = 0x30U;
    requireRoomError(ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        invalidPolygonVector));

    const auto sentinel = std::bit_cast<float>(0x7FC00001U);
    const auto nanMetadata = airfix::assets::parseCcf(ccfRoomDocument(
        ccfBspNode(
            0U,
            std::nullopt,
            0U,
            std::nullopt,
            {sentinel, 0.0F, 0.0F},
            {0.0F, sentinel, 0.0F},
            ccfBspPolygon(sentinel, 4U, 5U))));
    const auto& nanTree = nanMetadata.rooms[0].staticBspTrees[0];
    require(std::isnan(nanTree.nodes[0].splitNormal[0]) &&
            std::bit_cast<std::uint32_t>(nanTree.nodes[0].splitNormal[0]) ==
                0x7FC00001U &&
            std::isnan(nanTree.polygons[0].faceCross[0]) &&
            std::bit_cast<std::uint32_t>(nanTree.polygons[0].faceCross[0]) ==
                0x7FC00001U,
        "CCF BSP NaN sentinel bits were rejected or normalized");

    auto tooDeep = ccfBspNode();
    for (std::size_t depth = 0U; depth < 1'024U; ++depth) {
        tooDeep = ccfBspNode(1U, tooDeep);
    }
    requireRoomError(tooDeep);

    // A balanced 131,071-node payload exceeds the explicit 100k node bound.
    // The shared descriptor budget is intentionally stronger (each node also
    // owns two F040 descriptors), so the parser may reject it even earlier.
    auto tooManyNodes = ccfBspNode();
    for (std::size_t level = 0U; level < 16U; ++level) {
        tooManyNodes = ccfBspNode(
            1U, tooManyNodes, 2U, tooManyNodes);
    }
    requireRoomError(tooManyNodes);

    // Every polygon consumes its F0C1 plus five vector descriptors. This is
    // the smallest useful polygon-shaped bomb that exhausts the 250k shared
    // descriptor cap, before the separate polygon arena can grow unbounded.
    constexpr std::size_t kPolygonDescriptorBombCount = 41'667U;
    const auto polygonDescriptor = ccfBspPolygon(1.0F, 2U, 3U);
    Bytes tooManyPolygonDescriptors;
    tooManyPolygonDescriptors.reserve(
        kPolygonDescriptorBombCount * polygonDescriptor.size());
    for (std::size_t index = 0U;
         index < kPolygonDescriptorBombCount;
         ++index) {
        appendBytes(tooManyPolygonDescriptors, polygonDescriptor);
    }
    requireRoomError(ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        tooManyPolygonDescriptors));
}

void testCcfPlacedNodes() {
    Bytes objectChildren = ccfU32Property(0xF0B0U, 1U);
    appendBytes(objectChildren, ccfU32Property(0xF0B1U, 2U));
    appendBytes(objectChildren, ccfU32Property(0x4501U, 3U));
    const auto dynamicChild = ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {0.0F, 1.0F, 0.0F},
        {2.0F, 3.0F, 4.0F},
        ccfBspPolygon(20.0F, 6U, 100U));
    Bytes dynamicBspPayload = ccfChunk(0x41FFU, Bytes{0xA5U});
    appendBytes(dynamicBspPayload, ccfBspNode(
        1U,
        dynamicChild,
        0U,
        std::nullopt,
        {1.0F, 0.0F, 0.0F},
        {5.0F, 6.0F, 7.0F},
        ccfBspPolygon(1.0F, 5U, 100U)));
    appendBytes(dynamicBspPayload, ccfBspNode(
        0U,
        std::nullopt,
        0U,
        std::nullopt,
        {0.0F, 0.0F, 1.0F},
        {8.0F, 9.0F, 10.0F},
        ccfBspPolygon(40.0F, 7U, 100U)));
    appendBytes(dynamicBspPayload, ccfChunk(0x41FEU, {}));
    appendBytes(objectChildren, ccfChunk(0x4101U, dynamicBspPayload));
    appendBytes(objectChildren, ccfChunk(0x4999U, Bytes{0xAAU}));

    Bytes nullBlock;
    appendU32(nullBlock, 3U);
    nullBlock.insert(nullBlock.end(), {0x10U, 0x20U, 0x30U});
    Bytes nullChildren = ccfChunk(0x4210U, nullBlock);
    appendBytes(nullChildren, ccfU32Property(0x4500U, 44U));

    Bytes lightChildren = ccfPlacedLight4310(std::string("lightmap.gti"));
    appendBytes(lightChildren, ccfPlacedLight4320(
        std::array<std::string, 2>{"flare.gti", "glow.gti"}));
    appendBytes(lightChildren, ccfPlacedLight4330());
    appendBytes(lightChildren, ccfU32Property(0xF0B0U, 55U));
    appendBytes(lightChildren, ccfChunk(0x43FFU, Bytes{0xBBU, 0xCCU}));

    Bytes records = makePlacedRecord(0x4100U, objectChildren);
    appendBytes(records, makePlacedRecord(0x4200U, nullChildren, true));
    appendBytes(records, makePlacedRecord(0x4300U, lightChildren));
    const auto bytes = makePlacedCcf(records);
    const auto metadata = airfix::assets::parseCcf(bytes);
    require(metadata.placedNodes.size() == 3U,
        "CCF placed-node count mismatch");
    require(metadata.placedNodes[0].kind == airfix::assets::CcfPlacedNodeKind::object &&
        metadata.placedNodes[1].kind == airfix::assets::CcfPlacedNodeKind::nullNode &&
        metadata.placedNodes[2].kind == airfix::assets::CcfPlacedNodeKind::light,
        "CCF placed-node physical order mismatch");

    const auto& object = metadata.placedNodes[0];
    require(object.name == "Object" && object.prefix == "Room" &&
        object.currentReference == 100U && object.roomReference == 300U &&
        object.parentReference == 400U &&
        object.transform.position == airfix::assets::CcfVector3{1.0F, 2.0F, 3.0F} &&
        object.transform.rawScalar == 4.0F &&
        std::holds_alternative<std::array<airfix::assets::CcfVector3, 3>>(
            object.transform.orientation),
        "CCF placed object common fields mismatch");
    const auto& objectData = std::get<airfix::assets::CcfPlacedObjectMetadata>(
        object.data);
    require(objectData.meshReference == 200U && objectData.rawFlag == 0x7EU &&
        objectData.portalType == 500U && objectData.portalRoomReference == 600U &&
        objectData.propertyF0B0 == 1U && objectData.propertyF0B1 == 2U &&
        objectData.value4501 == 3U && objectData.bsp4101.has_value() &&
        objectData.bsp4101->directChildren.size() == 4U &&
        objectData.bsp4101->directChildren.front().id == 0x41FFU &&
        objectData.bsp4101->directChildren.back().id == 0x41FEU &&
        objectData.dynamicBspTrees.size() == 2U,
        "CCF placed object variant fields mismatch");
    const auto& firstDynamicTree = objectData.dynamicBspTrees[0];
    require(
        firstDynamicTree.kind ==
                airfix::assets::CcfBspTreeKind::dynamicObjectTree &&
            firstDynamicTree.source ==
                airfix::assets::CcfBspTreeSource::placedObject4101 &&
            firstDynamicTree.nodes.size() == 2U &&
            firstDynamicTree.polygons.size() == 2U &&
            firstDynamicTree.nodes[0].childAPresenceRaw == 1U &&
            firstDynamicTree.nodes[0].childAIndex == 1U &&
            !firstDynamicTree.nodes[0].childBIndex.has_value() &&
            firstDynamicTree.nodes[0].splitNormal ==
                airfix::assets::CcfVector3{1.0F, 0.0F, 0.0F} &&
            firstDynamicTree.polygons[0].polygonIndex == 5U &&
            firstDynamicTree.polygons[0].placedObjectReference == 100U &&
            firstDynamicTree.polygons[1].polygonIndex == 6U &&
            firstDynamicTree.polygons[1].placedObjectReference == 100U,
        "CCF placed object first dynamic BSP tree mismatch");
    const auto& secondDynamicTree = objectData.dynamicBspTrees[1];
    require(
        secondDynamicTree.nodes.size() == 1U &&
            secondDynamicTree.polygons.size() == 1U &&
            secondDynamicTree.nodes[0].splitNormal ==
                airfix::assets::CcfVector3{0.0F, 0.0F, 1.0F} &&
            secondDynamicTree.polygons[0].polygonIndex == 7U,
        "CCF placed object physical dynamic BSP root order mismatch");
    require(object.directChildren.size() == 5U &&
        object.directChildren.back().id == 0x4999U,
        "CCF placed object unknown child was not retained");

    const auto emptyDynamic = airfix::assets::parseCcf(
        makePlacedCcf(makePlacedRecord(
            0x4100U, ccfChunk(0x4101U, {}))));
    const auto& emptyDynamicData =
        std::get<airfix::assets::CcfPlacedObjectMetadata>(
            emptyDynamic.placedNodes[0].data);
    require(
        emptyDynamicData.bsp4101.has_value() &&
            emptyDynamicData.bsp4101->directChildren.empty() &&
            emptyDynamicData.dynamicBspTrees.empty(),
        "CCF placed object empty dynamic BSP wrapper mismatch");

    const auto& nullNode = metadata.placedNodes[1];
    require(std::holds_alternative<airfix::assets::CcfVector3>(
        nullNode.transform.orientation) &&
        std::get<airfix::assets::CcfVector3>(nullNode.transform.orientation) ==
            airfix::assets::CcfVector3{0.25F, 0.5F, 0.75F},
        "CCF placed alternate orientation mismatch");
    const auto& nullData = std::get<airfix::assets::CcfPlacedNullMetadata>(
        nullNode.data);
    require(nullData.block4210.has_value() && nullData.block4210->length == 3U &&
        nullData.value4500 == 44U,
        "CCF placed null fields mismatch");
    const auto blockOffset = static_cast<std::size_t>(nullData.block4210->offset);
    require(blockOffset + 3U <= bytes.size() && bytes[blockOffset] == 0x10U &&
        bytes[blockOffset + 1U] == 0x20U && bytes[blockOffset + 2U] == 0x30U,
        "CCF placed null zero-copy block range mismatch");

    const auto& light = metadata.placedNodes[2];
    const auto& lightData = std::get<airfix::assets::CcfPlacedLightMetadata>(
        light.data);
    require(lightData.property4310.has_value() &&
        lightData.property4310->first == 1.25F &&
        lightData.property4310->vector ==
            airfix::assets::CcfVector3{0.1F, 0.2F, 0.3F} &&
        lightData.property4310->second == 2.25F &&
        lightData.property4310->third == 3.25F &&
        lightData.property4310->texture == "lightmap.gti",
        "CCF placed light 0x4310 mismatch");
    require(lightData.property4320.has_value() &&
        lightData.property4320->values == std::array<float, 4>{
            4.25F, 5.25F, 6.25F, 7.25F} &&
        lightData.property4320->textures.has_value() &&
        (*lightData.property4320->textures)[0] == "flare.gti" &&
        (*lightData.property4320->textures)[1] == "glow.gti",
        "CCF placed light 0x4320 mismatch");
    require(lightData.property4330.has_value() &&
        lightData.property4330->vector ==
            airfix::assets::CcfVector3{8.25F, 9.25F, 10.25F} &&
        lightData.property4330->first == 11.25F &&
        lightData.property4330->second == 12.25F &&
        lightData.propertyF0B0 == 55U && light.directChildren.back().id == 0x43FFU,
        "CCF placed light 0x4330 or retained child mismatch");

    Bytes noStringProperties = ccfPlacedLight4310(std::nullopt);
    appendBytes(noStringProperties, ccfPlacedLight4320(std::nullopt));
    const auto noStrings = airfix::assets::parseCcf(
        makePlacedCcf(makePlacedRecord(0x4300U, noStringProperties)));
    const auto& noStringData = std::get<airfix::assets::CcfPlacedLightMetadata>(
        noStrings.placedNodes[0].data);
    require(noStringData.property4310.has_value() &&
        !noStringData.property4310->texture.has_value() &&
        noStringData.property4320.has_value() &&
        !noStringData.property4320->textures.has_value(),
        "CCF placed light zero-string variants mismatch");

    const auto requirePlacedError = [](const std::uint16_t id, const Bytes& children,
                                      const bool alternate = false,
                                      const Bytes* orientation = nullptr) {
        const auto invalid = makePlacedCcf(
            makePlacedRecord(id, children, alternate, orientation));
        requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
    };

    for (const auto id : {0x4100U, 0x4200U, 0x4300U}) {
        auto truncated = makePlacedRecord(static_cast<std::uint16_t>(id));
        truncated.pop_back();
        truncated[2] = static_cast<std::uint8_t>(truncated.size());
        truncated[3] = static_cast<std::uint8_t>(truncated.size() >> 8U);
        truncated[4] = static_cast<std::uint8_t>(truncated.size() >> 16U);
        truncated[5] = static_cast<std::uint8_t>(truncated.size() >> 24U);
        const auto invalid = makePlacedCcf(truncated);
        requireParseError([&] { (void)airfix::assets::parseCcf(invalid); });
    }

    Bytes invalidOrientationChild = ccfChunk(
        0xF070U, ccfVector3(0xF030U, 1.0F, 2.0F, 3.0F));
    requirePlacedError(0x4200U, {}, false, &invalidOrientationChild);
    Bytes twoOrientationChildren = ccfVector3(0xF040U, 1.0F, 2.0F, 3.0F);
    appendBytes(twoOrientationChildren, ccfVector3(0xF040U, 4.0F, 5.0F, 6.0F));
    auto invalidOrientationCount = ccfChunk(0xF070U, twoOrientationChildren);
    requirePlacedError(0x4300U, {}, false, &invalidOrientationCount);

    Bytes badObjectU32 = ccfChunk(0xF0B0U, Bytes(3U, 0U));
    requirePlacedError(0x4100U, badObjectU32);
    Bytes duplicateObject = ccfU32Property(0xF0B1U, 1U);
    appendBytes(duplicateObject, ccfU32Property(0xF0B1U, 2U));
    requirePlacedError(0x4100U, duplicateObject);
    Bytes duplicateBsp = ccfChunk(0x4101U, {});
    appendBytes(duplicateBsp, ccfChunk(0x4101U, {}));
    requirePlacedError(0x4100U, duplicateBsp);
    requirePlacedError(0x4100U, ccfChunk(0x4101U, Bytes{
        0xFFU, 0xFFU, 0x01U, 0x00U, 0x00U, 0x00U, 0xA5U,
    }));
    requirePlacedError(0x4100U, ccfChunk(
        0x4101U,
        ccfBspNode(
            1U,
            ccfChunk(0xF0C1U, Bytes(98U, 0U)))));
    requirePlacedError(0x4100U, ccfChunk(
        0x4101U,
        ccfBspNode(
            0U,
            std::nullopt,
            0U,
            std::nullopt,
            {1.0F, 0.0F, 0.0F},
            {0.0F, 1.0F, 0.0F},
            ccfChunk(0xF0C1U, Bytes(97U, 0U)))));
    auto tooDeepDynamic = ccfBspNode();
    for (std::size_t depth = 0U; depth < 1'024U; ++depth) {
        tooDeepDynamic = ccfBspNode(1U, tooDeepDynamic);
    }
    requirePlacedError(
        0x4100U, ccfChunk(0x4101U, tooDeepDynamic));

    Bytes shortNullBlock;
    appendU32(shortNullBlock, 4U);
    shortNullBlock.insert(shortNullBlock.end(), {1U, 2U, 3U});
    requirePlacedError(0x4200U, ccfChunk(0x4210U, shortNullBlock));
    Bytes oversizedNullBlock;
    appendU32(oversizedNullBlock, std::numeric_limits<std::uint32_t>::max());
    requirePlacedError(0x4200U, ccfChunk(0x4210U, oversizedNullBlock));
    Bytes duplicateNull = ccfU32Property(0x4500U, 1U);
    appendBytes(duplicateNull, ccfU32Property(0x4500U, 2U));
    requirePlacedError(0x4200U, duplicateNull);

    Bytes oneTexture4320;
    for (const auto value : {1.0F, 2.0F, 3.0F, 4.0F}) {
        appendFloat(oneTexture4320, value);
    }
    appendBytes(oneTexture4320, ccfString("only-one.gti"));
    requirePlacedError(0x4300U, ccfChunk(0x4320U, oneTexture4320));
    requirePlacedError(0x4300U, ccfChunk(0x4310U, Bytes(29U, 0U)));
    requirePlacedError(0x4300U, ccfChunk(0x4320U, Bytes(15U, 0U)));
    requirePlacedError(0x4300U, ccfChunk(0x4330U, Bytes(25U, 0U)));
    requirePlacedError(0x4300U, ccfPlacedLight4310(std::string(4097U, 'X')));
    Bytes duplicateLight = ccfPlacedLight4310(std::nullopt);
    appendBytes(duplicateLight, ccfPlacedLight4310(std::string("again.gti")));
    requirePlacedError(0x4300U, duplicateLight);
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
        testCcfRoomSpatialMetadata();
        testCcfRoomSpatialFailures();
        testCcfPlacedNodes();
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
