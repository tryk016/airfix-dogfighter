#include "airfix/content/MissionWorldRoomPublication.hpp"
#include "airfix/content/PlayerSpawnPoseBuilder.hpp"

#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
    if (trackAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
}

} // namespace

void* operator new(const std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0U ? 1U : size);
        memory != nullptr) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

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

[[nodiscard]] std::uint64_t independentPublishedCpuBytes(
    const LoadedMissionWorldRoom& room) {
    std::uint64_t total = sizeof(LoadedMissionWorldRoom);
    total += room.setupEntry.logicalPath.size();
    total += room.textures.size() *
        sizeof(airfix::content::LoadedTextureAsset);
    total += room.ccfCacheIndexByLoadSource.size() * sizeof(std::size_t);
    total += room.retainedSpatialBytes;
    if (room.playerVisual.has_value()) {
        total +=
            room.playerVisual->objectDefinitionSource.logicalPath.size();
        total += room.playerVisual->modelCcfSource.logicalPath.size();
        total += room.playerVisual->blueprintSelector.size();
        if (room.playerVisual->textureRoot.has_value()) {
            total += room.playerVisual->textureRoot->size();
        }
    }
    if (room.selectedStart.has_value()) {
        total += room.selectedStart->roomName.size();
    }
    for (const auto& texture : room.textures) {
        total += texture.upload.uploadLevels.size() *
            sizeof(airfix::render::GtiUploadLevel);
        total += texture.uploadLevels.size() *
            sizeof(airfix::assets::RgbaImage);
        for (const auto& level : texture.uploadLevels) {
            total += level.pixels.size();
        }
    }
    total += room.model.meshes.size() *
        sizeof(airfix::render::DrawMeshPayload);
    total += room.model.instances.size() *
        sizeof(airfix::render::DrawMeshInstance);
    for (const auto& mesh : room.model.meshes) {
        total += mesh.vertices.size() * sizeof(airfix::render::DrawVertex);
        total += mesh.indices.size() * sizeof(std::uint32_t);
        total += mesh.materials.size() *
            sizeof(airfix::render::DrawMaterial);
        total += mesh.ranges.size() * sizeof(airfix::render::DrawRange);
    }
    total += room.meshProvenance.size() *
        sizeof(airfix::render::MissionWorldRoomMeshProvenance);
    total += room.instanceProvenance.size() *
        sizeof(airfix::render::MissionWorldRoomInstanceProvenance);
    total += room.playerActorMeshProvenance.size() *
        sizeof(airfix::render::PlayerActorSceneMeshProvenance);
    total += room.playerActorInstanceProvenance.size() *
        sizeof(airfix::render::PlayerActorSceneInstanceProvenance);
    if (room.playerActorCollision.has_value()) {
        total += room.playerActorCollision->retainedPayloadBytes;
    }
    total += room.submission.meshUploads.size() *
        sizeof(airfix::render::DrawMeshUploadMetadata);
    total += room.submission.commands.size() *
        sizeof(airfix::render::DrawSubmissionCommand);
    return total;
}

void refreshPublishedCpuBytes(LoadedMissionWorldRoom& room) {
    room.publishedCpuBytes = independentPublishedCpuBytes(room);
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
    room.spatialArena.rooms.resize(1U);
    room.spatialArena.retainedPayloadBytes =
        sizeof(airfix::assets::MissionWorldSpatialRoom);
    room.retainedSpatialBytes =
        room.spatialArena.retainedPayloadBytes;
    refreshPublishedCpuBytes(room);
    return room;
}

