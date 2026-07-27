#include "airfix/render/PlayerActorSceneAssembly.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace airfix::render;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] DrawMeshPayload mesh(const std::uint32_t marker) {
    DrawMeshPayload result;
    result.vertices = {
        {.position = {0.0F, 0.0F, 0.0F},
         .normal = {0.0F, 0.0F, 1.0F},
         .uv = {0.0F, 0.0F}},
        {.position = {1.0F, 0.0F, 0.0F},
         .normal = {0.0F, 0.0F, 1.0F},
         .uv = {1.0F, 0.0F}},
        {.position = {0.0F, 1.0F, 0.0F},
         .normal = {0.0F, 0.0F, 1.0F},
         .uv = {0.0F, 1.0F}},
    };
    result.indices = {0U, 1U, 2U};
    result.materials = {{
        .sourceReference = marker,
        .primary = std::nullopt,
        .secondary = std::nullopt,
        .environment = std::nullopt,
    }};
    result.ranges = {{
        .firstIndex = 0U,
        .indexCount = 3U,
        .materialSlot = 0U,
        .texcoordMode = TexcoordMode::uv0,
    }};
    result.localBounds = {
        .minimum = {0.0F, 0.0F, 0.0F},
        .maximum = {1.0F, 1.0F, 0.0F},
    };
    return result;
}

[[nodiscard]] ConvertedNodeTransform transform(
    const Mat3& linear,
    const Vec3 translation) {
    return {
        .linear = linear,
        .translation = translation,
        .rawScalar = 1.0F,
    };
}

[[nodiscard]] DrawMeshInstance instance(
    const std::uint32_t meshSlot,
    const std::uint32_t sourceReference,
    const ConvertedNodeTransform& value) {
    return {
        .meshSlot = meshSlot,
        .sourceNodeReference = sourceReference,
        .modelLinear = value.linear,
        .modelTranslation = value.translation,
    };
}

[[nodiscard]] DrawModelPayload staticModel() {
    DrawModelPayload result;
    result.meshes = {mesh(10U)};
    result.instances = {instance(
        0U,
        10U,
        transform(
            Mat3{{
                Vec3{1.0F, 0.0F, 0.0F},
                Vec3{0.0F, 2.0F, 0.0F},
                Vec3{0.0F, 0.0F, 3.0F},
            }},
            Vec3{-7.0F, 8.0F, 9.0F}))};
    return result;
}

[[nodiscard]] PlayerActorVisualDrawAssembly actorVisual() {
    const PlayerActorVisualProvenance mesh0{
        0U, 11U, 101U, 7U};
    const PlayerActorVisualProvenance mesh1{
        0U, 12U, 102U, 9U};
    const PlayerActorVisualProvenance sharedAgain{
        0U, 13U, 103U, 7U};

    PlayerActorVisualDrawAssembly result;
    result.model.meshes = {mesh(101U), mesh(102U)};
    result.model.instances = {
        instance(
            0U,
            101U,
            transform(
                Mat3{{
                    Vec3{1.0F, 1.0F, 0.0F},
                    Vec3{0.0F, 1.0F, 0.0F},
                    Vec3{0.0F, 0.0F, 1.0F},
                }},
                Vec3{2.0F, -3.0F, 4.0F})),
        instance(
            1U,
            102U,
            transform(
                Mat3{{
                    Vec3{0.0F, 1.0F, 0.0F},
                    Vec3{-1.0F, 0.0F, 0.0F},
                    Vec3{0.0F, 0.0F, 1.0F},
                }},
                Vec3{5.0F, 6.0F, -2.0F})),
        instance(
            0U,
            103U,
            transform(
                Mat3{{
                    Vec3{2.0F, 0.0F, 0.0F},
                    Vec3{0.0F, 1.0F, 0.0F},
                    Vec3{0.0F, 0.0F, 0.5F},
                }},
                Vec3{-1.0F, 7.0F, 3.0F})),
    };
    result.meshProvenance = {mesh0, mesh1};
    result.instanceProvenance = {mesh0, mesh1, sharedAgain};
    return result;
}

