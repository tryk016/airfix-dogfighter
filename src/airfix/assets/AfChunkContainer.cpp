#include "airfix/assets/AfChunkContainer.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>

namespace airfix::assets {
namespace {

[[nodiscard]] std::uint64_t checkedAdd(
    const std::uint64_t left,
    const std::uint64_t right,
    const std::string_view field) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw ParseError(std::string(field) + " overflows");
    }
    return left + right;
}

void requireRange(
    const std::uint64_t offset,
    const std::uint64_t size,
    const std::uint64_t limit,
    const std::string_view field) {
    if (offset > limit || checkedAdd(offset, size, field) > limit) {
        throw ParseError(std::string(field) + " is outside its container");
    }
}

[[nodiscard]] std::uint32_t readU32(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 4U, bytes.size(), field);
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] float readFloat(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    return std::bit_cast<float>(readU32(bytes, offset, field));
}

} // namespace

AfChunkContainer parseAfChunkContainer(
    const std::span<const std::uint8_t> bytes,
    const std::size_t chunkLimit) {
    if (bytes.size() < 8U) {
        throw ParseError("AfChunkContainer is shorter than its root header");
    }
    const auto rootId = readU32(bytes, 0U, "AfChunkContainer root ID");
    const auto payloadSize = readU32(bytes, 4U, "AfChunkContainer root size");
    if (static_cast<std::uint64_t>(payloadSize) != bytes.size() - 8U) {
        throw ParseError("AfChunkContainer root size does not match the file");
    }

    AfChunkContainer container{
        .rootId = rootId,
        .payloadSize = payloadSize,
        .chunks = {},
    };
    auto cursor = std::size_t{8U};
    while (cursor < bytes.size()) {
        requireRange(cursor, 8U, bytes.size(), "AfChunk child header");
        if (container.chunks.size() >= chunkLimit) {
            throw ParseError("AfChunkContainer exceeds the configured chunk limit");
        }
        const auto id = readU32(bytes, cursor, "AfChunk child ID");
        const auto childPayloadSize = readU32(bytes, cursor + 4U, "AfChunk child size");
        const auto payloadOffset = checkedAdd(cursor, 8U, "AfChunk payload offset");
        requireRange(
            payloadOffset,
            childPayloadSize,
            bytes.size(),
            "AfChunk child payload");
        container.chunks.push_back({
            .id = id,
            .payloadSize = childPayloadSize,
            .payloadOffset = payloadOffset,
        });
        cursor = static_cast<std::size_t>(checkedAdd(
            payloadOffset, childPayloadSize, "AfChunk next child"));
    }
    return container;
}

std::span<const std::uint8_t> afChunkPayload(
    const std::span<const std::uint8_t> bytes,
    const AfChunk& chunk) {
    requireRange(
        chunk.payloadOffset,
        chunk.payloadSize,
        bytes.size(),
        "AfChunk payload view");
    return bytes.subspan(
        static_cast<std::size_t>(chunk.payloadOffset),
        static_cast<std::size_t>(chunk.payloadSize));
}

std::string readAfChunkString(
    const std::span<const std::uint8_t> bytes,
    const AfChunk& chunk,
    const std::size_t stringLimit) {
    const auto payload = afChunkPayload(bytes, chunk);
    if (payload.empty() || payload.size() > checkedAdd(stringLimit, 1U, "AfChunk string")) {
        throw ParseError("AfChunk string is empty or exceeds its configured limit");
    }
    const auto terminator = std::find(payload.begin(), payload.end(), 0U);
    if (terminator == payload.end() || terminator + 1U != payload.end()) {
        throw ParseError("AfChunk string is not exactly NUL-terminated");
    }
    return std::string(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::size_t>(terminator - payload.begin()));
}

ObjectDefinition parseObjectDefinition(const std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kObjectChunkLimit = 256U;
    constexpr std::size_t kObjectStringLimit = 4096U;
    constexpr auto kType = fourCC('T', 'Y', 'P', 'E');
    constexpr auto kCategory = fourCC('C', 'A', 'T', 'G');
    constexpr auto kName = fourCC('N', 'A', 'M', 'E');
    constexpr auto kNationality = fourCC('N', 'A', 'T', 'Y');
    constexpr auto kTextureRoot = fourCC('T', 'E', 'X', 'U');
    constexpr auto kCcfPath = fourCC('C', 'C', 'F', 'F');
    constexpr auto kMeshName = fourCC('M', 'E', 'S', 'H');
    constexpr auto kGravity = fourCC('G', 'R', 'A', 'V');
    constexpr auto kHidden = fourCC('H', 'I', 'D', 'E');

    const auto container = parseAfChunkContainer(bytes, kObjectChunkLimit);
    if (container.rootId != kAfObjectRoot && container.rootId != kAfModelRoot) {
        throw ParseError("object definition has an unsupported root ID");
    }
    ObjectDefinition definition;
    definition.kind = container.rootId == kAfModelRoot
        ? ObjectDefinitionKind::model
        : ObjectDefinitionKind::object;
    const auto storeString = [&](std::optional<std::string>& destination,
                                 const AfChunk& chunk) {
        if (destination.has_value()) {
            throw ParseError("object definition repeats a singleton string chunk");
        }
        destination = readAfChunkString(bytes, chunk, kObjectStringLimit);
    };

    for (const auto& chunk : container.chunks) {
        switch (chunk.id) {
        case kType: storeString(definition.type, chunk); break;
        case kCategory: storeString(definition.category, chunk); break;
        case kName: storeString(definition.name, chunk); break;
        case kNationality: storeString(definition.nationality, chunk); break;
        case kTextureRoot: storeString(definition.textureRoot, chunk); break;
        case kCcfPath: storeString(definition.ccfPath, chunk); break;
        case kMeshName: storeString(definition.meshName, chunk); break;
        case kGravity: {
            if (definition.gravity.has_value() || chunk.payloadSize != 12U) {
                throw ParseError("object definition has invalid gravity data");
            }
            const auto payload = afChunkPayload(bytes, chunk);
            definition.gravity = std::array<float, 3>{
                readFloat(payload, 0U, "object gravity X"),
                readFloat(payload, 4U, "object gravity Y"),
                readFloat(payload, 8U, "object gravity Z"),
            };
            break;
        }
        case kHidden:
            if (definition.hidden || chunk.payloadSize != 0U) {
                throw ParseError("object definition has invalid hidden marker");
            }
            definition.hidden = true;
            break;
        default:
            definition.unknownChunks.push_back(chunk);
            break;
        }
    }
    return definition;
}

} // namespace airfix::assets
