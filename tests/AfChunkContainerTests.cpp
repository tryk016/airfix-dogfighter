#include "airfix/assets/AfChunkContainer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendU32(Bytes& bytes, const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8U)));
    }
}

void appendFloat(Bytes& bytes, const float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

void appendI16(Bytes& bytes, const std::int16_t value) {
    const auto bits = std::bit_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(bits));
    bytes.push_back(static_cast<std::uint8_t>(bits >> 8U));
}

template <std::size_t Count>
[[nodiscard]] Bytes floatPayload(const std::array<float, Count>& values) {
    Bytes bytes;
    bytes.reserve(Count * sizeof(float));
    for (const auto value : values) {
        appendFloat(bytes, value);
    }
    return bytes;
}

void appendChunk(Bytes& bytes, const std::uint32_t id, const Bytes& payload) {
    appendU32(bytes, id);
    appendU32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

[[nodiscard]] airfix::assets::AfChunk appendDescribedChunk(
    Bytes& bytes,
    const std::uint32_t id,
    const Bytes& payload) {
    const airfix::assets::AfChunk descriptor{
        .id = id,
        .payloadSize = static_cast<std::uint32_t>(payload.size()),
        .payloadOffset = static_cast<std::uint64_t>(bytes.size()) + 16U,
    };
    appendChunk(bytes, id, payload);
    return descriptor;
}

[[nodiscard]] Bytes stringPayload(const std::string_view value) {
    Bytes bytes(value.begin(), value.end());
    bytes.push_back(0U);
    return bytes;
}

[[nodiscard]] Bytes roomPayload(
    const std::uint8_t id,
    const std::string_view internalName,
    const std::string_view localizedName,
    const std::uint8_t floor) {
    Bytes bytes{id};
    const auto internal = stringPayload(internalName);
    const auto localized = stringPayload(localizedName);
    bytes.insert(bytes.end(), internal.begin(), internal.end());
    bytes.insert(bytes.end(), localized.begin(), localized.end());
    bytes.push_back(floor);
    return bytes;
}

[[nodiscard]] Bytes placementPayload(
    const std::array<float, 6>& transform,
    const std::string_view room,
    const std::string_view objectPath) {
    auto bytes = floatPayload(transform);
    const auto roomBytes = stringPayload(room);
    const auto objectBytes = stringPayload(objectPath);
    bytes.insert(bytes.end(), roomBytes.begin(), roomBytes.end());
    bytes.insert(bytes.end(), objectBytes.begin(), objectBytes.end());
    return bytes;
}

[[nodiscard]] Bytes wrapContainer(const std::uint32_t root, const Bytes& chunks) {
    Bytes bytes;
    appendU32(bytes, root);
    appendU32(bytes, static_cast<std::uint32_t>(chunks.size()));
    bytes.insert(bytes.end(), chunks.begin(), chunks.end());
    return bytes;
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireDescriptor(
    const Bytes& containerBytes,
    const airfix::assets::AfChunk& actual,
    const airfix::assets::AfChunk& expected,
    const Bytes& expectedPayload,
    const std::string& message) {
    const auto payload = airfix::assets::afChunkPayload(containerBytes, actual);
    require(actual.id == expected.id &&
            actual.payloadSize == expected.payloadSize &&
            actual.payloadOffset == expected.payloadOffset &&
            payload.size() == expectedPayload.size() &&
            std::equal(payload.begin(), payload.end(), expectedPayload.begin()),
        message);
}

void requireParseError(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const airfix::assets::ParseError&) {
        return;
    }
    throw std::runtime_error("expected AfChunkContainer ParseError");
}

void testGenericContainer() {
    Bytes chunks;
    appendChunk(chunks, airfix::assets::fourCC('O', 'D', 'D', '3'), Bytes{1U, 2U, 3U});
    appendChunk(chunks, airfix::assets::fourCC('Z', 'E', 'R', 'O'), {});
    appendChunk(chunks, airfix::assets::fourCC('O', 'D', 'D', '3'), Bytes{4U});
    const auto bytes = wrapContainer(airfix::assets::kAfObjectRoot, chunks);

    const auto container = airfix::assets::parseAfChunkContainer(bytes, 3U);
    require(container.rootId == airfix::assets::kAfObjectRoot, "root ID mismatch");
    require(container.payloadSize == chunks.size(), "root payload size mismatch");
    require(container.chunks.size() == 3U, "child count mismatch");
    require(container.chunks[0].id == container.chunks[2].id,
        "duplicate chunk order was not preserved");
    const auto oddPayload = airfix::assets::afChunkPayload(bytes, container.chunks[0]);
    require(oddPayload.size() == 3U &&
            std::equal(oddPayload.begin(), oddPayload.end(), bytes.begin() + 16),
        "odd-sized chunk payload mismatch");
    require(airfix::assets::afChunkPayload(bytes, container.chunks[1]).empty(),
        "zero-sized chunk was not preserved");

    require(airfix::assets::parseAfChunkContainer(
                wrapContainer(airfix::assets::kAfPathRoot, {}), 0U).chunks.empty(),
        "empty container failed");
    requireParseError([&] { (void)airfix::assets::parseAfChunkContainer(bytes, 2U); });
}

void testMalformedContainers() {
    requireParseError([] {
        (void)airfix::assets::parseAfChunkContainer(Bytes{1U, 2U, 3U}, 4U);
    });

    auto wrongRootSize = wrapContainer(airfix::assets::kAfObjectRoot, {});
    wrongRootSize[4] = 1U;
    requireParseError([&] {
        (void)airfix::assets::parseAfChunkContainer(wrongRootSize, 4U);
    });

    Bytes shortHeader{1U, 2U, 3U, 4U};
    const auto truncatedHeader = wrapContainer(airfix::assets::kAfObjectRoot, shortHeader);
    requireParseError([&] {
        (void)airfix::assets::parseAfChunkContainer(truncatedHeader, 4U);
    });

    Bytes oversizedChild;
    appendU32(oversizedChild, airfix::assets::fourCC('B', 'A', 'D', '!'));
    appendU32(oversizedChild, 100U);
    const auto truncatedPayload = wrapContainer(
        airfix::assets::kAfObjectRoot, oversizedChild);
    requireParseError([&] {
        (void)airfix::assets::parseAfChunkContainer(truncatedPayload, 4U);
    });
}

void testObjectDefinition() {
    Bytes chunks;
    appendChunk(chunks, airfix::assets::fourCC('T', 'Y', 'P', 'E'),
        stringPayload("accessory"));
    appendChunk(chunks, airfix::assets::fourCC('N', 'A', 'M', 'E'),
        stringPayload("@1234"));
    appendChunk(chunks, airfix::assets::fourCC('T', 'E', 'X', 'U'),
        stringPayload("Graphics\\textures\\"));
    appendChunk(chunks, airfix::assets::fourCC('C', 'C', 'F', 'F'),
        stringPayload("Graphics\\Synthetic\\model.ccf"));
    appendChunk(chunks, airfix::assets::fourCC('M', 'E', 'S', 'H'),
        stringPayload("thirdperson"));
    Bytes gravity;
    appendFloat(gravity, 1.0F);
    appendFloat(gravity, -2.0F);
    appendFloat(gravity, 0.5F);
    appendChunk(chunks, airfix::assets::fourCC('G', 'R', 'A', 'V'), gravity);
    appendChunk(chunks, airfix::assets::fourCC('H', 'I', 'D', 'E'), {});
    appendChunk(chunks, airfix::assets::fourCC('N', 'E', 'W', '!'), Bytes{0xAAU});

    const auto bytes = wrapContainer(airfix::assets::kAfObjectRoot, chunks);
    const auto definition = airfix::assets::parseObjectDefinition(bytes);
    require(definition.kind == airfix::assets::ObjectDefinitionKind::object,
        "object kind mismatch");
    require(definition.type == "accessory", "TYPE mismatch");
    require(definition.name == "@1234", "localization reference was changed");
    require(definition.meshName == "thirdperson", "MESH mismatch");
    require(definition.gravity.has_value(), "GRAV missing");
    require((*definition.gravity)[0] == 1.0F && (*definition.gravity)[1] == -2.0F &&
            (*definition.gravity)[2] == 0.5F,
        "GRAV values mismatch");
    require(definition.hidden, "HIDE marker missing");
    require(definition.unknownChunks.size() == 1U, "unknown chunk not preserved");

    const auto modelBytes = wrapContainer(airfix::assets::kAfModelRoot, {});
    require(airfix::assets::parseObjectDefinition(modelBytes).kind ==
            airfix::assets::ObjectDefinitionKind::model,
        "model kind mismatch");
}

void testMalformedObjectDefinition() {
    Bytes chunks;
    appendChunk(chunks, airfix::assets::fourCC('T', 'Y', 'P', 'E'),
        Bytes{'b', 'a', 'd'});
    const auto unterminated = wrapContainer(airfix::assets::kAfObjectRoot, chunks);
    requireParseError([&] { (void)airfix::assets::parseObjectDefinition(unterminated); });

    chunks.clear();
    appendChunk(chunks, airfix::assets::fourCC('H', 'I', 'D', 'E'), Bytes{0U});
    const auto invalidHidden = wrapContainer(airfix::assets::kAfObjectRoot, chunks);
    requireParseError([&] { (void)airfix::assets::parseObjectDefinition(invalidHidden); });

    chunks.clear();
    appendChunk(chunks, airfix::assets::fourCC('G', 'R', 'A', 'V'), Bytes(8U));
    const auto invalidGravity = wrapContainer(airfix::assets::kAfObjectRoot, chunks);
    requireParseError([&] { (void)airfix::assets::parseObjectDefinition(invalidGravity); });

    chunks.clear();
    appendChunk(chunks, airfix::assets::fourCC('N', 'A', 'M', 'E'), stringPayload("a"));
    appendChunk(chunks, airfix::assets::fourCC('N', 'A', 'M', 'E'), stringPayload("b"));
    const auto duplicateName = wrapContainer(airfix::assets::kAfObjectRoot, chunks);
    requireParseError([&] { (void)airfix::assets::parseObjectDefinition(duplicateName); });

    requireParseError([] {
        (void)airfix::assets::parseObjectDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, {}));
    });
}

