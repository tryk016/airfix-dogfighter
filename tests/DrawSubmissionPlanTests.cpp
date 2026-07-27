#include "airfix/render/DrawSubmissionPlan.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>

namespace {

using namespace airfix::render;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool hasIssue(
    const DrawSubmissionDescription& result,
    const DrawSubmissionIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

[[nodiscard]] DrawVertex vertex(const float x = 0.0F) {
    return {
        .position = {x, 0.0F, 0.0F},
        .normal = {0.0F, 1.0F, 0.0F},
        .uv = {x, 0.0F},
    };
}

[[nodiscard]] DrawMeshPayload mesh(
    const std::uint32_t primary,
    const bool twoRanges = false) {
    DrawMeshPayload result;
    result.vertices = {vertex(0.0F), vertex(1.0F), vertex(2.0F)};
    result.indices = twoRanges
        ? std::vector<std::uint32_t>{0U, 1U, 2U, 2U, 1U, 0U}
        : std::vector<std::uint32_t>{0U, 1U, 2U};
    result.materials = {
        DrawMaterial{
            .sourceReference = 10U,
            .primary = TextureAssetId{primary},
            .secondary = TextureAssetId{0U},
            .environment = std::nullopt,
        },
        DrawMaterial{
            .sourceReference = 20U,
            .primary = std::nullopt,
            .secondary = TextureAssetId{2U},
            .environment = TextureAssetId{3U},
        },
    };
    result.ranges = {
        DrawRange{0U, 3U, 0U, TexcoordMode::uv0},
    };
    if (twoRanges) {
        result.ranges.push_back(
            DrawRange{3U, 3U, 1U, TexcoordMode::none});
    }
    result.localBounds = {{0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}};
    return result;
}

[[nodiscard]] DrawMeshInstance instance(const std::uint32_t meshSlot) {
    return {
        .meshSlot = meshSlot,
        .sourceNodeReference = 100U + meshSlot,
    };
}

[[nodiscard]] DrawModelPayload representativeModel() {
    DrawModelPayload model;
    model.meshes = {mesh(1U, false), mesh(4U, true)};
    model.instances = {instance(1U), instance(0U), instance(1U)};
    return model;
}

void testDeterministicSharedMeshCommands() {
    const auto result = buildDrawSubmissionPlan(
        representativeModel(), 5U);
    require(result.issues.empty() && result.plan.has_value(),
        "valid model was rejected");
    require(
        result.plan->meshUploads ==
            std::vector<DrawMeshUploadMetadata>{
                {0U, 3U, 3U},
                {1U, 3U, 6U},
            },
        "mesh upload metadata changed order or counts");
    require(result.plan->commands.size() == 5U,
        "shared meshes did not produce one command per instance/range");

    const auto& commands = result.plan->commands;
    require(
        commands[0].instanceIndex == 0U &&
            commands[0].meshSlot == 1U &&
            commands[0].rangeIndex == 0U &&
            commands[1].instanceIndex == 0U &&
            commands[1].meshSlot == 1U &&
            commands[1].rangeIndex == 1U &&
            commands[2].instanceIndex == 1U &&
            commands[2].meshSlot == 0U &&
            commands[3].instanceIndex == 2U &&
            commands[3].rangeIndex == 0U &&
            commands[4].instanceIndex == 2U &&
            commands[4].rangeIndex == 1U,
        "commands are not ordered by instance then range");
    require(
        commands[0].firstIndex == 0U &&
            commands[0].indexCount == 3U &&
            commands[0].materialSlot == 0U &&
            commands[0].texcoordMode == TexcoordMode::uv0 &&
            commands[0].primary == TextureAssetId{4U} &&
            commands[0].secondary == TextureAssetId{0U} &&
            !commands[0].environment.has_value() &&
            commands[1].firstIndex == 3U &&
            commands[1].materialSlot == 1U &&
            commands[1].texcoordMode == TexcoordMode::none &&
            !commands[1].primary.has_value() &&
            commands[1].secondary == TextureAssetId{2U} &&
            commands[1].environment == TextureAssetId{3U},
        "command lost range or complete optional texture bindings");
}

void testEmptyInputsAndEmptyMesh() {
    {
        const DrawModelPayload empty;
        const auto result = buildDrawSubmissionPlan(empty, 0U);
        require(result.issues.empty() && result.plan.has_value() &&
                    result.plan->meshUploads.empty() &&
                    result.plan->commands.empty(),
            "empty model is not a successful empty plan");
    }
    {
        DrawModelPayload model;
        model.meshes.emplace_back();
        model.instances.push_back(instance(0U));
        const auto result = buildDrawSubmissionPlan(model, 0U);
        require(result.issues.empty() && result.plan.has_value() &&
                    result.plan->meshUploads.size() == 1U &&
                    result.plan->meshUploads[0].vertexCount == 0U &&
                    result.plan->meshUploads[0].indexCount == 0U &&
                    result.plan->commands.empty(),
            "completely empty mesh did not produce zero commands");
    }
}

void testTextureOptionalityAndBounds() {
    DrawModelPayload model;
    model.meshes.push_back(mesh(0U));
    model.meshes[0].materials[0].secondary = std::nullopt;
    model.meshes[0].materials[0].environment = TextureAssetId{1U};
    model.instances.push_back(instance(0U));

    auto result = buildDrawSubmissionPlan(model, 4U);
    require(result.issues.empty() && result.plan.has_value() &&
                result.plan->commands[0].primary == TextureAssetId{0U} &&
                !result.plan->commands[0].secondary.has_value() &&
                result.plan->commands[0].environment == TextureAssetId{1U},
        "ID zero or missing primary/secondary/environment was rejected");

    model.meshes[0].materials[0].environment = TextureAssetId{4U};
    result = buildDrawSubmissionPlan(model, 4U);
    require(
        hasIssue(
            result,
            DrawSubmissionIssueKind::textureAssetOutOfRange) &&
            !result.plan.has_value() &&
            result.issues[0].meshSlot == 0U &&
            result.issues[0].materialSlot == 0U &&
            result.issues[0].textureRole ==
                DrawSubmissionTextureRole::environment,
        "texture ID equal to available count was not typed and atomic");
}

void testInvalidSlotsAndIndices() {
    {
        auto model = representativeModel();
        model.instances[1].meshSlot = 2U;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(
                result,
                DrawSubmissionIssueKind::invalidInstanceMeshSlot) &&
                !result.plan.has_value() &&
                result.issues[0].instanceIndex == 1U,
            "invalid instance mesh slot was not rejected with context");
    }
    {
        auto model = representativeModel();
        model.meshes[0].ranges[0].materialSlot = 2U;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(
                result,
                DrawSubmissionIssueKind::invalidMaterialSlot) &&
                !result.plan.has_value(),
            "invalid material slot was not rejected atomically");
    }
    {
        auto model = representativeModel();
        model.meshes[0].indices[1] = 3U;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(result, DrawSubmissionIssueKind::indexOutOfRange) &&
                !result.plan.has_value() &&
                result.issues[0].indexPosition == 1U,
            "out-of-range index was not rejected with context");
    }
}

