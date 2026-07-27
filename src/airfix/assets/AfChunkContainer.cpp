#include "airfix/assets/AfChunkContainer.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>
#include <utility>

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

[[nodiscard]] std::int16_t readI16(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset,
    const std::string_view field) {
    requireRange(offset, 2U, bytes.size(), field);
    const auto bits = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
    return std::bit_cast<std::int16_t>(bits);
}

template <std::size_t Count>
[[nodiscard]] std::array<float, Count> readFloatArray(
    const std::span<const std::uint8_t> payload,
    const std::string_view field) {
    constexpr auto byteCount = Count * sizeof(float);
    if (payload.size() != byteCount) {
        throw ParseError(std::string(field) + " has an invalid payload size");
    }
    std::array<float, Count> result{};
    for (std::size_t index = 0U; index < Count; ++index) {
        result[index] = readFloat(payload, index * sizeof(float), field);
    }
    return result;
}

[[nodiscard]] std::string readEmbeddedString(
    const std::span<const std::uint8_t> payload,
    std::size_t& cursor,
    const std::size_t stringLimit,
    const std::string_view field) {
    if (cursor >= payload.size()) {
        throw ParseError(std::string(field) + " is missing its NUL-terminated string");
    }
    const auto remaining = payload.subspan(cursor);
    const auto terminator = std::find(remaining.begin(), remaining.end(), 0U);
    if (terminator == remaining.end()) {
        throw ParseError(std::string(field) + " is not NUL-terminated");
    }
    const auto length = static_cast<std::size_t>(terminator - remaining.begin());
    if (length > stringLimit) {
        throw ParseError(std::string(field) + " exceeds its configured string limit");
    }
    std::string result(
        reinterpret_cast<const char*>(remaining.data()),
        length);
    cursor += length + 1U;
    return result;
}

[[nodiscard]] std::vector<std::array<float, 4>> readWorldLines(
    const std::span<const std::uint8_t> payload,
    const std::size_t totalBefore,
    const std::size_t lineLimit) {
    constexpr std::size_t kLineBytes = 4U * sizeof(float);
    if (payload.size() % kLineBytes != 0U) {
        throw ParseError("world LDAT payload is not a whole number of line records");
    }
    const auto count = payload.size() / kLineBytes;
    if (totalBefore > lineLimit || count > lineLimit - totalBefore) {
        throw ParseError("world definition exceeds the configured line-record limit");
    }
    std::vector<std::array<float, 4>> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto record = payload.subspan(index * kLineBytes, kLineBytes);
        result.push_back(readFloatArray<4U>(record, "world LDAT line"));
    }
    return result;
}

[[nodiscard]] WorldRoom readWorldRoom(
    const std::span<const std::uint8_t> payload,
    const std::size_t stringLimit) {
    if (payload.size() < 4U) {
        throw ParseError("world ROOM payload is too short");
    }
    WorldRoom room;
    room.id = payload.front();
    auto cursor = std::size_t{1U};
    room.internalName = readEmbeddedString(
        payload, cursor, stringLimit, "world ROOM internal name");
    room.localizedName = readEmbeddedString(
        payload, cursor, stringLimit, "world ROOM localized name");
    if (cursor >= payload.size() || payload.size() - cursor != 1U) {
        throw ParseError("world ROOM payload has trailing or missing floor data");
    }
    room.floor = payload[cursor];
    return room;
}

[[nodiscard]] LevelObjectPlacement readLevelPlacementPrefix(
    const std::span<const std::uint8_t> payload,
    std::size_t& cursor,
    const std::size_t stringLimit,
    const std::string_view field) {
    constexpr std::size_t kTransformBytes = 6U * sizeof(float);
    if (payload.size() < kTransformBytes + 2U) {
        throw ParseError(std::string(field) + " payload is too short");
    }
    LevelObjectPlacement placement;
    const auto transform = readFloatArray<6U>(
        payload.first(kTransformBytes), std::string(field) + " transform");
    std::copy_n(transform.begin(), 3U, placement.position.begin());
    std::copy_n(transform.begin() + 3U, 3U, placement.axisRotation.begin());
    cursor = kTransformBytes;
    placement.room = readEmbeddedString(
        payload, cursor, stringLimit, std::string(field) + " room");
    placement.objectPath = readEmbeddedString(
        payload, cursor, stringLimit, std::string(field) + " object path");
    return placement;
}

