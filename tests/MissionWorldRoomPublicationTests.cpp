#include "airfix/content/MissionWorldRoomPublication.hpp"
#include "airfix/content/PlayerSpawnPoseBuilder.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using airfix::assets::MissionStartPosition;
using airfix::assets::MissionWorldStartSelectionSource;
using airfix::content::ContentRevision;
using airfix::content::LoadedMissionWorldRoom;
using airfix::content::MissionWorldRoomPublicationIssue;
using airfix::content::MissionWorldRoomPublicationIssueKind;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ContentRevision revision(const std::uint64_t generation = 7U) {
    ContentRevision result{
        .generation = generation,
        .pack =
            {
                .size = 4'096U,
                .sha256 = {},
            },
    };
    for (std::size_t index = 0U; index < result.pack.sha256.size(); ++index) {
        result.pack.sha256[index] = static_cast<std::uint8_t>(0x20U + index);
    }
    return result;
}

[[nodiscard]] LoadedMissionWorldRoom validRootRoom() {
    LoadedMissionWorldRoom room;
    room.revision = revision();
    room.setupEntry.logicalPath = "Missions/Test/Setup.txt";
    room.setupEntry.archiveFileIndex = 0U;
    room.setupSourceFootprintBytes = 0U;
    room.startSelection = {
        .source = MissionWorldStartSelectionSource::rootRoomFallback,
        .startPositionIndex = std::nullopt,
        .worldRoomIndex = 0U,
    };
    room.semanticCcfSourceCount = 3U;
    room.uniqueCcfSourceCount = 2U;
    room.ccfCacheIndexByLoadSource = {0U, 1U, 1U};
    return room;
}

[[nodiscard]] LoadedMissionWorldRoom validTableRoom() {
    auto room = validRootRoom();
    room.startSelection = {
        .source = MissionWorldStartSelectionSource::table,
        .startPositionIndex = airfix::assets::legacyMissionStartCapacity - 1U,
        .worldRoomIndex = std::numeric_limits<std::size_t>::max(),
    };
    room.selectedStart = MissionStartPosition{
        .roomName = "Room",
        .position =
            {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::lowest(),
                0.0F,
            },
        .axisRotation =
            {
                -std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                -0.0F,
            },
        .sourceOffset = std::numeric_limits<std::uint64_t>::max(),
    };
    const auto pose = airfix::content::buildPlayerSpawnPose(
        room.startSelection, room.selectedStart, room.runtimeBasis);
    if (!pose.success()) {
        throw std::runtime_error("valid table pose fixture failed to build");
    }
    room.playerSpawnPose = *pose.pose;
    return room;
}

