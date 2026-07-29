#include "airfix/render/MissionPlacedDynamicBspAssembly.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>

#if defined(__GNUC__)
#pragma GCC diagnostic push
// Partial designated initialization deliberately leaves diagnostic context
// optionals disengaged. GCC/Clang otherwise warn for every omitted optional.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace airfix::render {
namespace {

// Must match LegacyDynamicBsp's line-object admission boundary. An assembly
// reported complete here must never be rejected later by the direct consumer.
constexpr double kOrthonormalTolerance = 1.0e-5;

using PhysicalRoomMap =
    std::vector<std::vector<std::optional<std::size_t>>>;

struct ReferenceMatch final {
    std::size_t first{};
    std::size_t count{};
};

struct PendingRoomObject final {
    LegacyDynamicBspLineObject object;
    MissionPlacedDynamicBspObjectProvenance provenance;
};

void clearPublishable(MissionPlacedDynamicBspAssembly& result) noexcept {
    std::vector<LegacyDynamicBspMesh>{}.swap(result.meshes);
    std::vector<MissionPlacedDynamicBspMeshProvenance>{}.swap(
        result.meshProvenance);
    std::vector<LegacyDynamicBspLineObject>{}.swap(result.objects);
    std::vector<MissionPlacedDynamicBspObjectProvenance>{}.swap(
        result.objectProvenance);
    std::vector<LegacyDynamicBspRoomObjectRange>{}.swap(
        result.roomObjectRanges);
    result.retainedPayloadBytes = 0U;
}

void fail(
    MissionPlacedDynamicBspAssembly& result,
    MissionPlacedDynamicBspIssue issue) {
    clearPublishable(result);
    result.issues.push_back(std::move(issue));
}

[[nodiscard]] bool checkedAdd(
    std::size_t& total,
    const std::size_t addition,
    const std::size_t limit) noexcept {
    if (total > limit || addition > limit - total) {
        return false;
    }
    total += addition;
    return true;
}

[[nodiscard]] bool checkedBytes(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t elementSize) noexcept {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (elementSize != 0U &&
        count > maximum / elementSize) {
        return false;
    }
    const auto bytes =
        static_cast<std::uint64_t>(count) * elementSize;
    if (bytes > maximum - total) {
        return false;
    }
    total += bytes;
    return true;
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(
    const assets::CcfVector3& value) noexcept {
    return std::ranges::all_of(
        value,
        [](const float component) {
            return std::isfinite(component);
        });
}

[[nodiscard]] bool legacyNanSentinel(
    const assets::CcfVector3& value) noexcept {
    return std::ranges::all_of(
        value,
        [](const float component) {
            return std::bit_cast<std::uint32_t>(component) ==
                0xFFC00000U;
        });
}

[[nodiscard]] double dot(
    const Vec3& left,
    const Vec3& right) noexcept {
    return static_cast<double>(left.x) * right.x +
        static_cast<double>(left.y) * right.y +
        static_cast<double>(left.z) * right.z;
}

[[nodiscard]] bool orthonormal(const Mat3& value) noexcept {
    if (!finite(value.columns[0]) ||
        !finite(value.columns[1]) ||
        !finite(value.columns[2])) {
        return false;
    }
    for (std::size_t index = 0U; index < 3U; ++index) {
        if (std::abs(
                dot(value.columns[index], value.columns[index]) -
                1.0) > kOrthonormalTolerance) {
            return false;
        }
    }
    return
        std::abs(dot(value.columns[0], value.columns[1])) <=
            kOrthonormalTolerance &&
        std::abs(dot(value.columns[0], value.columns[2])) <=
            kOrthonormalTolerance &&
        std::abs(dot(value.columns[1], value.columns[2])) <=
            kOrthonormalTolerance;
}

[[nodiscard]] Vec3 runtimeVector(
    const assets::CcfVector3& value) noexcept {
    return {value[0], value[1], value[2]};
}

[[nodiscard]] assets::CcfVector3 sourceVector(
    const Vec3& value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] Vec3 scaled(
    const Vec3& value,
    const double factor) noexcept {
    return {
        static_cast<float>(static_cast<double>(value.x) * factor),
        static_cast<float>(static_cast<double>(value.y) * factor),
        static_cast<float>(static_cast<double>(value.z) * factor),
    };
}

[[nodiscard]] Vec3 negated(const Vec3& value) noexcept {
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] Vec3 added(
    const Vec3& left,
    const Vec3& right) noexcept {
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

[[nodiscard]] std::optional<assets::CcfVector3> transformVector(
    const assets::CcfVector3& value,
    const BasisTransform& basis,
    const double scale) noexcept {
    if (!finite(value)) {
        return std::nullopt;
    }
    const auto transformed = scaled(
        applyRuntimeColumn(
            basis.sourceToRuntime,
            runtimeVector(value)),
        scale);
    if (!finite(transformed)) {
        return std::nullopt;
    }
    return sourceVector(transformed);
}

[[nodiscard]] bool sameCatalog(
    const assets::MissionWorldRoomCatalog& left,
    const assets::MissionWorldRoomCatalog& right) {
    return left.complete() && right.complete() &&
        left.sourceCount == right.sourceCount &&
        left.sourcePhysicalRoomCounts ==
            right.sourcePhysicalRoomCounts &&
        left.initialRootName == right.initialRootName &&
        left.rooms == right.rooms;
}

[[nodiscard]] bool validMeshRetainedBytes(
    const LegacyDynamicBspMesh& mesh) noexcept {
    std::uint64_t arenaBytes = 0U;
    if (!checkedBytes(
            arenaBytes,
            mesh.localArena.rooms.size(),
            sizeof(assets::MissionWorldSpatialRoom)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.treeReferences.size(),
            sizeof(std::size_t)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.trees.size(),
            sizeof(assets::MissionWorldSpatialTree)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.nodes.size(),
            sizeof(assets::MissionWorldSpatialNode)) ||
        !checkedBytes(
            arenaBytes,
            mesh.localArena.polygons.size(),
            sizeof(assets::MissionWorldSpatialPolygon)) ||
        arenaBytes != mesh.localArena.retainedPayloadBytes) {
        return false;
    }
    auto meshBytes = arenaBytes;
    return checkedBytes(
               meshBytes,
               mesh.polygonMaterialReferences.size(),
               sizeof(std::uint32_t)) &&
        meshBytes == mesh.retainedPayloadBytes;
}

[[nodiscard]] std::unordered_map<std::uint32_t, ReferenceMatch>
buildMaterialReferences(
    const assets::CcfMetadata& ccf) {
    std::unordered_map<std::uint32_t, ReferenceMatch> references;
    references.reserve(ccf.materials.size());
    for (std::size_t index = 0U;
         index < ccf.materials.size();
         ++index) {
        const auto reference = ccf.materials[index].reference;
        const auto [iterator, inserted] = references.emplace(
            reference,
            ReferenceMatch{.first = index, .count = 1U});
        if (!inserted) {
            ++iterator->second.count;
        }
    }
    return references;
}

[[nodiscard]] std::optional<std::uint32_t> materialMode(
    const assets::CcfMetadata& ccf,
    const std::unordered_map<std::uint32_t, ReferenceMatch>& references,
    const std::uint32_t materialReference) noexcept {
    const auto match = references.find(materialReference);
    if (match == references.end() ||
        match->second.count != 1U ||
        match->second.first >= ccf.materials.size()) {
        return std::nullopt;
    }
    return ccf.materials[match->second.first].collisionMode2152;
}

[[nodiscard]] bool validateTree(
    const assets::CcfBspTreeMetadata& tree,
    const std::uint32_t ownerReference,
    const std::size_t meshTriangleCount,
    const std::size_t maximumDepth,
    const std::size_t sourceIndex,
    const std::size_t placedNodeIndex,
    const std::size_t physicalMeshIndex,
    const std::size_t treeIndex,
    MissionPlacedDynamicBspAssembly& result) {
    const auto issue = [&](const MissionPlacedDynamicBspIssueKind kind,
                           const std::optional<std::size_t> nodeIndex =
                               std::nullopt,
                           const std::optional<std::size_t> polygonIndex =
                               std::nullopt,
                           const std::optional<std::uint32_t> reference =
                               std::nullopt) {
        fail(result, {
            .kind = kind,
            .sourceIndex = sourceIndex,
            .placedNodeIndex = placedNodeIndex,
            .physicalMeshIndex = physicalMeshIndex,
            .treeIndex = treeIndex,
            .nodeIndex = nodeIndex,
            .polygonMetadataIndex = polygonIndex,
            .reference = reference,
        });
    };

    if (tree.kind != assets::CcfBspTreeKind::dynamicObjectTree ||
        tree.source != assets::CcfBspTreeSource::placedObject4101) {
        issue(MissionPlacedDynamicBspIssueKind::invalidTreeKind);
        return false;
    }
    if (tree.nodes.empty() ||
        tree.rootNodeIndex >= tree.nodes.size()) {
        issue(MissionPlacedDynamicBspIssueKind::invalidTreeStructure);
        return false;
    }

    std::vector<std::uint8_t> visited(tree.nodes.size(), 0U);
    std::vector<std::uint8_t> polygonOwners(
        tree.polygons.size(), 0U);
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    stack.reserve(tree.nodes.size());
    stack.emplace_back(tree.rootNodeIndex, 1U);
    std::size_t visitedCount = 0U;

    while (!stack.empty()) {
        const auto [nodeIndex, depth] = stack.back();
        stack.pop_back();
        if (depth > maximumDepth) {
            issue(
                MissionPlacedDynamicBspIssueKind::limitExceeded,
                nodeIndex);
            return false;
        }
        if (nodeIndex >= tree.nodes.size() ||
            visited[nodeIndex] != 0U) {
            issue(
                MissionPlacedDynamicBspIssueKind::invalidTreeStructure,
                nodeIndex);
            return false;
        }
        visited[nodeIndex] = 1U;
        ++visitedCount;

        const auto& node = tree.nodes[nodeIndex];
        if ((node.childAPresenceRaw != 0U) !=
                node.childAIndex.has_value() ||
            (node.childBPresenceRaw != 0U) !=
                node.childBIndex.has_value()) {
            issue(
                MissionPlacedDynamicBspIssueKind::invalidTreeStructure,
                nodeIndex);
            return false;
        }
        if (!finite(node.splitNormal) ||
            !finite(node.pointOnPlane)) {
            issue(
                MissionPlacedDynamicBspIssueKind::invalidSpatialValue,
                nodeIndex);
            return false;
        }
        for (const auto polygonIndex : node.polygonIndices) {
            if (polygonIndex >= tree.polygons.size() ||
                polygonOwners[polygonIndex] != 0U) {
                issue(
                    MissionPlacedDynamicBspIssueKind::
                        invalidTreeStructure,
                    nodeIndex,
                    polygonIndex);
                return false;
            }
            polygonOwners[polygonIndex] = 1U;
        }
        if (node.childBIndex.has_value()) {
            stack.emplace_back(*node.childBIndex, depth + 1U);
        }
        if (node.childAIndex.has_value()) {
            stack.emplace_back(*node.childAIndex, depth + 1U);
        }
    }
    if (visitedCount != tree.nodes.size() ||
        std::ranges::any_of(
            polygonOwners,
            [](const std::uint8_t owner) {
                return owner != 1U;
            })) {
        issue(MissionPlacedDynamicBspIssueKind::invalidTreeStructure);
        return false;
    }

    for (std::size_t polygonIndex = 0U;
         polygonIndex < tree.polygons.size();
         ++polygonIndex) {
        const auto& polygon = tree.polygons[polygonIndex];
        if (!finite(polygon.faceCross) ||
            (!finite(polygon.faceNormal) &&
             !legacyNanSentinel(polygon.faceNormal)) ||
            !finite(polygon.point0) ||
            !finite(polygon.edge01) ||
            !finite(polygon.edge12)) {
            issue(
                MissionPlacedDynamicBspIssueKind::invalidSpatialValue,
                std::nullopt,
                polygonIndex);
            return false;
        }
        if (polygon.placedObjectReference != ownerReference) {
            issue(
                MissionPlacedDynamicBspIssueKind::
                    placedObjectReferenceMismatch,
                std::nullopt,
                polygonIndex,
                polygon.placedObjectReference);
            return false;
        }
        if (polygon.polygonIndex >= meshTriangleCount) {
            issue(
                MissionPlacedDynamicBspIssueKind::
                    polygonIndexOutOfRange,
                std::nullopt,
                polygonIndex);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool buildMesh(
    const assets::CcfMetadata& ccf,
    const assets::CcfPlacedNodeMetadata& placed,
    const assets::CcfPlacedObjectMetadata& object,
    const std::size_t sourceIndex,
    const std::size_t placedNodeIndex,
    const std::size_t physicalMeshIndex,
    const BasisTransform& basis,
    const MissionPlacedDynamicBspLimits& limits,
    const std::unordered_map<std::uint32_t, ReferenceMatch>&
        materialReferences,
    MissionPlacedDynamicBspAssembly& result,
    LegacyDynamicBspMesh& output) {
    const auto& sourceMesh = ccf.meshes[physicalMeshIndex];
    ConvertedMeshGeometry geometry;
    try {
        geometry = convertLegacyGeometry(
            sourceMesh,
            basis,
            UvPolicy::preserveRaw,
            limits.geometryPerMesh);
    }
    catch (const GeometryError& error) {
        fail(result, {
            .kind =
                MissionPlacedDynamicBspIssueKind::geometryFailure,
            .sourceIndex = sourceIndex,
            .placedNodeIndex = placedNodeIndex,
            .physicalMeshIndex = physicalMeshIndex,
            .geometryError = error.code(),
        });
        return false;
    }

    double maximumRadiusSquared = 0.0;
    for (const auto& vertex : geometry.vertices) {
        const auto radiusSquared = dot(
            vertex.position, vertex.position);
        if (!std::isfinite(radiusSquared) ||
            radiusSquared < 0.0) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        invalidSpatialValue,
                .sourceIndex = sourceIndex,
                .placedNodeIndex = placedNodeIndex,
                .physicalMeshIndex = physicalMeshIndex,
            });
            return false;
        }
        maximumRadiusSquared =
            std::max(maximumRadiusSquared, radiusSquared);
    }
    output.localBoundingRadius =
        static_cast<float>(std::sqrt(maximumRadiusSquared));
    if (!std::isfinite(output.localBoundingRadius)) {
        fail(result, {
            .kind =
                MissionPlacedDynamicBspIssueKind::invalidSpatialValue,
            .sourceIndex = sourceIndex,
            .placedNodeIndex = placedNodeIndex,
            .physicalMeshIndex = physicalMeshIndex,
        });
        return false;
    }

    output.localArena.rooms.resize(1U);
    const bool reflected =
        determinant(basis.sourceToRuntime) < 0.0F;
    const double unitScale =
        static_cast<double>(basis.runtimeUnitsPerSourceUnit);
    const double areaScale = unitScale * unitScale;
    if (!std::isfinite(areaScale)) {
        fail(result, {
            .kind =
                MissionPlacedDynamicBspIssueKind::invalidSpatialValue,
            .sourceIndex = sourceIndex,
            .placedNodeIndex = placedNodeIndex,
            .physicalMeshIndex = physicalMeshIndex,
        });
        return false;
    }

    for (std::size_t treeIndex = 0U;
         treeIndex < object.dynamicBspTrees.size();
         ++treeIndex) {
        const auto& tree = object.dynamicBspTrees[treeIndex];
        if (!validateTree(
                tree,
                placed.currentReference,
                sourceMesh.triangles.size(),
                limits.maximumDepth,
                sourceIndex,
                placedNodeIndex,
                physicalMeshIndex,
                treeIndex,
                result)) {
            return false;
        }

        const auto firstNodeIndex = output.localArena.nodes.size();
        const auto outputTreeIndex = output.localArena.trees.size();
        output.localArena.trees.push_back({
            .kind = assets::CcfBspTreeKind::staticTree,
            .sourceIndex = 0U,
            .physicalRoomIndex = 0U,
            .worldRoomIndex = 0U,
            .rootNodeIndex =
                firstNodeIndex + tree.rootNodeIndex,
            .firstNodeIndex = firstNodeIndex,
            .nodeCount = tree.nodes.size(),
        });

        for (std::size_t nodeIndex = 0U;
             nodeIndex < tree.nodes.size();
             ++nodeIndex) {
            const auto& node = tree.nodes[nodeIndex];
            const auto splitNormal = transformVector(
                node.splitNormal, basis, 1.0);
            const auto pointOnPlane = transformVector(
                node.pointOnPlane, basis, unitScale);
            if (!splitNormal.has_value() ||
                !pointOnPlane.has_value()) {
                fail(result, {
                    .kind =
                        MissionPlacedDynamicBspIssueKind::
                            invalidSpatialValue,
                    .sourceIndex = sourceIndex,
                    .placedNodeIndex = placedNodeIndex,
                    .physicalMeshIndex = physicalMeshIndex,
                    .treeIndex = treeIndex,
                    .nodeIndex = nodeIndex,
                });
                return false;
            }
            const auto firstPolygonIndex =
                output.localArena.polygons.size();
            output.localArena.nodes.push_back({
                .childAIndex = node.childAIndex.has_value()
                    ? std::optional<std::size_t>{
                          firstNodeIndex + *node.childAIndex}
                    : std::nullopt,
                .childBIndex = node.childBIndex.has_value()
                    ? std::optional<std::size_t>{
                          firstNodeIndex + *node.childBIndex}
                    : std::nullopt,
                .splitNormal = *splitNormal,
                .pointOnPlane = *pointOnPlane,
                .firstPolygonIndex = firstPolygonIndex,
                .polygonCount = node.polygonIndices.size(),
            });

            for (auto iterator = node.polygonIndices.rbegin();
                 iterator != node.polygonIndices.rend();
                 ++iterator) {
                const auto polygonMetadataIndex = *iterator;
                const auto& polygon =
                    tree.polygons[polygonMetadataIndex];
                const auto faceCross = transformVector(
                    polygon.faceCross, basis, areaScale);
                const auto point0 = transformVector(
                    polygon.point0, basis, unitScale);
                std::optional<assets::CcfVector3> faceNormal;
                if (legacyNanSentinel(polygon.faceNormal)) {
                    faceNormal = polygon.faceNormal;
                }
                else {
                    faceNormal = transformVector(
                        polygon.faceNormal, basis, 1.0);
                }

                const auto sourceEdge01 =
                    runtimeVector(polygon.edge01);
                const auto sourceEdge12 =
                    runtimeVector(polygon.edge12);
                const auto selectedEdge01 = reflected
                    ? added(sourceEdge01, sourceEdge12)
                    : sourceEdge01;
                const auto selectedEdge12 = reflected
                    ? negated(sourceEdge12)
                    : sourceEdge12;
                const auto edge01 = transformVector(
                    sourceVector(selectedEdge01),
                    basis,
                    unitScale);
                const auto edge12 = transformVector(
                    sourceVector(selectedEdge12),
                    basis,
                    unitScale);
                if (!faceCross.has_value() ||
                    !faceNormal.has_value() ||
                    !point0.has_value() ||
                    !edge01.has_value() ||
                    !edge12.has_value()) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidSpatialValue,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                        .treeIndex = treeIndex,
                        .nodeIndex = nodeIndex,
                        .polygonMetadataIndex =
                            polygonMetadataIndex,
                    });
                    return false;
                }

                const auto triangleIndex =
                    static_cast<std::size_t>(
                        polygon.polygonIndex);
                const auto materialReference =
                    sourceMesh.triangles[triangleIndex]
                        .materialReference;
                output.localArena.polygons.push_back({
                    .faceCross = *faceCross,
                    .faceNormal = *faceNormal,
                    .point0 = *point0,
                    .edge01 = *edge01,
                    .edge12 = *edge12,
                    .polygonIndex = polygon.polygonIndex,
                    .placedObjectReference =
                        polygon.placedObjectReference,
                    .materialCollisionMode2152 = materialMode(
                        ccf,
                        materialReferences,
                        materialReference),
                    .portalWorldRoomIndex = std::nullopt,
                    .portalMeshSelectionFlagB = false,
                    .portalType = 0U,
                    .portalObjectVisible = false,
                });
                output.polygonMaterialReferences.push_back(
                    materialReference);
            }
        }
        output.localArena.treeReferences.push_back(
            outputTreeIndex);
    }
    std::ranges::reverse(output.localArena.treeReferences);
    output.localArena.rooms[0].staticTreeCount =
        output.localArena.treeReferences.size();