[[nodiscard]] ConvertedNodeTransform actorWorld() {
    return transform(
        Mat3{{
            Vec3{0.0F, 2.0F, 0.0F},
            Vec3{-3.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 0.5F},
        }},
        Vec3{11.0F, -13.0F, 17.0F});
}

[[nodiscard]] ConvertedNodeTransform transformOf(
    const DrawMeshInstance& value) {
    return {
        .linear = value.modelLinear,
        .translation = value.modelTranslation,
        .rawScalar = 1.0F,
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

[[nodiscard]] bool close(
    const ConvertedNodeTransform& left,
    const ConvertedNodeTransform& right,
    const float tolerance = 1.0e-5F) {
    return close(left.linear, right.linear, tolerance) &&
        close(left.translation, right.translation, tolerance);
}

[[nodiscard]] bool hasIssue(
    const PlayerActorSceneAssembly& result,
    const PlayerActorSceneIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

void requireAtomicFailure(
    const PlayerActorSceneAssembly& result,
    const PlayerActorSceneIssueKind kind,
    const std::string& message) {
    require(
        hasIssue(result, kind) &&
            result.model.meshes.empty() &&
            result.model.instances.empty() &&
            result.actorMeshProvenance.empty() &&
            result.actorInstanceProvenance.empty() &&
            !result.actorBinding.has_value(),
        message);
}

void testAppendRemapProvenanceAndStaticStability() {
    const auto staticInput = staticModel();
    const auto actorInput = actorVisual();
    const auto result = buildPlayerActorSceneAssembly(
        staticInput, actorInput, actorWorld());
    require(result.complete(), "valid scene assembly was rejected");
    require(
        result.model.meshes.size() == 3U &&
            result.model.instances.size() == 4U,
        "final model counts are wrong");
    require(
        result.model.meshes[0].materials[0].sourceReference == 10U &&
            result.model.meshes[1].materials[0].sourceReference == 101U &&
            result.model.meshes[2].materials[0].sourceReference == 102U,
        "actor meshes were not appended in exact first-use order");
    require(
        result.model.instances[1].meshSlot == 1U &&
            result.model.instances[2].meshSlot == 2U &&
            result.model.instances[3].meshSlot == 1U,
        "shared actor mesh slots were not remapped by exact static count");
    require(
        result.model.instances[0] == staticInput.instances[0],
        "static instance transform or identity was changed");

    require(
        result.actorBinding ==
            std::optional<PlayerActorSceneBinding>{
                PlayerActorSceneBinding{1U, 2U, 1U, 3U}},
        "contiguous actor binding range is wrong");
    require(
        result.actorMeshProvenance ==
            std::vector<PlayerActorSceneMeshProvenance>{
                {actorInput.meshProvenance[0], 1U},
                {actorInput.meshProvenance[1], 2U},
            },
        "actor mesh first-use provenance lost final slots");
    require(
        result.actorInstanceProvenance ==
            std::vector<PlayerActorSceneInstanceProvenance>{
                {
                    actorInput.instanceProvenance[0],
                    1U,
                    transformOf(actorInput.model.instances[0]),
                },
                {
                    actorInput.instanceProvenance[1],
                    2U,
                    transformOf(actorInput.model.instances[1]),
                },
                {
                    actorInput.instanceProvenance[2],
                    3U,
                    transformOf(actorInput.model.instances[2]),
                },
            },
        "actor instance provenance lost final indices or actor-local poses");
}

void testNoncommutativeWorldCompositionOccursExactlyOnce() {
    const auto actorInput = actorVisual();
    const auto world = actorWorld();
    const auto actorLocal = transformOf(actorInput.model.instances[0]);
    const auto result = buildPlayerActorSceneAssembly(
        staticModel(), actorInput, world);
    require(result.complete(), "composition fixture was rejected");

    const auto actual = transformOf(result.model.instances[1]);
    const auto expected = composeNodeTransforms(world, actorLocal);
    const auto reversed = composeNodeTransforms(actorLocal, world);
    const auto doubled = composeNodeTransforms(world, expected);
    require(
        close(actual, expected),
        "actor world was not composed with actor-local exactly once");
    require(
        result.actorInstanceProvenance.size() ==
            actorInput.model.instances.size(),
        "actor-local publication count is not parallel");
    for (std::size_t index = 0U;
         index < actorInput.model.instances.size();
        ++index) {
        require(
            close(
                result.actorInstanceProvenance[index].actorLocal,
                transformOf(actorInput.model.instances[index])),
            "published actor-local transform changed before composition");
    }
    auto tampered = result.actorInstanceProvenance.front();
    tampered.actorLocal.translation.x += 1.0F;
    require(
        tampered != result.actorInstanceProvenance.front(),
        "instance provenance equality ignored actor-local tampering");
    require(
        !close(actual, actorLocal) &&
            !close(actual, reversed) &&
            !close(actual, doubled),
        "composition test did not reject local/reversed/double placement");
}

void testActorOnlyModelIsValid() {
    const auto actorInput = actorVisual();
    const auto result = buildPlayerActorSceneAssembly(
        DrawModelPayload{}, actorInput, actorWorld());
    require(
        result.complete() &&
            result.model.meshes.size() == 2U &&
            result.model.instances.size() == 3U &&
            result.actorBinding ==
                std::optional<PlayerActorSceneBinding>{
                    PlayerActorSceneBinding{0U, 2U, 0U, 3U}},
        "valid actor-only scene was not published");
}

void testForgedAndIncompleteActorVisualsFailClosed() {
    auto actor = actorVisual();
    actor.issues.push_back({
        .kind = PlayerActorVisualDrawIssueKind::invalidActorLocalTransform,
        .objectVisualIssue = std::nullopt,
        .blueprintIndex = std::nullopt,
        .physicalMeshIndex = std::nullopt,
        .sourceReference = std::nullopt,
        .dependencyIssue = std::nullopt,
        .graphIssue = std::nullopt,
        .geometryError = GeometryErrorCode::singularTransform,
        .drawMeshError = std::nullopt,
    });
    auto result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::actorVisualFailure,
        "upstream actor failure published a partial scene");
    require(
        result.issues.front().actorVisualIssue ==
                PlayerActorVisualDrawIssueKind::invalidActorLocalTransform &&
            result.issues.front().geometryError ==
                GeometryErrorCode::singularTransform,
        "upstream typed actor cause was lost");

    actor = actorVisual();
    actor.instanceProvenance.pop_back();
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorVisualAssembly,
        "nonparallel actor provenance published a partial scene");

    actor = actorVisual();
    actor.model.meshes.clear();
    actor.meshProvenance.clear();
    actor.model.instances.clear();
    actor.instanceProvenance.clear();
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::emptyActorVisual,
        "empty actor visual published a scene");

    actor = actorVisual();
    std::swap(actor.model.instances[0], actor.model.instances[1]);
    std::swap(
        actor.instanceProvenance[0],
        actor.instanceProvenance[1]);
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorProvenance,
        "forged non-first-use mesh order was accepted");

    actor = actorVisual();
    actor.instanceProvenance[0].blueprintReference = 999U;
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorProvenance,
        "source-reference/provenance mismatch was accepted");

    actor = actorVisual();
    actor.instanceProvenance[2].physicalMeshIndex = 9U;
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorProvenance,
        "repeated mesh use with mismatched physical provenance was accepted");
    require(
        result.issues.front().actorInstanceIndex ==
                std::optional<std::size_t>{2U} &&
            result.issues.front().actorMeshSlot ==
                std::optional<std::size_t>{0U} &&
            result.issues.front().actorProvenance ==
                std::optional<PlayerActorVisualProvenance>{
                    actor.instanceProvenance[2]},
        "repeated-use physical mismatch lost exact provenance");

    actor = actorVisual();
    actor.instanceProvenance[2].legacySkinSlot = 1U;
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorProvenance,
        "repeated mesh use from an unproven skin slot was accepted");
}