void testInvalidRanges() {
    {
        auto model = representativeModel();
        model.meshes[1].ranges[1].firstIndex = 4U;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(
                result,
                DrawSubmissionIssueKind::rangeCoverageMismatch) &&
                !result.plan.has_value(),
            "range gap was not rejected");
    }
    {
        auto model = representativeModel();
        model.meshes[1].ranges[1].firstIndex = 2U;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(
                result,
                DrawSubmissionIssueKind::rangeCoverageMismatch) &&
                !result.plan.has_value(),
            "range overlap was not rejected");
    }
    {
        auto model = representativeModel();
        model.meshes[0].ranges[0].indexCount = 2U;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(result, DrawSubmissionIssueKind::invalidRange) &&
                !result.plan.has_value(),
            "non-triangle range was not rejected");
    }
    {
        auto model = representativeModel();
        model.meshes[0].ranges.clear();
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(
                result,
                DrawSubmissionIssueKind::rangeCoverageMismatch) &&
                !result.plan.has_value(),
            "uncovered index buffer was not rejected");
    }
}

void testNonFinitePayloads() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    {
        auto model = representativeModel();
        model.meshes[0].vertices[0].normal.x = nan;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(result, DrawSubmissionIssueKind::nonFiniteVertex) &&
                !result.plan.has_value(),
            "non-finite vertex attribute was not rejected");
    }
    {
        auto model = representativeModel();
        model.meshes[0].localBounds.maximum.z = nan;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(result, DrawSubmissionIssueKind::nonFiniteBounds) &&
                !result.plan.has_value(),
            "non-finite bounds were not rejected");
    }
    {
        auto model = representativeModel();
        model.instances[0].modelLinear.columns[1].y = nan;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(
                result,
                DrawSubmissionIssueKind::nonFiniteTransform) &&
                !result.plan.has_value(),
            "non-finite transform was not rejected");
    }
}

