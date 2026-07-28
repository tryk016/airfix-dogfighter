#include "airfix/assets/MissionWorldSpatialArena.hpp"
#include "airfix/assets/MissionWorldSpatialLineTrace.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace airfix::assets {
namespace {

using PhysicalRoomMap =
    std::vector<std::vector<std::optional<std::size_t>>>;

void clearPayload(MissionWorldSpatialArena& arena) noexcept {
    std::vector<MissionWorldSpatialRoom>{}.swap(arena.rooms);
    std::vector<std::size_t>{}.swap(arena.treeReferences);
    std::vector<MissionWorldSpatialTree>{}.swap(arena.trees);
    std::vector<MissionWorldSpatialNode>{}.swap(arena.nodes);
    std::vector<MissionWorldSpatialPolygon>{}.swap(arena.polygons);
    arena.retainedPayloadBytes = 0U;
}

void fail(
    MissionWorldSpatialArena& arena,
    const MissionWorldSpatialArenaIssueKind kind,
    const std::optional<std::size_t> sourceIndex = std::nullopt,
    const std::optional<std::size_t> physicalRoomIndex = std::nullopt,
    const std::optional<std::size_t> worldRoomIndex = std::nullopt,
    const std::optional<RoomSceneIssueKind> roomSceneIssue = std::nullopt) {
    clearPayload(arena);
    arena.issues.push_back({
        .kind = kind,
        .sourceIndex = sourceIndex,
        .physicalRoomIndex = physicalRoomIndex,
        .worldRoomIndex = worldRoomIndex,
        .roomSceneIssue = roomSceneIssue,
    });
}

[[nodiscard]] bool addWithinLimit(
    const std::size_t current,
    const std::size_t addition,
    const std::size_t limit,
    std::size_t& result) noexcept {
    if (current > limit || addition > limit - current) {
        return false;
    }
    result = current + addition;
    return true;
}

[[nodiscard]] bool finite(const CcfVector3& value) noexcept {
    return std::ranges::all_of(
        value, [](const float component) { return std::isfinite(component); });
}

[[nodiscard]] bool legacyFaceNormal(const CcfVector3& value) noexcept {
    if (finite(value)) {
        return true;
    }
    return std::ranges::all_of(value, [](const float component) {
        return std::bit_cast<std::uint32_t>(component) == 0xFFC00000U;
    });
}

[[nodiscard]] bool validSpatialValues(
    const CcfBspTreeMetadata& tree) noexcept {
    for (const auto& node : tree.nodes) {
        if (!finite(node.splitNormal) || !finite(node.pointOnPlane)) {
            return false;
        }
    }
    for (const auto& polygon : tree.polygons) {
        if (!finite(polygon.faceCross) ||
            !legacyFaceNormal(polygon.faceNormal) ||
            !finite(polygon.point0) ||
            !finite(polygon.edge01) ||
            !finite(polygon.edge12)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool checkedPayloadBytes(
    const std::size_t roomCount,
    const std::size_t treeCount,
    const std::size_t nodeCount,
    const std::size_t polygonCount,
    std::uint64_t& result) noexcept {
    result = 0U;
    const auto add = [&result](
                         const std::size_t count,
                         const std::size_t elementSize) noexcept {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
            if (count > static_cast<std::size_t>(maximum)) {
                return false;
            }
        }
        const auto count64 = static_cast<std::uint64_t>(count);
        const auto size64 = static_cast<std::uint64_t>(elementSize);
        if (size64 != 0U && count64 > maximum / size64) {
            return false;
        }
        const auto bytes = count64 * size64;
        if (bytes > maximum - result) {
            return false;
        }
        result += bytes;
        return true;
    };
    return add(roomCount, sizeof(MissionWorldSpatialRoom)) &&
        add(treeCount, sizeof(std::size_t)) &&
        add(treeCount, sizeof(MissionWorldSpatialTree)) &&
        add(nodeCount, sizeof(MissionWorldSpatialNode)) &&
        add(polygonCount, sizeof(MissionWorldSpatialPolygon));
}

[[nodiscard]] bool checkedPayloadBytes(
    const MissionWorldSpatialArena& arena,
    std::uint64_t& result) noexcept {
    if (arena.treeReferences.size() != arena.trees.size()) {
        return false;
    }
    return checkedPayloadBytes(
        arena.rooms.size(),
        arena.trees.size(),
        arena.nodes.size(),
        arena.polygons.size(),
        result);
}

[[nodiscard]] bool sameCatalog(
    const MissionWorldRoomCatalog& left,
    const MissionWorldRoomCatalog& right) {
    return left.complete() && right.complete() &&
        left.sourceCount == right.sourceCount &&
        left.sourcePhysicalRoomCounts ==
            right.sourcePhysicalRoomCounts &&
        left.initialRootName == right.initialRootName &&
        left.rooms == right.rooms;
}

[[nodiscard]] bool buildPhysicalRoomMap(
    const std::span<const MissionCcfRoomLoadSource> sources,
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldSpatialArenaLimits& limits,
    MissionWorldSpatialArena& arena,
    PhysicalRoomMap& physicalToWorld) {
    if (!catalog.complete()) {
        fail(
            arena,
            MissionWorldSpatialArenaIssueKind::catalogIncomplete);
        return false;
    }
    if (sources.size() > limits.maximumSources ||
        catalog.rooms.size() > limits.maximumWorldRooms) {
        fail(arena, MissionWorldSpatialArenaIssueKind::limitExceeded);
        return false;
    }
    if (catalog.sourceCount != sources.size() ||
        catalog.sourcePhysicalRoomCounts.size() != sources.size()) {
        fail(
            arena,
            MissionWorldSpatialArenaIssueKind::sourceCountMismatch);
        return false;
    }

    physicalToWorld.resize(sources.size());
    std::size_t physicalRoomCount = 0U;
    for (std::size_t sourceIndex = 0U; sourceIndex < sources.size();
         ++sourceIndex) {
        const auto& source = sources[sourceIndex];
        if (source.ccf == nullptr ||
            catalog.sourcePhysicalRoomCounts[sourceIndex] !=
                source.ccf->rooms.size()) {
            fail(
                arena,
                MissionWorldSpatialArenaIssueKind::invalidSourceMetadata,
                sourceIndex);
            return false;
        }
        if (!source.roomSectionEnabled && source.placedSceneEnabled) {
            fail(
                arena,
                MissionWorldSpatialArenaIssueKind::unsupportedSourceFlags,
                sourceIndex);
            return false;
        }
        if (!source.roomSectionEnabled) {
            continue;
        }
        std::size_t nextPhysicalRoomCount = 0U;
        if (!addWithinLimit(
                physicalRoomCount,
                source.ccf->rooms.size(),
                limits.maximumPhysicalRooms,
                nextPhysicalRoomCount)) {
            fail(
                arena,
                MissionWorldSpatialArenaIssueKind::limitExceeded,
                sourceIndex);
            return false;
        }
        physicalRoomCount = nextPhysicalRoomCount;
        physicalToWorld[sourceIndex].resize(source.ccf->rooms.size());
    }

    auto authenticationLimits = limits.catalogAuthentication;
    authenticationLimits.maximumSources = std::min(
        authenticationLimits.maximumSources, limits.maximumSources);
    authenticationLimits.maximumRuntimeRooms = std::min(
        authenticationLimits.maximumRuntimeRooms,
        limits.maximumWorldRooms);
    authenticationLimits.maximumContributors = std::min(
        authenticationLimits.maximumContributors,
        limits.maximumPhysicalRooms);
    const auto canonicalCatalog = buildMissionWorldRoomCatalog(
        {
            .initialRootName = catalog.initialRootName,
            .sources = sources,
        },
        authenticationLimits);
    if (!sameCatalog(canonicalCatalog, catalog)) {
        fail(
            arena,
            MissionWorldSpatialArenaIssueKind::catalogMismatch);
        return false;
    }

    for (std::size_t worldRoomIndex = 0U;
         worldRoomIndex < catalog.rooms.size();
         ++worldRoomIndex) {
        for (const auto& contributor :
             catalog.rooms[worldRoomIndex].contributors) {
            if (contributor.sourceIndex >= sources.size() ||
                contributor.physicalRoomIndex >=
                    physicalToWorld[contributor.sourceIndex].size() ||
                !sources[contributor.sourceIndex].roomSectionEnabled) {
                fail(
                    arena,
                    MissionWorldSpatialArenaIssueKind::invalidContributor,
                    contributor.sourceIndex,
                    contributor.physicalRoomIndex,
                    worldRoomIndex);
                return false;
            }
            auto& mapped =
                physicalToWorld[contributor.sourceIndex]
                               [contributor.physicalRoomIndex];
            if (mapped.has_value()) {
                fail(
                    arena,
                    MissionWorldSpatialArenaIssueKind::duplicateContributor,
                    contributor.sourceIndex,
                    contributor.physicalRoomIndex,
                    worldRoomIndex);
                return false;
            }
            mapped = worldRoomIndex;
        }
    }

    for (std::size_t sourceIndex = 0U; sourceIndex < sources.size();
         ++sourceIndex) {
        const auto& source = sources[sourceIndex];
        if (!source.roomSectionEnabled) {
            continue;
        }
        for (std::size_t physicalRoomIndex = 0U;
             physicalRoomIndex < physicalToWorld[sourceIndex].size();
             ++physicalRoomIndex) {
            if (!physicalToWorld[sourceIndex][physicalRoomIndex].has_value()) {
                fail(
                    arena,
                    MissionWorldSpatialArenaIssueKind::missingContributor,
                    sourceIndex,
                    physicalRoomIndex);
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool preflightArenaShape(
    const std::span<const MissionCcfRoomLoadSource> sources,
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldSpatialArenaLimits& limits,
    MissionWorldSpatialArena& arena) {
    std::size_t treeCount = 0U;
    std::size_t nodeCount = 0U;
    std::size_t polygonCount = 0U;
    for (std::size_t sourceIndex = 0U; sourceIndex < sources.size();
         ++sourceIndex) {
        const auto& source = sources[sourceIndex];
        if (!source.roomSectionEnabled ||
            !source.placedSceneEnabled) {
            continue;
        }
        for (const auto& room : source.ccf->rooms) {
            for (const auto* trees :
                 {&room.staticBspTrees, &room.portalBspTrees}) {
                std::size_t nextTreeCount = 0U;
                if (!addWithinLimit(
                        treeCount,
                        trees->size(),
                        limits.maximumTrees,
                        nextTreeCount)) {
                    fail(
                        arena,
                        MissionWorldSpatialArenaIssueKind::limitExceeded,
                        sourceIndex);
                    return false;
                }
                treeCount = nextTreeCount;
                for (const auto& tree : *trees) {
                    std::size_t nextNodeCount = 0U;
                    std::size_t nextPolygonCount = 0U;
                    if (!addWithinLimit(
                            nodeCount,
                            tree.nodes.size(),
                            limits.maximumNodes,
                            nextNodeCount) ||
                        !addWithinLimit(
                            polygonCount,
                            tree.polygons.size(),
                            limits.maximumPolygons,
                            nextPolygonCount)) {
                        fail(
                            arena,
                            MissionWorldSpatialArenaIssueKind::
                                limitExceeded,
                            sourceIndex);
                        return false;
                    }
                    nodeCount = nextNodeCount;
                    polygonCount = nextPolygonCount;
                }
            }
        }
    }

    std::uint64_t payloadBytes = 0U;
    if (!checkedPayloadBytes(
            catalog.rooms.size(),
            treeCount,
            nodeCount,
            polygonCount,
            payloadBytes)) {
        fail(
            arena,
            MissionWorldSpatialArenaIssueKind::integerOverflow);
        return false;
    }
    if (payloadBytes > limits.maximumRetainedBytes) {
        fail(
            arena,
            MissionWorldSpatialArenaIssueKind::
                retainedByteLimitExceeded);
        return false;
    }
    return true;
}

[[nodiscard]] bool bindingMatches(
    const ResolvedBspPolygonBinding& binding,
    const std::size_t roomIndex,
    const CcfBspTreeKind kind,
    const std::size_t treeIndex,
    const std::size_t nodeIndex,
    const std::size_t polygonIndex) noexcept {
    return binding.roomIndex == roomIndex &&
        binding.treeKind == kind &&
        binding.treeIndex == treeIndex &&
        binding.nodeIndex == nodeIndex &&
        binding.polygonMetadataIndex == polygonIndex;
}

} // namespace

MissionWorldSpatialArena buildMissionWorldSpatialArena(
    const std::span<const MissionCcfRoomLoadSource> sources,
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldSpatialArenaLimits& limits) {
    MissionWorldSpatialArena arena;
    try {
        PhysicalRoomMap physicalToWorld;
        if (!buildPhysicalRoomMap(
                sources, catalog, limits, arena, physicalToWorld)) {
            return arena;
        }
        if (!preflightArenaShape(
                sources, catalog, limits, arena)) {
            return arena;
        }

        arena.rooms.resize(catalog.rooms.size());
        std::vector<std::vector<std::size_t>> staticTrees(
            catalog.rooms.size());
        std::vector<std::vector<std::size_t>> portalTrees(
            catalog.rooms.size());

        for (std::size_t sourceIndex = 0U; sourceIndex < sources.size();
             ++sourceIndex) {
            const auto& source = sources[sourceIndex];
            if (!source.roomSectionEnabled ||
                !source.placedSceneEnabled) {
                continue;
            }

            auto roomSceneLimits = limits.roomScenePerSource;
            roomSceneLimits.maximumDepth = std::min(
                roomSceneLimits.maximumDepth,
                kMissionWorldSpatialMaximumTraceDepth);
            const auto resolved =
                resolveCcfRoomScene(*source.ccf, roomSceneLimits);
            if (!resolved.issues.empty()) {
                const auto& issue = resolved.issues.front();
                fail(
                    arena,
                    MissionWorldSpatialArenaIssueKind::roomSceneFailure,
                    sourceIndex,
                    issue.roomIndex,
                    std::nullopt,
                    issue.kind);
                return arena;
            }

            std::size_t bindingCursor = 0U;
            for (std::size_t physicalRoomIndex = 0U;
                 physicalRoomIndex < source.ccf->rooms.size();
                 ++physicalRoomIndex) {
                const auto worldRoom =
                    physicalToWorld[sourceIndex][physicalRoomIndex];
                if (!worldRoom.has_value()) {
                    fail(
                        arena,
                        MissionWorldSpatialArenaIssueKind::
                            missingContributor,
                        sourceIndex,
                        physicalRoomIndex);
                    return arena;
                }
                const auto& room = source.ccf->rooms[physicalRoomIndex];
                for (const auto kind :
                     {CcfBspTreeKind::staticTree,
                      CcfBspTreeKind::portalTree}) {
                    const auto& trees =
                        kind == CcfBspTreeKind::staticTree
                        ? room.staticBspTrees
                        : room.portalBspTrees;
                    for (std::size_t treeIndex = 0U;
                         treeIndex < trees.size();
                         ++treeIndex) {
                        const auto& tree = trees[treeIndex];
                        if (tree.kind != kind ||
                            !validSpatialValues(tree)) {
                            fail(
                                arena,
                                tree.kind != kind
                                    ? MissionWorldSpatialArenaIssueKind::
                                          invalidRoomSceneBinding
                                    : MissionWorldSpatialArenaIssueKind::
                                          invalidSpatialValue,
                                sourceIndex,
                                physicalRoomIndex,
                                *worldRoom);
                            return arena;
                        }

                        std::size_t nextTreeCount = 0U;
                        std::size_t nextNodeCount = 0U;
                        std::size_t nextPolygonCount = 0U;
                        if (!addWithinLimit(
                                arena.trees.size(),
                                1U,
                                limits.maximumTrees,
                                nextTreeCount) ||
                            !addWithinLimit(
                                arena.nodes.size(),
                                tree.nodes.size(),
                                limits.maximumNodes,
                                nextNodeCount) ||
                            !addWithinLimit(
                                arena.polygons.size(),
                                tree.polygons.size(),
                                limits.maximumPolygons,
                                nextPolygonCount)) {
                            fail(
                                arena,
                                MissionWorldSpatialArenaIssueKind::
                                    limitExceeded,
                                sourceIndex,
                                physicalRoomIndex,
                                *worldRoom);
                            return arena;
                        }

                        std::vector<
                            const ResolvedBspPolygonBinding*>
                            bindingByPolygon(tree.polygons.size(), nullptr);
                        for (std::size_t nodeIndex = 0U;
                             nodeIndex < tree.nodes.size();
                             ++nodeIndex) {
                            for (const auto polygonIndex :
                                 tree.nodes[nodeIndex].polygonIndices) {
                                if (bindingCursor >=
                                        resolved.bindings.size() ||
                                    polygonIndex >=
                                        bindingByPolygon.size() ||
                                    !bindingMatches(
                                        resolved.bindings[bindingCursor],
                                        physicalRoomIndex,
                                        kind,
                                        treeIndex,
                                        nodeIndex,
                                        polygonIndex) ||
                                    bindingByPolygon[polygonIndex] !=
                                        nullptr) {
                                    fail(
                                        arena,
                                        MissionWorldSpatialArenaIssueKind::
                                            invalidRoomSceneBinding,
                                        sourceIndex,
                                        physicalRoomIndex,
                                        *worldRoom);
                                    return arena;
                                }
                                bindingByPolygon[polygonIndex] =
                                    &resolved.bindings[bindingCursor];
                                ++bindingCursor;
                            }
                        }
                        if (std::ranges::any_of(
                                bindingByPolygon,
                                [](const auto* binding) {
                                    return binding == nullptr;
                                })) {
                            fail(
                                arena,
                                MissionWorldSpatialArenaIssueKind::
                                    invalidRoomSceneBinding,
                                sourceIndex,
                                physicalRoomIndex,
                                *worldRoom);
                            return arena;
                        }

                        const auto firstNodeIndex = arena.nodes.size();
                        const auto spatialTreeIndex = arena.trees.size();
                        arena.trees.push_back({
                            .kind = kind,
                            .sourceIndex = sourceIndex,
                            .physicalRoomIndex = physicalRoomIndex,
                            .worldRoomIndex = *worldRoom,
                            .rootNodeIndex =
                                firstNodeIndex + tree.rootNodeIndex,
                            .firstNodeIndex = firstNodeIndex,
                            .nodeCount = tree.nodes.size(),
                        });
                        auto& roomTrees =
                            kind == CcfBspTreeKind::staticTree
                            ? staticTrees[*worldRoom]
                            : portalTrees[*worldRoom];
                        roomTrees.push_back(spatialTreeIndex);

                        arena.nodes.reserve(nextNodeCount);
                        arena.polygons.reserve(nextPolygonCount);
                        for (const auto& node : tree.nodes) {
                            const auto firstPolygonIndex =
                                arena.polygons.size();
                            arena.nodes.push_back({
                                .childAIndex =
                                    node.childAIndex.has_value()
                                    ? std::optional<std::size_t>{
                                          firstNodeIndex +
                                          *node.childAIndex}
                                    : std::nullopt,
                                .childBIndex =
                                    node.childBIndex.has_value()
                                    ? std::optional<std::size_t>{
                                          firstNodeIndex +
                                          *node.childBIndex}
                                    : std::nullopt,
                                .splitNormal = node.splitNormal,
                                .pointOnPlane = node.pointOnPlane,
                                .firstPolygonIndex = firstPolygonIndex,
                                .polygonCount =
                                    node.polygonIndices.size(),
                            });
                            for (auto polygonIterator =
                                     node.polygonIndices.rbegin();
                                 polygonIterator !=
                                 node.polygonIndices.rend();
                                 ++polygonIterator) {
                                const auto polygonIndex =
                                    *polygonIterator;
                                const auto& polygon =
                                    tree.polygons[polygonIndex];
                                const auto& binding =
                                    *bindingByPolygon[polygonIndex];
                                std::optional<std::size_t>
                                    portalWorldRoomIndex;
                                std::optional<std::uint32_t>
                                    materialCollisionMode2152;
                                bool portalMeshSelectionFlagB = false;
                                std::uint32_t portalType = 0U;
                                bool portalObjectVisible = false;
                                if (binding.materialIndex.has_value()) {
                                    if (*binding.materialIndex >=
                                        source.ccf->materials.size()) {
                                        fail(
                                            arena,
                                            MissionWorldSpatialArenaIssueKind::
                                                invalidRoomSceneBinding,
                                            sourceIndex,
                                            physicalRoomIndex,
                                            *worldRoom);
                                        return arena;
                                    }
                                    materialCollisionMode2152 =
                                        source.ccf
                                            ->materials[*binding.materialIndex]
                                            .collisionMode2152;
                                }
                                if (kind ==
                                    CcfBspTreeKind::portalTree) {
                                    if (!binding.portalRoomIndex
                                             .has_value() ||
                                        *binding.portalRoomIndex >=
                                            physicalToWorld[sourceIndex]
                                                .size() ||
                                        !physicalToWorld[sourceIndex]
                                             [*binding.portalRoomIndex]
                                                 .has_value()) {
                                        fail(
                                            arena,
                                            MissionWorldSpatialArenaIssueKind::
                                                missingPortalWorldRoom,
                                            sourceIndex,
                                            physicalRoomIndex,
                                            *worldRoom);
                                        return arena;
                                    }
                                    portalWorldRoomIndex =
                                        physicalToWorld[sourceIndex]
                                                       [*binding
                                                            .portalRoomIndex];
                                    if (binding.meshIndex >=
                                            source.ccf->meshes.size() ||
                                        binding.placedNodeIndex >=
                                            source.ccf->placedNodes.size()) {
                                        fail(
                                            arena,
                                            MissionWorldSpatialArenaIssueKind::
                                                invalidRoomSceneBinding,
                                            sourceIndex,
                                            physicalRoomIndex,
                                            *worldRoom);
                                        return arena;
                                    }
                                    const auto* placedObject =
                                        std::get_if<
                                            CcfPlacedObjectMetadata>(
                                            &source.ccf
                                                 ->placedNodes
                                                      [binding
                                                           .placedNodeIndex]
                                                 .data);
                                    if (placedObject == nullptr) {
                                        fail(
                                            arena,
                                            MissionWorldSpatialArenaIssueKind::
                                                invalidRoomSceneBinding,
                                            sourceIndex,
                                            physicalRoomIndex,
                                            *worldRoom);
                                        return arena;
                                    }
                                    portalMeshSelectionFlagB =
                                        source.ccf
                                            ->meshes[binding.meshIndex]
                                            .selectionFlagB != 0U;
                                    portalType =
                                        placedObject->portalType;
                                    portalObjectVisible =
                                        placedObject->rawFlag != 0U;
                                }
                                arena.polygons.push_back({
                                    .faceCross = polygon.faceCross,
                                    .faceNormal = polygon.faceNormal,
                                    .point0 = polygon.point0,
                                    .edge01 = polygon.edge01,
                                    .edge12 = polygon.edge12,
                                    .polygonIndex =
                                        polygon.polygonIndex,
                                    .placedObjectReference =
                                        polygon.placedObjectReference,
                                    .materialCollisionMode2152 =
                                        materialCollisionMode2152,
                                    .portalWorldRoomIndex =
                                        portalWorldRoomIndex,
                                    .portalMeshSelectionFlagB =
                                        portalMeshSelectionFlagB,
                                    .portalType = portalType,
                                    .portalObjectVisible =
                                        portalObjectVisible,
                                });
                            }
                        }
                    }
                }
            }
            if (bindingCursor != resolved.bindings.size()) {
                fail(
                    arena,
                    MissionWorldSpatialArenaIssueKind::
                        invalidRoomSceneBinding,
                    sourceIndex);
                return arena;
            }
        }

        for (std::size_t worldRoomIndex = 0U;
             worldRoomIndex < arena.rooms.size();
             ++worldRoomIndex) {
            auto& room = arena.rooms[worldRoomIndex];
            auto& staticReferences = staticTrees[worldRoomIndex];
            auto& portalReferences = portalTrees[worldRoomIndex];
            std::ranges::reverse(staticReferences);
            std::ranges::reverse(portalReferences);

            room.firstStaticTreeReference =
                arena.treeReferences.size();
            room.staticTreeCount = staticReferences.size();
            arena.treeReferences.insert(
                arena.treeReferences.end(),
                staticReferences.begin(),
                staticReferences.end());
            room.firstPortalTreeReference =
                arena.treeReferences.size();
            room.portalTreeCount = portalReferences.size();
            arena.treeReferences.insert(
                arena.treeReferences.end(),
                portalReferences.begin(),
                portalReferences.end());
        }
        if (arena.treeReferences.size() > limits.maximumTrees) {
            fail(
                arena,
                MissionWorldSpatialArenaIssueKind::limitExceeded);
            return arena;
        }

        std::uint64_t payloadBytes = 0U;
        if (!checkedPayloadBytes(arena, payloadBytes)) {
            fail(
                arena,
                MissionWorldSpatialArenaIssueKind::integerOverflow);
            return arena;
        }
        if (payloadBytes > limits.maximumRetainedBytes) {
            fail(
                arena,
                MissionWorldSpatialArenaIssueKind::
                    retainedByteLimitExceeded);
            return arena;
        }
        arena.retainedPayloadBytes = payloadBytes;
        return arena;
    }
    catch (const std::bad_alloc&) {
        fail(
            arena,
            MissionWorldSpatialArenaIssueKind::allocationFailure);
        return arena;
    }
    catch (...) {
        fail(
            arena,
            MissionWorldSpatialArenaIssueKind::allocationFailure);
        return arena;
    }
}

} // namespace airfix::assets