void testInvalidMeshSlotsFailClosedWithProvenance() {
    auto invalidStatic = staticModel();
    invalidStatic.instances[0].meshSlot = 1U;
    auto result = buildPlayerActorSceneAssembly(
        invalidStatic, actorVisual(), actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidStaticMeshSlot,
        "invalid static mesh slot published a scene");
    require(
        result.issues.front().staticInstanceIndex ==
            std::optional<std::size_t>{0U},
        "invalid static mesh slot lost its instance index");

    auto actor = actorVisual();
    actor.model.instances[2].meshSlot = 2U;
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorMeshSlot,
        "invalid actor mesh slot published a scene");
    require(
        result.issues.front().actorInstanceIndex ==
                std::optional<std::size_t>{2U} &&
            result.issues.front().actorMeshSlot ==
                std::optional<std::size_t>{2U} &&
            result.issues.front().actorProvenance ==
                std::optional<PlayerActorVisualProvenance>{
                    actor.instanceProvenance[2]},
        "invalid actor mesh slot lost index/provenance");
}

void testInvalidWorldLocalAndStaticTransformsFailClosed() {
    auto invalidStatic = staticModel();
    invalidStatic.instances[0].modelTranslation.x =
        std::numeric_limits<float>::quiet_NaN();
    auto result = buildPlayerActorSceneAssembly(
        invalidStatic, actorVisual(), actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidStaticTransform,
        "non-finite static transform published a scene");
    require(
        result.issues.front().geometryError ==
            GeometryErrorCode::nonFiniteValue,
        "non-finite static transform lost geometry cause");

    invalidStatic = staticModel();
    invalidStatic.instances[0].modelLinear.columns[1] =
        invalidStatic.instances[0].modelLinear.columns[0];
    result = buildPlayerActorSceneAssembly(
        invalidStatic, actorVisual(), actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidStaticTransform,
        "singular static transform published a scene");
    require(
        result.issues.front().geometryError ==
            GeometryErrorCode::singularTransform,
        "singular static transform lost geometry cause");

    auto actor = actorVisual();
    actor.model.instances[1].modelTranslation.z =
        std::numeric_limits<float>::infinity();
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorLocalTransform,
        "non-finite actor-local transform published a scene");
    require(
        result.issues.front().actorInstanceIndex ==
                std::optional<std::size_t>{1U} &&
            result.issues.front().geometryError ==
                GeometryErrorCode::nonFiniteValue,
        "invalid actor-local transform lost index/geometry cause");

    actor = actorVisual();
    actor.model.instances[0].modelLinear.columns[2] =
        actor.model.instances[0].modelLinear.columns[1];
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, actorWorld());
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorLocalTransform,
        "singular actor-local transform published a scene");

    auto world = actorWorld();
    world.translation.y = std::numeric_limits<float>::quiet_NaN();
    result = buildPlayerActorSceneAssembly(
        staticModel(), actorVisual(), world);
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorWorldTransform,
        "non-finite actor world transform published a scene");

    world = actorWorld();
    world.linear.columns[2] = world.linear.columns[0];
    result = buildPlayerActorSceneAssembly(
        staticModel(), actorVisual(), world);
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidActorWorldTransform,
        "singular actor world transform published a scene");

    actor = actorVisual();
    const auto maximumFinite = std::numeric_limits<float>::max();
    actor.model.instances[0].modelLinear = Mat3{};
    actor.model.instances[0].modelTranslation =
        Vec3{maximumFinite, 0.0F, 0.0F};
    world = transform(Mat3{}, Vec3{maximumFinite, 0.0F, 0.0F});
    result = buildPlayerActorSceneAssembly(
        staticModel(), actor, world);
    requireAtomicFailure(
        result,
        PlayerActorSceneIssueKind::invalidComposedTransform,
        "finite inputs whose composition overflowed published a scene");
    require(
        result.issues.front().actorInstanceIndex ==
                std::optional<std::size_t>{0U} &&
            result.issues.front().actorProvenance ==
                std::optional<PlayerActorVisualProvenance>{
                    actor.instanceProvenance[0]} &&
            result.issues.front().geometryError ==
                GeometryErrorCode::nonFiniteValue,
        "composed-transform overflow lost index/provenance/geometry cause");
}

