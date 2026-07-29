#include "airfix/render/PlayerActorPoseRuntimePreparation.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace airfix::render;

static_assert(noexcept(planPlayerActorPoseRuntime(
    std::declval<const std::optional<PlayerActorSceneBinding>&>(),
    std::declval<
        std::span<const PlayerActorSceneInstanceProvenance>>(),
    std::declval<std::span<const DrawMeshInstance>>())));
static_assert(noexcept(preparePlayerActorPoseRuntime(
    std::declval<const std::optional<PlayerActorSceneBinding>&>(),
    std::declval<
        std::span<const PlayerActorSceneInstanceProvenance>>(),
    std::declval<std::span<const DrawMeshInstance>>(),
    std::declval<const ConvertedNodeTransform&>(),
    std::declval<const PlayerActorPoseRuntimePlan&>())));

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] ConvertedNodeTransform transform(
    const Vec3 translation = {}) {
    return {
        .linear = {},
        .translation = translation,
        .rawScalar = 1.0F,
    };
}

struct Fixture final {
    std::optional<PlayerActorSceneBinding> binding{
        PlayerActorSceneBinding{
            .firstMeshSlot = 1U,
            .meshCount = 1U,
            .firstInstanceIndex = 1U,
            .instanceCount = 2U,
        }};
    ConvertedNodeTransform actorWorld{
        transform({10.0F, 20.0F, 30.0F})};
    std::vector<PlayerActorSceneInstanceProvenance> provenance;
    std::vector<DrawMeshInstance> instances;
};

[[nodiscard]] Fixture fixture() {
    Fixture value;
    value.provenance = {
        PlayerActorSceneInstanceProvenance{
            .actor = {},
            .finalInstanceIndex = 1U,
            .actorLocal = transform({1.0F, 2.0F, 3.0F}),
        },
        PlayerActorSceneInstanceProvenance{
            .actor = {},
            .finalInstanceIndex = 2U,
            .actorLocal = transform({-4.0F, 5.0F, -6.0F}),
        },
    };
    value.instances.resize(3U);
    value.instances[0U] = DrawMeshInstance{
        .meshSlot = 0U,
        .sourceNodeReference = 7U,
        .modelLinear = {},
        .modelTranslation = {100.0F, 200.0F, 300.0F},
    };
    for (std::size_t index = 0U;
         index < value.provenance.size();
         ++index) {
        const auto composed = composeNodeTransforms(
            value.actorWorld, value.provenance[index].actorLocal);
        value.instances[index + 1U] = DrawMeshInstance{
            .meshSlot = 1U,
            .sourceNodeReference =
                static_cast<std::uint32_t>(index + 10U),
            .modelLinear = composed.linear,
            .modelTranslation = composed.translation,
        };
    }
    return value;
}

void testNoPlayerIsACompleteEmptyPath() {
    const std::optional<PlayerActorSceneBinding> binding;
    const std::vector<PlayerActorSceneInstanceProvenance> provenance;
    const std::vector<DrawMeshInstance> instances(2U);

    const auto plan =
        planPlayerActorPoseRuntime(binding, provenance, instances);
    require(
        plan.status ==
                PlayerActorPoseRuntimePreparationStatus::noPlayer &&
            !plan.ready() && plan.retainedPoseBytes == 0U,
        "no-player preflight returned the wrong contract");

    const auto prepared = preparePlayerActorPoseRuntime(
        binding, provenance, instances, transform(), plan);
    require(
        prepared.status ==
                PlayerActorPoseRuntimePreparationStatus::noPlayer &&
            prepared.runtime == nullptr && !prepared.complete(),
        "no-player preparation created a runtime");
}