[[nodiscard]] airfix::render::ConvertedNodeTransform
actorWorldFrom(
    const airfix::simulation::PlayerSpawnPose &pose) {
    const auto vectorAt = [](const std::array<float, 3U> &value) {
        return airfix::render::Vec3{
            value[0], value[1], value[2]};
    };
    return {
        .linear =
            {
                .columns =
                    {
                        vectorAt(
                            pose.runtimeWorldRotationColumns[0]),
                        vectorAt(
                            pose.runtimeWorldRotationColumns[1]),
                        vectorAt(
                            pose.runtimeWorldRotationColumns[2]),
                    },
            },
        .translation = vectorAt(pose.runtimeWorldPosition),
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] LoadedMissionWorldRoom validPlayerRoom() {
    auto room = validTableRoom();
    room.semanticCcfSourceCount = 3U;
    room.uniqueCcfSourceCount = 3U;
    room.ccfCacheIndexByLoadSource = {0U, 1U, 1U};
    room.playerVisualCcfCacheIndex = 2U;
    room.playerVisual =
        airfix::content::MissionPlayerVisualDescriptor{
            .objectDefinitionSource =
                {
                    .logicalPath =
                        "Game\\Objects\\Player.object",
                    .archiveFileIndex = 7U,
                },
            .modelCcfSource =
                {
                    .logicalPath = "Graphics\\Player.ccf",
                    .archiveFileIndex = 8U,
                },
            .blueprintSelector = "Root",
            .textureRoot = std::string{"Graphics\\Textures"},
            .legacySkinSlot = 0U,
            .objectDefinitionSourceAllocationFootprintBytes =
                64U,
            .modelCcfSourceAllocationFootprintBytes = 128U,
        };

    room.model.meshes.resize(2U);
    room.model.instances.push_back({
        .meshSlot = 0U,
        .sourceNodeReference = 11U,
        .modelLinear = {},
        .modelTranslation = {},
    });
    room.meshProvenance.resize(1U);
    room.instanceProvenance.resize(1U);

    const airfix::render::PlayerActorVisualProvenance actor{
        .legacySkinSlot = 0U,
        .blueprintIndex = 4U,
        .blueprintReference = 77U,
        .physicalMeshIndex = 5U,
    };
    const airfix::render::ConvertedNodeTransform actorLocal{
        .linear = {},
        .translation = {2.0F, 3.0F, 4.0F},
        .rawScalar = 1.0F,
    };
    const auto absolute = airfix::render::composeNodeTransforms(
        actorWorldFrom(room.playerSpawnPose), actorLocal);
    room.model.instances.push_back({
        .meshSlot = 1U,
        .sourceNodeReference = actor.blueprintReference,
        .modelLinear = absolute.linear,
        .modelTranslation = absolute.translation,
    });
    room.playerActorMeshProvenance.push_back({
        .actor = actor,
        .finalMeshSlot = 1U,
    });
    room.playerActorInstanceProvenance.push_back({
        .actor = actor,
        .finalInstanceIndex = 1U,
        .actorLocal = actorLocal,
    });
    room.playerActorBinding =
        airfix::render::PlayerActorSceneBinding{
            .firstMeshSlot = 1U,
            .meshCount = 1U,
            .firstInstanceIndex = 1U,
            .instanceCount = 1U,
        };
    return room;
}

void requireIssue(const LoadedMissionWorldRoom &room,
                  const MissionWorldRoomPublicationIssue expected,
                  const std::string_view message) {
    const auto actual =
        airfix::content::validateMissionWorldRoomPublication(room, revision());
    require(actual == std::optional{expected}, message);
}

void testValidBoundaryRooms() {
    const auto root = validRootRoom();
    require(
        !airfix::content::validateMissionWorldRoomPublication(root, revision())
             .has_value(),
        "valid root fallback or zero setup footprint was rejected");

    const auto table = validTableRoom();
    require(
        !airfix::content::validateMissionWorldRoomPublication(table, revision())
             .has_value(),
        "valid table boundary values were rejected");
}

void testIdentityAndParallelProvenance() {
    {
        auto room = validRootRoom();
        const auto issue = airfix::content::validateMissionWorldRoomPublication(
            room, revision(8U));
        require(issue == std::optional{MissionWorldRoomPublicationIssue{
                             .kind = MissionWorldRoomPublicationIssueKind::
                                 revisionMismatch}},
                "revision mismatch was not rejected");
    }
    {
        auto room = validRootRoom();
        room.setupEntry.logicalPath.clear();
        requireIssue(
            room,
            {.kind =
                 MissionWorldRoomPublicationIssueKind::emptySetupLogicalPath},
            "empty setup identity was not rejected");
    }
    {
        auto room = validRootRoom();
        room.meshProvenance.push_back({});
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          meshProvenanceCountMismatch},
                     "mesh provenance mismatch was not rejected");
    }
    {
        auto room = validRootRoom();
        room.instanceProvenance.push_back({});
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          instanceProvenanceCountMismatch},
                     "instance provenance mismatch was not rejected");
    }
}

void testRootFallbackMutations() {
    {
        auto room = validRootRoom();
        room.startSelection.source =
            static_cast<MissionWorldStartSelectionSource>(0xffU);
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          invalidStartSelectionSource},
                     "forged start-selection source was not rejected");
    }
    {
        auto room = validRootRoom();
        room.startSelection.startPositionIndex = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          rootFallbackStartPositionIndexPresent},
                     "root fallback table index was not rejected");
    }
    {
        auto room = validRootRoom();
        room.selectedStart = MissionStartPosition{.roomName = "Room"};
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          rootFallbackSelectedStartPresent},
                     "root fallback selected start was not rejected");
    }
    {
        auto room = validRootRoom();
        room.startSelection.worldRoomIndex = 1U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          rootFallbackWorldRoomIndexNotZero},
                     "non-root fallback room was not rejected");
    }
}

