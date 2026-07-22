#include "airfix/assets/MissionEntryResolver.hpp"

#include <string_view>
#include <utility>

namespace airfix::assets {
namespace {

[[nodiscard]] std::string archiveLogicalPath(
    const udsp::Archive& archive,
    const std::size_t directoryIndex,
    const std::size_t fileIndex) {
    std::string result = archive.directories().at(directoryIndex).path;
    if (!result.empty()) {
        result.push_back('\\');
    }
    result += archive.files().at(fileIndex).name;
    return result;
}

[[nodiscard]] MissionArchiveEntry resolveEntry(
    const std::string_view sourcePath,
    const udsp::Archive& archive,
    const std::size_t pathLimit) {
    MissionArchiveEntry result{
        .status = MissionEntryStatus::notFound,
        .logicalPath = {},
        .archiveDirectoryIndex = std::nullopt,
        .archiveFileIndex = std::nullopt,
        .archiveLogicalPath = std::nullopt,
    };

    try {
        result.logicalPath = udsp::normalizeLogicalPath(sourcePath, pathLimit);
    }
    catch (const udsp::ParseError&) {
        result.status = MissionEntryStatus::invalid;
        return result;
    }

    const auto lookup = archive.lookup(result.logicalPath, pathLimit);
    switch (lookup.status) {
    case udsp::LookupStatus::notFound:
        result.status = MissionEntryStatus::notFound;
        break;
    case udsp::LookupStatus::unique:
        result.status = MissionEntryStatus::unique;
        result.archiveDirectoryIndex = lookup.directoryIndex;
        result.archiveFileIndex = lookup.fileIndex;
        result.archiveLogicalPath = archiveLogicalPath(
            archive, lookup.directoryIndex, lookup.fileIndex);
        break;
    case udsp::LookupStatus::ambiguous:
        result.status = MissionEntryStatus::ambiguous;
        break;
    }
    return result;
}

} // namespace

MissionEntryResolution resolveMissionEntries(
    const LevelDefinition& level,
    const udsp::Archive& archive,
    const MissionEntryResolutionLimits& limits) {
    MissionEntryResolution result;

    if (level.worldPath.has_value()) {
        result.worldEntry = resolveEntry(
            *level.worldPath, archive, limits.maximumLogicalPathBytes);
    }

    if (level.objects.size() > limits.maximumPlacements ||
        level.models.size() > limits.maximumPlacements - level.objects.size()) {
        result.issues.push_back({.kind = MissionEntryIssueKind::limitExceeded});
        return result;
    }

    result.placements.reserve(level.objects.size());
    for (std::size_t index = 0U; index < level.objects.size(); ++index) {
        const auto& placement = level.objects[index];
        ResolvedMissionPlacement resolved;
        resolved.placementIndex = index;
        resolved.placement = placement;
        resolved.objectEntry = resolveEntry(
            placement.objectPath,
            archive,
            limits.maximumLogicalPathBytes);
        result.placements.push_back(std::move(resolved));
    }
    result.models.reserve(level.models.size());
    for (std::size_t index = 0U; index < level.models.size(); ++index) {
        const auto& model = level.models[index];
        ResolvedMissionModelPlacement resolved;
        resolved.modelIndex = index;
        resolved.model = model;
        resolved.objectEntry = resolveEntry(
            model.placement.objectPath,
            archive,
            limits.maximumLogicalPathBytes);
        result.models.push_back(std::move(resolved));
    }
    return result;
}

} // namespace airfix::assets