void testMissingBindingRejectsRetainedProvenance() {
    const std::optional<PlayerActorSceneBinding> binding;
    const std::vector<PlayerActorSceneInstanceProvenance> provenance{
        PlayerActorSceneInstanceProvenance{
            .actor = {},
            .finalInstanceIndex = 0U,
            .actorLocal = transform(),
        },
    };
    const std::vector<DrawMeshInstance> instances(1U);

    const auto plan =
        planPlayerActorPoseRuntime(binding, provenance, instances);
    require(
        plan.status ==
            PlayerActorPoseRuntimePreparationStatus::invalidPayload,
        "orphan player provenance was accepted as no-player");
    const auto prepared = preparePlayerActorPoseRuntime(
        binding, provenance, instances, transform(), plan);
    require(
        prepared.status ==
                PlayerActorPoseRuntimePreparationStatus::
                    invalidPayload &&
            prepared.runtime == nullptr,
        "invalid no-binding payload created a runtime");
}

void testExactPlanAndStepZeroPublication() {
    const auto value = fixture();
    const auto plan = planPlayerActorPoseRuntime(
        value.binding, value.provenance, value.instances);
    const std::size_t expectedFrameBytes =
        2U * sizeof(DynamicInstancePoseOverride);
    const std::size_t expectedRetainedBytes =
        2U * sizeof(PlayerActorPoseRuntimeSource) +
        3U * expectedFrameBytes;
    require(
        plan.ready() &&
            plan.exactLimits.maximumInstances == 3U &&
            plan.exactLimits.maximumOverrides == 2U &&
            plan.exactLimits.maximumFrameBytes ==
                expectedFrameBytes &&
            plan.retainedPoseBytes == expectedRetainedBytes,
        "portable preflight did not preserve the exact Metal limits");

    auto prepared = preparePlayerActorPoseRuntime(
        value.binding,
        value.provenance,
        value.instances,
        value.actorWorld,
        plan);
    require(
        prepared.complete() &&
            prepared.runtime->retainedBytes() ==
                expectedRetainedBytes,
        "valid player pose runtime was not prepared");

    auto lease = prepared.runtime->tryAcquire();
    require(
        lease.has_value() && lease->simulationStep() == 0U &&
            lease->overrides().size() == 2U,
        "prepared runtime did not retain exact step zero");
    for (std::uint32_t instanceIndex = 1U;
         instanceIndex <= 2U;
         ++instanceIndex) {
        const auto& authored = value.instances[instanceIndex];
        require(
            lease->resolve(
                instanceIndex,
                authored.modelLinear,
                authored.modelTranslation) ==
                ResolvedInstancePose{
                    .modelLinear = authored.modelLinear,
                    .modelTranslation =
                        authored.modelTranslation,
                },
            "step-zero override differs from authored pose");
    }
}

void testPreparedRuntimePublishesChangingPose() {
    const auto value = fixture();
    const auto plan = planPlayerActorPoseRuntime(
        value.binding, value.provenance, value.instances);
    auto prepared = preparePlayerActorPoseRuntime(
        value.binding,
        value.provenance,
        value.instances,
        value.actorWorld,
        plan);
    require(prepared.complete(), "pose runtime fixture failed");

    const auto changedWorld =
        transform({40.0F, -50.0F, 60.0F});
    const auto published =
        prepared.runtime->tryPublish(changedWorld, 1U);
    require(
        published.result ==
            PlayerActorPoseRuntimePublishResult::published,
        "prepared runtime rejected a valid changing pose");
    auto lease = prepared.runtime->tryAcquire();
    require(
        lease.has_value() && lease->simulationStep() == 1U,
        "changing pose publication was unavailable");
    const auto expected = composeNodeTransforms(
        changedWorld, value.provenance[0U].actorLocal);
    const auto resolved = lease->resolve(
        1U,
        value.instances[1U].modelLinear,
        value.instances[1U].modelTranslation);
    require(
        resolved.modelLinear == expected.linear &&
            resolved.modelTranslation == expected.translation,
        "changing actor world was not composed by the prepared runtime");
}