void testTableMutationsAndFiniteness() {
    {
        auto room = validTableRoom();
        room.startSelection.startPositionIndex.reset();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableStartPositionIndexMissing},
                     "missing table index was not rejected");
    }
    {
        auto room = validTableRoom();
        room.startSelection.startPositionIndex =
            airfix::assets::legacyMissionStartCapacity;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 tableStartPositionIndexLimitExceeded,
             .sourceIndex = airfix::assets::legacyMissionStartCapacity},
            "one-past legacy start capacity was not rejected");
    }
    {
        auto room = validTableRoom();
        room.selectedStart.reset();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableSelectedStartMissing},
                     "missing selected table start was not rejected");
    }
    {
        auto room = validTableRoom();
        room.startSelection.worldRoomIndex = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableWorldRoomIndexIsRoot},
                     "table selection of the root room was not rejected");
    }
    {
        auto room = validTableRoom();
        room.selectedStart->roomName.clear();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          tableSelectedStartRoomNameEmpty},
                     "empty selected table room name was not rejected");
    }
    for (std::size_t component = 0U; component < 3U; ++component) {
        auto room = validTableRoom();
        room.selectedStart->position[component] =
            std::numeric_limits<float>::infinity();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          selectedStartPositionNotFinite,
                      .componentIndex = component},
                     "non-finite selected position component was not rejected");
    }
    for (std::size_t component = 0U; component < 3U; ++component) {
        auto room = validTableRoom();
        room.selectedStart->axisRotation[component] =
            std::numeric_limits<float>::quiet_NaN();
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          selectedStartAxisRotationNotFinite,
                      .componentIndex = component},
                     "non-finite selected rotation component was not rejected");
    }
}

void testPlayerSpawnPoseBinding() {
    {
        auto room = validRootRoom();
        room.playerSpawnPose.startPositionIndex = 0U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerSpawnPoseMismatch},
            "forged root player pose was accepted");
    }
    {
        auto room = validTableRoom();
        room.playerSpawnPose.legacyAxisRotationRadians[2] = 0.0F;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerSpawnPoseMismatch},
            "table player pose lost the authenticated negative zero");
    }
    {
        auto room = validTableRoom();
        room.runtimeBasis.runtimeUnitsPerSourceUnit = 2.0F;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerSpawnPoseMismatch},
            "player pose detached from the room basis was accepted");
    }
}

void testCcfCountAndCacheMutations() {
    {
        auto room = validRootRoom();
        room.semanticCcfSourceCount = 0U;
        room.ccfCacheIndexByLoadSource.clear();
        room.uniqueCcfSourceCount = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          semanticCcfSourceCountZero},
                     "zero semantic CCF count was not rejected");
    }
    {
        auto room = validRootRoom();
        room.semanticCcfSourceCount = 4U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          semanticCcfSourceCountMismatch},
                     "semantic CCF/vector count mismatch was not rejected");
    }
    {
        auto room = validRootRoom();
        room.uniqueCcfSourceCount = 0U;
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          uniqueCcfSourceCountZero},
                     "zero unique CCF count was not rejected");
    }
    {
        auto room = validRootRoom();
        room.uniqueCcfSourceCount = 4U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 uniqueCcfSourceCountExceedsSemanticCount},
            "unique CCF count greater than semantic count was not rejected");
    }
    {
        auto room = validRootRoom();
        room.ccfCacheIndexByLoadSource[2] = 2U;
        requireIssue(
            room,
            {.kind =
                 MissionWorldRoomPublicationIssueKind::ccfCacheIndexOutOfRange,
             .sourceIndex = 2U},
            "out-of-range cache index was not rejected");
    }
    {
        auto room = validRootRoom();
        room.ccfCacheIndexByLoadSource = {1U, 0U, 1U};
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          ccfCacheFirstUseOrderMismatch,
                      .sourceIndex = 0U},
                     "non-canonical cache first-use order was not rejected");
    }
    {
        auto room = validRootRoom();
        room.ccfCacheIndexByLoadSource = {0U, 0U, 0U};
        requireIssue(room,
                     {.kind = MissionWorldRoomPublicationIssueKind::
                          uniqueCcfSourceCountMismatch},
                     "unrepresented unique cache index was not rejected");
    }
}