    std::uint64_t retainedBytes = 0U;
    if (!checkedBytes(
            retainedBytes,
            output.localArena.rooms.size(),
            sizeof(assets::MissionWorldSpatialRoom)) ||
        !checkedBytes(
            retainedBytes,
            output.localArena.treeReferences.size(),
            sizeof(std::size_t)) ||
        !checkedBytes(
            retainedBytes,
            output.localArena.trees.size(),
            sizeof(assets::MissionWorldSpatialTree)) ||
        !checkedBytes(
            retainedBytes,
            output.localArena.nodes.size(),
            sizeof(assets::MissionWorldSpatialNode)) ||
        !checkedBytes(
            retainedBytes,
            output.localArena.polygons.size(),
            sizeof(assets::MissionWorldSpatialPolygon))) {
        fail(result, {
            .kind =
                MissionPlacedDynamicBspIssueKind::integerOverflow,
            .sourceIndex = sourceIndex,
            .placedNodeIndex = placedNodeIndex,
            .physicalMeshIndex = physicalMeshIndex,
        });
        return false;
    }
    output.localArena.retainedPayloadBytes = retainedBytes;
    if (!checkedBytes(
            retainedBytes,
            output.polygonMaterialReferences.size(),
            sizeof(std::uint32_t))) {
        fail(result, {
            .kind =
                MissionPlacedDynamicBspIssueKind::integerOverflow,
            .sourceIndex = sourceIndex,
            .placedNodeIndex = placedNodeIndex,
            .physicalMeshIndex = physicalMeshIndex,
        });
        return false;
    }
    output.retainedPayloadBytes = retainedBytes;
    return true;
}

} // namespace

