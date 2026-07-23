#include "airfix/assets/CcfPlacedScene.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using airfix::assets::CcfMetadata;
using airfix::assets::CcfPlacedNodeKind;
using airfix::assets::CcfPlacedNodeMetadata;
using airfix::assets::PlacedParentTarget;
using airfix::assets::PlacedParentTargetKind;
using airfix::assets::PlacedPortalRoomTargetKind;
using airfix::assets::PlacedRoomTargetKind;
using airfix::assets::PlacedSceneIssue;
using airfix::assets::PlacedSceneIssueKind;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] airfix::assets::CcfRoomMetadata room(
    const std::uint32_t reference) {
    return {
        .name = "room",
        .prefix = "",
        .reference = reference,
    };
}

[[nodiscard]] airfix::assets::CcfMeshMetadata mesh(
    const std::uint32_t reference) {
    return {
        .name = "mesh",
        .prefix = "",
        .reference = reference,
    };
}

[[nodiscard]] CcfPlacedNodeMetadata nullNode(
    const std::uint32_t currentReference,
    const std::uint32_t parentReference,
    const std::uint32_t roomReference = 0U) {
    return {
        .kind = CcfPlacedNodeKind::nullNode,
        .name = "null",
        .prefix = "",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .parentReference = parentReference,
        .data = airfix::assets::CcfPlacedNullMetadata{},
    };
}

[[nodiscard]] CcfPlacedNodeMetadata light(
    const std::uint32_t currentReference,
    const std::uint32_t parentReference,
    const std::uint32_t roomReference = 0U) {
    return {
        .kind = CcfPlacedNodeKind::light,
        .name = "light",
        .prefix = "",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .parentReference = parentReference,
        .data = airfix::assets::CcfPlacedLightMetadata{},
    };
}

[[nodiscard]] CcfPlacedNodeMetadata object(
    const std::uint32_t currentReference,
    const std::uint32_t parentReference,
    const std::uint32_t roomReference,
    const std::uint32_t meshReference,
    const std::uint32_t portalRoomReference = 0U) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "object",
        .prefix = "",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .parentReference = parentReference,
        .data = airfix::assets::CcfPlacedObjectMetadata{
            .meshReference = meshReference,
            .portalRoomReference = portalRoomReference,
        },
    };
}

[[nodiscard]] bool hasIssue(
    const std::vector<PlacedSceneIssue>& issues,
    const PlacedSceneIssueKind kind) {
    return std::ranges::any_of(
        issues, [kind](const auto& issue) { return issue.kind == kind; });
}

[[nodiscard]] std::size_t countIssue(
    const std::vector<PlacedSceneIssue>& issues,
    const PlacedSceneIssueKind kind,
    const std::uint32_t reference) {
    return static_cast<std::size_t>(std::ranges::count_if(
        issues,
        [kind, reference](const auto& issue) {
            return issue.kind == kind && issue.reference == reference;
        }));
}

void testStableGraphAndResourceResolution() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U), room(11U)};
    ccf.meshes = {mesh(100U), mesh(101U), mesh(500U)};
    // Children may precede their placed parent. A mesh prototype may also own
    // placed children, without an indexing assumption between CCF sections.
    ccf.placedNodes = {
        object(2U, 1U, 10U, 100U, 11U),
        nullNode(1U, 0U, 999U),
        light(3U, 1U, 10U),
        object(4U, 500U, 10U, 101U),
        light(5U, 500U, 11U),
    };

    const auto scene = airfix::assets::resolvePlacedScene(ccf);
    require(scene.issues.empty(), "valid placed scene was rejected");
    require(scene.nodes.size() == ccf.placedNodes.size(),
        "resolved nodes are not parallel to placed metadata");
    require(scene.rootIndices == std::vector<std::size_t>{1U},
        "placed root order mismatch");
    require(scene.nodes[1].childIndices == std::vector<std::size_t>{0U, 2U},
        "placed child physical order mismatch");
    require(scene.nodes[0].parentTarget ==
            std::optional<PlacedParentTarget>{{
                .kind = PlacedParentTargetKind::placedNode,
                .index = 1U,
            }},
        "placed parent resolution mismatch");
    require(scene.nodes[3].parentTarget ==
            std::optional<PlacedParentTarget>{{
                .kind = PlacedParentTargetKind::meshPrototype,
                .index = 2U,
            }},
        "mesh prototype parent resolution mismatch");
    require(scene.meshChildIndices.size() == ccf.meshes.size() &&
            scene.meshChildIndices[2] == std::vector<std::size_t>{3U, 4U},
        "mesh prototype child physical order mismatch");

    const auto& objectNode = scene.nodes[0];
    require(objectNode.instantiated &&
            objectNode.roomTarget.kind == PlacedRoomTargetKind::parsedRoom &&
            objectNode.roomTarget.roomIndex == 0U &&
            objectNode.meshTarget.meshIndex == 0U &&
            objectNode.portalRoomTarget.kind ==
                PlacedPortalRoomTargetKind::parsedRoom &&
            objectNode.portalRoomTarget.roomIndex == 1U,
        "object resources were not resolved");
    require(scene.nodes[1].roomTarget.kind ==
            PlacedRoomTargetKind::externalReceiverFallback,
        "missing ordinary room did not select external receiver fallback");
    require(scene.nodes[3].portalRoomTarget.kind ==
            PlacedPortalRoomTargetKind::notSet,
        "missing portal room did not remain unset");
}

