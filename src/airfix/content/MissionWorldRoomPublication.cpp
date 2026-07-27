#include "airfix/content/MissionWorldRoomPublication.hpp"

#include "airfix/content/PlayerSpawnPoseBuilder.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>

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

[[nodiscard]] bool checkedAdd(const std::size_t left,
                              const std::size_t right,
                              std::size_t &result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool sameVecBits(const render::Vec3 &left,
                               const render::Vec3 &right) noexcept {
    return sameFloatBits(left.x, right.x) &&
           sameFloatBits(left.y, right.y) &&
           sameFloatBits(left.z, right.z);
}

[[nodiscard]] bool sameMatBits(const render::Mat3 &left,
                               const render::Mat3 &right) noexcept {
    return sameVecBits(left.columns[0], right.columns[0]) &&
           sameVecBits(left.columns[1], right.columns[1]) &&
           sameVecBits(left.columns[2], right.columns[2]);
}

[[nodiscard]] render::ConvertedNodeTransform
actorWorldFrom(const simulation::PlayerSpawnPose &pose) noexcept {
    const auto vectorAt = [](const std::array<float, 3U> &value) {
        return render::Vec3{value[0], value[1], value[2]};
    };
    return {
        .linear =
            {
                .columns =
                    {
                        vectorAt(pose.runtimeWorldRotationColumns[0]),
                        vectorAt(pose.runtimeWorldRotationColumns[1]),
                        vectorAt(pose.runtimeWorldRotationColumns[2]),
                    },
            },
        .translation = vectorAt(pose.runtimeWorldPosition),
        .rawScalar = 1.0F,
    };
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
    const bool hasPlayer = room.playerVisual.has_value();
    if (!hasPlayer) {
        if (room.playerVisualCcfCacheIndex.has_value() ||
            room.playerActorBinding.has_value() ||
            !room.playerActorMeshProvenance.empty() ||
            !room.playerActorInstanceProvenance.empty()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerVisualCooccurrenceMismatch);
        }
        if (room.meshProvenance.size() != room.model.meshes.size()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             meshProvenanceCountMismatch);
        }
        if (room.instanceProvenance.size() !=
            room.model.instances.size()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             instanceProvenanceCountMismatch);
        }
    } else if (
        !room.playerVisualCcfCacheIndex.has_value() ||
        !room.playerActorBinding.has_value() ||
        room.playerActorMeshProvenance.empty() ||
        room.playerActorInstanceProvenance.empty()) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         playerVisualCooccurrenceMismatch);
    }
    if (hasPlayer && !room.playerVisual->valid()) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         invalidPlayerVisualDescriptor);
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
    std::size_t totalSemanticCcfSources = 0U;
    if (!checkedAdd(room.semanticCcfSourceCount,
                    hasPlayer ? 1U : 0U,
                    totalSemanticCcfSources)) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         playerVisualCooccurrenceMismatch);
    }
    if (room.uniqueCcfSourceCount > totalSemanticCcfSources) {
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
    if (hasPlayer) {
        const auto cacheIndex = *room.playerVisualCcfCacheIndex;
        if (cacheIndex >= room.uniqueCcfSourceCount) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    playerVisualCcfCacheIndexOutOfRange,
                room.semanticCcfSourceCount);
        }
        if (cacheIndex > observedUniqueCount) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    ccfCacheFirstUseOrderMismatch,
                room.semanticCcfSourceCount);
        }
        if (cacheIndex == observedUniqueCount) {
            ++observedUniqueCount;
        }
    }
    if (observedUniqueCount != room.uniqueCcfSourceCount) {
        return issue(
            MissionWorldRoomPublicationIssueKind::uniqueCcfSourceCountMismatch);
    }

    if (hasPlayer) {
        const auto &binding = *room.playerActorBinding;
        std::size_t finalMeshCount = 0U;
        std::size_t finalInstanceCount = 0U;
        if (!checkedAdd(binding.firstMeshSlot,
                        binding.meshCount,
                        finalMeshCount) ||
            !checkedAdd(binding.firstInstanceIndex,
                        binding.instanceCount,
                        finalInstanceCount)) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerActorBindingRangeOverflow);
        }
        if (room.meshProvenance.size() != binding.firstMeshSlot) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             staticMeshProvenancePrefixMismatch);
        }
        if (room.instanceProvenance.size() !=
            binding.firstInstanceIndex) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             staticInstanceProvenancePrefixMismatch);
        }
        if (finalMeshCount != room.model.meshes.size()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerActorFinalMeshCountMismatch);
        }
        if (finalInstanceCount != room.model.instances.size()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerActorFinalInstanceCountMismatch);
        }
        if (room.playerActorMeshProvenance.size() !=
            binding.meshCount) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerActorMeshProvenanceCountMismatch);
        }
        if (room.playerActorInstanceProvenance.size() !=
            binding.instanceCount) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerActorInstanceProvenanceCountMismatch);
        }

        for (std::size_t actorMeshIndex = 0U;
             actorMeshIndex <
             room.playerActorMeshProvenance.size();
             ++actorMeshIndex) {
            const auto &provenance =
                room.playerActorMeshProvenance[actorMeshIndex];
            const auto expectedSlot =
                binding.firstMeshSlot + actorMeshIndex;
            if (provenance.finalMeshSlot != expectedSlot) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorFinalMeshSlotMismatch,
                    actorMeshIndex);
            }
            if (provenance.actor.legacySkinSlot != 0U) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorSkinSlotMismatch,
                    actorMeshIndex);
            }
        }

        const auto actorWorld = actorWorldFrom(room.playerSpawnPose);
        for (std::size_t actorInstanceIndex = 0U;
             actorInstanceIndex <
             room.playerActorInstanceProvenance.size();
             ++actorInstanceIndex) {
            const auto &provenance =
                room.playerActorInstanceProvenance[
                    actorInstanceIndex];
            const auto expectedIndex =
                binding.firstInstanceIndex + actorInstanceIndex;
            if (provenance.finalInstanceIndex != expectedIndex) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorFinalInstanceIndexMismatch,
                    actorInstanceIndex);
            }
            if (provenance.actor.legacySkinSlot != 0U) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorSkinSlotMismatch,
                    actorInstanceIndex);
            }
            const auto &instance =
                room.model.instances[expectedIndex];
            if (instance.sourceNodeReference !=
                provenance.actor.blueprintReference) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorSourceReferenceMismatch,
                    actorInstanceIndex);
            }
            const auto finalMeshSlot =
                static_cast<std::size_t>(instance.meshSlot);
            if (finalMeshSlot < binding.firstMeshSlot ||
                finalMeshSlot >= finalMeshCount) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorFinalMeshSlotMismatch,
                    actorInstanceIndex);
            }
            const auto actorMeshSlot =
                finalMeshSlot - binding.firstMeshSlot;
            if (provenance.actor.physicalMeshIndex !=
                room.playerActorMeshProvenance[actorMeshSlot]
                    .actor.physicalMeshIndex) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorPhysicalMeshMismatch,
                    actorInstanceIndex);
            }
            if (!sameFloatBits(
                    provenance.actorLocal.rawScalar, 1.0F)) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorTransformMismatch,
                    actorInstanceIndex);
            }
            try {
                const auto expected =
                    render::composeNodeTransforms(
                        actorWorld, provenance.actorLocal);
                if (!sameMatBits(
                        expected.linear, instance.modelLinear) ||
                    !sameVecBits(
                        expected.translation,
                        instance.modelTranslation)) {
                    return indexedIssue(
                        MissionWorldRoomPublicationIssueKind::
                            playerActorTransformMismatch,
                        actorInstanceIndex);
                }
            } catch (...) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorTransformMismatch,
                    actorInstanceIndex);
            }
        }
    }

    return std::nullopt;
}

} // namespace airfix::content
