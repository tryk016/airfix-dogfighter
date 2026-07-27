#include "airfix/render/PlayerActorVisualDrawAssembly.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
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
    const CcfVector3 position,
    const std::array<CcfVector3, 3>& orientation) {
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

[[nodiscard]] ObjectDefinition object() {
    return {
        .ccfPath = "authenticated/object.ccf",
        .meshName = "Root",
    };
}

[[nodiscard]] std::vector<DrawMaterial> bindings() {
    return {
        {.sourceReference = 71U, .primary = TextureAssetId{1U}},
        {.sourceReference = 72U, .primary = TextureAssetId{2U}},
    };
}

[[nodiscard]] CcfMetadata nullRootScene() {
    const std::array<CcfVector3, 3> rootRotation{
        CcfVector3{0.0F, 0.0F, -1.0F},
        CcfVector3{0.0F, 1.0F, 0.0F},
        CcfVector3{1.0F, 0.0F, 0.0F},
    };
    const std::array<CcfVector3, 3> childRotation{
        CcfVector3{0.0F, 1.0F, 0.0F},
        CcfVector3{-1.0F, 0.0F, 0.0F},
        CcfVector3{0.0F, 0.0F, 1.0F},
    };

    CcfMetadata ccf;
    ccf.materials = {
        {.name = "first", .reference = 71U},
        {.name = "second", .reference = 72U},
    };
    ccf.meshes = {
        mesh("shared", 501U, 71U),
        mesh("second", 502U, 72U),
    };
    ccf.blueprints = {
        blueprint(
            CcfBlueprintKind::nullNode,
            "Root",
            101U,
            0U,
            std::nullopt,
            {8.0F, -3.0F, 12.0F},
            rootRotation),
        blueprint(
            CcfBlueprintKind::mesh,
            "child",
            102U,
            101U,
            0U,
            {-4.0F, 9.0F, 2.0F},
            childRotation),
        blueprint(
            CcfBlueprintKind::nullNode,
            "branch",
            103U,
            101U,
            std::nullopt,
            {3.0F, 4.0F, 5.0F},
            identityOrientation()),
        blueprint(
            CcfBlueprintKind::mesh,
            "grandchild",
            104U,
            103U,
            1U,
            {11.0F, 1.0F, -7.0F},
            identityOrientation()),
        blueprint(
            CcfBlueprintKind::mesh,
            "shared-again",
            105U,
            101U,
            0U,
            {6.0F, 5.0F, 4.0F},
            identityOrientation()),
    };
    return ccf;
}

[[nodiscard]] BasisTransform nontrivialBasis() {
    return {
        .sourceToRuntime = Mat3{{
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{0.0F, 0.0F, 1.0F},
            Vec3{-1.0F, 0.0F, 0.0F},
        }},
        .runtimeUnitsPerSourceUnit = 2.5F,
    };
}

[[nodiscard]] bool close(
    const float left,
    const float right,
    const float tolerance = 1.0e-5F) {
    return std::fabs(left - right) <= tolerance;
}

[[nodiscard]] bool close(
    const Vec3& left,
    const Vec3& right,
    const float tolerance = 1.0e-5F) {
    return close(left.x, right.x, tolerance) &&
        close(left.y, right.y, tolerance) &&
        close(left.z, right.z, tolerance);
}

[[nodiscard]] bool close(
    const Mat3& left,
    const Mat3& right,
    const float tolerance = 1.0e-5F) {
    return close(left.columns[0], right.columns[0], tolerance) &&
        close(left.columns[1], right.columns[1], tolerance) &&
        close(left.columns[2], right.columns[2], tolerance);
}

[[nodiscard]] ConvertedNodeTransform instanceTransform(
    const DrawMeshInstance& instance) {
    return {
        .linear = instance.modelLinear,
        .translation = instance.modelTranslation,
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] bool hasIssue(
    const PlayerActorVisualDrawAssembly& result,
    const PlayerActorVisualDrawIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

void requireAtomicFailure(
    const PlayerActorVisualDrawAssembly& result,
    const PlayerActorVisualDrawIssueKind kind,
    const std::string& message) {
    require(
        hasIssue(result, kind) &&
            result.model.meshes.empty() &&
            result.model.instances.empty() &&
            result.meshProvenance.empty() &&
            result.instanceProvenance.empty(),
        message);
}

void testNullRootBecomesRootRelativeActorLocal() {
    const auto ccf = nullRootScene();
    const auto basis = nontrivialBasis();
    const auto result = buildPlayerActorVisualDrawAssembly(
        object(), ccf, bindings(), basis);
    require(result.issues.empty(), "valid null-root actor visual was rejected");
    require(
        result.model.meshes.size() == 2U &&
            result.model.instances.size() == 3U,
        "physical-mesh sharing or mesh-bearing DFS order was lost");
    require(
        result.model.instances[0].meshSlot == 0U &&
            result.model.instances[1].meshSlot == 1U &&
            result.model.instances[2].meshSlot == 0U,
        "first-use mesh slots changed in the actor adapter");
    require(
        result.instanceProvenance ==
            std::vector<PlayerActorVisualProvenance>{
                {0U, 1U, 102U, 0U},
                {0U, 3U, 104U, 1U},
                {0U, 4U, 105U, 0U},
            },
        "slot-zero instance provenance is not parallel to DFS output");
    require(
        result.meshProvenance ==
            std::vector<PlayerActorVisualProvenance>{
                {0U, 1U, 102U, 0U},
                {0U, 3U, 104U, 1U},
            },
        "slot-zero first-use mesh provenance changed");

    const auto authoredRoot = convertLegacyTransform(
        ccf.blueprints[0].authoredTransform, basis);
    const auto authoredChild = convertLegacyTransform(
        ccf.blueprints[1].authoredTransform, basis);
    const auto relative =
        deriveLocalTransform(authoredRoot, authoredChild);
    const auto recoveredRoot = convertLegacyAxisRotationWorldPose(
        Vec3{},
        {0.0F, std::numbers::pi_v<float>, 0.0F},
        basis);
    const auto expectedActorLocal =
        composeNodeTransforms(recoveredRoot, relative);
    const auto actual = instanceTransform(result.model.instances[0]);
    require(
        close(actual.linear, expectedActorLocal.linear) &&
            close(actual.translation, expectedActorLocal.translation),
        "descendant was not made selected-root-relative");

    const auto incorrectlyDirect =
        composeNodeTransforms(recoveredRoot, authoredChild);
    require(
        !close(actual.linear, incorrectlyDirect.linear) ||
            !close(actual.translation, incorrectlyDirect.translation),
        "authored-world child was applied directly as actor-local");
}

void testMeshRootIsExactlyRecoveredSlotPose() {
    auto ccf = nullRootScene();
    ccf.blueprints[0].kind = CcfBlueprintKind::mesh;
    ccf.blueprints[0].meshIndex = 1U;
    const auto basis = nontrivialBasis();
    const auto result = buildPlayerActorVisualDrawAssembly(
        object(), ccf, bindings(), basis);
    require(result.issues.empty(), "valid mesh root was rejected");
    require(
        !result.model.instances.empty() &&
            result.instanceProvenance.front() ==
                PlayerActorVisualProvenance{0U, 0U, 101U, 1U},
        "mesh root was not first in stable DFS order");

    const auto expected = convertLegacyAxisRotationWorldPose(
        Vec3{},
        {0.0F, std::numbers::pi_v<float>, 0.0F},
        basis);
    const auto actual = instanceTransform(result.model.instances.front());
    require(
        actual.translation == Vec3{} &&
            actual.linear == expected.linear,
        "selected mesh root is not exact zero-translation legacy Y(pi)");
}

void testAbsoluteSpawnIsComposedLaterExactlyOnce() {
    const auto ccf = nullRootScene();
    const auto basis = nontrivialBasis();
    const auto result = buildPlayerActorVisualDrawAssembly(
        object(), ccf, bindings(), basis);
    require(result.issues.empty(), "actor-local fixture was rejected");

    const auto spawnWorld = convertLegacyAxisRotationWorldPose(
        Vec3{13.0F, -2.0F, 7.0F},
        {0.25F, -0.4F, 0.125F},
        basis);
    const auto actorLocal =
        instanceTransform(result.model.instances.front());
    const auto absolute =
        composeNodeTransforms(spawnWorld, actorLocal);

    const auto authoredRoot = convertLegacyTransform(
        ccf.blueprints[0].authoredTransform, basis);
    const auto authoredChild = convertLegacyTransform(
        ccf.blueprints[1].authoredTransform, basis);
    const auto recoveredRoot = convertLegacyAxisRotationWorldPose(
        Vec3{},
        {0.0F, std::numbers::pi_v<float>, 0.0F},
        basis);
    const auto expected = composeNodeTransforms(
        spawnWorld,
        composeNodeTransforms(
            recoveredRoot,
            deriveLocalTransform(authoredRoot, authoredChild)));
    require(
        close(absolute.linear, expected.linear) &&
            close(absolute.translation, expected.translation),
        "later spawn-world composition does not place actor-local once");
    require(
        !close(actorLocal.translation, absolute.translation),
        "adapter appears to have pre-applied absolute spawn placement");
}

void testUpstreamDependencyGraphAndGeometryCausesSurvive() {
    auto missingCcf = object();
    missingCcf.ccfPath.reset();
    auto result = buildPlayerActorVisualDrawAssembly(
        missingCcf, nullRootScene(), bindings());
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::objectVisualFailure,
        "dependency failure published partial actor output");
    require(
        result.issues.front().objectVisualIssue ==
                ObjectVisualDrawIssueKind::dependencyFailure &&
            result.issues.front().dependencyIssue ==
                DependencyIssueKind::missingCcfPath,
        "dependency failure lost its upstream typed cause");

    auto malformed = nullRootScene();
    malformed.blueprints[3].parentReference = 999U;
    result = buildPlayerActorVisualDrawAssembly(
        object(), malformed, bindings());
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::objectVisualFailure,
        "graph failure published partial actor output");
    require(
        std::ranges::any_of(
            result.issues,
            [](const auto& issue) {
                return issue.objectVisualIssue ==
                        ObjectVisualDrawIssueKind::graphFailure &&
                    issue.graphIssue ==
                        BlueprintGraphIssueKind::missingParent;
            }),
        "graph failure lost its exact upstream cause");

    auto invalidMeshNode = nullRootScene();
    invalidMeshNode.blueprints[1].authoredTransform.position[0] =
        std::numeric_limits<float>::quiet_NaN();
    result = buildPlayerActorVisualDrawAssembly(
        object(), invalidMeshNode, bindings());
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::objectVisualFailure,
        "generic transform failure published partial actor output");
    require(
        result.issues.front().objectVisualIssue ==
                ObjectVisualDrawIssueKind::invalidTransform &&
            result.issues.front().geometryError ==
                GeometryErrorCode::nonFiniteValue &&
            result.issues.front().blueprintIndex ==
                std::optional<std::size_t>{1U} &&
            result.issues.front().physicalMeshIndex ==
                std::optional<std::size_t>{0U} &&
            result.issues.front().sourceReference ==
                std::optional<std::uint32_t>{102U},
        "generic geometry failure lost index/reference provenance");
}