[[nodiscard]] LoadedMissionWorldRoom validTableRoom() {
    auto room = validRootRoom();
    room.startSelection = {
        .source = MissionWorldStartSelectionSource::table,
        .startPositionIndex = airfix::assets::legacyMissionStartCapacity - 1U,
        .worldRoomIndex = 1U,
    };
    room.spatialArena.rooms.resize(2U);
    room.spatialArena.retainedPayloadBytes =
        2U * sizeof(airfix::assets::MissionWorldSpatialRoom);
    room.retainedSpatialBytes =
        room.spatialArena.retainedPayloadBytes;
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
    refreshPublishedCpuBytes(room);
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
    airfix::render::ConvertedMeshGeometry collisionGeometry;
    collisionGeometry.reference = 88U;
    collisionGeometry.vertices = {
        {{-1.0F, -1.0F, 0.0F}},
        {{1.0F, -1.0F, 0.0F}},
        {{0.0F, 1.0F, 0.0F}},
    };
    collisionGeometry.triangles = {
        {
            .vertexIndices = {0U, 1U, 2U},
            .materialReference = 99U,
        },
    };
    auto collisionMesh =
        airfix::render::buildLegacyDynamicBsp(
            collisionGeometry);
    if (!collisionMesh.complete()) {
        throw std::runtime_error(
            "valid player collision fixture failed to build");
    }
    airfix::render::PlayerActorCollisionAssembly collision;
    collision.meshes.push_back(std::move(collisionMesh));
    collision.meshProvenance.push_back({
        .actor = actor,
        .collisionMeshIndex = 0U,
        .sourceMeshReference = 88U,
    });
    collision.instances.push_back({
        .collisionMeshIndex = 0U,
        .actor = actor,
        .actorLocal = actorLocal,
    });
    collision.retainedPayloadBytes =
        sizeof(airfix::render::LegacyDynamicBspMesh) +
        sizeof(airfix::render::PlayerActorCollisionMeshProvenance) +
        sizeof(airfix::render::PlayerActorCollisionInstance) +
        collision.meshes[0].retainedPayloadBytes;
    if (!collision.complete()) {
        throw std::runtime_error(
            "valid player collision fixture is incomplete");
    }
    room.playerActorCollision = std::move(collision);
    refreshPublishedCpuBytes(room);
    return room;
}

void requireIssue(const LoadedMissionWorldRoom &room,
                  const MissionWorldRoomPublicationIssue expected,
                  const std::string_view message) {
    const auto actual =
        airfix::content::validateMissionWorldRoomPublication(room, revision());
    require(actual == std::optional{expected}, message);
}

void refreshSpatialBytes(LoadedMissionWorldRoom& room) {
    auto& arena = room.spatialArena;
    arena.retainedPayloadBytes =
        arena.rooms.size() *
            sizeof(airfix::assets::MissionWorldSpatialRoom) +
        arena.treeReferences.size() * sizeof(std::size_t) +
        arena.trees.size() *
            sizeof(airfix::assets::MissionWorldSpatialTree) +
        arena.nodes.size() *
            sizeof(airfix::assets::MissionWorldSpatialNode) +
        arena.polygons.size() *
            sizeof(airfix::assets::MissionWorldSpatialPolygon);
    room.retainedSpatialBytes = arena.retainedPayloadBytes;
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

void testPublishedCpuByteAccountingBoundary() {
    {
        const auto root = validRootRoom();
        const auto table = validTableRoom();
        const auto player = validPlayerRoom();
        require(
            root.publishedCpuBytes ==
                    independentPublishedCpuBytes(root) &&
                table.publishedCpuBytes ==
                    independentPublishedCpuBytes(table) &&
                player.publishedCpuBytes ==
                    independentPublishedCpuBytes(player),
            "valid fixture published-byte counters are stale");
    }
    {
        auto room = validPlayerRoom();
        room.textures.resize(1U);
        room.textures[0].upload.uploadLevels.resize(2U);
        room.textures[0].uploadLevels.resize(2U);
        room.textures[0].uploadLevels[0].pixels.resize(3U);
        room.textures[0].uploadLevels[1].pixels.resize(7U);

        auto& mesh = room.model.meshes[0];
        mesh.vertices.resize(1U);
        mesh.indices.resize(2U);
        mesh.materials.resize(3U);
        mesh.ranges.resize(4U);
        room.submission.meshUploads.resize(2U);
        room.submission.commands.resize(5U);
        refreshPublishedCpuBytes(room);

        require(
            !airfix::content::validateMissionWorldRoomPublication(
                 room, revision())
                 .has_value(),
            "independently accounted nested published payload was rejected");
        require(
            room.publishedCpuBytes ==
                independentPublishedCpuBytes(room),
            "nested published payload was not accounted exactly");

        ++room.publishedCpuBytes;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 publishedCpuBytesMismatch},
            "one-byte published CPU overcount was accepted");

        room.publishedCpuBytes =
            independentPublishedCpuBytes(room) - 1U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 publishedCpuBytesMismatch},
            "one-byte published CPU undercount was accepted");
    }
    {
        auto room = validRootRoom();
        room.publishedCpuBytes =
            std::numeric_limits<std::uint64_t>::max();
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 publishedCpuBytesMismatch},
            "maximum forged published CPU count was accepted");
    }
}

