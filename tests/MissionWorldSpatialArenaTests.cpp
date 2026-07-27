#include "airfix/assets/MissionWorldSpatialArena.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace airfix::assets;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] CcfPlacedNodeMetadata object(
    const std::uint32_t currentReference,
    const std::uint32_t roomReference,
    const std::uint32_t meshReference,
    const std::uint32_t portalRoomReference = 0U) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "object",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .data = CcfPlacedObjectMetadata{
            .meshReference = meshReference,
            .rawFlag = 1U,
            .portalType = 0U,
            .portalRoomReference = portalRoomReference,
        },
    };
}

[[nodiscard]] CcfBspPolygonMetadata polygon(
    const std::uint32_t placedReference,
    const float marker) {
    return {
        .faceCross = {0.0F, 0.0F, 1.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {marker, 0.0F, 0.0F},
        .edge01 = {1.0F, 0.0F, 0.0F},
        .edge12 = {-1.0F, 1.0F, 0.0F},
        .polygonIndex = 0U,
        .placedObjectReference = placedReference,
    };
}

[[nodiscard]] CcfBspTreeMetadata tree(
    const CcfBspTreeKind kind,
    const std::uint32_t placedReference,
    const float firstMarker,
    const float secondMarker) {
    CcfBspTreeMetadata result{
        .kind = kind,
        .source = CcfBspTreeSource::wrapped,
        .rootNodeIndex = 0U,
    };
    result.nodes.push_back({
        .splitNormal = {1.0F, 0.0F, 0.0F},
        .pointOnPlane = {firstMarker, 0.0F, 0.0F},
        .polygonIndices = {0U, 1U},
    });
    result.polygons = {
        polygon(placedReference, firstMarker),
        polygon(placedReference, secondMarker),
    };
    return result;
}

[[nodiscard]] CcfMetadata source() {
    CcfMetadata ccf;
    ccf.roomSections.push_back({
        .firstPhysicalRoomIndex = 0U,
        .physicalRoomCount = 2U,
        .firstDirectChildIsRoom = true,
    });
    ccf.rooms = {
        {
            .name = "",
            .reference = 10U,
            .primaryBinding = true,
        },
        {
            .name = "next",
            .reference = 20U,
        },
    };
    ccf.meshes.push_back({
        .name = "mesh",
        .reference = 30U,
        .selectionFlagB = 1U,
        .triangles = {CcfMeshTriangleMetadata{}},
    });
    ccf.placedNodes = {
        object(40U, 10U, 30U, 20U),
    };
    ccf.rooms[0].staticBspTrees = {
        tree(CcfBspTreeKind::staticTree, 40U, 1.0F, 2.0F),
        tree(CcfBspTreeKind::staticTree, 40U, 3.0F, 4.0F),
    };
    ccf.rooms[0].portalBspTrees = {
        tree(CcfBspTreeKind::portalTree, 40U, 5.0F, 6.0F),
    };
    return ccf;
}

[[nodiscard]] MissionWorldRoomCatalog catalogFor(
    const std::vector<MissionCcfRoomLoadSource>& sources) {
    return buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = sources,
    });
}

void testArenaPreservesBindingsAndLegacyPrependOrder() {
    auto ccf = source();
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    const auto catalog = catalogFor(sources);
    const auto arena =
        buildMissionWorldSpatialArena(sources, catalog);

    require(arena.complete(), "valid spatial arena was rejected");
    require(
        arena.rooms.size() == catalog.rooms.size() &&
            arena.trees.size() == 3U &&
            arena.nodes.size() == 3U &&
            arena.polygons.size() == 6U &&
            arena.treeReferences.size() == 3U,
        "spatial arena counts are wrong");

    const auto& root = arena.rooms[0];
    require(
        root.staticTreeCount == 2U &&
            root.portalTreeCount == 1U &&
            arena.treeReferences[root.firstStaticTreeReference] == 1U &&
            arena.treeReferences[root.firstStaticTreeReference + 1U] == 0U,
        "tree references did not reproduce legacy prepend order");
    require(
        arena.polygons[0].point0[0] == 2.0F &&
            arena.polygons[1].point0[0] == 1.0F &&
            arena.polygons[2].point0[0] == 4.0F &&
            arena.polygons[3].point0[0] == 3.0F,
        "node polygons did not reproduce legacy prepend order");

    const auto portalTreeReference =
        arena.treeReferences[root.firstPortalTreeReference];
    const auto& portalTree = arena.trees[portalTreeReference];
    const auto& portalPolygon =
        arena.polygons[arena.nodes[portalTree.rootNodeIndex]
                           .firstPolygonIndex];
    require(
        portalPolygon.portalWorldRoomIndex.has_value() &&
            *portalPolygon.portalWorldRoomIndex == 1U &&
            portalPolygon.portalMeshSelectionFlagB &&
            portalPolygon.portalType == 0U &&
            portalPolygon.portalObjectVisible,
        "portal physical room was not translated to runtime room");
    require(
        arena.retainedPayloadBytes ==
            arena.rooms.size() * sizeof(MissionWorldSpatialRoom) +
                arena.treeReferences.size() * sizeof(std::size_t) +
                arena.trees.size() * sizeof(MissionWorldSpatialTree) +
                arena.nodes.size() * sizeof(MissionWorldSpatialNode) +
                arena.polygons.size() *
                    sizeof(MissionWorldSpatialPolygon),
        "retained payload byte accounting is not exact");
}