[[nodiscard]] std::size_t logicalBytes(
    const PlayerActorSceneAssembly& result) {
    std::size_t total =
        result.model.meshes.size() * sizeof(DrawMeshPayload) +
        result.model.instances.size() * sizeof(DrawMeshInstance) +
        result.actorMeshProvenance.size() *
            sizeof(PlayerActorSceneMeshProvenance) +
        result.actorInstanceProvenance.size() *
            sizeof(PlayerActorSceneInstanceProvenance) +
        sizeof(PlayerActorSceneBinding);
    for (const auto& value : result.model.meshes) {
        total += value.vertices.size() * sizeof(DrawVertex);
        total += value.indices.size() * sizeof(std::uint32_t);
        total += value.materials.size() * sizeof(DrawMaterial);
        total += value.ranges.size() * sizeof(DrawRange);
    }
    return total;
}

void testGlobalCountAndByteLimitsAreExactAndAtomic() {
    const auto baseline = buildPlayerActorSceneAssembly(
        staticModel(), actorVisual(), actorWorld());
    require(baseline.complete(), "limit baseline was rejected");

    PlayerActorSceneLimits exact{
        .maximumMeshes = baseline.model.meshes.size(),
        .maximumInstances = baseline.model.instances.size(),
        .maximumTotalVertices = 0U,
        .maximumTotalIndices = 0U,
        .maximumTotalMaterials = 0U,
        .maximumTotalRanges = 0U,
        .maximumTotalBytes = logicalBytes(baseline),
    };
    for (const auto& value : baseline.model.meshes) {
        exact.maximumTotalVertices += value.vertices.size();
        exact.maximumTotalIndices += value.indices.size();
        exact.maximumTotalMaterials += value.materials.size();
        exact.maximumTotalRanges += value.ranges.size();
    }
    auto result = buildPlayerActorSceneAssembly(
        staticModel(), actorVisual(), actorWorld(), exact);
    require(
        result.complete() && logicalBytes(result) == exact.maximumTotalBytes,
        "exact global count/byte limits were rejected");

    const auto assertOneBelowFails =
        [&](const PlayerActorSceneLimits& oneBelow,
            const std::string& label) {
            const auto failed = buildPlayerActorSceneAssembly(
                staticModel(), actorVisual(), actorWorld(), oneBelow);
            requireAtomicFailure(
                failed,
                PlayerActorSceneIssueKind::limitExceeded,
                label + " limit one below published partial output");
        };

    auto oneBelow = exact;
    --oneBelow.maximumMeshes;
    assertOneBelowFails(oneBelow, "mesh count");
    oneBelow = exact;
    --oneBelow.maximumInstances;
    assertOneBelowFails(oneBelow, "instance count");
    oneBelow = exact;
    --oneBelow.maximumTotalVertices;
    assertOneBelowFails(oneBelow, "vertex count");
    oneBelow = exact;
    --oneBelow.maximumTotalIndices;
    assertOneBelowFails(oneBelow, "index count");
    oneBelow = exact;
    --oneBelow.maximumTotalMaterials;
    assertOneBelowFails(oneBelow, "material count");
    oneBelow = exact;
    --oneBelow.maximumTotalRanges;
    assertOneBelowFails(oneBelow, "range count");
    oneBelow = exact;
    --oneBelow.maximumTotalBytes;
    assertOneBelowFails(oneBelow, "logical byte");

    static_assert(
        PlayerActorSceneIssueKind::meshSlotOverflow !=
            PlayerActorSceneIssueKind::instanceIndexOverflow &&
        PlayerActorSceneIssueKind::integerOverflow !=
            PlayerActorSceneIssueKind::limitExceeded);
    static_assert(
        std::is_same_v<
            decltype(PlayerActorSceneBinding{}.firstMeshSlot),
            std::size_t> &&
        std::is_same_v<
            decltype(PlayerActorSceneBinding{}.meshCount),
            std::size_t> &&
        std::is_same_v<
            decltype(PlayerActorSceneBinding{}.firstInstanceIndex),
            std::size_t> &&
        std::is_same_v<
            decltype(PlayerActorSceneBinding{}.instanceCount),
            std::size_t>);
}

} // namespace

int main() {
    try {
        testAppendRemapProvenanceAndStaticStability();
        testNoncommutativeWorldCompositionOccursExactlyOnce();
        testActorOnlyModelIsValid();
        testForgedAndIncompleteActorVisualsFailClosed();
        testInvalidMeshSlotsFailClosedWithProvenance();
        testInvalidWorldLocalAndStaticTransformsFailClosed();
        testGlobalCountAndByteLimitsAreExactAndAtomic();
        std::cout << "PlayerActorSceneAssembly tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "PlayerActorSceneAssembly tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