bool MissionPlacedDynamicBspAssembly::complete() const noexcept {
    if (!issues.empty() ||
        roomObjectRanges.empty() ||
        meshes.size() != meshProvenance.size() ||
        objects.size() != objectProvenance.size()) {
        return false;
    }

    std::uint64_t retained = 0U;
    if (!checkedBytes(
            retained,
            meshes.size(),
            sizeof(LegacyDynamicBspMesh)) ||
        !checkedBytes(
            retained,
            meshProvenance.size(),
            sizeof(MissionPlacedDynamicBspMeshProvenance)) ||
        !checkedBytes(
            retained,
            objects.size(),
            sizeof(LegacyDynamicBspLineObject)) ||
        !checkedBytes(
            retained,
            objectProvenance.size(),
            sizeof(MissionPlacedDynamicBspObjectProvenance)) ||
        !checkedBytes(
            retained,
            roomObjectRanges.size(),
            sizeof(LegacyDynamicBspRoomObjectRange))) {
        return false;
    }
    for (std::size_t meshIndex = 0U;
         meshIndex < meshes.size();
         ++meshIndex) {
        if (!meshes[meshIndex].complete() ||
            !validMeshRetainedBytes(meshes[meshIndex]) ||
            meshes[meshIndex].retainedPayloadBytes >
                std::numeric_limits<std::uint64_t>::max() -
                    retained) {
            return false;
        }
        retained += meshes[meshIndex].retainedPayloadBytes;
    }

    std::size_t nextObjectIndex = 0U;
    for (std::size_t roomIndex = 0U;
         roomIndex < roomObjectRanges.size();
         ++roomIndex) {
        const auto& range = roomObjectRanges[roomIndex];
        if (range.firstObjectIndex != nextObjectIndex ||
            range.objectCount >
                objects.size() - nextObjectIndex) {
            return false;
        }
        for (std::size_t localIndex = 0U;
             localIndex < range.objectCount;
             ++localIndex) {
            const auto objectIndex =
                range.firstObjectIndex + localIndex;
            const auto& object = objects[objectIndex];
            const auto& provenance =
                objectProvenance[objectIndex];
            if (object.meshIndex >= meshes.size() ||
                provenance.worldRoomIndex != roomIndex ||
                provenance.physicalMeshIndex !=
                    meshProvenance[object.meshIndex]
                        .physicalMeshIndex ||
                provenance.sourceIndex !=
                    meshProvenance[object.meshIndex].sourceIndex ||
                object.actorObjectId != 0U ||
                !object.active ||
                !orthonormal(object.objectLocalToRuntime) ||
                !finite(object.runtimeTranslation) ||
                object.portalType < -1 ||
                object.portalType > 1 ||
                (object.portalType == -1) !=
                    !object.portalWorldRoomIndex.has_value() ||
                (object.portalWorldRoomIndex.has_value() &&
                 *object.portalWorldRoomIndex >=
                     roomObjectRanges.size())) {
                return false;
            }
        }
        nextObjectIndex += range.objectCount;
    }
    return nextObjectIndex == objects.size() &&
        retained == retainedPayloadBytes;
}