void testPlacedSceneSuppressionDoesNotCreateGhostTrees() {
    auto ccf = source();
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf, .placedSceneEnabled = false},
    };
    const auto catalog = catalogFor(sources);
    const auto arena =
        buildMissionWorldSpatialArena(sources, catalog);

    require(
        arena.complete() && arena.rooms.size() == 2U &&
            arena.trees.empty() && arena.nodes.empty() &&
            arena.polygons.empty(),
        "placed-scene suppression published ghost collision data");
}

void testDisabledSourceDoesNotConsumePhysicalRoomBudget() {
    auto ccf = source();
    const std::vector<MissionCcfRoomLoadSource> sources{
        {
            .ccf = &ccf,
            .roomSectionEnabled = false,
            .placedSceneEnabled = false,
        },
    };
    auto limits = MissionWorldSpatialArenaLimits{};
    limits.maximumPhysicalRooms = 0U;
    const auto arena = buildMissionWorldSpatialArena(
        sources, catalogFor(sources), limits);

    require(
        arena.complete() && arena.rooms.size() == 1U &&
            arena.treeReferences.empty() && arena.trees.empty() &&
            arena.nodes.empty() && arena.polygons.empty(),
        "disabled source consumed physical-room budget or retained a map");
}

void testAggregatePhysicalRoomLimitIsExact() {
    auto ccf = source();
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
        {.ccf = &ccf},
    };
    auto limits = MissionWorldSpatialArenaLimits{};
    limits.maximumPhysicalRooms = 3U;
    const auto arena = buildMissionWorldSpatialArena(
        sources, catalogFor(sources), limits);

    require(
        arena.issues.size() == 1U &&
            arena.issues.front().kind ==
                MissionWorldSpatialArenaIssueKind::limitExceeded &&
            arena.issues.front().sourceIndex ==
                std::optional<std::size_t>{1U} &&
            arena.rooms.empty() && arena.treeReferences.empty() &&
            arena.trees.empty() && arena.nodes.empty() &&
            arena.polygons.empty(),
        "four active physical rooms passed a three-room aggregate limit");
}

void testForgedContributorCatalogIsRejected() {
    auto ccf = source();
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    auto forgedCatalog = catalogFor(sources);
    require(
        forgedCatalog.complete() && forgedCatalog.rooms.size() == 2U &&
            forgedCatalog.rooms[0].contributors.size() == 1U &&
            forgedCatalog.rooms[1].contributors.size() == 1U,
        "catalog fixture cannot exercise contributor authentication");
    std::swap(
        forgedCatalog.rooms[0].contributors,
        forgedCatalog.rooms[1].contributors);
    require(
        forgedCatalog.complete(),
        "forged catalog no longer satisfies the shallow complete contract");

    const auto arena =
        buildMissionWorldSpatialArena(sources, forgedCatalog);
    require(
        arena.issues.size() == 1U &&
            arena.issues.front().kind ==
                MissionWorldSpatialArenaIssueKind::catalogMismatch &&
            arena.rooms.empty() && arena.treeReferences.empty() &&
            arena.trees.empty() && arena.nodes.empty() &&
            arena.polygons.empty(),
        "swapped complete contributor catalog was accepted");
}

