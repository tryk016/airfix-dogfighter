#include "airfix/content/MissionWorldRoomPublication.hpp"

#include "airfix/content/MissionWorldRoomLoaderDetail.hpp"
#include "airfix/content/PlayerSpawnPoseBuilder.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <string>

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

[[nodiscard]] bool rangeWithin(
    const std::size_t first,
    const std::size_t count,
    const std::size_t size) noexcept {
    return first <= size && count <= size - first;
}

[[nodiscard]] bool finite(
    const assets::CcfVector3& value) noexcept {
    for (const auto component : value) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validFaceNormal(
    const assets::CcfVector3& value) noexcept {
    if (finite(value)) {
        return true;
    }
    for (const auto component : value) {
        if (std::bit_cast<std::uint32_t>(component) !=
            0xFFC00000U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool addPayloadBytes(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t elementSize) noexcept {
    std::uint64_t bytes = 0U;
    return detail::checkedMissionWorldRoomByteProduct(
               count, elementSize, bytes) &&
        detail::checkedMissionWorldRoomByteAdd(total, bytes);
}

[[nodiscard]] bool addPublishedCount(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t elementSize) noexcept {
    std::uint64_t bytes = 0U;
    return detail::checkedMissionWorldRoomByteProduct(
               count, elementSize, bytes) &&
        detail::checkedMissionWorldRoomByteAdd(total, bytes);
}

[[nodiscard]] bool addPublishedString(
    std::uint64_t& total,
    const std::string& value) noexcept {
    return addPublishedCount(total, value.size(), sizeof(char));
}

[[nodiscard]] bool recalculatePublishedCpuBytes(
    const LoadedMissionWorldRoom& room,
    std::uint64_t& total) noexcept {
    total = sizeof(LoadedMissionWorldRoom);
    if (!addPublishedString(total, room.setupEntry.logicalPath) ||
        !addPublishedCount(
            total, room.textures.size(), sizeof(LoadedTextureAsset)) ||
        !addPublishedCount(
            total,
            room.ccfCacheIndexByLoadSource.size(),
            sizeof(std::size_t)) ||
        !addPublishedCount(total, room.levelObjectAdmissions.size(),
                           sizeof(LevelObjectAdmission)) ||
        !detail::checkedMissionWorldRoomByteAdd(total,
                                                room.retainedSpatialBytes)) {
        return false;
    }

    if (room.playerVisual.has_value()) {
        const auto& player = *room.playerVisual;
        if (!addPublishedString(
                total,
                player.objectDefinitionSource.logicalPath) ||
            !addPublishedString(
                total, player.modelCcfSource.logicalPath) ||
            !addPublishedString(total, player.blueprintSelector) ||
            (player.textureRoot.has_value() &&
             !addPublishedString(total, *player.textureRoot))) {
            return false;
        }
    }
    if (room.selectedStart.has_value() &&
        !addPublishedString(total, room.selectedStart->roomName)) {
        return false;
    }

    for (const auto& texture : room.textures) {
        if (!addPublishedCount(
                total,
                texture.upload.uploadLevels.size(),
                sizeof(render::GtiUploadLevel)) ||
            !addPublishedCount(
                total,
                texture.uploadLevels.size(),
                sizeof(assets::RgbaImage))) {
            return false;
        }
        for (const auto& level : texture.uploadLevels) {
            if (!addPublishedCount(
                    total, level.pixels.size(), sizeof(std::uint8_t))) {
                return false;
            }
        }
    }

    if (!addPublishedCount(
            total,
            room.model.meshes.size(),
            sizeof(render::DrawMeshPayload)) ||
        !addPublishedCount(
            total,
            room.model.instances.size(),
            sizeof(render::DrawMeshInstance))) {
        return false;
    }
    for (const auto& mesh : room.model.meshes) {
        if (!addPublishedCount(
                total,
                mesh.vertices.size(),
                sizeof(render::DrawVertex)) ||
            !addPublishedCount(
                total,
                mesh.indices.size(),
                sizeof(std::uint32_t)) ||
            !addPublishedCount(
                total,
                mesh.materials.size(),
                sizeof(render::DrawMaterial)) ||
            !addPublishedCount(
                total,
                mesh.ranges.size(),
                sizeof(render::DrawRange))) {
            return false;
        }
    }

    return addPublishedCount(
               total,
               room.meshProvenance.size(),
               sizeof(render::MissionWorldRoomMeshProvenance)) &&
        addPublishedCount(
               total,
               room.instanceProvenance.size(),
               sizeof(render::MissionWorldRoomInstanceProvenance)) &&
           addPublishedCount(total, room.levelObjectMeshProvenance.size(),
                             sizeof(render::LevelObjectSceneMeshProvenance)) &&
           addPublishedCount(
               total, room.levelObjectInstanceProvenance.size(),
               sizeof(render::LevelObjectSceneInstanceProvenance)) &&
           addPublishedCount(total, room.levelObjectPlacementRanges.size(),
                             sizeof(render::LevelObjectScenePlacementRange)) &&
           addPublishedCount(total, room.playerActorMeshProvenance.size(),
               sizeof(render::PlayerActorSceneMeshProvenance)) &&
        addPublishedCount(
               total,
               room.playerActorInstanceProvenance.size(),
               sizeof(render::PlayerActorSceneInstanceProvenance)) &&
        detail::checkedMissionWorldRoomByteAdd(
            total,
            room.placedDynamicCollision.retainedPayloadBytes) &&
        (!room.playerActorCollision.has_value() ||
         detail::checkedMissionWorldRoomByteAdd(
             total,
             room.playerActorCollision->retainedPayloadBytes)) &&
        addPublishedCount(
               total,
               room.submission.meshUploads.size(),
               sizeof(render::DrawMeshUploadMetadata)) &&
        addPublishedCount(
               total,
               room.submission.commands.size(),
               sizeof(render::DrawSubmissionCommand));
}

[[nodiscard]] std::optional<MissionWorldRoomPublicationIssue>
validateSpatialArena(const LoadedMissionWorldRoom& room) noexcept {
    const auto& arena = room.spatialArena;
    if (!arena.complete()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                spatialArenaIncomplete);
    }
    if (room.startSelection.worldRoomIndex >= arena.rooms.size()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                spatialStartRoomOutOfRange);
    }

    std::uint64_t payloadBytes = 0U;
    if (!addPayloadBytes(
            payloadBytes,
            arena.rooms.size(),
            sizeof(assets::MissionWorldSpatialRoom)) ||
        !addPayloadBytes(
            payloadBytes,
            arena.treeReferences.size(),
            sizeof(std::size_t)) ||
        !addPayloadBytes(
            payloadBytes,
            arena.trees.size(),
            sizeof(assets::MissionWorldSpatialTree)) ||
        !addPayloadBytes(
            payloadBytes,
            arena.nodes.size(),
            sizeof(assets::MissionWorldSpatialNode)) ||
        !addPayloadBytes(
            payloadBytes,
            arena.polygons.size(),
            sizeof(assets::MissionWorldSpatialPolygon)) ||
        payloadBytes != arena.retainedPayloadBytes ||
        payloadBytes != room.retainedSpatialBytes) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                spatialPayloadByteMismatch);
    }

    std::size_t expectedReference = 0U;
    for (std::size_t roomIndex = 0U;
         roomIndex < arena.rooms.size();
         ++roomIndex) {
        const auto& spatialRoom = arena.rooms[roomIndex];
        if (spatialRoom.firstStaticTreeReference !=
                expectedReference ||
            !rangeWithin(
                spatialRoom.firstStaticTreeReference,
                spatialRoom.staticTreeCount,
                arena.treeReferences.size())) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    spatialRoomRangeInvalid,
                roomIndex);
        }
        expectedReference += spatialRoom.staticTreeCount;
        if (spatialRoom.firstPortalTreeReference !=
                expectedReference ||
            !rangeWithin(
                spatialRoom.firstPortalTreeReference,
                spatialRoom.portalTreeCount,
                arena.treeReferences.size())) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    spatialRoomRangeInvalid,
                roomIndex);
        }
        expectedReference += spatialRoom.portalTreeCount;

        for (const auto kind :
             {assets::CcfBspTreeKind::staticTree,
              assets::CcfBspTreeKind::portalTree}) {
            const auto first =
                kind == assets::CcfBspTreeKind::staticTree
                ? spatialRoom.firstStaticTreeReference
                : spatialRoom.firstPortalTreeReference;
            const auto count =
                kind == assets::CcfBspTreeKind::staticTree
                ? spatialRoom.staticTreeCount
                : spatialRoom.portalTreeCount;
            std::optional<std::size_t> previousTreeIndex;
            for (std::size_t local = 0U; local < count; ++local) {
                const auto treeIndex =
                    arena.treeReferences[first + local];
                if (treeIndex >= arena.trees.size()) {
                    return indexedIssue(
                        MissionWorldRoomPublicationIssueKind::
                            spatialTreeInvalid,
                        treeIndex);
                }
                if (previousTreeIndex.has_value() &&
                    treeIndex >= *previousTreeIndex) {
                    return indexedIssue(
                        MissionWorldRoomPublicationIssueKind::
                            spatialTreeInvalid,
                        treeIndex);
                }
                previousTreeIndex = treeIndex;
                const auto& tree = arena.trees[treeIndex];
                if (tree.kind != kind ||
                    tree.worldRoomIndex != roomIndex ||
                    tree.sourceIndex >=
                        room.semanticCcfSourceCount) {
                    return indexedIssue(
                        MissionWorldRoomPublicationIssueKind::
                            spatialTreeInvalid,
                        treeIndex);
                }
            }
        }
    }
    if (expectedReference != arena.treeReferences.size() ||
        arena.treeReferences.size() != arena.trees.size()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                spatialRoomRangeInvalid);
    }

    std::size_t expectedNode = 0U;
    std::size_t expectedPolygon = 0U;
    for (std::size_t treeIndex = 0U;
         treeIndex < arena.trees.size();
         ++treeIndex) {
        const auto& tree = arena.trees[treeIndex];
        if (tree.firstNodeIndex != expectedNode ||
            tree.nodeCount == 0U ||
            !rangeWithin(
                tree.firstNodeIndex,
                tree.nodeCount,
                arena.nodes.size()) ||
            tree.rootNodeIndex != tree.firstNodeIndex ||
            tree.worldRoomIndex >= arena.rooms.size()) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    spatialTreeInvalid,
                treeIndex);
        }
        expectedNode += tree.nodeCount;

        std::array<
            std::size_t,
            assets::kMissionWorldSpatialMaximumTraceDepth>
            traversalStack{};
        std::size_t traversalDepth = 1U;
        std::size_t visitedNodeCount = 0U;
        traversalStack[0] = tree.rootNodeIndex;
        while (traversalDepth != 0U) {
            const auto nodeIndex =
                traversalStack[--traversalDepth];
            if (nodeIndex !=
                tree.firstNodeIndex + visitedNodeCount) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        spatialNodeInvalid,
                    nodeIndex);
            }
            ++visitedNodeCount;
            const auto& node = arena.nodes[nodeIndex];
            const auto pushChild =
                [&traversalStack,
                 &traversalDepth,
                 &tree](
                    const std::optional<std::size_t> child) noexcept {
                    if (!child.has_value()) {
                        return true;
                    }
                    if (*child < tree.firstNodeIndex ||
                        *child >=
                            tree.firstNodeIndex + tree.nodeCount ||
                        traversalDepth >= traversalStack.size()) {
                        return false;
                    }
                    traversalStack[traversalDepth++] = *child;
                    return true;
                };
            // LIFO push B before A reproduces the serialized preorder.
            if (!pushChild(node.childBIndex) ||
                !pushChild(node.childAIndex)) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        spatialNodeInvalid,
                    nodeIndex);
            }
        }
        if (visitedNodeCount != tree.nodeCount) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    spatialNodeInvalid,
                tree.rootNodeIndex);
        }

        for (std::size_t nodeIndex = tree.firstNodeIndex;
             nodeIndex < expectedNode;
             ++nodeIndex) {
            const auto& node = arena.nodes[nodeIndex];
            const auto validChild =
                [&tree](
                    const std::optional<std::size_t> child) noexcept {
                    return !child.has_value() ||
                        (*child >= tree.firstNodeIndex &&
                         *child <
                             tree.firstNodeIndex +
                                 tree.nodeCount);
                };
            if (!validChild(node.childAIndex) ||
                !validChild(node.childBIndex) ||
                !finite(node.splitNormal) ||
                !finite(node.pointOnPlane) ||
                node.firstPolygonIndex != expectedPolygon ||
                !rangeWithin(
                    node.firstPolygonIndex,
                    node.polygonCount,
                    arena.polygons.size())) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        spatialNodeInvalid,
                    nodeIndex);
            }
            expectedPolygon += node.polygonCount;
            for (std::size_t polygonIndex =
                     node.firstPolygonIndex;
                 polygonIndex < expectedPolygon;
                 ++polygonIndex) {
                const auto& polygon = arena.polygons[polygonIndex];
                const bool portal =
                    tree.kind ==
                    assets::CcfBspTreeKind::portalTree;
                if (!finite(polygon.faceCross) ||
                    !validFaceNormal(polygon.faceNormal) ||
                    !finite(polygon.point0) ||
                    !finite(polygon.edge01) ||
                    !finite(polygon.edge12) ||
                    (portal &&
                     (!polygon.portalWorldRoomIndex.has_value() ||
                      *polygon.portalWorldRoomIndex >=
                          arena.rooms.size())) ||
                    (!portal &&
                     (polygon.portalWorldRoomIndex.has_value() ||
                      polygon.portalMeshSelectionFlagB ||
                      polygon.portalType != 0U ||
                      polygon.portalObjectVisible))) {
                    return indexedIssue(
                        MissionWorldRoomPublicationIssueKind::
                            spatialPolygonInvalid,
                        polygonIndex);
                }
            }
        }
    }
    if (expectedNode != arena.nodes.size() ||
        expectedPolygon != arena.polygons.size()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                spatialTreeInvalid);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<MissionWorldRoomPublicationIssue>