void testZeroReferencesUseNormalLookup() {
    CcfMetadata ccf;
    ccf.rooms = {room(0U)};
    ccf.meshes = {mesh(0U), mesh(99U)};
    ccf.placedNodes = {
        object(1U, 0U, 0U, 0U, 0U),
        nullNode(2U, 99U, 0U),
    };

    const auto scene = airfix::assets::resolvePlacedScene(ccf);
    require(scene.issues.empty(), "valid zero resource reference was rejected");
    require(scene.nodes[0].roomTarget.roomIndex == 0U &&
            scene.nodes[0].meshTarget.meshIndex == 0U &&
            scene.nodes[0].portalRoomTarget.roomIndex == 0U,
        "zero resource references bypassed normal lookup");
    require(scene.nodes[1].parentTarget ==
            std::optional<PlacedParentTarget>{{
                .kind = PlacedParentTargetKind::meshPrototype,
                .index = 1U,
            }},
        "mesh prototype parent was not resolved");

    CcfMetadata zeroCurrent;
    zeroCurrent.meshes = {mesh(99U)};
    zeroCurrent.placedNodes = {nullNode(0U, 99U)};
    const auto zeroCurrentScene =
        airfix::assets::resolvePlacedScene(zeroCurrent);
    require(zeroCurrentScene.issues.empty() &&
            zeroCurrentScene.nodes[0].parentTarget.has_value(),
        "zero current SRT reference was treated as invalid");
}

void testOnlyMeshesArePrototypeParents() {
    CcfMetadata ccf;
    ccf.blueprints = {{
        .kind = airfix::assets::CcfBlueprintKind::mesh,
        .name = "synthetic blueprint without a loaded mesh",
        .reference = 77U,
    }};
    ccf.placedNodes = {nullNode(1U, 77U)};
    const auto scene = airfix::assets::resolvePlacedScene(ccf);
    require(hasIssue(scene.issues, PlacedSceneIssueKind::missingParent) &&
            !scene.nodes[0].parentTarget.has_value(),
        "non-mesh blueprint was exposed as a loaded SRT parent");
}

void testCrossDomainAmbiguityFailsClosed() {
    CcfMetadata parentCollision;
    parentCollision.meshes = {mesh(10U)};
    parentCollision.placedNodes = {
        nullNode(10U, 0U),
        nullNode(20U, 10U),
    };
    const auto ambiguousParent =
        airfix::assets::resolvePlacedScene(parentCollision);
    require(countIssue(
                ambiguousParent.issues,
                PlacedSceneIssueKind::duplicateSrtReference,
                10U) == 1U,
        "cross-domain SRT duplicate was not reported exactly once");
    require(hasIssue(
                ambiguousParent.issues,
                PlacedSceneIssueKind::ambiguousParentReference) &&
            !ambiguousParent.nodes[1].parentTarget.has_value(),
        "ambiguous cross-domain parent was guessed");

    CcfMetadata childCollision;
    childCollision.meshes = {mesh(10U), mesh(30U)};
    childCollision.placedNodes = {nullNode(10U, 30U)};
    const auto ambiguousChild =
        airfix::assets::resolvePlacedScene(childCollision);
    require(hasIssue(
                ambiguousChild.issues,
                PlacedSceneIssueKind::ambiguousChildReference) &&
            !ambiguousChild.nodes[0].parentTarget.has_value() &&
            ambiguousChild.meshChildIndices[1].empty(),
        "ambiguous child lookup created a parent edge");
}