void testInvalidSelectedNullRootFailsAtomically() {
    auto singular = nullRootScene();
    singular.blueprints[0].authoredTransform.orientation = {
        CcfVector3{1.0F, 0.0F, 0.0F},
        CcfVector3{2.0F, 0.0F, 0.0F},
        CcfVector3{0.0F, 0.0F, 1.0F},
    };
    auto result = buildPlayerActorVisualDrawAssembly(
        object(), singular, bindings());
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::invalidSelectedRootTransform,
        "singular selected null root published actor output");
    require(
        result.issues.front().geometryError ==
                GeometryErrorCode::singularTransform &&
            result.issues.front().blueprintIndex ==
                std::optional<std::size_t>{0U} &&
            result.issues.front().sourceReference ==
                std::optional<std::uint32_t>{101U},
        "singular selected root lost typed source provenance");

    auto nonFinite = nullRootScene();
    nonFinite.blueprints[0].authoredTransform.position[1] =
        std::numeric_limits<float>::infinity();
    result = buildPlayerActorVisualDrawAssembly(
        object(), nonFinite, bindings());
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::invalidSelectedRootTransform,
        "non-finite selected null root published actor output");
    require(
        result.issues.front().geometryError ==
            GeometryErrorCode::nonFiniteValue,
        "non-finite selected root lost typed geometry cause");
}

