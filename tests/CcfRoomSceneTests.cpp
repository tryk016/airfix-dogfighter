#include "airfix/assets/CcfRoomScene.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace airfix::assets;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] CcfRoomMetadata room(const std::uint32_t reference) {
    return {.name = "room", .reference = reference};
}

[[nodiscard]] CcfMeshMetadata mesh(
    const std::uint32_t reference,
    const std::size_t triangles = 1U) {
    auto result = CcfMeshMetadata{.name = "mesh", .reference = reference};
    result.triangles.resize(triangles);
    return result;
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
            .portalRoomReference = portalRoomReference,
        },
    };
}

[[nodiscard]] CcfPlacedNodeMetadata nullNode(
    const std::uint32_t currentReference,
    const std::uint32_t roomReference) {
    return {
        .kind = CcfPlacedNodeKind::nullNode,
        .name = "null",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .data = CcfPlacedNullMetadata{},
    };
}

[[nodiscard]] CcfBspPolygonMetadata polygon(
    const std::uint32_t placedReference,
    const std::uint32_t polygonIndex = 0U) {
    return {
        .polygonIndex = polygonIndex,
        .placedObjectReference = placedReference,
    };
}

[[nodiscard]] CcfBspTreeMetadata tree(
    const CcfBspTreeKind kind,
    std::vector<CcfBspPolygonMetadata> polygons) {
    CcfBspTreeMetadata result{
        .kind = kind,
        .source = CcfBspTreeSource::wrapped,
        .rootNodeIndex = 0U,
    };
    result.nodes.push_back(CcfBspNodeMetadata{});
    result.polygons = std::move(polygons);
    result.nodes[0].polygonIndices.resize(result.polygons.size());
    for (std::size_t index = 0U; index < result.polygons.size(); ++index) {
        result.nodes[0].polygonIndices[index] = index;
    }
    return result;
}

[[nodiscard]] bool hasIssue(
    const ResolvedCcfRoomScene& scene,
    const RoomSceneIssueKind kind) {
    return std::ranges::any_of(
        scene.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

void testStableStaticAndPortalBindings() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U), room(20U)};
    ccf.meshes = {mesh(300U, 3U), mesh(400U, 2U)};
    ccf.materials = {
        CcfMaterialMetadata{.reference = 500U},
        CcfMaterialMetadata{.reference = 600U},
    };
    ccf.meshes[0].triangles[2].materialReference = 600U;
    ccf.meshes[1].triangles[1].materialReference = 500U;
    // References deliberately do not correspond to physical indices and the
    // first polygon points forward to the second placed record.
    ccf.placedNodes = {
        object(700U, 10U, 400U),
        object(900U, 10U, 300U, 20U),
    };
    ccf.rooms[0].staticBspTrees.push_back(tree(
        CcfBspTreeKind::staticTree,
        {polygon(900U, 2U), polygon(700U, 1U)}));
    ccf.rooms[0].portalBspTrees.push_back(tree(
        CcfBspTreeKind::portalTree, {polygon(900U, 0U)}));

    const auto scene = resolveCcfRoomScene(ccf);
    require(scene.issues.empty(), "valid room scene was rejected");
    require(scene.bindings == std::vector<ResolvedBspPolygonBinding>{
            {
                .roomIndex = 0U,
                .treeKind = CcfBspTreeKind::staticTree,
                .treeIndex = 0U,
                .nodeIndex = 0U,
                .polygonMetadataIndex = 0U,
                .placedNodeIndex = 1U,
                .meshIndex = 0U,
                .triangleIndex = 2U,
                .materialIndex = 1U,
            },
            {
                .roomIndex = 0U,
                .treeKind = CcfBspTreeKind::staticTree,
                .treeIndex = 0U,
                .nodeIndex = 0U,
                .polygonMetadataIndex = 1U,
                .placedNodeIndex = 0U,
                .meshIndex = 1U,
                .triangleIndex = 1U,
                .materialIndex = 0U,
            },
            {
                .roomIndex = 0U,
                .treeKind = CcfBspTreeKind::portalTree,
                .treeIndex = 0U,
                .nodeIndex = 0U,
                .polygonMetadataIndex = 0U,
                .placedNodeIndex = 1U,
                .meshIndex = 0U,
                .triangleIndex = 0U,
                .portalRoomIndex = 1U,
            }},
        "bindings did not preserve physical order or exact targets");
}

