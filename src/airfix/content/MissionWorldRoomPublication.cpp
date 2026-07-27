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
        !detail::checkedMissionWorldRoomByteAdd(
            total, room.retainedSpatialBytes)) {
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
        addPublishedCount(
               total,
               room.playerActorMeshProvenance.size(),
               sizeof(render::PlayerActorSceneMeshProvenance)) &&
        addPublishedCount(
               total,
               room.playerActorInstanceProvenance.size(),
               sizeof(render::PlayerActorSceneInstanceProvenance)) &&
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
    if (const auto spatialIssue = validateSpatialArena(room);
        spatialIssue.has_value()) {
        return spatialIssue;
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
