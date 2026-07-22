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

inline constexpr std::uint32_t kAfObjectRoot = fourCC('O', 'B', 'J', 'E');
inline constexpr std::uint32_t kAfModelRoot = fourCC('M', 'O', 'D', 'L');
inline constexpr std::uint32_t kAfHouseRoot = fourCC('H', 'O', 'U', 'S');
inline constexpr std::uint32_t kAfFullHouseRoot = fourCC('F', 'H', 'O', 'U');
inline constexpr std::uint32_t kAfBriefingRoot = fourCC('B', 'R', 'I', 'F');
inline constexpr std::uint32_t kAfPathRoot = fourCC('P', 'A', 'T', 'H');

struct AfChunk {
    std::uint32_t id{};
    std::uint32_t payloadSize{};
    std::uint64_t payloadOffset{};
};

struct AfChunkContainer {
    std::uint32_t rootId{};
    std::uint32_t payloadSize{};
    std::vector<AfChunk> chunks;
};

enum class ObjectDefinitionKind : std::uint8_t {
    object,
    model,
};

struct ObjectDefinition {
    ObjectDefinitionKind kind{ObjectDefinitionKind::object};
    std::optional<std::string> type;
    std::optional<std::string> category;
    std::optional<std::string> name;
    std::optional<std::string> nationality;
    std::optional<std::string> textureRoot;
    std::optional<std::string> ccfPath;
    std::optional<std::string> meshName;
    std::optional<std::array<float, 3>> gravity;
    bool hidden{};
    std::vector<AfChunk> unknownChunks;
};

struct DefinitionParseLimits {
    std::size_t maxChunks{4096U};
    std::size_t maxStringBytes{4096U};
    std::size_t maxRooms{4096U};
    std::size_t maxLineLists{4096U};
    std::size_t maxLineRecords{65'536U};
    std::size_t maxPlacements{65'536U};
    std::size_t maxPathRecords{65'536U};
};

struct WorldRoom {
    std::uint8_t id{};
    std::string internalName;
    std::string localizedName;
    std::uint8_t floor{};
};

struct WorldLineList {
    std::string name;
    std::vector<std::array<float, 4>> lines;
};

struct WorldDefinition {
    std::optional<std::string> textureRoot;
    std::optional<std::string> ccfPath;
    std::optional<std::string> backdrop;
    std::optional<std::array<float, 3>> floorYLevels;
    std::vector<WorldRoom> rooms;
    std::vector<WorldLineList> lineLists;
    std::vector<AfChunk> ignoredDuplicateChunks;
    std::vector<AfChunk> unknownChunks;
};

struct LevelObjectPlacement {
    std::array<float, 3> position{};
    std::array<float, 3> axisRotation{};
    std::string room;
    std::string objectPath;
};

struct LevelDefinition {
    std::optional<std::uint32_t> geometryChecksum;
    std::optional<std::string> worldPath;
    std::vector<LevelObjectPlacement> objects;
    std::vector<AfChunk> unknownChunks;
};

struct BriefingDefinition {
    std::optional<std::string> name;
    std::optional<std::string> outline;
    std::optional<std::string> outline2;
    std::optional<std::string> text;
    std::optional<std::string> text2;
    std::optional<std::string> primary;
    std::optional<std::string> secondary;
    std::optional<std::string> aircraft;
    std::optional<std::string> selectedAircraft;
    std::vector<AfChunk> unknownChunks;
};

struct PathDeltaRecord {
    std::array<std::int16_t, 6> deltas{};
};

struct PathDefinition {
    std::optional<std::array<float, 6>> pose;
    std::vector<PathDeltaRecord> records;
    std::vector<AfChunk> unknownChunks;
};

[[nodiscard]] AfChunkContainer parseAfChunkContainer(
    std::span<const std::uint8_t> bytes,
    std::size_t chunkLimit);
[[nodiscard]] std::span<const std::uint8_t> afChunkPayload(
    std::span<const std::uint8_t> bytes,
    const AfChunk& chunk);
[[nodiscard]] std::string readAfChunkString(
    std::span<const std::uint8_t> bytes,
    const AfChunk& chunk,
    std::size_t stringLimit);
[[nodiscard]] ObjectDefinition parseObjectDefinition(
    std::span<const std::uint8_t> bytes);
[[nodiscard]] WorldDefinition parseWorldDefinition(
    std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits = {});
[[nodiscard]] LevelDefinition parseLevelDefinition(
    std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits = {});
[[nodiscard]] BriefingDefinition parseBriefing(
    std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits = {});
[[nodiscard]] PathDefinition parsePathDefinition(
    std::span<const std::uint8_t> bytes,
    const DefinitionParseLimits& limits = {});

} // namespace airfix::assets