[[nodiscard]] LevelObjectPlacement readLevelObject(
    const std::span<const std::uint8_t> payload,
    const std::size_t stringLimit) {
    auto cursor = std::size_t{};
    auto placement = readLevelPlacementPrefix(
        payload, cursor, stringLimit, "level OBJE");
    if (cursor != payload.size()) {
        throw ParseError("level OBJE payload has trailing bytes");
    }
    return placement;
}

[[nodiscard]] LevelModelPlacement readLevelModel(
    const std::span<const std::uint8_t> payload,
    const std::size_t stringLimit) {
    auto cursor = std::size_t{};
    LevelModelPlacement model;
    model.placement = readLevelPlacementPrefix(
        payload, cursor, stringLimit, "level MODL");
    for (std::size_t index = 0U; index < model.stateStrings.size(); ++index) {
        model.stateStrings[index] = readEmbeddedString(
            payload, cursor, stringLimit, "level MODL state string");
        if (payload.size() - cursor < sizeof(std::uint32_t)) {
            throw ParseError("level MODL is missing a state value");
        }
        model.stateValues[index] = readU32(
            payload, cursor, "level MODL state value");
        cursor += sizeof(std::uint32_t);
    }
    if (cursor == payload.size()) {
        return model;
    }
    if (payload.size() - cursor != sizeof(std::uint32_t)) {
        throw ParseError("level MODL has invalid trailing state data");
    }
    model.compatibilityValue = readU32(
        payload, cursor, "level MODL compatibility value");
    return model;
}

