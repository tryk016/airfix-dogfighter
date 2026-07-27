#include "airfix/content/MissionWorldRoomPublication.hpp"

#include "airfix/content/PlayerSpawnPoseBuilder.hpp"

#include <bit>
#include <cmath>

namespace airfix::content {
namespace {

[[nodiscard]] std::optional<MissionWorldRoomPublicationIssue>
issue(const MissionWorldRoomPublicationIssueKind kind) noexcept {
    return MissionWorldRoomPublicationIssue{
        .kind = kind,
        .sourceIndex = std::nullopt,
        .componentIndex = std::nullopt,
    };
}

[[nodiscard]] std::optional<MissionWorldRoomPublicationIssue>
indexedIssue(const MissionWorldRoomPublicationIssueKind kind,
             const std::size_t sourceIndex) noexcept {
    return MissionWorldRoomPublicationIssue{
        .kind = kind,
        .sourceIndex = sourceIndex,
        .componentIndex = std::nullopt,
    };
}

[[nodiscard]] bool sameFloatBits(
    const float left, const float right) noexcept {
    return std::bit_cast<std::uint32_t>(left) ==
           std::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool samePlayerSpawnPose(
    const simulation::PlayerSpawnPose& left,
    const simulation::PlayerSpawnPose& right) noexcept {
    if (left.source != right.source ||
        left.startPositionIndex != right.startPositionIndex ||
        left.worldRoomIndex != right.worldRoomIndex) {
        return false;
    }
    for (std::size_t index = 0U;
         index < left.legacyWorldPosition.size(); ++index) {
        if (!sameFloatBits(left.legacyWorldPosition[index],
                           right.legacyWorldPosition[index]) ||
            !sameFloatBits(left.legacyAxisRotationRadians[index],
                           right.legacyAxisRotationRadians[index]) ||
            !sameFloatBits(left.runtimeWorldPosition[index],
                           right.runtimeWorldPosition[index])) {
            return false;
        }
    }
    for (std::size_t column = 0U;
         column < left.runtimeWorldRotationColumns.size(); ++column) {
        for (std::size_t component = 0U;
             component <
             left.runtimeWorldRotationColumns[column].size(); ++component) {
            if (!sameFloatBits(
                    left.runtimeWorldRotationColumns[column][component],
                    right.runtimeWorldRotationColumns[column][component])) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::optional<MissionWorldRoomPublicationIssue>
validateMissionWorldRoomPublication(
    const LoadedMissionWorldRoom &room,
    const ContentRevision &expectedRevision) noexcept {
    if (room.revision != expectedRevision) {
        return issue(MissionWorldRoomPublicationIssueKind::revisionMismatch);
    }
    if (room.setupEntry.logicalPath.empty()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::emptySetupLogicalPath);
    }
    if (room.meshProvenance.size() != room.model.meshes.size()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::meshProvenanceCountMismatch);
    }
    if (room.instanceProvenance.size() != room.model.instances.size()) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         instanceProvenanceCountMismatch);
    }

    switch (room.startSelection.source) {
    case assets::MissionWorldStartSelectionSource::rootRoomFallback:
        if (room.startSelection.startPositionIndex.has_value()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             rootFallbackStartPositionIndexPresent);
        }
        if (room.selectedStart.has_value()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             rootFallbackSelectedStartPresent);
        }
        if (room.startSelection.worldRoomIndex != 0U) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             rootFallbackWorldRoomIndexNotZero);
        }
        break;
    case assets::MissionWorldStartSelectionSource::table:
        if (!room.startSelection.startPositionIndex.has_value()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             tableStartPositionIndexMissing);
        }
        if (*room.startSelection.startPositionIndex >=
            assets::legacyMissionStartCapacity) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    tableStartPositionIndexLimitExceeded,
                                *room.startSelection.startPositionIndex);
        }
        if (room.startSelection.worldRoomIndex == 0U) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             tableWorldRoomIndexIsRoot);
        }
        if (!room.selectedStart.has_value()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             tableSelectedStartMissing);
        }
        if (room.selectedStart->roomName.empty()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             tableSelectedStartRoomNameEmpty);
        }
        break;
    default:
        return issue(
            MissionWorldRoomPublicationIssueKind::invalidStartSelectionSource);
    }

    if (room.selectedStart.has_value()) {
        for (std::size_t index = 0U;
             index < room.selectedStart->position.size(); ++index) {
            if (!std::isfinite(room.selectedStart->position[index])) {
                return MissionWorldRoomPublicationIssue{
                    .kind = MissionWorldRoomPublicationIssueKind::
                        selectedStartPositionNotFinite,
                    .sourceIndex = std::nullopt,
                    .componentIndex = index,
                };
            }
        }
        for (std::size_t index = 0U;
             index < room.selectedStart->axisRotation.size(); ++index) {
            if (!std::isfinite(room.selectedStart->axisRotation[index])) {
                return MissionWorldRoomPublicationIssue{
                    .kind = MissionWorldRoomPublicationIssueKind::
                        selectedStartAxisRotationNotFinite,
                    .sourceIndex = std::nullopt,
                    .componentIndex = index,
                };
            }
        }
    }
    const auto expectedPose = buildPlayerSpawnPose(
        room.startSelection, room.selectedStart, room.runtimeBasis);
    if (!expectedPose.success() ||
        !samePlayerSpawnPose(*expectedPose.pose, room.playerSpawnPose)) {
        return issue(
            MissionWorldRoomPublicationIssueKind::playerSpawnPoseMismatch);
    }

    if (room.semanticCcfSourceCount == 0U) {
        return issue(
            MissionWorldRoomPublicationIssueKind::semanticCcfSourceCountZero);
    }
    if (room.ccfCacheIndexByLoadSource.size() != room.semanticCcfSourceCount) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         semanticCcfSourceCountMismatch);
    }
    if (room.uniqueCcfSourceCount == 0U) {
        return issue(
            MissionWorldRoomPublicationIssueKind::uniqueCcfSourceCountZero);
    }
    if (room.uniqueCcfSourceCount > room.semanticCcfSourceCount) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         uniqueCcfSourceCountExceedsSemanticCount);
    }

    std::size_t observedUniqueCount = 0U;
    for (std::size_t sourceIndex = 0U;
         sourceIndex < room.ccfCacheIndexByLoadSource.size(); ++sourceIndex) {
        const auto cacheIndex = room.ccfCacheIndexByLoadSource[sourceIndex];
        if (cacheIndex >= room.uniqueCcfSourceCount) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::ccfCacheIndexOutOfRange,
                sourceIndex);
        }
        if (cacheIndex > observedUniqueCount) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    ccfCacheFirstUseOrderMismatch,
                                sourceIndex);
        }
        if (cacheIndex == observedUniqueCount) {
            ++observedUniqueCount;
        }
    }
    if (observedUniqueCount != room.uniqueCcfSourceCount) {
        return issue(
            MissionWorldRoomPublicationIssueKind::uniqueCcfSourceCountMismatch);
    }

    return std::nullopt;
}

} // namespace airfix::content