void testLegacyFaceNormalSentinelIsBitPreserved() {
    auto ccf = source();
    const auto sentinel = std::bit_cast<float>(0xFFC00000U);
    ccf.rooms[0].staticBspTrees[0].polygons[0].faceNormal = {
        sentinel,
        sentinel,
        sentinel,
    };
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    const auto arena =
        buildMissionWorldSpatialArena(sources, catalogFor(sources));

    require(arena.complete(), "legacy face-normal sentinel was rejected");
    const auto& preserved = arena.polygons[1].faceNormal;
    require(
        std::bit_cast<std::uint32_t>(preserved[0]) == 0xFFC00000U &&
            std::bit_cast<std::uint32_t>(preserved[1]) == 0xFFC00000U &&
            std::bit_cast<std::uint32_t>(preserved[2]) == 0xFFC00000U,
        "legacy face-normal sentinel bits changed");
}

void testFailuresAreAtomicAndTyped() {
    auto ccf = source();
    const std::vector<MissionCcfRoomLoadSource> unsupported{
        {
            .ccf = &ccf,
            .roomSectionEnabled = false,
            .placedSceneEnabled = true,
        },
    };
    auto forgedCatalog = MissionWorldRoomCatalog{
        .sourceCount = 1U,
        .sourcePhysicalRoomCounts = {2U},
        .rooms = {MissionRuntimeRoom{}},
    };
    auto arena =
        buildMissionWorldSpatialArena(unsupported, forgedCatalog);
    require(
        arena.issues.size() == 1U &&
            arena.issues.front().kind ==
                MissionWorldSpatialArenaIssueKind::
                    unsupportedSourceFlags &&
            arena.rooms.empty() && arena.trees.empty(),
        "unsupported load shape did not fail atomically");

    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    auto limits = MissionWorldSpatialArenaLimits{};
    limits.maximumPolygons = 1U;
    arena = buildMissionWorldSpatialArena(
        sources, catalogFor(sources), limits);
    require(
        arena.issues.size() == 1U &&
            arena.issues.front().kind ==
                MissionWorldSpatialArenaIssueKind::limitExceeded &&
            arena.rooms.empty() && arena.polygons.empty(),
        "polygon limit failure leaked a partial arena");

    limits = {};
    limits.maximumRetainedBytes = 1U;
    arena = buildMissionWorldSpatialArena(
        sources, catalogFor(sources), limits);
    require(
        arena.issues.size() == 1U &&
            arena.issues.front().kind ==
                MissionWorldSpatialArenaIssueKind::
                    retainedByteLimitExceeded &&
            arena.rooms.empty() && arena.retainedPayloadBytes == 0U,
        "retained byte failure leaked a partial arena");
}

void testRetainedByteOneUnderFailsAtomically() {
    auto ccf = source();
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    const auto catalog = catalogFor(sources);
    const auto exact = buildMissionWorldSpatialArena(sources, catalog);
    require(
        exact.complete() && exact.retainedPayloadBytes != 0U,
        "valid arena did not provide an exact retained-byte fixture");

    auto limits = MissionWorldSpatialArenaLimits{};
    limits.maximumRetainedBytes = exact.retainedPayloadBytes - 1U;
    const auto arena =
        buildMissionWorldSpatialArena(sources, catalog, limits);
    require(
        arena.issues.size() == 1U &&
            arena.issues.front().kind ==
                MissionWorldSpatialArenaIssueKind::
                    retainedByteLimitExceeded &&
            arena.retainedPayloadBytes == 0U &&
            arena.rooms.empty() && arena.rooms.capacity() == 0U &&
            arena.treeReferences.empty() &&
            arena.treeReferences.capacity() == 0U &&
            arena.trees.empty() && arena.trees.capacity() == 0U &&
            arena.nodes.empty() && arena.nodes.capacity() == 0U &&
            arena.polygons.empty() && arena.polygons.capacity() == 0U,
        "one-under retained-byte failure retained a partial allocation");
}

} // namespace

int main() {
    try {
        testArenaPreservesBindingsAndLegacyPrependOrder();
        testPlacedSceneSuppressionDoesNotCreateGhostTrees();
        testDisabledSourceDoesNotConsumePhysicalRoomBudget();
        testAggregatePhysicalRoomLimitIsExact();
        testForgedContributorCatalogIsRejected();
        testLegacyFaceNormalSentinelIsBitPreserved();
        testFailuresAreAtomicAndTyped();
        testRetainedByteOneUnderFailsAtomically();
        std::cout << "Mission world spatial arena tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Mission world spatial arena tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