void testMaterialBindingRequiresOneSourceLocalMatch() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U)};
    ccf.meshes = {mesh(30U)};
    ccf.meshes[0].triangles[0].materialReference = 50U;
    ccf.materials = {
        CcfMaterialMetadata{.reference = 50U},
    };
    ccf.placedNodes = {object(40U, 10U, 30U)};
    ccf.rooms[0].staticBspTrees.push_back(
        tree(CcfBspTreeKind::staticTree, {polygon(40U)}));

    auto scene = resolveCcfRoomScene(ccf);
    require(
        scene.issues.empty() && scene.bindings.size() == 1U &&
            scene.bindings[0].materialIndex ==
                std::optional<std::size_t>{0U},
        "unique triangle material did not resolve source-locally");

    ccf.materials.push_back(CcfMaterialMetadata{.reference = 50U});
    scene = resolveCcfRoomScene(ccf);
    require(
        scene.issues.empty() && scene.bindings.size() == 1U &&
            !scene.bindings[0].materialIndex.has_value(),
        "ambiguous material reference was presented as a unique binding");

    ccf.materials.clear();
    scene = resolveCcfRoomScene(ccf);
    require(
        scene.issues.empty() && scene.bindings.size() == 1U &&
            !scene.bindings[0].materialIndex.has_value(),
        "missing material pointer semantics did not remain nullable");
}

void testReferenceFailuresFailClosed() {
    CcfMetadata missing;
    missing.rooms = {room(10U)};
    missing.rooms[0].staticBspTrees.push_back(
        tree(CcfBspTreeKind::staticTree, {polygon(999U)}));
    const auto missingScene = resolveCcfRoomScene(missing);
    require(
        hasIssue(missingScene, RoomSceneIssueKind::missingPlacedObject) &&
            missingScene.bindings.empty(),
        "missing placed object did not fail closed");

    CcfMetadata ambiguous;
    ambiguous.rooms = {room(10U)};
    ambiguous.meshes = {mesh(30U)};
    ambiguous.placedNodes = {
        object(77U, 10U, 30U), object(77U, 10U, 30U)};
    ambiguous.rooms[0].staticBspTrees.push_back(
        tree(CcfBspTreeKind::staticTree, {polygon(77U)}));
    const auto ambiguousScene = resolveCcfRoomScene(ambiguous);
    require(
        hasIssue(
            ambiguousScene,
            RoomSceneIssueKind::placedSceneDependency) &&
            ambiguousScene.bindings.empty(),
        "ambiguous placed scene was traversed");

    auto inactive = ambiguous;
    inactive.placedNodes.resize(1U);
    inactive.placedNodes[0].data =
        CcfPlacedObjectMetadata{.meshReference = 999U};
    inactive.rooms[0].staticBspTrees[0].polygons[0] = polygon(77U);
    const auto inactiveScene = resolveCcfRoomScene(inactive);
    require(
        hasIssue(
            inactiveScene,
            RoomSceneIssueKind::placedSceneDependency) &&
            inactiveScene.bindings.empty(),
        "inactive placed object was exposed");

    CcfMetadata nonObject;
    nonObject.rooms = {room(10U)};
    nonObject.placedNodes = {nullNode(55U, 10U)};
    nonObject.rooms[0].staticBspTrees.push_back(
        tree(CcfBspTreeKind::staticTree, {polygon(55U)}));
    const auto nonObjectScene = resolveCcfRoomScene(nonObject);
    require(
        hasIssue(nonObjectScene, RoomSceneIssueKind::nonObjectPlacedNode) &&
            nonObjectScene.bindings.empty(),
        "non-object placed node was accepted as BSP geometry");
}

