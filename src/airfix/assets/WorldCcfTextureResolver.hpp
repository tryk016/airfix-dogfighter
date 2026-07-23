#pragma once

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/AssetResolver.hpp"
#include "airfix/assets/CcfRoomDrawPlan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace airfix::assets {

enum class WorldCcfBindingStatus : std::uint8_t {
    missing,
    invalid,
    notFound,
    unique,
    ambiguous,
    mismatch,
};

enum class WorldCcfTextureIssueKind : std::uint8_t {
    missingCcfPath,
    invalidCcfPath,
    ccfNotFound,
    ccfAmbiguous,
    ccfEntryMismatch,
    roomPlanDependency,
    // Compatibility name for the original first-room-only entry point.
    firstRoomPlanDependency = roomPlanDependency,
};

struct WorldCcfTextureIssue {
    WorldCcfTextureIssueKind kind{WorldCcfTextureIssueKind::missingCcfPath};
};

struct WorldCcfBinding {
    WorldCcfBindingStatus status{WorldCcfBindingStatus::missing};
    std::string logicalPath;
    std::optional<std::size_t> archiveDirectoryIndex;
    std::optional<std::size_t> archiveFileIndex;
    std::optional<std::string> archiveLogicalPath;
    std::size_t suppliedArchiveFileIndex{};
};

struct WorldCcfTextureResolutionLimits {
    std::size_t maximumCcfLogicalPathBytes{4'096U};
    CcfRoomDrawPlanLimits plan;
    TextureEntryResolutionLimits textureEntries;
};

struct WorldCcfTextureResolution {
    WorldCcfBinding ccf;
    CcfRoomDrawPlan plan;
    TextureEntryResolution textures;
    std::vector<WorldCcfTextureIssue> issues;
};

// Validates that the world's CCF path resolves to the supplied source-entry
// index, then derives texture edges for one explicit physical room. This
// metadata-only comparison does not prove where the CCF payload came from.
// The caller must create CCF metadata and its source-entry index in one
// immutable load transaction. No archive payload is read or copied here.
[[nodiscard]] WorldCcfTextureResolution resolveWorldRoomTextures(
    const WorldDefinition& world,
    const CcfMetadata& ccf,
    std::size_t ccfArchiveFileIndex,
    const udsp::Archive& archive,
    std::size_t ccfRoomIndex,
    const WorldCcfTextureResolutionLimits& limits = {});

// Validates that the world's CCF path resolves to the supplied source-entry
// index, then derives texture edges from that CCF's canonical first-room plan.
// This metadata-only comparison does not prove where the CCF payload came
// from. The caller must create CCF metadata and its source-entry index in one
// immutable load transaction. No archive payload is read or copied here.
[[nodiscard]] WorldCcfTextureResolution resolveWorldFirstRoomTextures(
    const WorldDefinition& world,
    const CcfMetadata& ccf,
    std::size_t ccfArchiveFileIndex,
    const udsp::Archive& archive,
    const WorldCcfTextureResolutionLimits& limits = {});

} // namespace airfix::assets
