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

} // namespace airfix::assets