void testPlayerActorPublicationBoundary() {
    {
        const auto room = validPlayerRoom();
        require(
            !airfix::content::validateMissionWorldRoomPublication(
                 room, revision()).has_value(),
            "valid player publication was rejected");
    }
    {
        auto room = validRootRoom();
        room.playerVisualCcfCacheIndex = 0U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerVisualCooccurrenceMismatch},
            "orphan player cache index was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorBinding.reset();
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerVisualCooccurrenceMismatch},
            "player descriptor without binding was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorMeshProvenance.clear();
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerVisualCooccurrenceMismatch},
            "player descriptor with empty actor provenance was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerVisual->blueprintSelector.clear();
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 invalidPlayerVisualDescriptor},
            "invalid player descriptor was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerVisualCcfCacheIndex = 3U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerVisualCcfCacheIndexOutOfRange,
             .sourceIndex = 3U},
            "out-of-range player cache index was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.uniqueCcfSourceCount = 4U;
        room.playerVisualCcfCacheIndex = 3U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 ccfCacheFirstUseOrderMismatch,
             .sourceIndex = 3U},
            "non-canonical player cache first use was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorBinding->firstMeshSlot = 0U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 staticMeshProvenancePrefixMismatch},
            "detached static mesh prefix was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorBinding->firstMeshSlot =
            std::numeric_limits<std::size_t>::max();
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorBindingRangeOverflow},
            "overflowing player actor binding was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorBinding->meshCount = 2U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorFinalMeshCountMismatch},
            "forged final actor mesh range was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorBinding->instanceCount = 2U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorFinalInstanceCountMismatch},
            "forged final actor instance range was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorMeshProvenance.push_back(
            room.playerActorMeshProvenance.front());
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorMeshProvenanceCountMismatch},
            "actor mesh provenance count mismatch was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorInstanceProvenance.push_back(
            room.playerActorInstanceProvenance.front());
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorInstanceProvenanceCountMismatch},
            "actor instance provenance count mismatch was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorMeshProvenance[0].finalMeshSlot = 0U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorFinalMeshSlotMismatch,
             .sourceIndex = 0U},
            "non-contiguous actor mesh slot was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorInstanceProvenance[0].finalInstanceIndex =
            0U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorFinalInstanceIndexMismatch,
             .sourceIndex = 0U},
            "non-contiguous actor instance index was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorInstanceProvenance[0]
            .actor.legacySkinSlot = 1U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorSkinSlotMismatch,
             .sourceIndex = 0U},
            "non-zero actor skin slot was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.model.instances[1].sourceNodeReference ^= 1U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorSourceReferenceMismatch,
             .sourceIndex = 0U},
            "forged actor source reference was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorInstanceProvenance[0]
            .actor.physicalMeshIndex ^= 1U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorPhysicalMeshMismatch,
             .sourceIndex = 0U},
            "forged actor physical mesh identity was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.model.instances[1].modelTranslation.x =
            std::nextafter(
                room.model.instances[1].modelTranslation.x,
                std::numeric_limits<float>::infinity());
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerActorTransformMismatch,
             .sourceIndex = 0U},
            "bitwise actor transform mutation was accepted");
    }
}

} // namespace

int main() {
    try {
        testValidBoundaryRooms();
        testIdentityAndParallelProvenance();
        testRootFallbackMutations();
        testTableMutationsAndFiniteness();
        testPlayerSpawnPoseBinding();
        testCcfCountAndCacheMutations();
        testPlayerActorPublicationBoundary();
    } catch (const std::exception &error) {
        std::cerr << "Mission world publication tests failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "Mission world publication tests passed\n";
    return 0;
}