void testPublicationValidationDoesNotAllocate() {
    const auto room = validPlayerRoom();
    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    const auto publicationIssue =
        airfix::content::validateMissionWorldRoomPublication(
            room, revision());
    trackAllocations.store(false, std::memory_order_release);

    require(
        !publicationIssue.has_value(),
        "valid player room failed allocation-free publication");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "publication validation allocated memory");
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
        room.playerActorCollision.reset();
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerCollisionCooccurrenceMismatch},
            "player descriptor without collision assets was accepted");
    }
    {
        auto player = validPlayerRoom();
        auto room = validRootRoom();
        room.playerActorCollision =
            std::move(player.playerActorCollision);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerCollisionCooccurrenceMismatch},
            "orphan player collision assets were accepted");
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
    {
        auto room = validPlayerRoom();
        ++room.playerActorCollision->retainedPayloadBytes;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerCollisionIncomplete},
            "forged collision retained bytes were accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorCollision->meshProvenance[0]
            .actor.blueprintReference ^= 1U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerCollisionProvenanceMismatch,
             .sourceIndex = 0U},
            "forged collision mesh provenance was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorCollision->instances[0]
            .actorLocal.translation.x =
            std::nextafter(
                room.playerActorCollision->instances[0]
                    .actorLocal.translation.x,
                std::numeric_limits<float>::infinity());
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerCollisionTransformMismatch,
             .sourceIndex = 0U},
            "bitwise collision-local transform mutation was accepted");
    }
    {
        auto room = validPlayerRoom();
        room.playerActorCollision->instances.push_back(
            room.playerActorCollision->instances.front());
        room.playerActorCollision->retainedPayloadBytes +=
            sizeof(airfix::render::PlayerActorCollisionInstance);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 playerCollisionInstanceCountMismatch},
            "collision instance count mismatch was accepted");
    }
}

