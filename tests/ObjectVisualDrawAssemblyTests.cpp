#include "airfix/render/ObjectVisualDrawAssembly.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace airfix::assets;
using namespace airfix::render;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::array<CcfVector3, 3> identityOrientation() {
    return {
        CcfVector3{1.0F, 0.0F, 0.0F},
        CcfVector3{0.0F, 1.0F, 0.0F},
        CcfVector3{0.0F, 0.0F, 1.0F},
    };
}

[[nodiscard]] CcfMeshMetadata mesh(
    const std::string& name,
    const std::uint32_t reference,
    const std::uint32_t materialReference) {
    CcfMeshMetadata result{
        .name = name,
        .reference = reference,
        .orientation = identityOrientation(),
    };
    result.vertices = {
        CcfMeshVertexMetadata{.position = {0.0F, 0.0F, 0.0F}},
        CcfMeshVertexMetadata{.position = {1.0F, 0.0F, 0.0F}},
        CcfMeshVertexMetadata{.position = {0.0F, 1.0F, 0.0F}},
    };
    result.triangles = {{
        .vertexIndices = {0U, 1U, 2U},
        .materialReference = materialReference,
    }};
    return result;
}

[[nodiscard]] CcfBlueprintMetadata blueprint(
    const CcfBlueprintKind kind,
    const std::string& name,
    const std::uint32_t reference,
    const std::uint32_t parentReference,
    const std::optional<std::size_t> meshIndex,
    const CcfVector3 position = {},
    const std::array<CcfVector3, 3>& orientation =
        identityOrientation()) {
    return {
        .kind = kind,
        .name = name,
        .reference = reference,
        .parentReference = parentReference,
        .authoredTransform = {
            .position = position,
            .rawScalar = 1.0F,
            .orientation = orientation,
        },
        .meshIndex = meshIndex,
    };
}

[[nodiscard]] ObjectDefinition object(const std::string& selector = "Root") {
    return {
        .ccfPath = "logical/object.ccf",
        .meshName = selector,
    };
}

[[nodiscard]] CcfMetadata representativeScene() {
    CcfMetadata ccf;
    ccf.materials = {
        {.name = "first", .reference = 51U},
        {.name = "second", .reference = 52U},
    };
    ccf.meshes = {
        mesh("physical-zero", 100U, 51U),
        mesh("physical-one", 200U, 52U),
    };

    const std::array<CcfVector3, 3> rootRotation{
        CcfVector3{1.0F, 0.0F, 0.0F},
        CcfVector3{0.0F, 0.0F, 1.0F},
        CcfVector3{0.0F, -1.0F, 0.0F},
    };
    const std::array<CcfVector3, 3> childRotation{
        CcfVector3{0.0F, 1.0F, 0.0F},
        CcfVector3{-1.0F, 0.0F, 0.0F},
        CcfVector3{0.0F, 0.0F, 1.0F},
    };

    // Physical child order produces DFS [0, 1, 2, 3, 4]. The root and
    // intermediate branch are intentionally null nodes.
    ccf.blueprints = {
        blueprint(
            CcfBlueprintKind::nullNode,
            "Root",
            10U,
            0U,
            std::nullopt,
            {9.0F, 8.0F, 7.0F},
            rootRotation),
        blueprint(
            CcfBlueprintKind::mesh,
            "first-child",
            20U,
            10U,
            1U,
            {1.0F, 2.0F, 3.0F},
            childRotation),
        blueprint(
            CcfBlueprintKind::nullNode,
            "branch",
            30U,
            10U,
            std::nullopt),
        blueprint(
            CcfBlueprintKind::mesh,
            "grandchild",
            40U,
            30U,
            0U,
            {4.0F, 5.0F, 6.0F}),
        blueprint(
            CcfBlueprintKind::mesh,
            "shared-child",
            50U,
            10U,
            1U,
            {7.0F, 8.0F, 9.0F}),
    };
    return ccf;
}

[[nodiscard]] std::vector<DrawMaterial> bindings() {
    return {
        {.sourceReference = 51U, .primary = TextureAssetId{1U}},
        {.sourceReference = 52U, .primary = TextureAssetId{2U}},
    };
}