[[nodiscard]] LevelInstanceState readLevelInstanceState(
    const std::span<const std::uint8_t> payload,
    const std::size_t stringLimit,
    const std::size_t wordsBefore,
    const std::size_t wordLimit) {
    LevelInstanceState state;
    auto cursor = std::size_t{};
    state.typeIdentity = readEmbeddedString(
        payload, cursor, stringLimit, "level IAOB type identity");
    state.instanceIdentity = readEmbeddedString(
        payload, cursor, stringLimit, "level IAOB instance identity");
    const auto remaining = payload.size() - cursor;
    if (remaining == 0U || remaining % sizeof(std::uint32_t) != 0U) {
        throw ParseError("level IAOB state is empty or not a sequence of u32 words");
    }
    const auto wordCount = remaining / sizeof(std::uint32_t);
    if (wordsBefore > wordLimit || wordCount > wordLimit - wordsBefore) {
        throw ParseError("level definition exceeds the configured IAOB state-word limit");
    }
    state.stateWords.reserve(wordCount);
    for (std::size_t index = 0U; index < wordCount; ++index) {
        state.stateWords.push_back(readU32(
            payload,
            cursor + index * sizeof(std::uint32_t),
            "level IAOB state word"));
    }
    return state;
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
    if (payload.empty() || payload.size() - 1U > stringLimit) {
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

WorldDefinition parseWorldDefinition(
    const std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits) {
    constexpr auto kTextureRoot = fourCC('T', 'E', 'X', 'U');
    constexpr auto kCcfPath = fourCC('C', 'C', 'F', 'F');
    constexpr auto kBackdrop = fourCC('B', 'C', 'K', 'D');
    constexpr auto kFloorYLevels = fourCC('F', 'L', 'R', 'Y');
    constexpr auto kRoom = fourCC('R', 'O', 'O', 'M');
    constexpr auto kLineList = fourCC('L', 'L', 'S', 'T');
    constexpr auto kLineData = fourCC('L', 'D', 'A', 'T');

    const auto container = parseAfChunkContainer(bytes, limits.maxChunks);
    if (container.rootId != kAfHouseRoot) {
        throw ParseError("world definition has an unsupported root ID");
    }

    WorldDefinition definition;
    std::vector<std::string> lineNames;
    std::vector<std::vector<std::array<float, 4>>> lineData;
    std::size_t totalLines = 0U;
    const auto storeString = [&](std::optional<std::string>& destination,
                                 const AfChunk& chunk,
                                 const std::string_view field) {
        if (destination.has_value()) {
            throw ParseError(std::string("world definition repeats ") + std::string(field));
        }
        destination = readAfChunkString(bytes, chunk, limits.maxStringBytes);
    };

    for (const auto& chunk : container.chunks) {
        switch (chunk.id) {
        case kTextureRoot:
            storeString(definition.textureRoot, chunk, "TEXU");
            break;
        case kCcfPath:
            storeString(definition.ccfPath, chunk, "CCFF");
            break;
        case kBackdrop:
            if (!definition.backdrop.has_value()) {
                definition.backdrop = readAfChunkString(
                    bytes, chunk, limits.maxStringBytes);
            }
            else {
                // The legacy loader requests only the first BCKD. Validate the
                // ignored duplicate's shape and retain its source descriptor.
                (void)readAfChunkString(bytes, chunk, limits.maxStringBytes);
                definition.ignoredDuplicateChunks.push_back(chunk);
            }
            break;
        case kFloorYLevels:
            if (definition.floorYLevels.has_value()) {
                throw ParseError("world definition repeats FLRY");
            }
            definition.floorYLevels = readFloatArray<3U>(
                afChunkPayload(bytes, chunk), "world FLRY");
            break;
        case kRoom:
            if (definition.rooms.size() >= limits.maxRooms) {
                throw ParseError("world definition exceeds the configured room limit");
            }
            definition.rooms.push_back(readWorldRoom(
                afChunkPayload(bytes, chunk), limits.maxStringBytes));
            break;
        case kLineList:
            if (lineNames.size() >= limits.maxLineLists) {
                throw ParseError("world definition exceeds the configured line-list limit");
            }
            lineNames.push_back(readAfChunkString(bytes, chunk, limits.maxStringBytes));
            break;
        case kLineData: {
            if (lineData.size() >= limits.maxLineLists) {
                throw ParseError("world definition exceeds the configured line-list limit");
            }
            auto lines = readWorldLines(
                afChunkPayload(bytes, chunk), totalLines, limits.maxLineRecords);
            totalLines += lines.size();
            lineData.push_back(std::move(lines));
            break;
        }
        default:
            definition.unknownChunks.push_back(chunk);
            break;
        }
    }

    if (lineNames.size() != lineData.size()) {
        throw ParseError("world definition has unpaired LLST/LDAT chunks");
    }
    definition.lineLists.reserve(lineNames.size());
    for (std::size_t index = 0U; index < lineNames.size(); ++index) {
        definition.lineLists.push_back({
            .name = std::move(lineNames[index]),
            .lines = std::move(lineData[index]),
        });
    }
    return definition;
}

LevelDefinition parseLevelDefinition(
    const std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits) {
    constexpr auto kChecksum = fourCC('G', 'C', 'C', 'S');
    constexpr auto kWorld = fourCC('H', 'O', 'U', 'S');
    constexpr auto kObject = fourCC('O', 'B', 'J', 'E');
    constexpr auto kModel = fourCC('M', 'O', 'D', 'L');
    constexpr auto kInstanceState = fourCC('I', 'A', 'O', 'B');

    const auto container = parseAfChunkContainer(bytes, limits.maxChunks);
    if (container.rootId != kAfFullHouseRoot) {
        throw ParseError("level definition has an unsupported root ID");
    }

    LevelDefinition definition;
    auto instanceStateWords = std::size_t{};
    for (const auto& chunk : container.chunks) {
        switch (chunk.id) {
        case kChecksum: {
            if (definition.geometryChecksum.has_value() || chunk.payloadSize != 4U) {
                throw ParseError("level definition has invalid or repeated GCCS");
            }
            definition.geometryChecksum = readU32(
                afChunkPayload(bytes, chunk), 0U, "level GCCS");
            break;
        }
        case kWorld:
            if (definition.worldPath.has_value()) {
                throw ParseError("level definition repeats HOUS");
            }
            definition.worldPath = readAfChunkString(
                bytes, chunk, limits.maxStringBytes);
            break;
        case kObject:
            if (definition.objects.size() > limits.maxPlacements ||
                definition.models.size() >=
                    limits.maxPlacements - definition.objects.size()) {
                throw ParseError("level definition exceeds the configured placement limit");
            }
            definition.objects.push_back(readLevelObject(
                afChunkPayload(bytes, chunk), limits.maxStringBytes));
            break;
        case kModel:
            if (definition.objects.size() > limits.maxPlacements ||
                definition.models.size() >=
                    limits.maxPlacements - definition.objects.size()) {
                throw ParseError("level definition exceeds the configured placement limit");
            }
            definition.models.push_back(readLevelModel(
                afChunkPayload(bytes, chunk), limits.maxStringBytes));
            break;
        case kInstanceState: {
            auto state = readLevelInstanceState(
                afChunkPayload(bytes, chunk),
                limits.maxStringBytes,
                instanceStateWords,
                limits.maxInstanceStateWords);
            instanceStateWords += state.stateWords.size();
            definition.instanceStates.push_back(std::move(state));
            break;
        }
        default:
            definition.unknownChunks.push_back(chunk);
            break;
        }
    }
    return definition;
}

BriefingDefinition parseBriefing(
    const std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits) {
    constexpr auto kName = fourCC('N', 'A', 'M', 'E');
    constexpr auto kOutline = fourCC('O', 'U', 'T', 'L');
    constexpr auto kOutline2 = fourCC('O', 'U', 'T', '2');
    constexpr auto kText = fourCC('T', 'E', 'X', 'T');
    constexpr auto kText2 = fourCC('T', 'X', 'T', '2');
    constexpr auto kPrimary = fourCC('P', 'R', 'I', 'M');
    constexpr auto kSecondary = fourCC('S', 'C', 'N', 'D');
    constexpr auto kAircraft = fourCC('A', 'I', 'R', 'S');
    constexpr auto kSelectedAircraft = fourCC('S', 'E', 'L', 'A');

    const auto container = parseAfChunkContainer(bytes, limits.maxChunks);
    if (container.rootId != kAfBriefingRoot) {
        throw ParseError("briefing definition has an unsupported root ID");
    }
    BriefingDefinition definition;
    const auto storeString = [&](std::optional<std::string>& destination,
                                 const AfChunk& chunk,
                                 const std::string_view field) {
        if (destination.has_value()) {
            throw ParseError(std::string("briefing repeats ") + std::string(field));
        }
        destination = readAfChunkString(bytes, chunk, limits.maxStringBytes);
    };
    for (const auto& chunk : container.chunks) {
        switch (chunk.id) {
        case kName: storeString(definition.name, chunk, "NAME"); break;
        case kOutline: storeString(definition.outline, chunk, "OUTL"); break;
        case kOutline2: storeString(definition.outline2, chunk, "OUT2"); break;
        case kText: storeString(definition.text, chunk, "TEXT"); break;
        case kText2: storeString(definition.text2, chunk, "TXT2"); break;
        case kPrimary: storeString(definition.primary, chunk, "PRIM"); break;
        case kSecondary: storeString(definition.secondary, chunk, "SCND"); break;
        case kAircraft: storeString(definition.aircraft, chunk, "AIRS"); break;
        case kSelectedAircraft:
            storeString(definition.selectedAircraft, chunk, "SELA");
            break;
        default:
            definition.unknownChunks.push_back(chunk);
            break;
        }
    }
    return definition;
}

PathDefinition parsePathDefinition(
    const std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits) {
    constexpr auto kPose = fourCC('P', 'P', 'O', 'S');
    constexpr auto kData = fourCC('P', 'D', 'A', 'T');
    constexpr std::size_t kRecordBytes = 6U * sizeof(std::int16_t);

    const auto container = parseAfChunkContainer(bytes, limits.maxChunks);
    if (container.rootId != kAfPathRoot) {
        throw ParseError("path definition has an unsupported root ID");
    }
    PathDefinition definition;
    bool sawData = false;
    for (const auto& chunk : container.chunks) {
        switch (chunk.id) {
        case kPose:
            if (definition.pose.has_value()) {
                throw ParseError("path definition repeats PPOS");
            }
            definition.pose = readFloatArray<6U>(
                afChunkPayload(bytes, chunk), "path PPOS");
            break;
        case kData: {
            if (sawData) {
                throw ParseError("path definition repeats PDAT");
            }
            sawData = true;
            const auto payload = afChunkPayload(bytes, chunk);
            if (payload.size() % kRecordBytes != 0U) {
                throw ParseError("path PDAT payload is not a whole number of records");
            }
            const auto count = payload.size() / kRecordBytes;
            if (count > limits.maxPathRecords) {
                throw ParseError("path definition exceeds the configured record limit");
            }
            definition.records.reserve(count);
            for (std::size_t index = 0U; index < count; ++index) {
                PathDeltaRecord record;
                const auto recordOffset = index * kRecordBytes;
                for (std::size_t component = 0U; component < record.deltas.size();
                     ++component) {
                    record.deltas[component] = readI16(
                        payload,
                        recordOffset + component * sizeof(std::int16_t),
                        "path PDAT delta");
                }
                definition.records.push_back(record);
            }
            break;
        }
        default:
            definition.unknownChunks.push_back(chunk);
            break;
        }
    }
    return definition;
}

} // namespace airfix::assets