void testWorldDefinition() {
    constexpr auto kTexu = airfix::assets::fourCC('T', 'E', 'X', 'U');
    constexpr auto kCcff = airfix::assets::fourCC('C', 'C', 'F', 'F');
    constexpr auto kBckd = airfix::assets::fourCC('B', 'C', 'K', 'D');
    constexpr auto kFlry = airfix::assets::fourCC('F', 'L', 'R', 'Y');
    constexpr auto kRoom = airfix::assets::fourCC('R', 'O', 'O', 'M');
    constexpr auto kLlst = airfix::assets::fourCC('L', 'L', 'S', 'T');
    constexpr auto kLdat = airfix::assets::fourCC('L', 'D', 'A', 'T');

    Bytes lineData;
    const auto firstLine = floatPayload(std::array{1.0F, 2.0F, 3.0F, 4.0F});
    const auto secondLine = floatPayload(std::array{-1.0F, -2.0F, -3.0F, -4.0F});
    lineData.insert(lineData.end(), firstLine.begin(), firstLine.end());
    lineData.insert(lineData.end(), secondLine.begin(), secondLine.end());

    Bytes chunks;
    appendChunk(chunks, kTexu, stringPayload("Textures\\World"));
    appendChunk(chunks, kCcff, stringPayload("Graphics\\World.ccf"));
    appendChunk(chunks, kBckd, stringPayload("Backdrop.gti"));
    const auto ignoredBackdropPayload = stringPayload("IgnoredBackdrop.gti");
    const auto ignoredBackdrop = appendDescribedChunk(
        chunks, kBckd, ignoredBackdropPayload);
    appendChunk(chunks, kFlry, floatPayload(std::array{
        0.0F, 1.0F, std::numeric_limits<float>::infinity()}));
    appendChunk(chunks, kRoom, roomPayload(7U, "internal", "@100", 3U));
    appendChunk(chunks, kRoom, roomPayload(8U, "", "", 4U));
    appendChunk(chunks, kLlst, stringPayload("boundaries"));
    appendChunk(chunks, kLlst, stringPayload("empty"));
    const Bytes unknownPayload{0x55U};
    const auto unknown = appendDescribedChunk(
        chunks, airfix::assets::fourCC('N', 'E', 'W', '!'), unknownPayload);
    appendChunk(chunks, kLdat, lineData);
    appendChunk(chunks, kLdat, {});

    const auto bytes = wrapContainer(airfix::assets::kAfHouseRoot, chunks);
    const auto definition = airfix::assets::parseWorldDefinition(bytes);
    require(definition.textureRoot == "Textures\\World", "world TEXU mismatch");
    require(definition.ccfPath == "Graphics\\World.ccf", "world CCFF mismatch");
    require(definition.backdrop == "Backdrop.gti", "world BCKD mismatch");
    require(definition.ignoredDuplicateChunks.size() == 1U,
        "world ignored BCKD duplicate was not retained");
    requireDescriptor(bytes, definition.ignoredDuplicateChunks[0],
        ignoredBackdrop, ignoredBackdropPayload,
        "world ignored BCKD descriptor mismatch");
    require(definition.floorYLevels.has_value() &&
            (*definition.floorYLevels)[1] == 1.0F &&
            (*definition.floorYLevels)[2] == std::numeric_limits<float>::infinity(),
        "world FLRY mismatch or non-finite value was rejected");
    require(definition.rooms.size() == 2U && definition.rooms[0].id == 7U &&
            definition.rooms[0].localizedName == "@100" &&
            definition.rooms[1].internalName.empty() && definition.rooms[1].floor == 4U,
        "world ROOM records mismatch");
    require(definition.lineLists.size() == 2U &&
            definition.lineLists[0].name == "boundaries" &&
            definition.lineLists[0].lines.size() == 2U &&
            definition.lineLists[0].lines[1][3] == -4.0F &&
            definition.lineLists[1].lines.empty(),
        "world LLST/LDAT pairing mismatch");
    require(definition.unknownChunks.size() == 1U,
        "world unknown chunk was not preserved");
    requireDescriptor(bytes, definition.unknownChunks[0], unknown, unknownPayload,
        "world unknown descriptor mismatch");
}

