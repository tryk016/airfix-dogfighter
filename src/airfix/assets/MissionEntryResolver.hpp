#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/AfChunkContainer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace airfix::assets {

enum class MissionEntryStatus : std::uint8_t {
    missing,
    invalid,
    notFound,
    unique,
    ambiguous,
};

struct MissionArchiveEntry {
    MissionEntryStatus status{MissionEntryStatus::missing};
    std::string logicalPath;
    std::optional<std::size_t> archiveDirectoryIndex;
    std::optional<std::size_t> archiveFileIndex;
    std::optional<std::string> archiveLogicalPath;
};

struct ResolvedMissionPlacement {
    std::size_t placementIndex{};
    LevelObjectPlacement placement;
    MissionArchiveEntry objectEntry;
};

struct ResolvedMissionModelPlacement {
    std::size_t modelIndex{};
    LevelModelPlacement model;
    MissionArchiveEntry objectEntry;
};

enum class MissionEntryIssueKind : std::uint8_t {
    limitExceeded,
};

struct MissionEntryIssue {
    MissionEntryIssueKind kind{MissionEntryIssueKind::limitExceeded};
};

struct MissionEntryResolutionLimits {
    std::size_t maximumPlacements{65'536U};
    std::size_t maximumLogicalPathBytes{4'096U};
};

struct MissionEntryResolution {
    MissionArchiveEntry worldEntry;
    std::vector<ResolvedMissionPlacement> placements;
    std::vector<ResolvedMissionModelPlacement> models;
    std::vector<MissionEntryIssue> issues;
};

// Resolves only UDSP metadata. It never opens, decompresses, or copies an entry
// payload, and it does not infer extensions or search alternate directories.
[[nodiscard]] MissionEntryResolution resolveMissionEntries(
    const LevelDefinition& level,
    const udsp::Archive& archive,
    const MissionEntryResolutionLimits& limits = {});

} // namespace airfix::assets