void testBoundsContract() {
    {
        auto model = representativeModel();
        model.meshes[0].localBounds.minimum.x = 3.0F;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(result, DrawSubmissionIssueKind::invalidBounds) &&
                !result.plan.has_value(),
            "inverted finite bounds were accepted");
    }
    {
        auto model = representativeModel();
        model.meshes[0].localBounds.maximum.x = 1.0F;
        const auto result = buildDrawSubmissionPlan(model, 5U);
        require(
            hasIssue(result, DrawSubmissionIssueKind::invalidBounds) &&
                !result.plan.has_value() &&
                result.issues[0].vertexIndex == 2U,
            "bounds that exclude a vertex were accepted");
    }
    {
        DrawModelPayload model;
        model.meshes.emplace_back();
        model.meshes[0].localBounds.maximum.x = 1.0F;
        const auto result = buildDrawSubmissionPlan(model, 0U);
        require(
            hasIssue(result, DrawSubmissionIssueKind::invalidBounds) &&
                !result.plan.has_value(),
            "nonzero bounds were accepted for an empty mesh");
    }
}

void testExactLimitsAndOnePast() {
    const auto model = representativeModel();
    const DrawSubmissionLimits exact{
        .maximumMeshes = 2U,
        .maximumInstances = 3U,
        .maximumTotalVertices = 6U,
        .maximumTotalIndices = 9U,
        .maximumTotalMaterials = 4U,
        .maximumTotalRanges = 3U,
        .maximumCommands = 5U,
        .maximumSourceBytes =
            2U * sizeof(DrawMeshPayload) +
            3U * sizeof(DrawMeshInstance) +
            6U * sizeof(DrawVertex) +
            9U * sizeof(std::uint32_t) +
            4U * sizeof(DrawMaterial) +
            3U * sizeof(DrawRange),
    };
    require(
        buildDrawSubmissionPlan(model, 5U, exact).plan.has_value(),
        "exact limits were rejected");

    auto expectLimit = [&](DrawSubmissionLimits limits) {
        const auto result = buildDrawSubmissionPlan(model, 5U, limits);
        require(
            hasIssue(result, DrawSubmissionIssueKind::limitExceeded) &&
                !result.plan.has_value(),
            "one-past limit did not fail closed");
    };
    auto limits = exact;
    limits.maximumMeshes = 1U;
    expectLimit(limits);
    limits = exact;
    limits.maximumInstances = 2U;
    expectLimit(limits);
    limits = exact;
    limits.maximumTotalVertices = 5U;
    expectLimit(limits);
    limits = exact;
    limits.maximumTotalIndices = 8U;
    expectLimit(limits);
    limits = exact;
    limits.maximumTotalMaterials = 3U;
    expectLimit(limits);
    limits = exact;
    limits.maximumTotalRanges = 2U;
    expectLimit(limits);
    limits = exact;
    limits.maximumCommands = 4U;
    expectLimit(limits);
    limits = exact;
    --limits.maximumSourceBytes;
    expectLimit(limits);
}

void testInvalidTexcoordMode() {
    auto model = representativeModel();
    model.meshes[0].ranges[0].texcoordMode =
        static_cast<TexcoordMode>(255U);
    const auto result = buildDrawSubmissionPlan(model, 5U);
    require(
        hasIssue(
            result,
            DrawSubmissionIssueKind::invalidTexcoordMode) &&
            !result.plan.has_value(),
        "unknown texcoord mode was accepted by a backend plan");
}

} // namespace

int main() {
    try {
        testDeterministicSharedMeshCommands();
        testEmptyInputsAndEmptyMesh();
        testTextureOptionalityAndBounds();
        testInvalidSlotsAndIndices();
        testInvalidRanges();
        testNonFinitePayloads();
        testBoundsContract();
        testExactLimitsAndOnePast();
        testInvalidTexcoordMode();
    }
    catch (const std::exception& error) {
        std::cerr << "Draw submission plan test failure: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "Draw submission plan tests passed\n";
    return 0;
}