void testSpatialPublicationBoundary() {
    {
        auto room = validRootRoom();
        room.spatialArena.issues.push_back({});
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialArenaIncomplete},
            "incomplete spatial arena was accepted");
    }
    {
        auto room = validRootRoom();
        ++room.retainedSpatialBytes;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialPayloadByteMismatch},
            "forged spatial byte count was accepted");
    }
    {
        auto room = validTableRoom();
        room.spatialArena.rooms.resize(1U);
        refreshSpatialBytes(room);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialStartRoomOutOfRange},
            "out-of-range selected spatial room was accepted");
    }
    {
        auto room = validRootRoom();
        room.spatialArena.rooms[0].firstStaticTreeReference = 1U;
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialRoomRangeInvalid,
             .sourceIndex = 0U},
            "forged spatial room range was accepted");
    }
    {
        auto room = validRootRoom();
        auto& arena = room.spatialArena;
        arena.treeReferences = {0U};
        arena.rooms[0].staticTreeCount = 1U;
        arena.rooms[0].firstPortalTreeReference = 1U;
        arena.trees.push_back({
            .kind = airfix::assets::CcfBspTreeKind::staticTree,
            .sourceIndex = room.semanticCcfSourceCount,
            .worldRoomIndex = 0U,
            .rootNodeIndex = 0U,
            .firstNodeIndex = 0U,
            .nodeCount = 1U,
        });
        arena.nodes.push_back({
            .splitNormal = {0.0F, 0.0F, 1.0F},
            .pointOnPlane = {},
        });
        refreshSpatialBytes(room);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialTreeInvalid,
             .sourceIndex = 0U},
            "forged spatial tree owner was accepted");
    }
    {
        auto room = validRootRoom();
        auto& arena = room.spatialArena;
        arena.treeReferences = {1U, 1U};
        arena.rooms[0].staticTreeCount = 2U;
        arena.rooms[0].firstPortalTreeReference = 2U;
        arena.trees = {
            {
                .kind = airfix::assets::CcfBspTreeKind::staticTree,
                .sourceIndex = 0U,
                .worldRoomIndex = 0U,
                .rootNodeIndex = 0U,
                .firstNodeIndex = 0U,
                .nodeCount = 1U,
            },
            {
                .kind = airfix::assets::CcfBspTreeKind::staticTree,
                .sourceIndex = 0U,
                .worldRoomIndex = 0U,
                .rootNodeIndex = 1U,
                .firstNodeIndex = 1U,
                .nodeCount = 1U,
            },
        };
        arena.nodes = {
            {
                .splitNormal = {0.0F, 0.0F, 1.0F},
                .pointOnPlane = {},
            },
            {
                .splitNormal = {0.0F, 0.0F, 1.0F},
                .pointOnPlane = {},
            },
        };
        refreshSpatialBytes(room);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialTreeInvalid,
             .sourceIndex = 1U},
            "duplicate spatial tree reference was accepted");
    }
    {
        auto room = validRootRoom();
        auto& arena = room.spatialArena;
        arena.treeReferences = {0U};
        arena.rooms[0].staticTreeCount = 1U;
        arena.rooms[0].firstPortalTreeReference = 1U;
        arena.trees.push_back({
            .kind = airfix::assets::CcfBspTreeKind::staticTree,
            .sourceIndex = 0U,
            .worldRoomIndex = 0U,
            .rootNodeIndex = 0U,
            .firstNodeIndex = 0U,
            .nodeCount = 1U,
        });
        arena.nodes.push_back({
            .childAIndex = 2U,
            .splitNormal = {0.0F, 0.0F, 1.0F},
            .pointOnPlane = {},
        });
        refreshSpatialBytes(room);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialNodeInvalid,
             .sourceIndex = 0U},
            "out-of-tree spatial child was accepted");
    }
    {
        auto room = validRootRoom();
        auto& arena = room.spatialArena;
        arena.treeReferences = {0U};
        arena.rooms[0].staticTreeCount = 1U;
        arena.rooms[0].firstPortalTreeReference = 1U;
        arena.trees.push_back({
            .kind = airfix::assets::CcfBspTreeKind::staticTree,
            .sourceIndex = 0U,
            .worldRoomIndex = 0U,
            .rootNodeIndex = 0U,
            .firstNodeIndex = 0U,
            .nodeCount = 2U,
        });
        arena.nodes = {
            {
                .childAIndex = 0U,
                .splitNormal = {0.0F, 0.0F, 1.0F},
                .pointOnPlane = {},
            },
            {
                .splitNormal = {0.0F, 0.0F, 1.0F},
                .pointOnPlane = {},
            },
        };
        refreshSpatialBytes(room);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialNodeInvalid,
             .sourceIndex = 0U},
            "spatial child cycle was accepted");

        arena.nodes[0].childAIndex = std::nullopt;
        refreshSpatialBytes(room);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialNodeInvalid,
             .sourceIndex = 0U},
            "disconnected spatial node was accepted");
    }
    {
        auto room = validRootRoom();
        auto& arena = room.spatialArena;
        arena.treeReferences = {0U};
        arena.rooms[0].staticTreeCount = 1U;
        arena.rooms[0].firstPortalTreeReference = 1U;
        arena.trees.push_back({
            .kind = airfix::assets::CcfBspTreeKind::staticTree,
            .sourceIndex = 0U,
            .worldRoomIndex = 0U,
            .rootNodeIndex = 0U,
            .firstNodeIndex = 0U,
            .nodeCount = 1U,
        });
        arena.nodes.push_back({
            .splitNormal = {0.0F, 0.0F, 1.0F},
            .pointOnPlane = {},
            .firstPolygonIndex = 0U,
            .polygonCount = 1U,
        });
        arena.polygons.push_back({
            .faceCross = {0.0F, 0.0F, 1.0F},
            .faceNormal = {0.0F, 0.0F, 1.0F},
            .point0 = {
                std::numeric_limits<float>::infinity(),
                0.0F,
                0.0F,
            },
            .edge01 = {1.0F, 0.0F, 0.0F},
            .edge12 = {-1.0F, 1.0F, 0.0F},
        });
        refreshSpatialBytes(room);
        requireIssue(
            room,
            {.kind = MissionWorldRoomPublicationIssueKind::
                 spatialPolygonInvalid,
             .sourceIndex = 0U},
            "non-finite spatial polygon was accepted");
    }
}

} // namespace

int main() {
    try {
        testValidBoundaryRooms();
        testPublishedCpuByteAccountingBoundary();
        testPublicationValidationDoesNotAllocate();
        testIdentityAndParallelProvenance();
        testRootFallbackMutations();
        testTableMutationsAndFiniteness();
        testPlayerSpawnPoseBinding();
        testCcfCountAndCacheMutations();
        testPlayerActorPublicationBoundary();
        testSpatialPublicationBoundary();
    } catch (const std::exception &error) {
        std::cerr << "Mission world publication tests failed: " << error.what()
                  << '\n';
        return 1;
    }

    std::cout << "Mission world publication tests passed\n";
    return 0;
}