void testMalformedWorldDefinition() {
    constexpr auto kTexu = airfix::assets::fourCC('T', 'E', 'X', 'U');
    constexpr auto kBckd = airfix::assets::fourCC('B', 'C', 'K', 'D');
    constexpr auto kFlry = airfix::assets::fourCC('F', 'L', 'R', 'Y');
    constexpr auto kRoom = airfix::assets::fourCC('R', 'O', 'O', 'M');
    constexpr auto kLlst = airfix::assets::fourCC('L', 'L', 'S', 'T');
    constexpr auto kLdat = airfix::assets::fourCC('L', 'D', 'A', 'T');

    requireParseError([] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfPathRoot, {}));
    });

    Bytes chunks;
    appendChunk(chunks, kTexu, stringPayload("a"));
    appendChunk(chunks, kTexu, stringPayload("b"));
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kTexu, Bytes{'a', 0U, 'x'});
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kFlry, Bytes(8U));
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kBckd, stringPayload("first"));
    appendChunk(chunks, kBckd, Bytes{'n', 'o', '-', 'n', 'u', 'l'});
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    auto room = roomPayload(1U, "room", "@1", 2U);
    room.push_back(0xEEU);
    appendChunk(chunks, kRoom, room);
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kRoom, Bytes{1U, 'a', 0U, 'b', 2U});
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kLlst, stringPayload("orphan"));
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kLdat, {});
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kLlst, stringPayload("bad-lines"));
    appendChunk(chunks, kLdat, Bytes(15U));
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks));
    });

    chunks.clear();
    appendChunk(chunks, kRoom, roomPayload(1U, "a", "b", 0U));
    auto limits = airfix::assets::DefinitionParseLimits{};
    limits.maxRooms = 0U;
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks), limits);
    });
    limits = {};
    limits.maxStringBytes = 0U;
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks), limits);
    });

    chunks.clear();
    appendChunk(chunks, kLlst, stringPayload("one"));
    appendChunk(chunks, kLdat, floatPayload(std::array{1.0F, 2.0F, 3.0F, 4.0F}));
    limits = {};
    limits.maxLineLists = 0U;
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks), limits);
    });
    limits = {};
    limits.maxLineRecords = 0U;
    requireParseError([&] {
        (void)airfix::assets::parseWorldDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, chunks), limits);
    });
}