void testLimitsAndMissingBindingsRemainAtomic() {
    auto limits = ObjectVisualDrawLimits{};
    limits.maximumInstances = 0U;
    auto result = buildPlayerActorVisualDrawAssembly(
        object(), nullRootScene(), bindings(), {}, {}, limits);
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::objectVisualFailure,
        "instance limit published partial actor output");
    require(
        result.issues.front().objectVisualIssue ==
            ObjectVisualDrawIssueKind::limitExceeded,
        "limit failure lost upstream issue kind");

    result = buildPlayerActorVisualDrawAssembly(
        object(),
        nullRootScene(),
        std::vector<DrawMaterial>{{.sourceReference = 71U}});
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::objectVisualFailure,
        "missing material binding published partial actor output");
    require(
        result.issues.front().objectVisualIssue ==
                ObjectVisualDrawIssueKind::missingMaterialBinding &&
            result.issues.front().physicalMeshIndex ==
                std::optional<std::size_t>{1U} &&
            result.issues.front().sourceReference ==
                std::optional<std::uint32_t>{72U},
        "missing binding lost physical mesh/reference provenance");
}

[[nodiscard]] std::size_t finalLogicalPayloadBytes(
    const PlayerActorVisualDrawAssembly& result) {
    std::size_t total =
        result.model.meshes.size() * sizeof(DrawMeshPayload) +
        result.model.instances.size() * sizeof(DrawMeshInstance) +
        result.meshProvenance.size() *
            sizeof(PlayerActorVisualProvenance) +
        result.instanceProvenance.size() *
            sizeof(PlayerActorVisualProvenance);
    for (const auto& mesh : result.model.meshes) {
        total += mesh.vertices.size() * sizeof(DrawVertex);
        total += mesh.indices.size() * sizeof(std::uint32_t);
        total += mesh.materials.size() * sizeof(DrawMaterial);
        total += mesh.ranges.size() * sizeof(DrawRange);
    }
    return total;
}

