#include "airfix/assets/MissionWorldSpatialLineTrace.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

namespace airfix::assets {
namespace {

inline constexpr double kLegacyLineEpsilon =
    static_cast<double>(1.0e-6F);

struct Vec3d {
    double x{};
    double y{};
    double z{};
};

struct SplitDistances {
    float start{};
    float end{};
    float crossing{};
};

[[nodiscard]] Vec3d vector(const CcfVector3& value) noexcept {
    return {
        static_cast<double>(value[0]),
        static_cast<double>(value[1]),
        static_cast<double>(value[2]),
    };
}

[[nodiscard]] CcfVector3 legacyLineDirection(
    const CcfVector3& start,
    const CcfVector3& end) noexcept {
    // GetPortalBspCollision spills every endpoint subtraction to binary32
    // before TraceBspNode consumes the direction.
    return {
        static_cast<float>(
            static_cast<double>(end[0]) - static_cast<double>(start[0])),
        static_cast<float>(
            static_cast<double>(end[1]) - static_cast<double>(start[1])),
        static_cast<float>(
            static_cast<double>(end[2]) - static_cast<double>(start[2])),
    };
}

[[nodiscard]] CcfVector3 legacyPortalHitPoint(
    const CcfVector3& start,
    const CcfVector3& direction,
    const float fraction) noexcept {
    // The recovered x86 schedule spills the X/Y products before adding the
    // start, while the Z product remains on the x87 stack until its add.
    const float offsetX = static_cast<float>(
        static_cast<double>(fraction) *
        static_cast<double>(direction[0]));
    const float offsetY = static_cast<float>(
        static_cast<double>(fraction) *
        static_cast<double>(direction[1]));
    const double offsetZ =
        static_cast<double>(fraction) *
        static_cast<double>(direction[2]);
    return {
        static_cast<float>(
            static_cast<double>(start[0]) +
            static_cast<double>(offsetX)),
        static_cast<float>(
            static_cast<double>(start[1]) +
            static_cast<double>(offsetY)),
        static_cast<float>(
            static_cast<double>(start[2]) + offsetZ),
    };
}

[[nodiscard]] Vec3d subtractBinary32(
    const Vec3d left,
    const Vec3d right) noexcept {
    return {
        static_cast<double>(static_cast<float>(left.x - right.x)),
        static_cast<double>(static_cast<float>(left.y - right.y)),
        static_cast<double>(static_cast<float>(left.z - right.z)),
    };
}

[[nodiscard]] Vec3d add(
    const Vec3d left,
    const Vec3d right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3d negatedSumBinary32(
    const Vec3d left,
    const Vec3d right) noexcept {
    return {
        static_cast<double>(
            static_cast<float>(-left.x - right.x)),
        static_cast<double>(
            static_cast<float>(-left.y - right.y)),
        static_cast<double>(
            static_cast<float>(-left.z - right.z)),
    };
}

[[nodiscard]] Vec3d negate(const Vec3d value) noexcept {
    return {-value.x, -value.y, -value.z};
}

[[nodiscard]] double dot(
    const Vec3d left,
    const Vec3d right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float legacySplitDistance(
    const CcfVector3& endpoint,
    const CcfVector3& point,
    const CcfVector3& normal) noexcept {
    // The x86 path spills endpoint-point components and the completed dot to
    // binary32. Its x87 additions are ordered Z, Y, X.
    const float relativeX = endpoint[0] - point[0];
    const float relativeY = endpoint[1] - point[1];
    const float relativeZ = endpoint[2] - point[2];
    const double sum =
        static_cast<double>(relativeZ) *
            static_cast<double>(normal[2]) +
        static_cast<double>(relativeY) *
            static_cast<double>(normal[1]) +
        static_cast<double>(relativeX) *
            static_cast<double>(normal[0]);
    return static_cast<float>(sum);
}

[[nodiscard]] SplitDistances legacySplitDistances(
    const CcfVector3& start,
    const CcfVector3& end,
    const MissionWorldSpatialNode& node) noexcept {
    const float startDistance = legacySplitDistance(
        start, node.pointOnPlane, node.splitNormal);
    const float endDistance = legacySplitDistance(
        end, node.pointOnPlane, node.splitNormal);
    const float crossing = static_cast<float>(
        static_cast<double>(startDistance) /
        (static_cast<double>(startDistance) -
         static_cast<double>(endDistance)));
    return {
        .start = startDistance,
        .end = endDistance,
        .crossing = crossing,
    };
}

[[nodiscard]] Vec3d cross(
    const Vec3d left,
    const Vec3d right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] bool finite(const CcfVector3& value) noexcept {
    return std::ranges::all_of(
        value, [](const float component) { return std::isfinite(component); });
}

[[nodiscard]] bool validFaceNormal(
    const CcfVector3& value) noexcept {
    if (finite(value)) {
        return true;
    }
    return std::ranges::all_of(value, [](const float component) {
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

[[nodiscard]] bool positive(const double value) noexcept {
    return 0.0 < value;
}

[[nodiscard]] bool crossesTriangleEdge(
    const Vec3d edge,
    const Vec3d lineDirection,
    const Vec3d relativePoint,
    const Vec3d oppositeOffset) noexcept {
    const auto edgeCrossLine = cross(edge, lineDirection);
    return positive(dot(edgeCrossLine, relativePoint)) !=
        positive(dot(
            edgeCrossLine, add(relativePoint, oppositeOffset)));
}

struct PolygonMatch {
    bool matches{};
    bool reverseFacing{};
};

[[nodiscard]] PolygonMatch polygonMatches(
    const MissionWorldSpatialPolygon& polygon,
    const MissionWorldSpatialNode& node,
    const Vec3d start,
    const Vec3d end,
    const Vec3d direction,
    const bool allowReverseFacing) noexcept {
    const auto faceNormal = vector(polygon.faceNormal);
    const auto nodePoint = vector(node.pointOnPlane);
    const double facing = dot(direction, faceNormal);

    Vec3d testedDirection = direction;
    // TraceBspNode spills both endpoint-nodePoint vectors before the polygon
    // plane tests reuse them.
    Vec3d relativeStart = subtractBinary32(start, nodePoint);
    Vec3d relativeEnd = subtractBinary32(end, nodePoint);
    bool reverseFacing = false;
    if (!(facing >= 0.0)) {
        if (!allowReverseFacing) {
            return {};
        }
        testedDirection = negate(direction);
        std::swap(relativeStart, relativeEnd);
        reverseFacing = true;
    }

    // These inclusive comparisons intentionally reject unordered/NaN
    // faceNormal values, including the documented legacy sentinel.
    if (!(-dot(relativeStart, faceNormal) >=
          -kLegacyLineEpsilon) ||
        !(-dot(relativeEnd, faceNormal) <=
          kLegacyLineEpsilon)) {
        return {};
    }

    const auto edge01 = vector(polygon.edge01);
    const auto edge12 = vector(polygon.edge12);
    // The reconstructed third edge is also spilled component-wise.
    const auto edge20 =
        negatedSumBinary32(edge01, edge12);
    // The original instructions always subtract the original line start here,
    // even in the reverse-facing path. The difference from the swapped start
    // is parallel to testedDirection and therefore drops from every triple
    // product, but preserving the expression also preserves operation order.
    const auto relativePoint =
        subtractBinary32(vector(polygon.point0), start);

    if (!crossesTriangleEdge(
            edge01, testedDirection, relativePoint, edge12) ||
        !crossesTriangleEdge(
            edge12, testedDirection, relativePoint, negate(edge20)) ||
        !crossesTriangleEdge(
            edge20, testedDirection, relativePoint, edge01)) {
        return {};
    }
    return {.matches = true, .reverseFacing = reverseFacing};
}

struct TraceState {
    float nearestFraction{std::numeric_limits<float>::max()};
    std::optional<MissionWorldSpatialLineHit> hit;
};

struct TraversalFrame {
    std::size_t nodeIndex{};
    float low{};
    float high{};
    float startDistance{};
    float endDistance{};
    float crossing{};
    std::uint8_t phase{};
};

[[nodiscard]] MissionWorldSpatialLineTraceStatus traceTree(
    const MissionWorldSpatialArena& arena,
    const MissionWorldSpatialTree& tree,
    const CcfVector3& startValue,
    const CcfVector3& endValue,
    const MissionWorldSpatialLineTraceOptions& options,
    TraceState& state) noexcept {
    if (!rangeWithin(
            tree.firstNodeIndex, tree.nodeCount, arena.nodes.size()) ||
        tree.nodeCount == 0U ||
        tree.rootNodeIndex < tree.firstNodeIndex ||
        tree.rootNodeIndex >= tree.firstNodeIndex + tree.nodeCount) {
        return MissionWorldSpatialLineTraceStatus::invalidArena;
    }

    const auto start = vector(startValue);
    const auto end = vector(endValue);
    const auto directionValue =
        legacyLineDirection(startValue, endValue);
    const auto direction = vector(directionValue);
    std::array<
        TraversalFrame,
        kMissionWorldSpatialMaximumTraceDepth>
        stack{};
    std::size_t depth = 1U;
    stack[0] = {
        .nodeIndex = tree.rootNodeIndex,
        .low = 0.0,
        .high = 1.0,
    };

    while (depth != 0U) {
        auto& frame = stack[depth - 1U];
        if (frame.nodeIndex < tree.firstNodeIndex ||
            frame.nodeIndex >= tree.firstNodeIndex + tree.nodeCount) {
            return MissionWorldSpatialLineTraceStatus::invalidArena;
        }
        const auto& node = arena.nodes[frame.nodeIndex];
        const auto validChild = [&tree](
                                    const std::optional<std::size_t> child) {
            return !child.has_value() ||
                (*child >= tree.firstNodeIndex &&
                 *child < tree.firstNodeIndex + tree.nodeCount);
        };
        if (!validChild(node.childAIndex) ||
            !validChild(node.childBIndex) ||
            !finite(node.splitNormal) ||
            !finite(node.pointOnPlane) ||
            !rangeWithin(
                node.firstPolygonIndex,
                node.polygonCount,
                arena.polygons.size())) {
            return MissionWorldSpatialLineTraceStatus::invalidArena;
        }

        if (frame.phase == 0U) {
            const auto distances =
                legacySplitDistances(startValue, endValue, node);
            frame.startDistance = distances.start;
            frame.endDistance = distances.end;
            frame.crossing = distances.crossing;
            frame.phase = 1U;

            if (frame.crossing >= frame.low) {
                const auto nearChild =
                    frame.startDistance < 0.0
                    ? node.childAIndex
                    : node.childBIndex;
                if (nearChild.has_value()) {
                    if (depth >= stack.size()) {
                        return MissionWorldSpatialLineTraceStatus::
                            traversalDepthExceeded;
                    }
                    stack[depth++] = {
                        .nodeIndex = *nearChild,
                        .low = frame.low,
                        .high = std::min(frame.crossing, frame.high),
                    };
                    continue;
                }
            }
        }

        if (frame.phase == 1U) {
            frame.phase = 2U;
            const bool startNonNegative = frame.startDistance >= 0.0;
            const bool endNonNegative = frame.endDistance >= 0.0;
            const bool polygonGate =
                startNonNegative != endNonNegative ||
                std::abs(frame.startDistance) < kLegacyLineEpsilon ||
                std::abs(frame.endDistance) < kLegacyLineEpsilon;
            if (polygonGate &&
                // The x87 compare enters the polygon block when unordered.
                !(frame.crossing >= state.nearestFraction)) {
                for (std::size_t localPolygonIndex = 0U;
                     localPolygonIndex < node.polygonCount;
                     ++localPolygonIndex) {
                    const auto polygonIndex =
                        node.firstPolygonIndex + localPolygonIndex;
                    const auto& polygon = arena.polygons[polygonIndex];
                    if (!finite(polygon.faceCross) ||
                        !validFaceNormal(polygon.faceNormal) ||
                        !finite(polygon.point0) ||
                        !finite(polygon.edge01) ||
                        !finite(polygon.edge12)) {
                        return MissionWorldSpatialLineTraceStatus::
                            invalidArena;
                    }
                    if (tree.kind == CcfBspTreeKind::portalTree) {
                        if (!polygon.portalWorldRoomIndex.has_value() ||
                            *polygon.portalWorldRoomIndex >=
                                arena.rooms.size()) {
                            return MissionWorldSpatialLineTraceStatus::
                                invalidArena;
                        }
                    }
                    else if (polygon.portalWorldRoomIndex.has_value()) {
                        return MissionWorldSpatialLineTraceStatus::
                            invalidArena;
                    }

                    const auto match = polygonMatches(
                        polygon,
                        node,
                        start,
                        end,
                        direction,
                        options.allowReverseFacing);
                    if (match.matches) {
                        state.nearestFraction =
                            static_cast<float>(frame.crossing);
                        const auto fraction = state.nearestFraction;
                        const auto hitPoint = legacyPortalHitPoint(
                            startValue, directionValue, fraction);
                        if (!finite(hitPoint)) {
                            return MissionWorldSpatialLineTraceStatus::
                                invalidInput;
                        }
                        state.hit = MissionWorldSpatialLineHit{
                            .fraction = fraction,
                            .point = hitPoint,
                            .normal = node.splitNormal,
                            .ownerWorldRoomIndex =
                                tree.worldRoomIndex,
                            .treeIndex =
                                static_cast<std::size_t>(&tree -
                                    arena.trees.data()),
                            .nodeIndex = frame.nodeIndex,
                            .polygonIndex = polygonIndex,
                            .portalWorldRoomIndex =
                                polygon.portalWorldRoomIndex,
                            .reverseFacing = match.reverseFacing,
                        };
                        break;
                    }
                }
            }
        }

        if (frame.phase == 2U) {
            frame.phase = 3U;
            // Legacy exits only for ordered high <= t. Unordered t therefore
            // continues through the far child with the prior low bound.
            if (!(frame.high <= frame.crossing)) {
                const auto farChild =
                    frame.endDistance >= 0.0
                    ? node.childBIndex
                    : node.childAIndex;
                if (farChild.has_value()) {
                    if (depth >= stack.size()) {
                        return MissionWorldSpatialLineTraceStatus::
                            traversalDepthExceeded;
                    }
                    stack[depth++] = {
                        .nodeIndex = *farChild,
                        .low = frame.crossing > frame.low
                            ? frame.crossing
                            : frame.low,
                        .high = frame.high,
                    };
                    continue;
                }
            }
        }
        --depth;
    }
    return MissionWorldSpatialLineTraceStatus::noHit;
}

} // namespace

MissionWorldSpatialLineTraceResult traceMissionWorldSpatialLine(
    const MissionWorldSpatialArena& arena,
    const std::size_t worldRoomIndex,
    const CcfBspTreeKind treeKind,
    const CcfVector3& start,
    const CcfVector3& end,
    const MissionWorldSpatialLineTraceOptions& options) noexcept {
    if (treeKind != CcfBspTreeKind::staticTree &&
        treeKind != CcfBspTreeKind::portalTree) {
        return {
            .status =
                MissionWorldSpatialLineTraceStatus::invalidInput,
            .hit = std::nullopt,
        };
    }
    if (!arena.complete() ||
        arena.rooms.size() >
            kMissionWorldSpatialMaximumTraceDepth *
                kMissionWorldSpatialMaximumTraceDepth) {
        return {
            .status =
                MissionWorldSpatialLineTraceStatus::invalidArena,
            .hit = std::nullopt,
        };
    }
    if (worldRoomIndex >= arena.rooms.size()) {
        return {
            .status =
                MissionWorldSpatialLineTraceStatus::invalidWorldRoom,
            .hit = std::nullopt,
        };
    }
    if (!finite(start) || !finite(end)) {
        return {
            .status =
                MissionWorldSpatialLineTraceStatus::invalidInput,
            .hit = std::nullopt,
        };
    }

    const auto& room = arena.rooms[worldRoomIndex];
    const auto firstReference =
        treeKind == CcfBspTreeKind::staticTree
        ? room.firstStaticTreeReference
        : room.firstPortalTreeReference;
    const auto referenceCount =
        treeKind == CcfBspTreeKind::staticTree
        ? room.staticTreeCount
        : room.portalTreeCount;
    if (!rangeWithin(
            firstReference,
            referenceCount,
            arena.treeReferences.size())) {
        return {
            .status =
                MissionWorldSpatialLineTraceStatus::invalidArena,
            .hit = std::nullopt,
        };
    }

    TraceState state;
    for (std::size_t localTreeIndex = 0U;
         localTreeIndex < referenceCount;
         ++localTreeIndex) {
        const auto treeIndex =
            arena.treeReferences[firstReference + localTreeIndex];
        if (treeIndex >= arena.trees.size()) {
            return {
                .status =
                    MissionWorldSpatialLineTraceStatus::invalidArena,
                .hit = std::nullopt,
            };
        }
        const auto& tree = arena.trees[treeIndex];
        if (tree.kind != treeKind ||
            tree.worldRoomIndex != worldRoomIndex) {
            return {
                .status =
                    MissionWorldSpatialLineTraceStatus::invalidArena,
                .hit = std::nullopt,
            };
        }
        const auto status = traceTree(
            arena, tree, start, end, options, state);
        if (status != MissionWorldSpatialLineTraceStatus::noHit) {
            return {.status = status, .hit = std::nullopt};
        }
    }

    if (!state.hit.has_value()) {
        return {
            .status = MissionWorldSpatialLineTraceStatus::noHit,
            .hit = std::nullopt,
        };
    }
    return {
        .status = MissionWorldSpatialLineTraceStatus::hit,
        .hit = state.hit,
    };
}

MissionWorldPortalTraceResult traceMissionWorldPortalTransition(
    const MissionWorldSpatialArena& arena,
    const std::size_t worldRoomIndex,
    const CcfVector3& start,
    const CcfVector3& end,
    const MissionWorldPortalTraceOptions& options) noexcept {
    if (options.maximumTransitions >
        kMissionWorldSpatialMaximumPortalTransitions) {
        return {
            .status = MissionWorldPortalTraceStatus::invalidInput,
            .hit = std::nullopt,
            .targetWorldRoomIndex = std::nullopt,
            .transitionCount = 0U,
        };
    }
    auto currentRoom = worldRoomIndex;
    auto currentStart = start;
    std::optional<MissionWorldSpatialLineHit> lastHit;
    std::size_t transitionCount = 0U;
    const auto finish = [&lastHit, &transitionCount](
                            const MissionWorldPortalTraceStatus status) {
        return MissionWorldPortalTraceResult{
            .status = status,
            .hit = lastHit,
            .targetWorldRoomIndex = std::nullopt,
            .transitionCount = transitionCount,
        };
    };
    const auto completeTransition =
        [&lastHit,
         &transitionCount,
         &currentRoom,
         worldRoomIndex]() {
            if (currentRoom == worldRoomIndex) {
                return MissionWorldPortalTraceResult{
                    .status =
                        MissionWorldPortalTraceStatus::noTransition,
                    .hit = lastHit,
                    .targetWorldRoomIndex = std::nullopt,
                    .transitionCount = transitionCount,
                };
            }
            return MissionWorldPortalTraceResult{
                .status = MissionWorldPortalTraceStatus::transition,
                .hit = lastHit,
                .targetWorldRoomIndex = currentRoom,
                .transitionCount = transitionCount,
            };
        };

    while (true) {
        const auto line = traceMissionWorldSpatialLine(
            arena,
            currentRoom,
            CcfBspTreeKind::portalTree,
            currentStart,
            end);
        if (line.hit.has_value()) {
            lastHit = line.hit;
        }
        switch (line.status) {
        case MissionWorldSpatialLineTraceStatus::noHit:
            if (transitionCount == 0U) {
                return finish(
                    MissionWorldPortalTraceStatus::noTransition);
            }
            return completeTransition();
        case MissionWorldSpatialLineTraceStatus::invalidArena:
            return finish(MissionWorldPortalTraceStatus::invalidArena);
        case MissionWorldSpatialLineTraceStatus::invalidWorldRoom:
            return finish(
                MissionWorldPortalTraceStatus::invalidWorldRoom);
        case MissionWorldSpatialLineTraceStatus::invalidInput:
            return finish(MissionWorldPortalTraceStatus::invalidInput);
        case MissionWorldSpatialLineTraceStatus::
            traversalDepthExceeded:
            return finish(
                MissionWorldPortalTraceStatus::traversalDepthExceeded);
        case MissionWorldSpatialLineTraceStatus::hit:
            break;
        }

        if (!line.hit.has_value() ||
            line.hit->polygonIndex >= arena.polygons.size()) {
            return finish(MissionWorldPortalTraceStatus::invalidArena);
        }
        const auto& polygon =
            arena.polygons[line.hit->polygonIndex];
        if (!polygon.portalWorldRoomIndex.has_value() ||
            *polygon.portalWorldRoomIndex >= arena.rooms.size()) {
            return finish(MissionWorldPortalTraceStatus::invalidArena);
        }
        if (!polygon.portalMeshSelectionFlagB ||
            polygon.portalType != 0U ||
            !polygon.portalObjectVisible) {
            if (transitionCount == 0U) {
                return finish(
                    MissionWorldPortalTraceStatus::noTransition);
            }
            return completeTransition();
        }
        if (transitionCount >= options.maximumTransitions) {
            return finish(
                MissionWorldPortalTraceStatus::
                    transitionLimitExceeded);
        }

        currentStart = line.hit->point;
        currentRoom = *polygon.portalWorldRoomIndex;
        ++transitionCount;
    }
}

} // namespace airfix::assets
