#include "airfix/render/MissionWorldRuntimeSphereCollision.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

struct PreparedSphereBasis final {
    Mat3 runtimeToSource{};
    float sourceUnitsPerRuntimeUnit{};
};

struct TraversalFrame final {
    std::size_t nodeIndex{};
    float splitDistance{};
    std::uint8_t phase{};
};

struct CollectionState final {
    std::span<MissionWorldRuntimeSphereCandidate> workspace;
    std::size_t count{};
    std::array<
        std::size_t,
        kMissionWorldRuntimeSphereMaximumPortalRooms>
        rooms{};
    std::array<
        bool,
        kMissionWorldRuntimeSphereMaximumPortalRooms>
        processed{};
    std::size_t roomCount{};
    std::size_t maximumPortalRooms{};
};

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) &&
        finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] bool finite(
    const assets::CcfVector3& value) noexcept {
    return std::ranges::all_of(
        value,
        [](const float component) {
            return std::isfinite(component);
        });
}

[[nodiscard]] bool legacyFaceNormal(
    const assets::CcfVector3& value) noexcept {
    if (finite(value)) {
        return true;
    }
    return std::ranges::all_of(
        value,
        [](const float component) {
            return std::bit_cast<std::uint32_t>(component) ==
                0xFFC00000U;
        });
}

[[nodiscard]] bool rangeWithin(
    const std::size_t first,
    const std::size_t count,
    const std::size_t size) noexcept {
    return first <= size && count <= size - first;
}

[[nodiscard]] double dot(
    const Vec3& left,
    const Vec3& right) noexcept {
    return static_cast<double>(left.x) * right.x +
        static_cast<double>(left.y) * right.y +
        static_cast<double>(left.z) * right.z;
}