[[nodiscard]] bool hasIssue(
    const ObjectVisualDrawAssembly& result,
    const ObjectVisualDrawIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

void requireAtomicFailure(
    const ObjectVisualDrawAssembly& result,
    const ObjectVisualDrawIssueKind kind,
    const std::string& message) {
    require(
        hasIssue(result, kind) &&
            result.model.meshes.empty() &&
            result.model.instances.empty() &&
            result.meshProvenance.empty() &&
            result.instanceProvenance.empty(),
        message);
}

void testNullRootDescendantsSharingAndOrder() {
    const auto result = buildObjectVisualDrawAssembly(
        object(), representativeScene(), bindings());
    require(result.issues.empty(), "valid null-root subtree was rejected");
    require(
        result.resolution.rootBlueprintIndex ==
            std::optional<std::size_t>{0U} &&
            result.resolution.blueprintIndices ==
                std::vector<std::size_t>{0U, 1U, 2U, 3U, 4U},
        "resolved subtree did not retain stable DFS order");
    require(
        result.model.meshes.size() == 2U &&
            result.model.instances.size() == 3U,
        "null nodes were rendered or shared physical mesh was duplicated");
    require(
        result.model.instances[0].meshSlot == 0U &&
            result.model.instances[1].meshSlot == 1U &&
            result.model.instances[2].meshSlot == 0U,
        "mesh slots do not follow physical-mesh first use");
    require(
        result.meshProvenance ==
            std::vector<ObjectVisualProvenance>{
                {1U, 1U},
                {3U, 0U},
            } &&
            result.instanceProvenance ==
                std::vector<ObjectVisualProvenance>{
                    {1U, 1U},
                    {3U, 0U},
                    {4U, 1U},
                },
        "mesh or instance provenance is not parallel to its payload");
    require(
        result.model.instances[0].sourceNodeReference == 20U &&
            result.model.instances[1].sourceNodeReference == 40U &&
            result.model.instances[2].sourceNodeReference == 50U,
        "instance order no longer follows mesh-bearing blueprint DFS");
}

void testSubtreeWithoutDrawableDescendantFails() {
    CcfMetadata ccf;
    ccf.blueprints = {
        blueprint(
            CcfBlueprintKind::nullNode,
            "Root",
            10U,
            0U,
            std::nullopt),
        blueprint(
            CcfBlueprintKind::light,
            "light",
            20U,
            10U,
            std::nullopt),
    };
    const auto result = buildObjectVisualDrawAssembly(
        object(), ccf, std::span<const DrawMaterial>{});
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::invalidResolution,
        "subtree without a drawable descendant was published as success");
}

void testAuthoredWorldTransformAndBasisAreDirect() {
    const auto ccf = representativeScene();
    const Mat3 sourceToRuntime{{
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    const BasisTransform basis{sourceToRuntime, 2.0F};
    const auto result =
        buildObjectVisualDrawAssembly(object(), ccf, bindings(), basis);
    require(result.issues.empty(), "valid basis conversion was rejected");

    const auto expectedChild =
        convertLegacyTransform(ccf.blueprints[1].authoredTransform, basis);
    const auto convertedRoot =
        convertLegacyTransform(ccf.blueprints[0].authoredTransform, basis);
    const auto incorrectlyRecomposed =
        composeNodeTransforms(convertedRoot, expectedChild);
    require(
        result.model.instances[0].modelLinear == expectedChild.linear &&
            result.model.instances[0].modelTranslation ==
                Vec3{4.0F, 2.0F, 6.0F},
        "basis was not applied to the authored-world child transform");
    require(
        result.model.instances[0].modelLinear !=
                incorrectlyRecomposed.linear ||
            result.model.instances[0].modelTranslation !=
                incorrectlyRecomposed.translation,
        "noncommutative parent transform was incorrectly recomposed");

    // This generic assembly deliberately stops before legacy SetSkin actor
    // binding. That adapter must derive descendants relative to the selected
    // root and replace each root slot's pose; these authored-world values are
    // therefore not final actor-local transforms.
}

void testBindingsAndSelectionFailAtomically() {
    const auto ccf = representativeScene();
    auto result = buildObjectVisualDrawAssembly(
        object(), ccf, std::vector<DrawMaterial>{{.sourceReference = 51U}});
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::missingMaterialBinding,
        "missing preassigned material binding published partial output");
    require(
        result.issues.front().physicalMeshIndex ==
                std::optional<std::size_t>{1U} &&
            result.issues.front().sourceReference ==
                std::optional<std::uint32_t>{52U},
        "missing binding lost physical-mesh provenance");

    result = buildObjectVisualDrawAssembly(
        object(),
        ccf,
        std::vector<DrawMaterial>{
            {.sourceReference = 51U},
            {.sourceReference = 51U},
        });
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::invalidMaterialBinding,
        "duplicate material binding was accepted");

    auto missingSelector = object();
    missingSelector.meshName.reset();
    result = buildObjectVisualDrawAssembly(
        missingSelector, ccf, bindings());
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::missingBlueprintSelector,
        "missing blueprint selector was treated as an empty success");
}