void testTamperedPlanAndAuthoredPoseFailClosed() {
    const auto value = fixture();
    const auto plan = planPlayerActorPoseRuntime(
        value.binding, value.provenance, value.instances);

    auto tamperedPlan = plan;
    ++tamperedPlan.retainedPoseBytes;
    const auto planRejected = preparePlayerActorPoseRuntime(
        value.binding,
        value.provenance,
        value.instances,
        value.actorWorld,
        tamperedPlan);
    require(
        planRejected.status ==
                PlayerActorPoseRuntimePreparationStatus::
                    invalidPayload &&
            planRejected.runtime == nullptr,
        "tampered preflight was accepted");

    auto inconsistentInstances = value.instances;
    inconsistentInstances[1U].modelTranslation.x += 1.0F;
    const auto inconsistentPlan = planPlayerActorPoseRuntime(
        value.binding, value.provenance, inconsistentInstances);
    const auto poseRejected = preparePlayerActorPoseRuntime(
        value.binding,
        value.provenance,
        inconsistentInstances,
        value.actorWorld,
        inconsistentPlan);
    require(
        poseRejected.status ==
                PlayerActorPoseRuntimePreparationStatus::
                    invalidPayload &&
            poseRejected.runtime == nullptr,
        "authored step-zero mismatch was accepted");

    auto signedZeroInstances = value.instances;
    require(
        std::bit_cast<std::uint32_t>(
            signedZeroInstances[1U]
                .modelLinear.columns[0U].y) == 0U,
        "fixture does not contain the expected positive zero");
    signedZeroInstances[1U]
        .modelLinear.columns[0U].y = -0.0F;
    const auto signedZeroPlan = planPlayerActorPoseRuntime(
        value.binding, value.provenance, signedZeroInstances);
    const auto signedZeroRejected =
        preparePlayerActorPoseRuntime(
            value.binding,
            value.provenance,
            signedZeroInstances,
            value.actorWorld,
            signedZeroPlan);
    require(
        signedZeroRejected.status ==
                PlayerActorPoseRuntimePreparationStatus::
                    invalidPayload &&
            signedZeroRejected.runtime == nullptr,
        "signed-zero step-zero mismatch was accepted");
}

void testMalformedBindingAndPlatformLimitClassification() {
    auto value = fixture();
    value.binding->instanceCount = 3U;
    const auto mismatchPlan = planPlayerActorPoseRuntime(
        value.binding, value.provenance, value.instances);
    require(
        mismatchPlan.status ==
            PlayerActorPoseRuntimePreparationStatus::invalidPayload,
        "binding/provenance count mismatch was not rejected");

    value = fixture();
    value.binding->firstInstanceIndex = 2U;
    const auto rangePlan = planPlayerActorPoseRuntime(
        value.binding, value.provenance, value.instances);
    require(
        rangePlan.status ==
            PlayerActorPoseRuntimePreparationStatus::invalidPayload,
        "binding range outside authored instances was not rejected");

    const std::size_t excessiveCount =
        DynamicInstancePoseLimits{}.maximumOverrides + 1U;
    std::vector<PlayerActorSceneInstanceProvenance>
        excessiveProvenance(excessiveCount);
    std::vector<DrawMeshInstance> excessiveInstances(excessiveCount);
    for (std::size_t index = 0U;
         index < excessiveProvenance.size();
         ++index) {
        excessiveProvenance[index].finalInstanceIndex =
            static_cast<std::uint32_t>(index);
        excessiveProvenance[index].actorLocal = transform();
    }
    const std::optional<PlayerActorSceneBinding> excessiveBinding{
        PlayerActorSceneBinding{
            .firstMeshSlot = 0U,
            .meshCount = 1U,
            .firstInstanceIndex = 0U,
            .instanceCount = excessiveCount,
        }};
    const auto resourcePlan = planPlayerActorPoseRuntime(
        excessiveBinding, excessiveProvenance, excessiveInstances);
    require(
        resourcePlan.status ==
            PlayerActorPoseRuntimePreparationStatus::resourceLimit,
        "platform override ceiling was not classified as a resource limit");
}

} // namespace

int main() {
    try {
        testNoPlayerIsACompleteEmptyPath();
        testMissingBindingRejectsRetainedProvenance();
        testExactPlanAndStepZeroPublication();
        testPreparedRuntimePublishesChangingPose();
        testTamperedPlanAndAuthoredPoseFailClosed();
        testMalformedBindingAndPlatformLimitClassification();
        std::cout
            << "PlayerActorPoseRuntimePreparation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "PlayerActorPoseRuntimePreparation tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