[[nodiscard]] bool orthonormal(
    const Mat3& value) noexcept {
    constexpr double tolerance = 1.0e-5;
    for (std::size_t column = 0U; column < 3U; ++column) {
        if (std::abs(dot(
                value.columns[column],
                value.columns[column]) -
                1.0) > tolerance) {
            return false;
        }
        for (std::size_t other = column + 1U;
             other < 3U;
             ++other) {
            if (std::abs(dot(
                    value.columns[column],
                    value.columns[other])) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::optional<PreparedSphereBasis> prepareBasis(
    const BasisTransform& basis) noexcept {
    if (!finite(basis.sourceToRuntime) ||
        !orthonormal(basis.sourceToRuntime) ||
        !std::isfinite(basis.runtimeUnitsPerSourceUnit) ||
        !(basis.runtimeUnitsPerSourceUnit > 0.0F)) {
        return std::nullopt;
    }
    const auto runtimeToSource =
        inverse(basis.sourceToRuntime);
    const float sourceUnitsPerRuntimeUnit =
        1.0F / basis.runtimeUnitsPerSourceUnit;
    if (!runtimeToSource.has_value() ||
        !finite(*runtimeToSource) ||
        !std::isfinite(sourceUnitsPerRuntimeUnit) ||
        !(sourceUnitsPerRuntimeUnit > 0.0F)) {
        return std::nullopt;
    }
    return PreparedSphereBasis{
        .runtimeToSource = *runtimeToSource,
        .sourceUnitsPerRuntimeUnit =
            sourceUnitsPerRuntimeUnit,
    };
}

[[nodiscard]] std::optional<Vec3> sourcePoint(
    const Vec3& runtimePoint,
    const PreparedSphereBasis& basis) noexcept {
    if (!finite(runtimePoint)) {
        return std::nullopt;
    }
    const Vec3 scaled{
        runtimePoint.x * basis.sourceUnitsPerRuntimeUnit,
        runtimePoint.y * basis.sourceUnitsPerRuntimeUnit,
        runtimePoint.z * basis.sourceUnitsPerRuntimeUnit,
    };
    const Vec3 converted =
        applyRuntimeColumn(basis.runtimeToSource, scaled);
    if (!finite(scaled) || !finite(converted)) {
        return std::nullopt;
    }
    return converted;
}

[[nodiscard]] std::optional<Vec3> runtimePoint(
    const Vec3& sourcePoint,
    const BasisTransform& basis) noexcept {
    if (!finite(sourcePoint)) {
        return std::nullopt;
    }
    Vec3 converted =
        applyRuntimeColumn(basis.sourceToRuntime, sourcePoint);
    converted.x *= basis.runtimeUnitsPerSourceUnit;
    converted.y *= basis.runtimeUnitsPerSourceUnit;
    converted.z *= basis.runtimeUnitsPerSourceUnit;
    if (!finite(converted)) {
        return std::nullopt;
    }
    return converted;
}

[[nodiscard]] std::optional<Vec3> runtimeDirection(
    const Vec3& sourceDirection,
    const bool distanceVector,
    const BasisTransform& basis) noexcept {
    if (!finite(sourceDirection)) {
        return std::nullopt;
    }
    Vec3 converted =
        applyRuntimeColumn(
            basis.sourceToRuntime, sourceDirection);
    if (distanceVector) {
        converted.x *= basis.runtimeUnitsPerSourceUnit;
        converted.y *= basis.runtimeUnitsPerSourceUnit;
        converted.z *= basis.runtimeUnitsPerSourceUnit;
    }
    if (!finite(converted)) {
        return std::nullopt;
    }
    return converted;
}

[[nodiscard]] Vec3 vector(
    const assets::CcfVector3& value) noexcept {
    return {value[0], value[1], value[2]};
}

[[nodiscard]] Vec3 addBinary32(
    const Vec3& left,
    const Vec3& right) noexcept {
    return {
        static_cast<float>(
            static_cast<double>(left.x) + right.x),
        static_cast<float>(
            static_cast<double>(left.y) + right.y),
        static_cast<float>(
            static_cast<double>(left.z) + right.z),
    };
}

[[nodiscard]] Vec3 negate(const Vec3& value) noexcept {
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] float splitDistance(
    const Vec3& center,
    const assets::MissionWorldSpatialNode& node) noexcept {
    const float relativeX =
        center.x - node.pointOnPlane[0];
    const float relativeY =
        center.y - node.pointOnPlane[1];
    const float relativeZ =
        center.z - node.pointOnPlane[2];
    const double result =
        static_cast<double>(relativeX) *
            node.splitNormal[0] +
        static_cast<double>(relativeY) *
            node.splitNormal[1] +
        static_cast<double>(relativeZ) *
            node.splitNormal[2];
    return static_cast<float>(result);
}

[[nodiscard]] bool validChild(
    const std::optional<std::size_t> child,
    const assets::MissionWorldSpatialTree& tree) noexcept {
    return !child.has_value() ||
        (*child >= tree.firstNodeIndex &&
         *child < tree.firstNodeIndex + tree.nodeCount);
}

[[nodiscard]] std::optional<std::size_t> roomSlot(
    const CollectionState& state,
    const std::size_t worldRoomIndex) noexcept {
    for (std::size_t index = 0U;
         index < state.roomCount;
         ++index) {
        if (state.rooms[index] == worldRoomIndex) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] MissionWorldRuntimeSphereCollisionStatus addPortalRoom(
    CollectionState& state,
    const std::size_t worldRoomIndex) noexcept {
    if (roomSlot(state, worldRoomIndex).has_value()) {
        return MissionWorldRuntimeSphereCollisionStatus::noContact;
    }
    if (state.roomCount >= state.maximumPortalRooms ||
        state.roomCount >= state.rooms.size()) {
        return MissionWorldRuntimeSphereCollisionStatus::
            portalRoomLimitExceeded;
    }
    state.rooms[state.roomCount] = worldRoomIndex;
    state.processed[state.roomCount] = false;
    ++state.roomCount;
    return MissionWorldRuntimeSphereCollisionStatus::noContact;
}

[[nodiscard]] MissionWorldRuntimeSphereCollisionStatus collectPolygon(
    const assets::MissionWorldSpatialArena& arena,
    const assets::MissionWorldSpatialTree& tree,
    const assets::MissionWorldSpatialNode& node,
    const std::size_t nodeIndex,
    const std::size_t polygonIndex,
    const Vec3& sphereCenter,
    const float sphereRadius,
    CollectionState& state) noexcept {
    if (polygonIndex >= arena.polygons.size()) {
        return MissionWorldRuntimeSphereCollisionStatus::invalidArena;
    }
    const auto& polygon = arena.polygons[polygonIndex];
    if (!finite(polygon.faceCross) ||
        !legacyFaceNormal(polygon.faceNormal) ||
        !finite(polygon.point0) ||
        !finite(polygon.edge01) ||
        !finite(polygon.edge12)) {
        return MissionWorldRuntimeSphereCollisionStatus::invalidArena;
    }

    const Vec3 point0 = vector(polygon.point0);
    const Vec3 edge01 = vector(polygon.edge01);
    const Vec3 edge02 = addBinary32(
        edge01, vector(polygon.edge12));
    const Vec3 point1 =
        addBinary32(point0, edge01);
    const Vec3 point2 =
        addBinary32(point0, edge02);
    if (!finite(edge02) ||
        !finite(point1) ||
        !finite(point2)) {
        return MissionWorldRuntimeSphereCollisionStatus::invalidArena;
    }
    const auto candidate =
        legacyGameplayCameraSphereTriangleCandidate(
            sphereCenter,
            sphereRadius,
            point0,
            point1,
            point2,
            vector(node.splitNormal));
    if (!candidate.has_value()) {
        return MissionWorldRuntimeSphereCollisionStatus::invalidArena;
    }
    if (!*candidate) {
        return MissionWorldRuntimeSphereCollisionStatus::noContact;
    }

    if (polygon.portalWorldRoomIndex.has_value()) {
        if (*polygon.portalWorldRoomIndex >= arena.rooms.size()) {
            return MissionWorldRuntimeSphereCollisionStatus::invalidArena;
        }
        if (polygon.portalType == 0U) {
            if (polygon.portalObjectVisible) {
                return addPortalRoom(
                    state, *polygon.portalWorldRoomIndex);
            }
            return MissionWorldRuntimeSphereCollisionStatus::noContact;
        }
    }
    if (polygon.materialCollisionMode2152 == 0U ||
        polygon.materialCollisionMode2152 == 8U) {
        return MissionWorldRuntimeSphereCollisionStatus::noContact;
    }
    if (state.count >= state.workspace.size()) {
        return MissionWorldRuntimeSphereCollisionStatus::
            candidateCapacityExceeded;
    }
    state.workspace[state.count++] =
        MissionWorldRuntimeSphereCandidate{
            .ownerWorldRoomIndex = tree.worldRoomIndex,
            .treeIndex =
                static_cast<std::size_t>(
                    &tree - arena.trees.data()),
            .nodeIndex = nodeIndex,
            .polygonIndex = polygonIndex,
            .active = true,
        };
    return MissionWorldRuntimeSphereCollisionStatus::noContact;
}

[[nodiscard]] MissionWorldRuntimeSphereCollisionStatus collectTree(
    const assets::MissionWorldSpatialArena& arena,
    const assets::MissionWorldSpatialTree& tree,
    const Vec3& sphereCenter,
    const float sphereRadius,
    CollectionState& state) noexcept {
    if (!rangeWithin(
            tree.firstNodeIndex,
            tree.nodeCount,
            arena.nodes.size()) ||
        tree.nodeCount == 0U ||
        tree.rootNodeIndex < tree.firstNodeIndex ||
        tree.rootNodeIndex >=
            tree.firstNodeIndex + tree.nodeCount) {
        return MissionWorldRuntimeSphereCollisionStatus::invalidArena;
    }

    std::array<
        TraversalFrame,
        assets::kMissionWorldSpatialMaximumTraceDepth>
        stack{};
    std::size_t depth = 1U;
    stack[0].nodeIndex = tree.rootNodeIndex;
    while (depth != 0U) {
        auto& frame = stack[depth - 1U];
        if (frame.nodeIndex < tree.firstNodeIndex ||
            frame.nodeIndex >=
                tree.firstNodeIndex + tree.nodeCount) {
            return MissionWorldRuntimeSphereCollisionStatus::
                invalidArena;
        }
        const auto& node = arena.nodes[frame.nodeIndex];
        if (!validChild(node.childAIndex, tree) ||
            !validChild(node.childBIndex, tree) ||
            !finite(node.splitNormal) ||
            !finite(node.pointOnPlane) ||
            !rangeWithin(
                node.firstPolygonIndex,
                node.polygonCount,
                arena.polygons.size())) {
            return MissionWorldRuntimeSphereCollisionStatus::
                invalidArena;
        }

        if (frame.phase == 0U) {
            frame.splitDistance =
                splitDistance(sphereCenter, node);
            if (!std::isfinite(frame.splitDistance)) {
                return MissionWorldRuntimeSphereCollisionStatus::
                    invalidInput;
            }
            frame.phase = 1U;
            if (-sphereRadius <= frame.splitDistance &&
                node.childBIndex.has_value()) {
                if (depth >= stack.size()) {
                    return MissionWorldRuntimeSphereCollisionStatus::
                        traversalDepthExceeded;
                }
                stack[depth++].nodeIndex =
                    *node.childBIndex;
                continue;
            }
        }
        if (frame.phase == 1U) {
            frame.phase = 2U;
            if (frame.splitDistance < sphereRadius &&
                node.childAIndex.has_value()) {
                if (depth >= stack.size()) {
                    return MissionWorldRuntimeSphereCollisionStatus::
                        traversalDepthExceeded;
                }
                stack[depth++].nodeIndex =
                    *node.childAIndex;
                continue;
            }
        }
        if (frame.phase == 2U) {
            frame.phase = 3U;
            for (std::size_t localPolygonIndex = 0U;
                 localPolygonIndex < node.polygonCount;
                 ++localPolygonIndex) {
                const auto status = collectPolygon(
                    arena,
                    tree,
                    node,
                    frame.nodeIndex,
                    node.firstPolygonIndex + localPolygonIndex,
                    sphereCenter,
                    sphereRadius,
                    state);
                if (status !=
                    MissionWorldRuntimeSphereCollisionStatus::
                        noContact) {
                    return status;
                }
            }
        }
        --depth;
    }
    return MissionWorldRuntimeSphereCollisionStatus::noContact;
}

[[nodiscard]] MissionWorldRuntimeSphereCollisionStatus collectRoom(
    const assets::MissionWorldSpatialArena& arena,
    const std::size_t worldRoomIndex,
    const Vec3& sphereCenter,
    const float sphereRadius,
    CollectionState& state) noexcept {
    if (worldRoomIndex >= arena.rooms.size()) {
        return MissionWorldRuntimeSphereCollisionStatus::
            invalidWorldRoom;
    }
    const auto& room = arena.rooms[worldRoomIndex];
    if (!rangeWithin(
            room.firstStaticTreeReference,
            room.staticTreeCount,
            arena.treeReferences.size())) {
        return MissionWorldRuntimeSphereCollisionStatus::invalidArena;
    }
    for (std::size_t localTreeIndex = 0U;
         localTreeIndex < room.staticTreeCount;
         ++localTreeIndex) {
        const auto treeIndex =
            arena.treeReferences[
                room.firstStaticTreeReference + localTreeIndex];
        if (treeIndex >= arena.trees.size()) {
            return MissionWorldRuntimeSphereCollisionStatus::
                invalidArena;
        }
        const auto& tree = arena.trees[treeIndex];
        if (tree.kind != assets::CcfBspTreeKind::staticTree ||
            tree.worldRoomIndex != worldRoomIndex) {
            return MissionWorldRuntimeSphereCollisionStatus::
                invalidArena;
        }
        const auto status = collectTree(
            arena, tree, sphereCenter, sphereRadius, state);
        if (status !=
            MissionWorldRuntimeSphereCollisionStatus::noContact) {
            return status;
        }
    }
    return MissionWorldRuntimeSphereCollisionStatus::noContact;
}

[[nodiscard]] MissionWorldRuntimeSphereCollisionStatus collectCandidates(
    const assets::MissionWorldSpatialArena& arena,
    const std::size_t worldRoomIndex,
    const Vec3& sphereCenter,
    const float sphereRadius,
    CollectionState& state) noexcept {
    state.rooms[0] = worldRoomIndex;
    state.roomCount = 1U;
    while (true) {
        std::optional<std::size_t> nextRoom;
        for (std::size_t index = state.roomCount;
             index > 0U;
             --index) {
            if (!state.processed[index - 1U]) {
                nextRoom = index - 1U;
                break;
            }
        }
        if (!nextRoom.has_value()) {
            return MissionWorldRuntimeSphereCollisionStatus::noContact;
        }
        state.processed[*nextRoom] = true;
        const auto status = collectRoom(
            arena,
            state.rooms[*nextRoom],
            sphereCenter,
            sphereRadius,
            state);
        if (status !=
            MissionWorldRuntimeSphereCollisionStatus::noContact) {
            return status;
        }
    }
}

[[nodiscard]] bool polygonPoints(
    const assets::MissionWorldSpatialPolygon& polygon,
    Vec3& point0,
    Vec3& point1,
    Vec3& point2,
    Vec3& edge01,
    Vec3& edge02) noexcept {
    point0 = vector(polygon.point0);
    edge01 = vector(polygon.edge01);
    edge02 = addBinary32(
        edge01, vector(polygon.edge12));
    point1 = addBinary32(
        point0, edge01);
    point2 = addBinary32(point0, edge02);
    return finite(point0) &&
        finite(point1) &&
        finite(point2) &&
        finite(edge01) &&
        finite(edge02);
}

[[nodiscard]] std::optional<MissionWorldRuntimeSphereContact>
adaptContact(
    const LegacyGameplayCameraSphereContact& sourceContact,
    const MissionWorldRuntimeSphereCandidate& candidate,
    const BasisTransform& runtimeBasis) noexcept {
    const bool distanceDirection =
        sourceContact.feature !=
        LegacyGameplayCameraSphereContactFeature::face;
    const auto direction = runtimeDirection(
        sourceContact.direction,
        distanceDirection,
        runtimeBasis);
    const auto point =
        runtimePoint(sourceContact.contactPoint, runtimeBasis);
    const float depth =
        sourceContact.penetrationDepth *
        runtimeBasis.runtimeUnitsPerSourceUnit;
    if (!direction.has_value() ||
        !point.has_value() ||
        !std::isfinite(depth)) {
        return std::nullopt;
    }
    return MissionWorldRuntimeSphereContact{
        .penetrationDepth = depth,
        .direction = *direction,
        .contactPoint = *point,
        .feature = sourceContact.feature,
        .ownerWorldRoomIndex =
            candidate.ownerWorldRoomIndex,
        .treeIndex = candidate.treeIndex,
        .nodeIndex = candidate.nodeIndex,
        .polygonIndex = candidate.polygonIndex,
    };
}

} // namespace

MissionWorldRuntimeSphereCollisionResult
resolveMissionWorldRuntimeStaticSphere(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const std::size_t worldRoomIndex,
    const Vec3& runtimeCenter,
    const float runtimeRadius,
    const std::span<MissionWorldRuntimeSphereCandidate>
        candidateWorkspace,
    const std::span<Vec3> constraintPlanesHeadFirst,
    const MissionWorldRuntimeSphereCollisionOptions& options) noexcept {
    MissionWorldRuntimeSphereCollisionResult result;
    result.originalCenter = runtimeCenter;
    result.resolvedCenter = runtimeCenter;
    if (!arena.complete()) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::invalidArena;
        return result;
    }
    if (worldRoomIndex >= arena.rooms.size()) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::invalidWorldRoom;
        return result;
    }
    if (!finite(runtimeCenter) ||
        !std::isfinite(runtimeRadius) ||
        !(runtimeRadius > 0.0F) ||
        options.maximumPortalRooms == 0U ||
        options.maximumPortalRooms >
            kMissionWorldRuntimeSphereMaximumPortalRooms) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::invalidInput;
        return result;
    }
    const auto basis = prepareBasis(runtimeBasis);
    if (!basis.has_value()) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::invalidBasis;
        return result;
    }
    const auto center =
        sourcePoint(runtimeCenter, *basis);
    const float sourceRadius =
        runtimeRadius * basis->sourceUnitsPerRuntimeUnit;
    if (!center.has_value() ||
        !std::isfinite(sourceRadius) ||
        !(sourceRadius > 0.0F)) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::invalidInput;
        return result;
    }

    std::fill(
        candidateWorkspace.begin(),
        candidateWorkspace.end(),
        MissionWorldRuntimeSphereCandidate{});
    std::fill(
        constraintPlanesHeadFirst.begin(),
        constraintPlanesHeadFirst.end(),
        Vec3{});
    CollectionState collection{
        .workspace = candidateWorkspace,
        .maximumPortalRooms = options.maximumPortalRooms,
    };
    const auto collectionStatus = collectCandidates(
        arena,
        worldRoomIndex,
        *center,
        sourceRadius,
        collection);
    result.candidateCount = collection.count;
    if (collectionStatus !=
        MissionWorldRuntimeSphereCollisionStatus::noContact) {
        result.status = collectionStatus;
        return result;
    }
    if (collection.count == 0U) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::noContact;
        return result;
    }

    Vec3 sourceCenter = *center;
    bool sawContact = false;
    std::size_t planeCount = 0U;
    for (std::size_t iteration = 0U;
         iteration < collection.count;
         ++iteration) {
        float bestDepth = 0.0F;
        std::optional<std::size_t> bestIndex;
        std::optional<LegacyGameplayCameraSphereContact> bestContact;
        for (std::size_t index = collection.count;
             index > 0U;
             --index) {
            auto& candidate =
                candidateWorkspace[index - 1U];
            if (!candidate.active ||
                candidate.polygonIndex >= arena.polygons.size()) {
                continue;
            }
            const auto& polygon =
                arena.polygons[candidate.polygonIndex];
            Vec3 point0;
            Vec3 point1;
            Vec3 point2;
            Vec3 edge01;
            Vec3 edge02;
            if (!polygonPoints(
                    polygon,
                    point0,
                    point1,
                    point2,
                    edge01,
                    edge02)) {
                result.status =
                    MissionWorldRuntimeSphereCollisionStatus::
                        invalidArena;
                return result;
            }
            const auto contact =
                legacyGameplayCameraSphereTriangleContact(
                    sourceCenter,
                    sourceRadius,
                    point0,
                    edge01,
                    edge02,
                    vector(polygon.faceNormal));
            if (!contact.valid()) {
                result.status =
                    MissionWorldRuntimeSphereCollisionStatus::
                        invalidArena;
                return result;
            }
            if (!contact.contact.has_value()) {
                continue;
            }
            sawContact = true;
            if (contact.contact->penetrationDepth > bestDepth) {
                bestDepth =
                    contact.contact->penetrationDepth;
                bestIndex = index - 1U;
                bestContact = contact.contact;
            }
        }
        if (!bestIndex.has_value() ||
            !bestContact.has_value()) {
            break;
        }

        const double negativeDepth = -bestDepth;
        const Vec3 requestedMove{
            static_cast<float>(
                negativeDepth * bestContact->direction.x),
            static_cast<float>(
                negativeDepth * bestContact->direction.y),
            static_cast<float>(
                negativeDepth * bestContact->direction.z),
        };
        const auto acceptedMove =
            legacyGameplayCameraAttemptConstrainedMove(
                requestedMove,
                constraintPlanesHeadFirst.first(planeCount));
        if (!acceptedMove.has_value()) {
            result.status =
                MissionWorldRuntimeSphereCollisionStatus::
                    invalidInput;
            return result;
        }
        sourceCenter = addBinary32(
            sourceCenter, *acceptedMove);
        if (!finite(sourceCenter)) {
            result.status =
                MissionWorldRuntimeSphereCollisionStatus::
                    invalidInput;
            return result;
        }

        const Vec3 newPlane =
            negate(bestContact->direction);
        const auto acceptsPlane =
            legacyGameplayCameraConstraintAcceptsPlane(
                constraintPlanesHeadFirst.first(planeCount),
                newPlane);
        if (!acceptsPlane.has_value()) {
            result.status =
                MissionWorldRuntimeSphereCollisionStatus::
                    invalidInput;
            return result;
        }
        if (*acceptsPlane) {
            if (planeCount >=
                constraintPlanesHeadFirst.size()) {
                result.status =
                    MissionWorldRuntimeSphereCollisionStatus::
                        constraintCapacityExceeded;
                return result;
            }
            for (std::size_t index = planeCount;
                 index > 0U;
                 --index) {
                constraintPlanesHeadFirst[index] =
                    constraintPlanesHeadFirst[index - 1U];
            }
            constraintPlanesHeadFirst[0] = newPlane;
            ++planeCount;
        }

        auto& selected =
            candidateWorkspace[*bestIndex];
        selected.active = false;
        ++result.resolutionIterations;
        result.constraintPlaneCount = planeCount;
        result.lastSelectedContact = adaptContact(
            *bestContact, selected, runtimeBasis);
        if (!result.lastSelectedContact.has_value()) {
            result.status =
                MissionWorldRuntimeSphereCollisionStatus::
                    invalidInput;
            return result;
        }

        for (std::size_t index = 0U;
             index < collection.count;
             ++index) {
            auto& candidate = candidateWorkspace[index];
            if (!candidate.active) {
                continue;
            }
            if (candidate.nodeIndex >= arena.nodes.size() ||
                candidate.polygonIndex >= arena.polygons.size()) {
                result.status =
                    MissionWorldRuntimeSphereCollisionStatus::
                        invalidArena;
                return result;
            }
            const auto& node = arena.nodes[candidate.nodeIndex];
            const auto& polygon =
                arena.polygons[candidate.polygonIndex];
            Vec3 point0;
            Vec3 point1;
            Vec3 point2;
            Vec3 edge01;
            Vec3 edge02;
            if (!polygonPoints(
                    polygon,
                    point0,
                    point1,
                    point2,
                    edge01,
                    edge02)) {
                result.status =
                    MissionWorldRuntimeSphereCollisionStatus::
                        invalidArena;
                return result;
            }
            const auto remainsCandidate =
                legacyGameplayCameraSphereTriangleCandidate(
                    sourceCenter,
                    sourceRadius,
                    point0,
                    point1,
                    point2,
                    vector(node.splitNormal));
            if (!remainsCandidate.has_value()) {
                result.status =
                    MissionWorldRuntimeSphereCollisionStatus::
                        invalidArena;
                return result;
            }
            candidate.active = *remainsCandidate;
        }
    }

    const auto resolved =
        runtimePoint(sourceCenter, runtimeBasis);
    if (!resolved.has_value()) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::invalidInput;
        return result;
    }
    result.resolvedCenter = *resolved;
    result.correction = {
        result.resolvedCenter.x - result.originalCenter.x,
        result.resolvedCenter.y - result.originalCenter.y,
        result.resolvedCenter.z - result.originalCenter.z,
    };
    if (!finite(result.correction)) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::invalidInput;
        return result;
    }
    if (result.resolutionIterations != 0U) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::resolved;
    }
    else if (sawContact) {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::touching;
    }
    else {
        result.status =
            MissionWorldRuntimeSphereCollisionStatus::noContact;
    }
    return result;
}

} // namespace airfix::render