void testMalformedGraphAndDependencyFailuresAreTyped() {
    auto ccf = representativeScene();
    ccf.blueprints[3].parentReference = 999U;
    auto result =
        buildObjectVisualDrawAssembly(object(), ccf, bindings());
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::graphFailure,
        "malformed graph was partially rendered");
    require(
        std::ranges::any_of(
            result.issues,
            [](const auto& issue) {
                return issue.graphIssue ==
                    BlueprintGraphIssueKind::missingParent;
            }),
        "graph failure lost its upstream typed cause");

    auto missingCcf = object();
    missingCcf.ccfPath.reset();
    result = buildObjectVisualDrawAssembly(
        missingCcf, representativeScene(), bindings());
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::dependencyFailure,
        "missing CCF dependency was traversed");
    require(
        result.issues.front().dependencyIssue ==
            DependencyIssueKind::missingCcfPath,
        "dependency failure lost its upstream typed cause");
}

void testSingularAndNonFiniteInputsFailAtomically() {
    auto ccf = representativeScene();
    ccf.blueprints[1].authoredTransform.orientation = {
        CcfVector3{1.0F, 0.0F, 0.0F},
        CcfVector3{2.0F, 0.0F, 0.0F},
        CcfVector3{0.0F, 0.0F, 1.0F},
    };
    auto result =
        buildObjectVisualDrawAssembly(object(), ccf, bindings());
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::invalidTransform,
        "singular authored transform published partial output");
    require(
        std::ranges::any_of(
            result.issues,
            [](const auto& issue) {
                return issue.geometryError ==
                    GeometryErrorCode::singularTransform;
            }),
        "singular transform lost its typed geometry cause");

    ccf = representativeScene();
    ccf.blueprints[1].authoredTransform.position[0] =
        std::numeric_limits<float>::quiet_NaN();
    result = buildObjectVisualDrawAssembly(object(), ccf, bindings());
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::invalidTransform,
        "non-finite authored transform published partial output");

    const Mat3 singularBasis{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    result = buildObjectVisualDrawAssembly(
        object(),
        representativeScene(),
        bindings(),
        BasisTransform{singularBasis, 1.0F});
    requireAtomicFailure(
        result,
        ObjectVisualDrawIssueKind::geometryFailure,
        "singular conversion basis published partial output");
}

void testHardLimitsAndOverflowSafeAccounting() {
    const auto requireLimit = [](
        const auto configure,
        const std::string& message) {
        auto limits = ObjectVisualDrawLimits{};
        configure(limits);
        const auto result = buildObjectVisualDrawAssembly(
            object(), representativeScene(), bindings(), {}, {}, limits);
        requireAtomicFailure(
            result,
            ObjectVisualDrawIssueKind::limitExceeded,
            message);
    };

    requireLimit(
        [](auto& limits) { limits.maximumMeshes = 1U; },
        "unique-mesh limit published partial output");
    requireLimit(
        [](auto& limits) { limits.maximumInstances = 2U; },
        "instance limit published partial output");
    requireLimit(
        [](auto& limits) { limits.maximumMaterialBindings = 1U; },
        "binding limit published partial output");
    requireLimit(
        [](auto& limits) { limits.maximumTotalVertices = 2U; },
        "aggregate vertex limit published partial output");
    requireLimit(
        [](auto& limits) { limits.maximumTotalIndices = 2U; },
        "aggregate index limit published partial output");
    requireLimit(
        [](auto& limits) { limits.maximumTotalMaterials = 1U; },
        "aggregate material limit published partial output");
    requireLimit(
        [](auto& limits) { limits.maximumTotalRanges = 1U; },
        "aggregate range limit published partial output");
    requireLimit(
        [](auto& limits) { limits.maximumTotalBytes = 0U; },
        "byte accounting limit published partial output");
    requireLimit(
        [](auto& limits) {
            limits.dependencies.graph.maximumSelectedNodes = 2U;
        },
        "dependency graph limit was not propagated atomically");

    // The public limit and issue types must retain an explicit overflow path;
    // all count*element-size accounting in the implementation is checked
    // before multiplication.
    static_assert(
        ObjectVisualDrawIssueKind::integerOverflow !=
        ObjectVisualDrawIssueKind::limitExceeded);
}

} // namespace

int main() {
    try {
        testNullRootDescendantsSharingAndOrder();
        testSubtreeWithoutDrawableDescendantFails();
        testAuthoredWorldTransformAndBasisAreDirect();
        testBindingsAndSelectionFailAtomically();
        testMalformedGraphAndDependencyFailuresAreTyped();
        testSingularAndNonFiniteInputsFailAtomically();
        testHardLimitsAndOverflowSafeAccounting();
        std::cout << "ObjectVisualDrawAssembly tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "ObjectVisualDrawAssembly tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