void testLevelDefinition() {
    constexpr auto kGccs = airfix::assets::fourCC('G', 'C', 'C', 'S');
    constexpr auto kHous = airfix::assets::fourCC('H', 'O', 'U', 'S');
    constexpr auto kObje = airfix::assets::fourCC('O', 'B', 'J', 'E');
    constexpr auto kModl = airfix::assets::fourCC('M', 'O', 'D', 'L');
    constexpr auto kIaob = airfix::assets::fourCC('I', 'A', 'O', 'B');

    Bytes checksum;
    appendU32(checksum, 0x89ABCDEFU);
    Bytes chunks;
    appendChunk(chunks, kGccs, checksum);
    appendChunk(chunks, kHous, stringPayload("Game\\World.world"));
    appendChunk(chunks, kObje, placementPayload(
        {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F}, "room", "object.object"));
    appendChunk(chunks, kObje, placementPayload(
        {-1.0F, -2.0F, -3.0F, 0.0F, 0.0F,
            std::numeric_limits<float>::infinity()}, "", "second.object"));
    const Bytes modelPayload{1U, 2U, 3U};
    const Bytes iaobPayload{4U, 5U};
    const auto model = appendDescribedChunk(chunks, kModl, modelPayload);
    const auto iaob = appendDescribedChunk(chunks, kIaob, iaobPayload);

    const auto bytes = wrapContainer(airfix::assets::kAfFullHouseRoot, chunks);
    const auto definition = airfix::assets::parseLevelDefinition(bytes);
    require(definition.geometryChecksum == 0x89ABCDEFU, "level GCCS mismatch");
    require(definition.worldPath == "Game\\World.world", "level HOUS mismatch");
    require(definition.objects.size() == 2U &&
            definition.objects[0].position[2] == 3.0F &&
            definition.objects[0].axisRotation[2] == 6.0F &&
            definition.objects[0].room == "room" &&
            definition.objects[1].room.empty() &&
            definition.objects[1].axisRotation[2] ==
                std::numeric_limits<float>::infinity(),
        "level OBJE mismatch or non-finite value was rejected");
    require(definition.unknownChunks.size() == 2U &&
            definition.unknownChunks[0].id == kModl &&
            definition.unknownChunks[1].id == kIaob,
        "level uncertain placement chunks were interpreted or discarded");
    requireDescriptor(bytes, definition.unknownChunks[0], model, modelPayload,
        "level MODL descriptor mismatch");
    requireDescriptor(bytes, definition.unknownChunks[1], iaob, iaobPayload,
        "level IAOB descriptor mismatch");
}

