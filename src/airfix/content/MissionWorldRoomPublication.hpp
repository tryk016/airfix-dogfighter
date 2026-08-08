#pragma once

#include "airfix/content/MissionWorldRoomLoader.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

// This is a narrow, allocation-free structural check at the native
// publication boundary. The renderer remains responsible for validating the
// complete DrawModelPayload and DrawSubmissionPlan contracts.
enum class MissionWorldRoomPublicationIssueKind : std::uint8_t {
    revisionMismatch,
    emptySetupLogicalPath,
    meshProvenanceCountMismatch,
    instanceProvenanceCountMismatch,
    invalidStartSelectionSource,
    rootFallbackStartPositionIndexPresent,
    rootFallbackSelectedStartPresent,
    rootFallbackWorldRoomIndexNotZero,
    tableStartPositionIndexMissing,
    tableStartPositionIndexLimitExceeded,
    tableWorldRoomIndexIsRoot,
    tableSelectedStartMissing,
    tableSelectedStartRoomNameEmpty,
    selectedStartPositionNotFinite,
    selectedStartAxisRotationNotFinite,
    playerSpawnPoseMismatch,
    spatialArenaIncomplete,
    spatialPayloadByteMismatch,
    spatialStartRoomOutOfRange,
    spatialRoomRangeInvalid,
    spatialTreeInvalid,
    spatialNodeInvalid,
    spatialPolygonInvalid,
    placedCollisionIncomplete,
    placedCollisionRoomCountMismatch,
    placedCollisionSourceIndexOutOfRange,
    textureModeSummaryMismatch,
    textureReplacementProvenanceMismatch,
    publishedCpuBytesMismatch,
    semanticCcfSourceCountZero,
    semanticCcfSourceCountMismatch,
    uniqueCcfSourceCountZero,
    uniqueCcfSourceCountExceedsSemanticCount,
    ccfCacheIndexOutOfRange,
    ccfCacheFirstUseOrderMismatch,
    uniqueCcfSourceCountMismatch,
    levelObjectAdmissionCooccurrenceMismatch,
    levelObjectAdmissionOrderMismatch,
    levelObjectDefinitionIndexOutOfRange,
    levelObjectCacheJoinMismatch,
    levelObjectRoomIdentityMismatch,
    levelObjectPlacementRangeMismatch,
    levelObjectMeshProvenanceMismatch,
    levelObjectInstanceProvenanceMismatch,
    levelObjectTransformMismatch,
    levelObjectMaterialAssetOutOfRange,
    playerVisualCooccurrenceMismatch,
    invalidPlayerVisualDescriptor,
    playerVisualCcfCacheIndexOutOfRange,
    staticMeshProvenancePrefixMismatch,
    staticInstanceProvenancePrefixMismatch,
    playerActorBindingRangeOverflow,
    playerActorFinalMeshCountMismatch,
    playerActorFinalInstanceCountMismatch,
    playerActorMeshProvenanceCountMismatch,
    playerActorInstanceProvenanceCountMismatch,
    playerActorFinalMeshSlotMismatch,
    playerActorFinalInstanceIndexMismatch,
    playerActorSkinSlotMismatch,
    playerActorSourceReferenceMismatch,
    playerActorPhysicalMeshMismatch,
    playerActorTransformMismatch,
    playerCollisionCooccurrenceMismatch,
    playerCollisionIncomplete,
    playerCollisionMeshCountMismatch,
    playerCollisionInstanceCountMismatch,
    playerCollisionProvenanceMismatch,
    playerCollisionTransformMismatch,
};

struct MissionWorldRoomPublicationIssue {
    MissionWorldRoomPublicationIssueKind kind{
        MissionWorldRoomPublicationIssueKind::revisionMismatch};
    // Set for a CCF source, table start, object placement/range, or object
    // provenance ordinal, as applicable to the issue kind.
    std::optional<std::size_t> sourceIndex;
    // Set for a non-finite position or axis-rotation component.
    std::optional<std::size_t> componentIndex;

    friend bool operator==(const MissionWorldRoomPublicationIssue &,
                           const MissionWorldRoomPublicationIssue &) = default;
};

// expectedRevision must be the publication ticket's immutable content
// revision. The first issue is returned deterministically and no memory is
// allocated.
[[nodiscard]] std::optional<MissionWorldRoomPublicationIssue>
validateMissionWorldRoomPublication(
    const LoadedMissionWorldRoom &room,
    const ContentRevision &expectedRevision) noexcept;

} // namespace airfix::content