validatePlacedDynamicCollision(
    const LoadedMissionWorldRoom& room) noexcept {
    const auto& collision = room.placedDynamicCollision;
    if (!collision.complete()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                placedCollisionIncomplete);
    }
    if (collision.roomObjectRanges.size() !=
        room.spatialArena.rooms.size()) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                placedCollisionRoomCountMismatch);
    }
    for (std::size_t meshIndex = 0U;
         meshIndex < collision.meshProvenance.size();
         ++meshIndex) {
        if (collision.meshProvenance[meshIndex].sourceIndex >=
            room.semanticCcfSourceCount) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    placedCollisionSourceIndexOutOfRange,
                collision.meshProvenance[meshIndex].sourceIndex);
        }
    }
    for (std::size_t objectIndex = 0U;
         objectIndex < collision.objectProvenance.size();
         ++objectIndex) {
        if (collision.objectProvenance[objectIndex].sourceIndex >=
            room.semanticCcfSourceCount) {
            return indexedIssue(
                MissionWorldRoomPublicationIssueKind::
                    placedCollisionSourceIndexOutOfRange,
                collision.objectProvenance[objectIndex].sourceIndex);
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool sameVecBits(const render::Vec3 &left,
                               const render::Vec3 &right) noexcept {
    return sameFloatBits(left.x, right.x) &&
           sameFloatBits(left.y, right.y) &&
           sameFloatBits(left.z, right.z);
}

[[nodiscard]] bool finiteVec(const render::Vec3 &value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finiteMat(const render::Mat3 &value) noexcept {
    return finiteVec(value.columns[0]) && finiteVec(value.columns[1]) &&
           finiteVec(value.columns[2]);
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
        if (room.playerActorCollision.has_value()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerCollisionCooccurrenceMismatch);
        }
        if (room.playerVisualCcfCacheIndex.has_value() ||
            room.playerActorBinding.has_value() ||
            !room.playerActorMeshProvenance.empty() ||
            !room.playerActorInstanceProvenance.empty()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             playerVisualCooccurrenceMismatch);
        }
        std::size_t expectedStaticMeshCount = 0U;
        std::size_t expectedStaticInstanceCount = 0U;
        if (!checkedAdd(room.meshProvenance.size(),
                        room.levelObjectMeshProvenance.size(),
                        expectedStaticMeshCount) ||
            !checkedAdd(room.instanceProvenance.size(),
                        room.levelObjectInstanceProvenance.size(),
                        expectedStaticInstanceCount) ||
            expectedStaticMeshCount != room.model.meshes.size()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             meshProvenanceCountMismatch);
        }
        if (expectedStaticInstanceCount != room.model.instances.size()) {
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
    if (hasPlayer && !room.playerActorCollision.has_value()) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         playerCollisionCooccurrenceMismatch);
    }
    if (hasPlayer && !room.playerVisual->valid()) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         invalidPlayerVisualDescriptor);
    }
    if (room.levelObjectAdmissions.empty()) {
        if (room.uniqueObjectDefinitionCount != 0U ||
            !room.levelObjectMeshProvenance.empty() ||
            !room.levelObjectInstanceProvenance.empty() ||
            !room.levelObjectPlacementRanges.empty()) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             levelObjectAdmissionCooccurrenceMismatch);
        }
    } else if (room.uniqueObjectDefinitionCount == 0U) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         levelObjectAdmissionCooccurrenceMismatch);
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
    if (const auto spatialIssue = validateSpatialArena(room);
        spatialIssue.has_value()) {
        return spatialIssue;
    }
    if (const auto placedCollisionIssue =
            validatePlacedDynamicCollision(room);
        placedCollisionIssue.has_value()) {
        return placedCollisionIssue;
    }

    std::size_t observedClassicTextureCount = 0U;
    std::size_t observedEnhancedTextureCount = 0U;
    std::uint64_t observedReplacementGeneration = 0U;
    for (std::size_t textureIndex = 0U;
         textureIndex < room.textures.size(); ++textureIndex) {
        const auto &texture = room.textures[textureIndex];
        if (room.requestedTextureMode == texture::TextureMode::enhanced) {
            if (texture.replacementGeneration == 0U ||
                (observedReplacementGeneration != 0U &&
                 texture.replacementGeneration !=
                     observedReplacementGeneration)) {
                return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                        textureReplacementProvenanceMismatch,
                                    textureIndex);
            }
            observedReplacementGeneration = texture.replacementGeneration;
        }
        switch (texture.sourceMode) {
        case texture::TextureMode::classic:
            ++observedClassicTextureCount;
            if (room.requestedTextureMode == texture::TextureMode::classic &&
                (texture.replacementGeneration != 0U ||
                 texture.sourceGtiSha256.has_value())) {
                return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                        textureReplacementProvenanceMismatch,
                                    textureIndex);
            }
            break;
        case texture::TextureMode::enhanced:
            ++observedEnhancedTextureCount;
            if (room.requestedTextureMode != texture::TextureMode::enhanced ||
                texture.replacementGeneration == 0U ||
                !texture.sourceGtiSha256.has_value()) {
                return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                        textureReplacementProvenanceMismatch,
                                    textureIndex);
            }
            break;
        default:
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    textureReplacementProvenanceMismatch,
                                textureIndex);
        }
    }
    const bool classicRequestSummaryValid =
        room.requestedTextureMode == texture::TextureMode::classic &&
        observedClassicTextureCount == room.textures.size() &&
        observedEnhancedTextureCount == 0U && room.textureFallbackCount == 0U;
    const bool enhancedRequestSummaryValid =
        room.requestedTextureMode == texture::TextureMode::enhanced &&
        room.textureFallbackCount == observedClassicTextureCount;
    if (observedClassicTextureCount != room.classicTextureCount ||
        observedEnhancedTextureCount != room.enhancedTextureCount ||
        (!classicRequestSummaryValid && !enhancedRequestSummaryValid)) {
        return issue(
            MissionWorldRoomPublicationIssueKind::textureModeSummaryMismatch);
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

    if (room.levelObjectAdmissions.size() > room.semanticCcfSourceCount) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         levelObjectAdmissionCooccurrenceMismatch);
    }
    const auto objectSourceBegin =
        room.semanticCcfSourceCount - room.levelObjectAdmissions.size();
    if (!room.levelObjectAdmissions.empty() && objectSourceBegin != 1U &&
        objectSourceBegin != 2U) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         levelObjectAdmissionOrderMismatch);
    }
    std::size_t selectedAdmissionCount = 0U;
    for (std::size_t admissionIndex = 0U;
         admissionIndex < room.levelObjectAdmissions.size(); ++admissionIndex) {
        const auto &admission = room.levelObjectAdmissions[admissionIndex];
        const auto expectedSourceIndex = objectSourceBegin + admissionIndex;
        if (admission.placementIndex != admissionIndex ||
            admission.sourceIndex != expectedSourceIndex) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    levelObjectAdmissionOrderMismatch,
                                admissionIndex);
        }
        if (admission.uniqueObjectDefinitionIndex >=
            room.uniqueObjectDefinitionCount) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    levelObjectDefinitionIndexOutOfRange,
                                admissionIndex);
        }
        if (admission.sourceIndex >= room.ccfCacheIndexByLoadSource.size() ||
            admission.ccfCacheIndex !=
                room.ccfCacheIndexByLoadSource[admission.sourceIndex]) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    levelObjectCacheJoinMismatch,
                                admissionIndex);
        }
        const auto expectedLegacyRoomId =
            assets::legacyCcRoomIdForWorldRoomIndex(
                room.spatialArena.rooms.size(),
                admission.targetRoom.worldRoomIndex);
        if (!expectedLegacyRoomId.has_value() ||
            *expectedLegacyRoomId != admission.targetRoom.legacyCcRoomId) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    levelObjectRoomIdentityMismatch,
                                admissionIndex);
        }
        const auto placementDeterminant =
            render::determinant(admission.placementRoot.linear);
        if (!std::isfinite(admission.placementRoot.rawScalar) ||
            admission.placementRoot.rawScalar == 0.0F ||
            !finiteMat(admission.placementRoot.linear) ||
            !finiteVec(admission.placementRoot.translation) ||
            !std::isfinite(placementDeterminant) ||
            placementDeterminant == 0.0F) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    levelObjectTransformMismatch,
                                admissionIndex);
        }
        if (admission.targetRoom.worldRoomIndex ==
            room.startSelection.worldRoomIndex) {
            ++selectedAdmissionCount;
        }
    }
    if (room.levelObjectPlacementRanges.size() != selectedAdmissionCount) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         levelObjectPlacementRangeMismatch);
    }

    std::size_t expectedObjectMeshSlot = room.meshProvenance.size();
    std::size_t expectedObjectInstanceIndex = room.instanceProvenance.size();
    std::size_t objectMeshProvenanceIndex = 0U;
    std::size_t objectInstanceProvenanceIndex = 0U;
    std::size_t selectedRangeIndex = 0U;
    for (std::size_t admissionIndex = 0U;
         admissionIndex < room.levelObjectAdmissions.size(); ++admissionIndex) {
        const auto &admission = room.levelObjectAdmissions[admissionIndex];
        if (admission.targetRoom.worldRoomIndex !=
            room.startSelection.worldRoomIndex) {
            continue;
        }
        const auto &range =
            room.levelObjectPlacementRanges[selectedRangeIndex++];
        if (range.placementIndex != admission.placementIndex ||
            range.sourceIndex != admission.sourceIndex ||
            range.uniqueObjectDefinitionIndex !=
                admission.uniqueObjectDefinitionIndex ||
            range.targetRoom != admission.targetRoom ||
            range.firstMeshSlot != expectedObjectMeshSlot ||
            range.firstInstanceIndex != expectedObjectInstanceIndex ||
            !rangeWithin(objectMeshProvenanceIndex, range.meshCount,
                         room.levelObjectMeshProvenance.size()) ||
            !rangeWithin(objectInstanceProvenanceIndex, range.instanceCount,
                         room.levelObjectInstanceProvenance.size()) ||
            !rangeWithin(range.firstMeshSlot, range.meshCount,
                         room.model.meshes.size()) ||
            !rangeWithin(range.firstInstanceIndex, range.instanceCount,
                         room.model.instances.size())) {
            return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                    levelObjectPlacementRangeMismatch,
                                selectedRangeIndex - 1U);
        }

        for (std::size_t localMesh = 0U; localMesh < range.meshCount;
             ++localMesh) {
            const auto finalSlot = range.firstMeshSlot + localMesh;
            const auto &provenance =
                room.levelObjectMeshProvenance[objectMeshProvenanceIndex +
                                               localMesh];
            if (provenance.placementIndex != admission.placementIndex ||
                provenance.sourceIndex != admission.sourceIndex ||
                provenance.uniqueObjectDefinitionIndex !=
                    admission.uniqueObjectDefinitionIndex ||
                provenance.targetRoom != admission.targetRoom ||
                provenance.finalMeshSlot != finalSlot) {
                return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                        levelObjectMeshProvenanceMismatch,
                                    objectMeshProvenanceIndex + localMesh);
            }
            for (const auto &material :
                 room.model.meshes[finalSlot].materials) {
                const auto assetOutOfRange = [&](const auto &asset) {
                    return asset.has_value() &&
                           asset->value >= room.textures.size();
                };
                if (assetOutOfRange(material.primary) ||
                    assetOutOfRange(material.secondary) ||
                    assetOutOfRange(material.environment)) {
                    return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                            levelObjectMaterialAssetOutOfRange,
                                        objectMeshProvenanceIndex + localMesh);
                }
            }
        }
        for (std::size_t localInstance = 0U;
             localInstance < range.instanceCount; ++localInstance) {
            const auto finalIndex = range.firstInstanceIndex + localInstance;
            const auto &provenance =
                room.levelObjectInstanceProvenance
                    [objectInstanceProvenanceIndex + localInstance];
            if (provenance.placementIndex != admission.placementIndex ||
                provenance.sourceIndex != admission.sourceIndex ||
                provenance.uniqueObjectDefinitionIndex !=
                    admission.uniqueObjectDefinitionIndex ||
                provenance.targetRoom != admission.targetRoom ||
                provenance.finalInstanceIndex != finalIndex ||
                provenance.finalMeshSlot < range.firstMeshSlot ||
                provenance.finalMeshSlot >=
                    range.firstMeshSlot + range.meshCount) {
                return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                        levelObjectInstanceProvenanceMismatch,
                                    objectInstanceProvenanceIndex +
                                        localInstance);
            }
            const auto &instance = room.model.instances[finalIndex];
            const auto expectedTransform = render::tryComposeNodeTransforms(
                admission.placementRoot, provenance.local);
            if (static_cast<std::size_t>(instance.meshSlot) !=
                    provenance.finalMeshSlot ||
                !expectedTransform.has_value() ||
                !sameMatBits(expectedTransform->linear, instance.modelLinear) ||
                !sameVecBits(expectedTransform->translation,
                             instance.modelTranslation)) {
                return indexedIssue(MissionWorldRoomPublicationIssueKind::
                                        levelObjectTransformMismatch,
                                    objectInstanceProvenanceIndex +
                                        localInstance);
            }
        }

        expectedObjectMeshSlot += range.meshCount;
        expectedObjectInstanceIndex += range.instanceCount;
        objectMeshProvenanceIndex += range.meshCount;
        objectInstanceProvenanceIndex += range.instanceCount;
    }
    if (objectMeshProvenanceIndex != room.levelObjectMeshProvenance.size() ||
        objectInstanceProvenanceIndex !=
            room.levelObjectInstanceProvenance.size()) {
        return issue(MissionWorldRoomPublicationIssueKind::
                         levelObjectPlacementRangeMismatch);
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
        if (expectedObjectMeshSlot != binding.firstMeshSlot) {
            return issue(MissionWorldRoomPublicationIssueKind::
                             staticMeshProvenancePrefixMismatch);
        }
        if (expectedObjectInstanceIndex != binding.firstInstanceIndex) {
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
            const auto expected =
                render::tryComposeNodeTransforms(
                    actorWorld, provenance.actorLocal);
            if (!expected.has_value() ||
                !sameMatBits(
                    expected->linear, instance.modelLinear) ||
                !sameVecBits(
                    expected->translation,
                    instance.modelTranslation)) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerActorTransformMismatch,
                    actorInstanceIndex);
            }
        }

        const auto& collision = *room.playerActorCollision;
        if (!collision.complete()) {
            return issue(
                MissionWorldRoomPublicationIssueKind::
                    playerCollisionIncomplete);
        }
        if (collision.meshes.size() !=
            room.playerActorMeshProvenance.size()) {
            return issue(
                MissionWorldRoomPublicationIssueKind::
                    playerCollisionMeshCountMismatch);
        }
        if (collision.instances.size() !=
            room.playerActorInstanceProvenance.size()) {
            return issue(
                MissionWorldRoomPublicationIssueKind::
                    playerCollisionInstanceCountMismatch);
        }
        for (std::size_t meshIndex = 0U;
             meshIndex < collision.meshProvenance.size();
             ++meshIndex) {
            if (collision.meshProvenance[meshIndex].actor !=
                room.playerActorMeshProvenance[meshIndex].actor) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerCollisionProvenanceMismatch,
                    meshIndex);
            }
        }
        for (std::size_t instanceIndex = 0U;
             instanceIndex < collision.instances.size();
             ++instanceIndex) {
            const auto& collisionInstance =
                collision.instances[instanceIndex];
            const auto& sceneInstance =
                room.playerActorInstanceProvenance[instanceIndex];
            const auto& modelInstance =
                room.model.instances[
                    sceneInstance.finalInstanceIndex];
            const auto collisionMeshIndex =
                static_cast<std::size_t>(modelInstance.meshSlot) -
                binding.firstMeshSlot;
            if (collisionInstance.actor != sceneInstance.actor ||
                collisionInstance.collisionMeshIndex !=
                    collisionMeshIndex) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerCollisionProvenanceMismatch,
                    instanceIndex);
            }
            if (!sameFloatBits(
                    collisionInstance.actorLocal.rawScalar,
                    sceneInstance.actorLocal.rawScalar) ||
                !sameMatBits(
                    collisionInstance.actorLocal.linear,
                    sceneInstance.actorLocal.linear) ||
                !sameVecBits(
                    collisionInstance.actorLocal.translation,
                    sceneInstance.actorLocal.translation)) {
                return indexedIssue(
                    MissionWorldRoomPublicationIssueKind::
                        playerCollisionTransformMismatch,
                    instanceIndex);
            }
        }
    }

    std::uint64_t expectedPublishedCpuBytes = 0U;
    if (!recalculatePublishedCpuBytes(
            room, expectedPublishedCpuBytes) ||
        room.publishedCpuBytes != expectedPublishedCpuBytes) {
        return issue(
            MissionWorldRoomPublicationIssueKind::
                publishedCpuBytesMismatch);
    }

    return std::nullopt;
}

} // namespace airfix::content