void testMalformedLevelDefinition() {
    constexpr auto kGccs = airfix::assets::fourCC('G', 'C', 'C', 'S');
    constexpr auto kHous = airfix::assets::fourCC('H', 'O', 'U', 'S');
    constexpr auto kObje = airfix::assets::fourCC('O', 'B', 'J', 'E');
    Bytes chunks;
    appendChunk(chunks, kGccs, Bytes(4U));
    appendChunk(chunks, kGccs, Bytes(4U));
    requireParseError([&] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfFullHouseRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kGccs, Bytes(3U));
    requireParseError([&] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfFullHouseRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kHous, stringPayload("a"));
    appendChunk(chunks, kHous, stringPayload("b"));
    requireParseError([&] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfFullHouseRoot, chunks));
    });
    chunks.clear();
    auto placement = placementPayload(
        {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}, "room", "object");
    placement.push_back(1U);
    appendChunk(chunks, kObje, placement);
    requireParseError([&] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfFullHouseRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kObje, Bytes(25U));
    requireParseError([&] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfFullHouseRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kObje, placementPayload(
        {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}, "room", "object"));
    auto limits = airfix::assets::DefinitionParseLimits{};
    limits.maxPlacements = 0U;
    requireParseError([&] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfFullHouseRoot, chunks), limits);
    });
    limits = {};
    limits.maxStringBytes = 3U;
    requireParseError([&] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfFullHouseRoot, chunks), limits);
    });
    requireParseError([] {
        (void)airfix::assets::parseLevelDefinition(
            wrapContainer(airfix::assets::kAfHouseRoot, {}));
    });
}