void testFinalLogicalByteLimitIsExactAndAtomic() {
    const auto baseline = buildPlayerActorVisualDrawAssembly(
        object(), nullRootScene(), bindings());
    require(
        baseline.issues.empty(),
        "baseline final-payload accounting fixture was rejected");
    const auto exactBytes = finalLogicalPayloadBytes(baseline);
    require(exactBytes > 0U, "final logical payload unexpectedly has zero bytes");

    auto limits = ObjectVisualDrawLimits{};
    limits.maximumTotalBytes = exactBytes;
    auto result = buildPlayerActorVisualDrawAssembly(
        object(), nullRootScene(), bindings(), {}, {}, limits);
    require(
        result.issues.empty() &&
            finalLogicalPayloadBytes(result) == exactBytes,
        "exact final logical byte limit was rejected");

    limits.maximumTotalBytes = exactBytes - 1U;
    result = buildPlayerActorVisualDrawAssembly(
        object(), nullRootScene(), bindings(), {}, {}, limits);
    requireAtomicFailure(
        result,
        PlayerActorVisualDrawIssueKind::limitExceeded,
        "limit one byte below final logical payload published output");

    // Count multiplication/addition is guarded before it is performed. The
    // present bounded synthetic vectors cannot reach size_t overflow, but the
    // public diagnostic keeps that condition distinct from an ordinary cap.
    static_assert(
        PlayerActorVisualDrawIssueKind::integerOverflow !=
        PlayerActorVisualDrawIssueKind::limitExceeded);
}

} // namespace

int main() {
    try {
        testNullRootBecomesRootRelativeActorLocal();
        testMeshRootIsExactlyRecoveredSlotPose();
        testAbsoluteSpawnIsComposedLaterExactlyOnce();
        testUpstreamDependencyGraphAndGeometryCausesSurvive();
        testInvalidSelectedNullRootFailsAtomically();
        testLimitsAndMissingBindingsRemainAtomic();
        testFinalLogicalByteLimitIsExactAndAtomic();
        std::cout << "PlayerActorVisualDrawAssembly tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "PlayerActorVisualDrawAssembly tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
