#include "airfix/assets/CcfBlueprintGraph.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using airfix::assets::BlueprintGraphIssueKind;
using airfix::assets::CcfBlueprintKind;
using airfix::assets::CcfBlueprintMetadata;
using airfix::assets::CcfMetadata;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] CcfBlueprintMetadata node(
    const std::uint32_t reference,
    const std::uint32_t parent,
    const CcfBlueprintKind kind = CcfBlueprintKind::nullNode,
    const std::optional<std::size_t> meshIndex = std::nullopt) {
    return {
        .kind = kind,
        .name = "node",
        .reference = reference,
        .parentReference = parent,
        .meshIndex = meshIndex,
    };
}

[[nodiscard]] bool hasIssue(
    const std::vector<airfix::assets::BlueprintGraphIssue>& issues,
    const BlueprintGraphIssueKind kind) {
    return std::ranges::any_of(
        issues, [kind](const auto& issue) { return issue.kind == kind; });
}

void testStableGraphAndPreorder() {
    CcfMetadata ccf;
    ccf.meshes.resize(2U);
    // A child may occur before its parent. Children still retain physical
    // order, so the root preorder is root, child 20 subtree, child 30.
    ccf.blueprints = {
        node(20U, 10U),
        node(21U, 20U, CcfBlueprintKind::mesh, 0U),
        node(10U, 0U),
        node(30U, 10U, CcfBlueprintKind::mesh, 1U),
        node(99U, 0U),
    };
    const auto graph = airfix::assets::resolveBlueprintGraph(ccf);
    require(graph.issues.empty(), "valid blueprint graph was rejected");
    require(graph.rootIndices == std::vector<std::size_t>{2U, 4U},
        "root order mismatch");
    require(graph.nodes[2].childIndices == std::vector<std::size_t>{0U, 3U},
        "physical child order mismatch");
    require(graph.nodes[0].parentIndex == 2U && graph.nodes[1].parentIndex == 0U,
        "parent resolution mismatch");

    const auto subtree = airfix::assets::resolveBlueprintSubtree(ccf, 2U);
    require(subtree.issues.empty(), "valid subtree was rejected");
    require(subtree.blueprintIndices == std::vector<std::size_t>{2U, 0U, 1U, 3U},
        "stable subtree preorder mismatch");
}

void testMalformedReferences() {
    CcfMetadata zero;
    zero.blueprints = {node(0U, 0U)};
    require(hasIssue(
        airfix::assets::resolveBlueprintGraph(zero).issues,
        BlueprintGraphIssueKind::invalidReference),
        "zero blueprint reference was accepted");

    CcfMetadata duplicate;
    duplicate.blueprints = {node(1U, 0U), node(1U, 0U)};
    require(hasIssue(
        airfix::assets::resolveBlueprintGraph(duplicate).issues,
        BlueprintGraphIssueKind::duplicateReference),
        "duplicate reference was accepted");

    CcfMetadata missing;
    missing.blueprints = {node(1U, 77U)};
    require(hasIssue(
        airfix::assets::resolveBlueprintGraph(missing).issues,
        BlueprintGraphIssueKind::missingParent),
        "missing parent was accepted");

    CcfMetadata self;
    self.blueprints = {node(1U, 1U)};
    require(hasIssue(
        airfix::assets::resolveBlueprintGraph(self).issues,
        BlueprintGraphIssueKind::selfParent),
        "self parent was accepted");

    CcfMetadata cycle;
    cycle.blueprints = {node(1U, 2U), node(2U, 1U)};
    require(hasIssue(
        airfix::assets::resolveBlueprintGraph(cycle).issues,
        BlueprintGraphIssueKind::cycle),
        "cycle was accepted");
}

void testMeshAndLimits() {
    CcfMetadata ccf;
    ccf.blueprints = {node(1U, 0U, CcfBlueprintKind::mesh, 4U)};
    require(hasIssue(
        airfix::assets::resolveBlueprintGraph(ccf).issues,
        BlueprintGraphIssueKind::invalidMeshIndex),
        "invalid mesh index was accepted");

    CcfMetadata oneRoot;
    oneRoot.blueprints = {node(1U, 0U)};
    require(hasIssue(
        airfix::assets::resolveBlueprintSubtree(oneRoot, 1U).issues,
        BlueprintGraphIssueKind::invalidRoot),
        "invalid subtree root was accepted");

    CcfMetadata deep;
    deep.blueprints = {node(1U, 0U), node(2U, 1U), node(3U, 2U)};
    auto limits = airfix::assets::BlueprintGraphLimits{};
    limits.maximumSelectedNodes = 2U;
    const auto subtree = airfix::assets::resolveBlueprintSubtree(deep, 0U, limits);
    require(subtree.blueprintIndices.empty() &&
            hasIssue(subtree.issues, BlueprintGraphIssueKind::limitExceeded),
        "subtree node limit was not enforced atomically");

    limits = {};
    limits.maximumEdges = 1U;
    require(hasIssue(
        airfix::assets::resolveBlueprintGraph(deep, limits).issues,
        BlueprintGraphIssueKind::limitExceeded),
        "edge limit was not enforced");

    limits = {};
    limits.maximumBlueprints = 2U;
    const auto limited = airfix::assets::resolveBlueprintGraph(deep, limits);
    require(limited.nodes.empty() &&
            hasIssue(limited.issues, BlueprintGraphIssueKind::limitExceeded),
        "blueprint count limit was not enforced before allocation");
}

void testDeepGraphIsIterative() {
    CcfMetadata ccf;
    constexpr std::size_t count = 20'000U;
    ccf.blueprints.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto reference = static_cast<std::uint32_t>(index + 1U);
        const auto parent = index == 0U ? 0U : reference - 1U;
        ccf.blueprints.push_back(node(reference, parent));
    }
    auto limits = airfix::assets::BlueprintGraphLimits{};
    limits.maximumDepth = count;
    const auto subtree = airfix::assets::resolveBlueprintSubtree(ccf, 0U, limits);
    require(subtree.issues.empty() && subtree.blueprintIndices.size() == count,
        "deep iterative graph resolution failed");
    require(subtree.blueprintIndices.back() == count - 1U,
        "deep preorder tail mismatch");

    limits.maximumDepth = 1'000U;
    const auto depthLimited = airfix::assets::resolveBlueprintSubtree(ccf, 0U, limits);
    require(depthLimited.blueprintIndices.empty() &&
            hasIssue(depthLimited.issues, BlueprintGraphIssueKind::limitExceeded),
        "graph depth limit was not enforced atomically");
}

} // namespace

int main() {
    try {
        testStableGraphAndPreorder();
        testMalformedReferences();
        testMeshAndLimits();
        testDeepGraphIsIterative();
        std::cout << "CcfBlueprintGraph tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "CcfBlueprintGraph tests failed: " << error.what() << '\n';
        return 1;
    }
}
