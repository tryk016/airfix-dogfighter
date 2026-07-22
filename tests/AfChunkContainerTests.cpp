#include "airfix/assets/AfChunkContainer.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
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

void appendChunk(Bytes& bytes, const std::uint32_t id, const Bytes& payload) {
    appendU32(bytes, id);
    appendU32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

[[nodiscard]] Bytes stringPayload(const std::string_view value) {
    Bytes bytes(value.begin(), value.end());
    bytes.push_back(0U);
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

} // namespace

int main() {
    try {
        testGenericContainer();
        testMalformedContainers();
        testObjectDefinition();
        testMalformedObjectDefinition();
        std::cout << "all AfChunkContainer tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "AfChunkContainer test failure: " << error.what() << '\n';
        return 1;
    }
}