void testMissingSelfAndCycle() {
    CcfMetadata missing;
    missing.placedNodes = {nullNode(1U, 77U)};
    require(hasIssue(
        airfix::assets::resolvePlacedScene(missing).issues,
        PlacedSceneIssueKind::missingParent),
        "missing parent was accepted");

    CcfMetadata self;
    self.placedNodes = {nullNode(1U, 1U)};
    require(hasIssue(
        airfix::assets::resolvePlacedScene(self).issues,
        PlacedSceneIssueKind::selfParent),
        "self parent was accepted");

    CcfMetadata cycle;
    cycle.placedNodes = {nullNode(1U, 2U), nullNode(2U, 1U)};
    const auto cycleScene = airfix::assets::resolvePlacedScene(cycle);
    require(hasIssue(cycleScene.issues, PlacedSceneIssueKind::cycle),
        "placed-node cycle was accepted");
}

void testAmbiguousAndMissingResources() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U), room(10U)};
    ccf.meshes = {mesh(20U), mesh(20U)};
    ccf.placedNodes = {
        object(1U, 0U, 10U, 20U, 10U),
        object(2U, 0U, 99U, 99U, 99U),
    };
    const auto scene = airfix::assets::resolvePlacedScene(ccf);

    require(hasIssue(
                scene.issues,
                PlacedSceneIssueKind::duplicateRoomReference) &&
            hasIssue(
                scene.issues,
                PlacedSceneIssueKind::ambiguousRoomReference) &&
            hasIssue(
                scene.issues,
                PlacedSceneIssueKind::ambiguousPortalRoomReference),
        "ambiguous room resources were not rejected");
    require(hasIssue(
                scene.issues,
                PlacedSceneIssueKind::duplicateMeshReference) &&
            hasIssue(
                scene.issues,
                PlacedSceneIssueKind::ambiguousMeshReference) &&
            hasIssue(scene.issues, PlacedSceneIssueKind::missingMesh),
        "ambiguous or missing mesh was not rejected");
    require(!scene.nodes[0].instantiated &&
            !scene.nodes[1].instantiated,
        "object without a unique mesh was instantiated");
    require(scene.nodes[0].roomTarget.kind ==
                PlacedRoomTargetKind::unresolvedAmbiguous &&
            scene.nodes[0].portalRoomTarget.kind ==
                PlacedPortalRoomTargetKind::unresolvedAmbiguous,
        "ambiguous room target retained a guessed index");
    require(scene.nodes[1].roomTarget.kind ==
                PlacedRoomTargetKind::externalReceiverFallback &&
            scene.nodes[1].portalRoomTarget.kind ==
                PlacedPortalRoomTargetKind::notSet,
        "missing room fallback or unset portal status mismatch");

    CcfMetadata skippedObject;
    skippedObject.placedNodes = {
        object(10U, 0U, 0U, 77U),
        nullNode(20U, 10U),
    };
    const auto skipped = airfix::assets::resolvePlacedScene(skippedObject);
    require(!skipped.nodes[0].instantiated &&
            skipped.rootIndices.empty() &&
            hasIssue(skipped.issues, PlacedSceneIssueKind::missingParent) &&
            !skipped.nodes[1].parentTarget.has_value(),
        "object skipped for a missing mesh remained an active SRT parent");

    CcfMetadata invalidVariant;
    invalidVariant.placedNodes = {object(1U, 0U, 0U, 1U)};
    invalidVariant.placedNodes[0].data =
        airfix::assets::CcfPlacedNullMetadata{};
    const auto invalid = airfix::assets::resolvePlacedScene(invalidVariant);
    require(hasIssue(invalid.issues, PlacedSceneIssueKind::invalidNodeData) &&
            !invalid.nodes[0].instantiated,
        "inconsistent object variant was accepted");
}

