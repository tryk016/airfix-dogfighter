#include "airfix/assets/WorldCcfTextureResolver.hpp"

#include <string_view>

namespace airfix::assets {
namespace {

[[nodiscard]] std::string archiveLogicalPath(
    const udsp::Archive& archive,
    const std::size_t directoryIndex,
    const std::size_t fileIndex) {
    std::string result = archive.directories()[directoryIndex].path;
    if (!result.empty()) {
        result.push_back('\\');
    }
    result += archive.files()[fileIndex].name;
    return result;
}

void addIssue(
    WorldCcfTextureResolution& result,
    const WorldCcfTextureIssueKind kind) {
    result.issues.push_back({.kind = kind});
}

} // namespace

WorldCcfTextureResolution resolveWorldRoomTextures(
    const WorldDefinition& world,
    const CcfMetadata& ccf,
    const std::size_t ccfArchiveFileIndex,
    const udsp::Archive& archive,
    const std::size_t ccfRoomIndex,
    const WorldCcfTextureResolutionLimits& limits) {
    WorldCcfTextureResolution result;
    result.ccf.suppliedArchiveFileIndex = ccfArchiveFileIndex;

    if (!world.ccfPath.has_value()) {
        addIssue(result, WorldCcfTextureIssueKind::missingCcfPath);
        return result;
    }

    const std::string_view ccfPath = *world.ccfPath;
    if (!udsp::isLogicalPathValid(
            ccfPath, limits.maximumCcfLogicalPathBytes)) {
        result.ccf.status = WorldCcfBindingStatus::invalid;
        addIssue(result, WorldCcfTextureIssueKind::invalidCcfPath);
        return result;
    }

    try {
        result.ccf.logicalPath = udsp::normalizeLogicalPath(
            ccfPath, limits.maximumCcfLogicalPathBytes);
        const auto lookup = archive.lookup(
            result.ccf.logicalPath, limits.maximumCcfLogicalPathBytes);
        switch (lookup.status) {
        case udsp::LookupStatus::notFound:
            result.ccf.status = WorldCcfBindingStatus::notFound;
            addIssue(result, WorldCcfTextureIssueKind::ccfNotFound);
            return result;
        case udsp::LookupStatus::ambiguous:
            result.ccf.status = WorldCcfBindingStatus::ambiguous;
            addIssue(result, WorldCcfTextureIssueKind::ccfAmbiguous);
            return result;
        case udsp::LookupStatus::unique:
            result.ccf.archiveDirectoryIndex = lookup.directoryIndex;
            result.ccf.archiveFileIndex = lookup.fileIndex;
            result.ccf.archiveLogicalPath = archiveLogicalPath(
                archive, lookup.directoryIndex, lookup.fileIndex);
            break;
        }
    }
    catch (const udsp::ParseError&) {
        result.ccf = {
            .status = WorldCcfBindingStatus::invalid,
            .logicalPath = {},
            .archiveDirectoryIndex = std::nullopt,
            .archiveFileIndex = std::nullopt,
            .archiveLogicalPath = std::nullopt,
            .suppliedArchiveFileIndex = ccfArchiveFileIndex,
        };
        addIssue(result, WorldCcfTextureIssueKind::invalidCcfPath);
        return result;
    }

    if (!result.ccf.archiveFileIndex.has_value() ||
        *result.ccf.archiveFileIndex != ccfArchiveFileIndex) {
        result.ccf.status = WorldCcfBindingStatus::mismatch;
        addIssue(result, WorldCcfTextureIssueKind::ccfEntryMismatch);
        return result;
    }

    result.ccf.status = WorldCcfBindingStatus::unique;
    result.plan = resolveRoomDrawPlan(
        ccf, ccfRoomIndex, limits.plan);
    if (!result.plan.issues.empty()) {
        addIssue(
            result,
            WorldCcfTextureIssueKind::roomPlanDependency);
        return result;
    }

    const auto textureRoot = world.textureRoot.has_value()
        ? std::optional<std::string_view>{*world.textureRoot}
        : std::nullopt;
    result.textures = resolveTextureEntries(
        textureRoot, result.plan.textures, archive, limits.textureEntries);
    return result;
}

WorldCcfTextureResolution resolveWorldFirstRoomTextures(
    const WorldDefinition& world,
    const CcfMetadata& ccf,
    const std::size_t ccfArchiveFileIndex,
    const udsp::Archive& archive,
    const WorldCcfTextureResolutionLimits& limits) {
    return resolveWorldRoomTextures(
        world, ccf, ccfArchiveFileIndex, archive, 0U, limits);
}

} // namespace airfix::assets