void testResourceAndRoomFailuresFailClosed() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U), room(20U)};
    ccf.meshes = {mesh(30U)};
    ccf.placedNodes = {object(40U, 10U, 30U, 20U)};
    ccf.rooms[0].staticBspTrees.push_back(
        tree(CcfBspTreeKind::staticTree, {polygon(40U)}));

    ccf.placedNodes[0] = object(40U, 10U, 999U, 20U);
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::placedSceneDependency),
        "unresolved mesh target was accepted");

    ccf.placedNodes[0] = object(40U, 999U, 30U, 20U);
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::unresolvedRoomTarget),
        "unresolved ordinary room target was accepted");

    ccf.placedNodes[0] = object(40U, 20U, 30U, 20U);
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::roomMismatch),
        "cross-room BSP binding was accepted");

    ccf.placedNodes[0] = object(40U, 10U, 30U, 20U);
    ccf.rooms[0].staticBspTrees[0].polygons[0].polygonIndex = 1U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::polygonIndexOutOfRange),
        "out-of-range mesh polygon was accepted");

    ccf.rooms[0].staticBspTrees.clear();
    ccf.rooms[0].portalBspTrees.push_back(
        tree(CcfBspTreeKind::portalTree, {polygon(40U)}));
    ccf.placedNodes[0] = object(40U, 10U, 30U, 999U);
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::missingPortalRoomTarget),
        "portal tree without a target room was accepted");
}

void testDependencyStructureAndLimits() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U)};
    ccf.meshes = {mesh(30U)};
    ccf.placedNodes = {object(40U, 10U, 30U)};
    ccf.rooms[0].staticBspTrees.push_back(
        tree(CcfBspTreeKind::staticTree, {polygon(40U)}));

    ccf.placedNodes[0].parentReference = 999U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::placedSceneDependency),
        "invalid placed-scene dependency was traversed");

    ccf.placedNodes[0].parentReference = 0U;
    ccf.rooms[0].staticBspTrees[0].nodes[0].childAPresenceRaw = 7U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::invalidTreeStructure),
        "inconsistent child-presence flag was accepted");

    ccf.rooms[0].staticBspTrees[0] =
        tree(CcfBspTreeKind::staticTree, {polygon(40U)});
    ccf.rooms[0].staticBspTrees[0].polygons.push_back(polygon(40U));
    require(
        hasIssue(
            resolveCcfRoomScene(ccf),
            RoomSceneIssueKind::invalidTreeStructure),
        "unowned polygon descriptor was accepted");
    auto arenaLimits = RoomSceneLimits{};
    arenaLimits.maximumBindings = 1U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf, arenaLimits),
            RoomSceneIssueKind::limitExceeded),
        "unowned polygon arena bypassed the preflight limit");

    ccf.rooms[0].staticBspTrees[0] =
        tree(CcfBspTreeKind::staticTree, {polygon(40U)});
    auto limits = RoomSceneLimits{};
    limits.maximumRooms = 0U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf, limits),
            RoomSceneIssueKind::limitExceeded),
        "room limit was not enforced");
    limits = {};
    limits.maximumTrees = 0U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf, limits),
            RoomSceneIssueKind::limitExceeded),
        "tree limit was not enforced");
    limits = {};
    limits.maximumNodes = 0U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf, limits),
            RoomSceneIssueKind::limitExceeded),
        "node limit was not enforced");
    limits = {};
    limits.maximumBindings = 0U;
    require(
        hasIssue(
            resolveCcfRoomScene(ccf, limits),
            RoomSceneIssueKind::limitExceeded),
        "binding limit was not enforced before growth");

    auto& treeMetadata = ccf.rooms[0].staticBspTrees[0];
    treeMetadata.nodes.push_back(CcfBspNodeMetadata{});
    treeMetadata.nodes[0].childAPresenceRaw = 2U;
    treeMetadata.nodes[0].childAIndex = 1U;
    limits = {};
    limits.maximumDepth = 1U;
    const auto depthLimited = resolveCcfRoomScene(ccf, limits);
    require(
        hasIssue(depthLimited, RoomSceneIssueKind::limitExceeded) &&
            depthLimited.bindings.empty(),
        "tree depth limit left partial bindings");
}

} // namespace

int main() {
    try {
        testStableStaticAndPortalBindings();
        testMaterialBindingRequiresOneSourceLocalMatch();
        testReferenceFailuresFailClosed();
        testResourceAndRoomFailuresFailClosed();
        testDependencyStructureAndLimits();
        std::cout << "CcfRoomScene tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "CcfRoomScene tests failed: " << error.what() << '\n';
        return 1;
    }
}