void testBriefingDefinition() {
    const std::array ids{
        airfix::assets::fourCC('N', 'A', 'M', 'E'),
        airfix::assets::fourCC('O', 'U', 'T', 'L'),
        airfix::assets::fourCC('O', 'U', 'T', '2'),
        airfix::assets::fourCC('T', 'E', 'X', 'T'),
        airfix::assets::fourCC('T', 'X', 'T', '2'),
        airfix::assets::fourCC('P', 'R', 'I', 'M'),
        airfix::assets::fourCC('S', 'C', 'N', 'D'),
        airfix::assets::fourCC('A', 'I', 'R', 'S'),
        airfix::assets::fourCC('S', 'E', 'L', 'A'),
    };
    Bytes chunks;
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        appendChunk(chunks, ids[index], stringPayload("value-" + std::to_string(index)));
    }
    const Bytes unknownPayload{7U};
    const auto unknown = appendDescribedChunk(
        chunks, airfix::assets::fourCC('U', 'N', 'K', 'N'), unknownPayload);
    const auto bytes = wrapContainer(airfix::assets::kAfBriefingRoot, chunks);
    const auto definition = airfix::assets::parseBriefing(bytes);
    require(definition.name == "value-0" && definition.outline2 == "value-2" &&
            definition.text2 == "value-4" && definition.secondary == "value-6" &&
            definition.selectedAircraft == "value-8",
        "briefing known strings mismatch");
    require(definition.unknownChunks.size() == 1U,
        "briefing unknown chunk was not preserved");
    requireDescriptor(bytes, definition.unknownChunks[0], unknown, unknownPayload,
        "briefing unknown descriptor mismatch");

    chunks.clear();
    appendChunk(chunks, ids[0], stringPayload("a"));
    appendChunk(chunks, ids[0], stringPayload("b"));
    requireParseError([&] {
        (void)airfix::assets::parseBriefing(
            wrapContainer(airfix::assets::kAfBriefingRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, ids[1], Bytes{'a', 0U, 'x'});
    requireParseError([&] {
        (void)airfix::assets::parseBriefing(
            wrapContainer(airfix::assets::kAfBriefingRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, ids[2], stringPayload("long"));
    auto limits = airfix::assets::DefinitionParseLimits{};
    limits.maxStringBytes = 3U;
    requireParseError([&] {
        (void)airfix::assets::parseBriefing(
            wrapContainer(airfix::assets::kAfBriefingRoot, chunks), limits);
    });
}

void testPathDefinition() {
    constexpr auto kPpos = airfix::assets::fourCC('P', 'P', 'O', 'S');
    constexpr auto kPdat = airfix::assets::fourCC('P', 'D', 'A', 'T');
    Bytes data;
    for (const auto value : std::array<std::int16_t, 12>{
            std::numeric_limits<std::int16_t>::min(), -1, 0, 1, 2,
            std::numeric_limits<std::int16_t>::max(),
            10, 20, 30, 40, 50, 60}) {
        appendI16(data, value);
    }
    Bytes chunks;
    appendChunk(chunks, kPpos, floatPayload(std::array{
        1.0F, 2.0F, 3.0F, 4.0F, 5.0F,
        std::numeric_limits<float>::infinity()}));
    appendChunk(chunks, kPdat, data);
    const Bytes unknownPayload{1U};
    const auto unknown = appendDescribedChunk(
        chunks, airfix::assets::fourCC('E', 'X', 'T', 'R'), unknownPayload);
    const auto bytes = wrapContainer(airfix::assets::kAfPathRoot, chunks);
    const auto definition = airfix::assets::parsePathDefinition(bytes);
    require(definition.pose.has_value() && (*definition.pose)[0] == 1.0F &&
            (*definition.pose)[5] == std::numeric_limits<float>::infinity(),
        "path PPOS mismatch or non-finite value was rejected");
    require(definition.records.size() == 2U &&
            definition.records[0].deltas[0] == std::numeric_limits<std::int16_t>::min() &&
            definition.records[0].deltas[5] == std::numeric_limits<std::int16_t>::max() &&
            definition.records[1].deltas[5] == 60,
        "path signed PDAT values mismatch");
    require(definition.unknownChunks.size() == 1U,
        "path unknown chunk was not preserved");
    requireDescriptor(bytes, definition.unknownChunks[0], unknown, unknownPayload,
        "path unknown descriptor mismatch");

    Bytes emptyDataChunks;
    appendChunk(emptyDataChunks, kPdat, {});
    require(airfix::assets::parsePathDefinition(
                wrapContainer(airfix::assets::kAfPathRoot, emptyDataChunks)).records.empty(),
        "empty path PDAT was rejected");
}

void testMalformedPathDefinition() {
    constexpr auto kPpos = airfix::assets::fourCC('P', 'P', 'O', 'S');
    constexpr auto kPdat = airfix::assets::fourCC('P', 'D', 'A', 'T');
    Bytes chunks;
    appendChunk(chunks, kPpos, Bytes(20U));
    requireParseError([&] {
        (void)airfix::assets::parsePathDefinition(
            wrapContainer(airfix::assets::kAfPathRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kPpos, floatPayload(std::array{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}));
    appendChunk(chunks, kPpos, floatPayload(std::array{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F}));
    requireParseError([&] {
        (void)airfix::assets::parsePathDefinition(
            wrapContainer(airfix::assets::kAfPathRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kPdat, Bytes(13U));
    requireParseError([&] {
        (void)airfix::assets::parsePathDefinition(
            wrapContainer(airfix::assets::kAfPathRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kPdat, Bytes(12U));
    appendChunk(chunks, kPdat, {});
    requireParseError([&] {
        (void)airfix::assets::parsePathDefinition(
            wrapContainer(airfix::assets::kAfPathRoot, chunks));
    });
    chunks.clear();
    appendChunk(chunks, kPdat, Bytes(12U));
    auto limits = airfix::assets::DefinitionParseLimits{};
    limits.maxPathRecords = 0U;
    requireParseError([&] {
        (void)airfix::assets::parsePathDefinition(
            wrapContainer(airfix::assets::kAfPathRoot, chunks), limits);
    });
    requireParseError([] {
        (void)airfix::assets::parsePathDefinition(
            wrapContainer(airfix::assets::kAfBriefingRoot, {}));
    });
}

} // namespace

int main() {
    try {
        testGenericContainer();
        testMalformedContainers();
        testObjectDefinition();
        testMalformedObjectDefinition();
        testWorldDefinition();
        testMalformedWorldDefinition();
        testLevelDefinition();
        testMalformedLevelDefinition();
        testBriefingDefinition();
        testPathDefinition();
        testMalformedPathDefinition();
        std::cout << "all AfChunkContainer tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AfChunkContainer test failure: " << error.what() << '\n';
        return 1;
    }
}