void testLimits() {
    CcfMetadata one;
    one.placedNodes = {nullNode(1U, 0U)};
    auto limits = airfix::assets::PlacedSceneLimits{};
    limits.maximumPlacedNodes = 0U;
    const auto nodeLimited = airfix::assets::resolvePlacedScene(one, limits);
    require(nodeLimited.nodes.empty() &&
            hasIssue(
                nodeLimited.issues, PlacedSceneIssueKind::limitExceeded),
        "placed-node count limit was not checked before allocation");

    CcfMetadata resources;
    resources.rooms = {room(1U)};
    resources.meshes = {mesh(2U)};
    limits = {};
    limits.maximumRooms = 0U;
    require(hasIssue(
        airfix::assets::resolvePlacedScene(resources, limits).issues,
        PlacedSceneIssueKind::limitExceeded),
        "room count limit was not enforced");
    limits = {};
    limits.maximumMeshes = 0U;
    require(hasIssue(
        airfix::assets::resolvePlacedScene(resources, limits).issues,
        PlacedSceneIssueKind::limitExceeded),
        "mesh count limit was not enforced");

    CcfMetadata edge;
    edge.placedNodes = {nullNode(1U, 0U), nullNode(2U, 1U)};
    limits = {};
    limits.maximumEdges = 0U;
    const auto edgeLimited = airfix::assets::resolvePlacedScene(edge, limits);
    require(hasIssue(
                edgeLimited.issues, PlacedSceneIssueKind::limitExceeded) &&
            !edgeLimited.nodes[1].parentTarget.has_value() &&
            edgeLimited.nodes[0].childIndices.empty(),
        "edge limit was not enforced before graph growth");

    CcfMetadata depth;
    depth.placedNodes = {
        nullNode(1U, 0U),
        nullNode(2U, 1U),
        nullNode(3U, 2U),
    };
    limits = {};
    limits.maximumDepth = 1U;
    require(hasIssue(
        airfix::assets::resolvePlacedScene(depth, limits).issues,
        PlacedSceneIssueKind::limitExceeded),
        "placed graph depth limit was not enforced");

    CcfMetadata meshDepth;
    meshDepth.meshes = {mesh(50U)};
    meshDepth.placedNodes = {nullNode(1U, 50U)};
    limits = {};
    limits.maximumDepth = 0U;
    require(hasIssue(
        airfix::assets::resolvePlacedScene(meshDepth, limits).issues,
        PlacedSceneIssueKind::limitExceeded),
        "edge to a mesh prototype was omitted from depth accounting");
}

void testDeepGraphIsIterative() {
    CcfMetadata ccf;
    constexpr std::size_t count = 20'000U;
    ccf.placedNodes.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto reference = static_cast<std::uint32_t>(index + 1U);
        ccf.placedNodes.push_back(
            nullNode(reference, index == 0U ? 0U : reference - 1U));
    }

    auto limits = airfix::assets::PlacedSceneLimits{};
    limits.maximumDepth = count;
    const auto scene = airfix::assets::resolvePlacedScene(ccf, limits);
    require(scene.issues.empty() && scene.nodes.size() == count,
        "deep iterative placed graph resolution failed");
    require(scene.rootIndices == std::vector<std::size_t>{0U} &&
            scene.nodes[count - 2U].childIndices ==
                std::vector<std::size_t>{count - 1U},
        "deep placed graph tail mismatch");

    limits.maximumDepth = 1'000U;
    require(hasIssue(
        airfix::assets::resolvePlacedScene(ccf, limits).issues,
        PlacedSceneIssueKind::limitExceeded),
        "deep graph depth limit was not reported");
}

} // namespace

int main() {
    try {
        testStableGraphAndResourceResolution();
        testZeroReferencesUseNormalLookup();
        testOnlyMeshesArePrototypeParents();
        testCrossDomainAmbiguityFailsClosed();
        testMissingSelfAndCycle();
        testAmbiguousAndMissingResources();
        testLimits();
        testDeepGraphIsIterative();
        std::cout << "CcfPlacedScene tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "CcfPlacedScene tests failed: " << error.what() << '\n';
        return 1;
    }
}