MissionPlacedDynamicBspAssembly
buildMissionPlacedDynamicBspAssembly(
    const std::span<const assets::MissionCcfRoomLoadSource> sources,
    const assets::MissionWorldRoomCatalog& catalog,
    const BasisTransform& basis,
    const MissionPlacedDynamicBspLimits& limits) {
    MissionPlacedDynamicBspAssembly result;
    try {
        if (!catalog.complete()) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        catalogIncomplete,
            });
            return result;
        }
        if (catalog.sourceCount != sources.size() ||
            catalog.sourcePhysicalRoomCounts.size() !=
                sources.size()) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        sourceCountMismatch,
            });
            return result;
        }
        if (sources.size() > limits.maximumSources ||
            catalog.rooms.size() > limits.maximumWorldRooms ||
            limits.maximumDepth == 0U ||
            limits.maximumDepth >
                assets::kMissionWorldSpatialMaximumTraceDepth) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::limitExceeded,
            });
            return result;
        }
        if (!orthonormal(basis.sourceToRuntime) ||
            !std::isfinite(basis.runtimeUnitsPerSourceUnit) ||
            !(basis.runtimeUnitsPerSourceUnit > 0.0F)) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        invalidTransform,
            });
            return result;
        }

        PhysicalRoomMap physicalToWorld(sources.size());
        std::size_t aggregatePhysicalRooms = 0U;
        std::size_t aggregateMeshes = 0U;
        std::size_t aggregatePlacedNodes = 0U;
        std::size_t aggregateMaterials = 0U;
        for (std::size_t sourceIndex = 0U;
             sourceIndex < sources.size();
             ++sourceIndex) {
            const auto& source = sources[sourceIndex];
            if (source.ccf == nullptr ||
                catalog.sourcePhysicalRoomCounts[sourceIndex] !=
                    source.ccf->rooms.size()) {
                fail(result, {
                    .kind =
                        MissionPlacedDynamicBspIssueKind::
                            invalidSourceMetadata,
                    .sourceIndex = sourceIndex,
                });
                return result;
            }
            if (!source.roomSectionEnabled &&
                source.placedSceneEnabled) {
                fail(result, {
                    .kind =
                        MissionPlacedDynamicBspIssueKind::
                            unsupportedSourceFlags,
                    .sourceIndex = sourceIndex,
                });
                return result;
            }
            if (source.roomSectionEnabled) {
                if (!checkedAdd(
                        aggregatePhysicalRooms,
                        source.ccf->rooms.size(),
                        limits.maximumPhysicalRooms)) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                limitExceeded,
                        .sourceIndex = sourceIndex,
                    });
                    return result;
                }
                physicalToWorld[sourceIndex].resize(
                    source.ccf->rooms.size());
            }
            if (!source.placedSceneEnabled) {
                continue;
            }
            if (!checkedAdd(
                    aggregateMeshes,
                    source.ccf->meshes.size(),
                    limits.maximumScannedMeshes) ||
                !checkedAdd(
                    aggregatePlacedNodes,
                    source.ccf->placedNodes.size(),
                    limits.maximumScannedPlacedNodes) ||
                !checkedAdd(
                    aggregateMaterials,
                    source.ccf->materials.size(),
                    limits.maximumScannedMaterials)) {
                fail(result, {
                    .kind =
                        MissionPlacedDynamicBspIssueKind::
                            limitExceeded,
                    .sourceIndex = sourceIndex,
                });
                return result;
            }
        }

        auto authenticationLimits = limits.catalogAuthentication;
        authenticationLimits.maximumSources = std::min(
            authenticationLimits.maximumSources,
            limits.maximumSources);
        authenticationLimits.maximumRuntimeRooms = std::min(
            authenticationLimits.maximumRuntimeRooms,
            limits.maximumWorldRooms);
        authenticationLimits.maximumContributors = std::min(
            authenticationLimits.maximumContributors,
            limits.maximumPhysicalRooms);
        const auto canonicalCatalog =
            assets::buildMissionWorldRoomCatalog(
                {
                    .initialRootName = catalog.initialRootName,
                    .sources = sources,
                },
                authenticationLimits);
        if (!sameCatalog(canonicalCatalog, catalog)) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        catalogMismatch,
            });
            return result;
        }

        for (std::size_t worldRoomIndex = 0U;
             worldRoomIndex < catalog.rooms.size();
             ++worldRoomIndex) {
            for (const auto& contributor :
                 catalog.rooms[worldRoomIndex].contributors) {
                if (contributor.sourceIndex >= sources.size() ||
                    contributor.physicalRoomIndex >=
                        physicalToWorld[contributor.sourceIndex]
                            .size() ||
                    !sources[contributor.sourceIndex]
                         .roomSectionEnabled) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidContributor,
                        .sourceIndex = contributor.sourceIndex,
                        .physicalRoomIndex =
                            contributor.physicalRoomIndex,
                        .worldRoomIndex = worldRoomIndex,
                    });
                    return result;
                }
                auto& mapped =
                    physicalToWorld[contributor.sourceIndex]
                                   [contributor.physicalRoomIndex];
                if (mapped.has_value()) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                duplicateContributor,
                        .sourceIndex = contributor.sourceIndex,
                        .physicalRoomIndex =
                            contributor.physicalRoomIndex,
                        .worldRoomIndex = worldRoomIndex,
                    });
                    return result;
                }
                mapped = worldRoomIndex;
            }
        }
        for (std::size_t sourceIndex = 0U;
             sourceIndex < sources.size();
             ++sourceIndex) {
            for (std::size_t physicalRoomIndex = 0U;
                 physicalRoomIndex <
                     physicalToWorld[sourceIndex].size();
                 ++physicalRoomIndex) {
                if (!physicalToWorld[sourceIndex][physicalRoomIndex]
                         .has_value()) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                missingContributor,
                        .sourceIndex = sourceIndex,
                        .physicalRoomIndex = physicalRoomIndex,
                    });
                    return result;
                }
            }
        }

        std::vector<std::vector<PendingRoomObject>>
            pendingByRoom(catalog.rooms.size());
        std::size_t aggregateTrees = 0U;
        std::size_t aggregateNodes = 0U;
        std::size_t aggregatePolygons = 0U;
        std::size_t aggregateObjects = 0U;

        for (std::size_t sourceIndex = 0U;
             sourceIndex < sources.size();
             ++sourceIndex) {
            const auto& loadSource = sources[sourceIndex];
            if (!loadSource.placedSceneEnabled) {
                continue;
            }
            const auto& ccf = *loadSource.ccf;
            const auto placedScene = assets::resolvePlacedScene(
                ccf, limits.placedScenePerSource);
            if (!placedScene.issues.empty() ||
                placedScene.nodes.size() !=
                    ccf.placedNodes.size()) {
                MissionPlacedDynamicBspIssue failure{
                    .kind =
                        MissionPlacedDynamicBspIssueKind::
                            placedSceneFailure,
                    .sourceIndex = sourceIndex,
                };
                if (!placedScene.issues.empty()) {
                    const auto& dependency =
                        placedScene.issues.front();
                    failure.placedNodeIndex =
                        dependency.placedNodeIndex;
                    failure.reference = dependency.reference;
                    failure.placedSceneIssue = dependency.kind;
                }
                fail(result, std::move(failure));
                return result;
            }

            const auto materialReferences =
                buildMaterialReferences(ccf);
            std::vector<std::optional<std::size_t>> meshCache(
                ccf.meshes.size());
            for (std::size_t placedNodeIndex = 0U;
                 placedNodeIndex < ccf.placedNodes.size();
                 ++placedNodeIndex) {
                const auto& placed =
                    ccf.placedNodes[placedNodeIndex];
                const auto* object =
                    std::get_if<assets::CcfPlacedObjectMetadata>(
                        &placed.data);
                if (placed.kind !=
                        assets::CcfPlacedNodeKind::object ||
                    object == nullptr ||
                    object->dynamicBspTrees.empty()) {
                    continue;
                }
                if (placedNodeIndex >= placedScene.nodes.size()) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidPlacedNode,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                    });
                    return result;
                }
                const auto& resolved =
                    placedScene.nodes[placedNodeIndex];
                if (!resolved.instantiated ||
                    resolved.meshTarget.kind !=
                        assets::PlacedMeshTargetKind::parsedMesh ||
                    !resolved.meshTarget.meshIndex.has_value() ||
                    *resolved.meshTarget.meshIndex >=
                        ccf.meshes.size()) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidMeshTarget,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .reference = object->meshReference,
                    });
                    return result;
                }
                const auto physicalMeshIndex =
                    *resolved.meshTarget.meshIndex;
                if (ccf.meshes[physicalMeshIndex].reference !=
                    object->meshReference) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidMeshTarget,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                        .reference = object->meshReference,
                    });
                    return result;
                }

                // Native cached 0x4101 handling copies mesh+0x13c into the
                // later object and skips every nested chunk. It does not
                // perform the SetRoom(root)/SetRoom(original) relink which
                // publishes the first-use object into room+0.
                if (meshCache[physicalMeshIndex].has_value()) {
                    continue;
                }

                std::size_t worldRoomIndex = 0U;
                std::optional<std::size_t> physicalRoomIndex;
                if (resolved.roomTarget.kind ==
                    assets::PlacedRoomTargetKind::parsedRoom) {
                    if (!resolved.roomTarget.roomIndex.has_value() ||
                        *resolved.roomTarget.roomIndex >=
                            physicalToWorld[sourceIndex].size() ||
                        !physicalToWorld[sourceIndex]
                                        [*resolved.roomTarget.roomIndex]
                                            .has_value()) {
                        fail(result, {
                            .kind =
                                MissionPlacedDynamicBspIssueKind::
                                    invalidRoomTarget,
                            .sourceIndex = sourceIndex,
                            .placedNodeIndex = placedNodeIndex,
                            .physicalMeshIndex =
                                physicalMeshIndex,
                            .reference = placed.roomReference,
                        });
                        return result;
                    }
                    physicalRoomIndex =
                        *resolved.roomTarget.roomIndex;
                    worldRoomIndex =
                        *physicalToWorld[sourceIndex]
                                        [*physicalRoomIndex];
                }
                else if (resolved.roomTarget.kind !=
                         assets::PlacedRoomTargetKind::
                             externalReceiverFallback) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidRoomTarget,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                        .reference = placed.roomReference,
                    });
                    return result;
                }

                const auto portalType =
                    std::bit_cast<std::int32_t>(
                        object->portalType);
                std::optional<std::size_t> portalWorldRoomIndex;
                if (portalType < -1 || portalType > 1) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidPortalType,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                    });
                    return result;
                }
                if (portalType == -1) {
                    if (resolved.portalRoomTarget.kind ==
                            assets::PlacedPortalRoomTargetKind::
                                parsedRoom &&
                        resolved.portalRoomTarget.roomIndex
                            .has_value()) {
                        fail(result, {
                            .kind =
                                MissionPlacedDynamicBspIssueKind::
                                    invalidPortalTarget,
                            .sourceIndex = sourceIndex,
                            .placedNodeIndex = placedNodeIndex,
                            .physicalMeshIndex =
                                physicalMeshIndex,
                            .reference =
                                object->portalRoomReference,
                        });
                        return result;
                    }
                }
                else {
                    if (resolved.portalRoomTarget.kind !=
                            assets::PlacedPortalRoomTargetKind::
                                parsedRoom ||
                        !resolved.portalRoomTarget.roomIndex
                             .has_value() ||
                        *resolved.portalRoomTarget.roomIndex >=
                            physicalToWorld[sourceIndex].size() ||
                        !physicalToWorld[sourceIndex]
                                        [*resolved.portalRoomTarget
                                             .roomIndex]
                                            .has_value()) {
                        fail(result, {
                            .kind =
                                MissionPlacedDynamicBspIssueKind::
                                    invalidPortalTarget,
                            .sourceIndex = sourceIndex,
                            .placedNodeIndex = placedNodeIndex,
                            .physicalMeshIndex =
                                physicalMeshIndex,
                            .reference =
                                object->portalRoomReference,
                        });
                        return result;
                    }
                    portalWorldRoomIndex =
                        *physicalToWorld[sourceIndex]
                                        [*resolved.portalRoomTarget
                                             .roomIndex];
                }

                ConvertedNodeTransform world;
                try {
                    world = convertLegacyTransform(
                        placed.transform, basis);
                }
                catch (const GeometryError& error) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                geometryFailure,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                        .geometryError = error.code(),
                    });
                    return result;
                }
                if (world.rawScalar != 1.0F ||
                    !orthonormal(world.linear) ||
                    !finite(world.translation)) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                invalidTransform,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                    });
                    return result;
                }

                if (result.meshes.size() >=
                        limits.maximumMeshes ||
                    aggregateObjects >= limits.maximumObjects ||
                    !checkedAdd(
                        aggregateTrees,
                        object->dynamicBspTrees.size(),
                        limits.maximumTrees)) {
                    fail(result, {
                        .kind =
                            MissionPlacedDynamicBspIssueKind::
                                limitExceeded,
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                    });
                    return result;
                }
                for (const auto& tree :
                     object->dynamicBspTrees) {
                    if (!checkedAdd(
                            aggregateNodes,
                            tree.nodes.size(),
                            limits.maximumNodes) ||
                        !checkedAdd(
                            aggregatePolygons,
                            tree.polygons.size(),
                            limits.maximumPolygons)) {
                        fail(result, {
                            .kind =
                                MissionPlacedDynamicBspIssueKind::
                                    limitExceeded,
                            .sourceIndex = sourceIndex,
                            .placedNodeIndex = placedNodeIndex,
                            .physicalMeshIndex =
                                physicalMeshIndex,
                        });
                        return result;
                    }
                }

                LegacyDynamicBspMesh mesh;
                if (!buildMesh(
                        ccf,
                        placed,
                        *object,
                        sourceIndex,
                        placedNodeIndex,
                        physicalMeshIndex,
                        basis,
                        limits,
                        materialReferences,
                        result,
                        mesh)) {
                    return result;
                }
                const auto meshIndex = result.meshes.size();
                result.meshes.push_back(std::move(mesh));
                result.meshProvenance.push_back({
                    .sourceIndex = sourceIndex,
                    .physicalMeshIndex = physicalMeshIndex,
                    .firstPlacedNodeIndex = placedNodeIndex,
                    .sourceMeshReference =
                        ccf.meshes[physicalMeshIndex].reference,
                });
                meshCache[physicalMeshIndex] = meshIndex;
                ++aggregateObjects;
                pendingByRoom[worldRoomIndex].push_back({
                    .object = {
                        .meshIndex = meshIndex,
                        .actorObjectId = 0U,
                        .active = true,
                        .objectLocalToRuntime = world.linear,
                        .runtimeTranslation = world.translation,
                        .portalType = portalType,
                        .portalWorldRoomIndex =
                            portalWorldRoomIndex,
                        .portalObjectVisible =
                            object->rawFlag != 0U,
                    },
                    .provenance = {
                        .sourceIndex = sourceIndex,
                        .placedNodeIndex = placedNodeIndex,
                        .physicalMeshIndex = physicalMeshIndex,
                        .worldRoomIndex = worldRoomIndex,
                        .sourceNodeReference =
                            placed.currentReference,
                    },
                });
            }
        }

        result.roomObjectRanges.reserve(pendingByRoom.size());
        for (auto& pending : pendingByRoom) {
            const auto firstObjectIndex = result.objects.size();
            for (auto iterator = pending.rbegin();
                 iterator != pending.rend();
                 ++iterator) {
                result.objects.push_back(iterator->object);
                result.objectProvenance.push_back(
                    iterator->provenance);
            }
            result.roomObjectRanges.push_back({
                .firstObjectIndex = firstObjectIndex,
                .objectCount =
                    result.objects.size() - firstObjectIndex,
            });
        }

        std::uint64_t retainedBytes = 0U;
        if (!checkedBytes(
                retainedBytes,
                result.meshes.size(),
                sizeof(LegacyDynamicBspMesh)) ||
            !checkedBytes(
                retainedBytes,
                result.meshProvenance.size(),
                sizeof(MissionPlacedDynamicBspMeshProvenance)) ||
            !checkedBytes(
                retainedBytes,
                result.objects.size(),
                sizeof(LegacyDynamicBspLineObject)) ||
            !checkedBytes(
                retainedBytes,
                result.objectProvenance.size(),
                sizeof(MissionPlacedDynamicBspObjectProvenance)) ||
            !checkedBytes(
                retainedBytes,
                result.roomObjectRanges.size(),
                sizeof(LegacyDynamicBspRoomObjectRange))) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        integerOverflow,
            });
            return result;
        }
        for (const auto& mesh : result.meshes) {
            if (mesh.retainedPayloadBytes >
                std::numeric_limits<std::uint64_t>::max() -
                    retainedBytes) {
                fail(result, {
                    .kind =
                        MissionPlacedDynamicBspIssueKind::
                            integerOverflow,
                });
                return result;
            }
            retainedBytes += mesh.retainedPayloadBytes;
        }
        if (retainedBytes > limits.maximumRetainedBytes) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        retainedByteLimitExceeded,
            });
            return result;
        }
        result.retainedPayloadBytes = retainedBytes;
        if (!result.complete()) {
            fail(result, {
                .kind =
                    MissionPlacedDynamicBspIssueKind::
                        invalidPlacedNode,
            });
        }
        return result;
    }
    catch (const std::bad_alloc&) {
        fail(result, {
            .kind =
                MissionPlacedDynamicBspIssueKind::allocationFailure,
        });
        return result;
    }
    catch (...) {
        fail(result, {
            .kind =
                MissionPlacedDynamicBspIssueKind::allocationFailure,
        });
        return result;
    }
}

} // namespace airfix::render

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
