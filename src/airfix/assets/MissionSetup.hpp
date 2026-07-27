#pragma once

#include "airfix/assets/LegacyFormats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace airfix::assets {

inline constexpr std::size_t legacyMissionStartCapacity = 16U;

struct MissionStartPosition {
    std::string roomName;
    std::array<float, 3> position{};
    std::array<float, 3> axisRotation{};
    std::uint64_t sourceOffset{};

    friend bool operator==(
        const MissionStartPosition&,
        const MissionStartPosition&) = default;
};

struct MissionSetupParseLimits {
    std::size_t maximumSourceBytes{1024U * 1024U};
    std::size_t maximumRoomNameBytes{4096U};
    std::size_t maximumStartPositions{legacyMissionStartCapacity};
};

enum class MissionSetupParseErrorCode : std::uint8_t {
    sourceLimitExceeded,
    roomNameLimitExceeded,
    startPositionLimitExceeded,
    malformedText,
    invalidNumber,
};

class MissionSetupParseError final : public std::runtime_error {
public:
    MissionSetupParseError(
        MissionSetupParseErrorCode code,
        std::uint64_t offset,
        const char* message);

    [[nodiscard]] MissionSetupParseErrorCode code() const noexcept {
        return code_;
    }

    [[nodiscard]] std::uint64_t offset() const noexcept {
        return offset_;
    }

private:
    MissionSetupParseErrorCode code_;
    std::uint64_t offset_;
};

// Scans AFS source for the exact legacy call shape:
// AddStartPos("room", coord3d(x,y,z), coord3d(rx,ry,rz));
// Other script constructs remain opaque. Comments and string literals cannot
// create false calls. A malformed AddStartPos fails the whole parse.
[[nodiscard]] std::vector<MissionStartPosition> parseMissionStartPositions(
    std::span<const std::uint8_t> source,
    const MissionSetupParseLimits& limits = {});

enum class MissionStartRoomIssueKind : std::uint8_t {
    startPositionLimitExceeded,
    missingPrimaryRoom,
    ambiguousPrimaryRoom,
    missingStartRoom,
    ambiguousStartRoom,
};

struct MissionStartRoomIssue {
    MissionStartRoomIssueKind kind{
        MissionStartRoomIssueKind::missingStartRoom};
    std::optional<std::size_t> startPositionIndex;
};

struct ResolvedMissionStartPosition {
    std::size_t startPositionIndex{};
    std::size_t physicalRoomIndex{};

    friend bool operator==(
        const ResolvedMissionStartPosition&,
        const ResolvedMissionStartPosition&) = default;
};

struct MissionStartRoomResolution {
    std::size_t physicalRoomCount{};
    std::optional<std::size_t> primaryPhysicalRoomIndex;
    std::vector<ResolvedMissionStartPosition> starts;
    std::vector<MissionStartRoomIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return issues.empty() && primaryPhysicalRoomIndex.has_value();
    }
};

// Normalizes authored campaign room names against one selected CCF in physical
// order. World ROOM records are intentionally outside this join. This is not
// the ordered multi-CCF lookup implemented by MissionWorldRooms. Any missing or
// ambiguous key clears every resolved start atomically.
[[nodiscard]] MissionStartRoomResolution resolveMissionStartRoomsInCcf(
    std::span<const MissionStartPosition> starts,
    const CcfMetadata& ccf);

enum class MissionStartSelectionSource : std::uint8_t {
    table,
    primaryRoomFallback,
};

struct MissionStartSelection {
    MissionStartSelectionSource source{
        MissionStartSelectionSource::primaryRoomFallback};
    std::optional<std::size_t> startPositionIndex;
    std::size_t physicalRoomIndex{};

    friend bool operator==(
        const MissionStartSelection&,
        const MissionStartSelection&) = default;
};

// Returns nullopt for an incomplete room resolution. Otherwise it mirrors the
// legacy selector: requestedIndex modulo start count, or the primary CCF room
// when the start table is empty.
[[nodiscard]] std::optional<MissionStartSelection> selectMissionStart(
    const MissionStartRoomResolution& resolution,
    std::uint32_t requestedIndex) noexcept;

} // namespace airfix::assets
