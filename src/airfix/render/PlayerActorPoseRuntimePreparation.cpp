#include "airfix/render/PlayerActorPoseRuntimePreparation.hpp"

#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace airfix::render {
namespace {

[[nodiscard]] constexpr bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept {
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] constexpr bool checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool sameFloatBits(
    const float left,
    const float right) noexcept {
    return std::bit_cast<std::uint32_t>(left) ==
        std::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool sameVecBits(
    const Vec3& left,
    const Vec3& right) noexcept {
    return sameFloatBits(left.x, right.x) &&
        sameFloatBits(left.y, right.y) &&
        sameFloatBits(left.z, right.z);
}

[[nodiscard]] bool sameMatBits(
    const Mat3& left,
    const Mat3& right) noexcept {
    return sameVecBits(left.columns[0], right.columns[0]) &&
        sameVecBits(left.columns[1], right.columns[1]) &&
        sameVecBits(left.columns[2], right.columns[2]);
}

[[nodiscard]] PlayerActorPoseRuntimePreparation failure(
    const PlayerActorPoseRuntimePreparationStatus status) noexcept {
    return {
        .status = status,
        .runtime = nullptr,
    };
}

} // namespace

PlayerActorPoseRuntimePlan planPlayerActorPoseRuntime(
    const std::optional<PlayerActorSceneBinding>& actorBinding,
    const std::span<const PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance,
    const std::span<const DrawMeshInstance> authoredInstances) noexcept {
    if (!actorBinding.has_value()) {
        return actorInstanceProvenance.empty()
            ? PlayerActorPoseRuntimePlan{}
            : PlayerActorPoseRuntimePlan{
                  .status =
                      PlayerActorPoseRuntimePreparationStatus::
                          invalidPayload,
              };
    }

    const auto& binding = *actorBinding;
    if (binding.instanceCount == 0U ||
        binding.instanceCount != actorInstanceProvenance.size()) {
        return {
            .status =
                PlayerActorPoseRuntimePreparationStatus::invalidPayload,
        };
    }

    const DynamicInstancePoseLimits platformCeiling;
    if (authoredInstances.size() >
            static_cast<std::size_t>(platformCeiling.maximumInstances) ||
        authoredInstances.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        return {
            .status =
                PlayerActorPoseRuntimePreparationStatus::resourceLimit,
        };
    }

    std::size_t instanceRangeEnd = 0U;
    if (!checkedAdd(
            binding.firstInstanceIndex,
            binding.instanceCount,
            instanceRangeEnd) ||
        instanceRangeEnd > authoredInstances.size()) {
        return {
            .status =
                PlayerActorPoseRuntimePreparationStatus::invalidPayload,
        };
    }

    std::size_t frameBytes = 0U;
    if (binding.instanceCount > platformCeiling.maximumOverrides ||
        !checkedMultiply(
            binding.instanceCount,
            sizeof(DynamicInstancePoseOverride),
            frameBytes) ||
        frameBytes > platformCeiling.maximumFrameBytes) {
        return {
            .status =
                PlayerActorPoseRuntimePreparationStatus::resourceLimit,
        };
    }

    std::size_t sourceBytes = 0U;
    std::size_t retainedOverrideBytes = 0U;
    std::size_t retainedPoseBytes = 0U;
    if (!checkedMultiply(
            binding.instanceCount,
            sizeof(PlayerActorPoseRuntimeSource),
            sourceBytes) ||
        !checkedMultiply(frameBytes, 3U, retainedOverrideBytes) ||
        !checkedAdd(
            sourceBytes,
            retainedOverrideBytes,
            retainedPoseBytes)) {
        return {
            .status =
                PlayerActorPoseRuntimePreparationStatus::resourceLimit,
        };
    }

    return {
        .status = PlayerActorPoseRuntimePreparationStatus::ready,
        .exactLimits =
            {
                .maximumInstances = static_cast<std::uint32_t>(
                    authoredInstances.size()),
                .maximumOverrides = binding.instanceCount,
                .maximumFrameBytes = frameBytes,
            },
        .retainedPoseBytes = retainedPoseBytes,
    };
}

PlayerActorPoseRuntimePreparation preparePlayerActorPoseRuntime(
    const std::optional<PlayerActorSceneBinding>& actorBinding,
    const std::span<const PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance,
    const std::span<const DrawMeshInstance> authoredInstances,
    const ConvertedNodeTransform& initialActorWorld,
    const PlayerActorPoseRuntimePlan& plan) noexcept {
    const auto expectedPlan = planPlayerActorPoseRuntime(
        actorBinding, actorInstanceProvenance, authoredInstances);
    if (expectedPlan != plan) {
        return failure(
            PlayerActorPoseRuntimePreparationStatus::invalidPayload);
    }
    if (plan.status ==
        PlayerActorPoseRuntimePreparationStatus::noPlayer) {
        return {};
    }
    if (plan.status !=
        PlayerActorPoseRuntimePreparationStatus::ready) {
        return failure(plan.status);
    }

    try {
        auto built = PlayerActorPoseRuntime::create(
            actorBinding,
            actorInstanceProvenance,
            authoredInstances.size(),
            initialActorWorld,
            0U,
            plan.exactLimits);
        if (!built.complete()) {
            const bool allocationFailure =
                built.issue.has_value() &&
                built.issue->kind ==
                    PlayerActorPoseRuntimeBuildIssueKind::
                        allocationFailure;
            return failure(
                allocationFailure
                    ? PlayerActorPoseRuntimePreparationStatus::
                          resourceLimit
                    : PlayerActorPoseRuntimePreparationStatus::
                          invalidPayload);
        }
        if (built.retainedBytes != plan.retainedPoseBytes ||
            built.runtime->retainedBytes() !=
                plan.retainedPoseBytes) {
            return failure(
                PlayerActorPoseRuntimePreparationStatus::
                    invalidPayload);
        }

        {
            auto initialLease = built.runtime->tryAcquire();
            if (!initialLease.has_value() ||
                initialLease->simulationStep() != 0U ||
                initialLease->overrides().size() !=
                    actorBinding->instanceCount) {
                return failure(
                    PlayerActorPoseRuntimePreparationStatus::
                        invalidPayload);
            }
            const auto initialOverrides = initialLease->overrides();
            for (std::size_t index = 0U;
                 index < initialOverrides.size();
                 ++index) {
                const auto& poseOverride = initialOverrides[index];
                std::size_t expectedInstanceIndex = 0U;
                if (!checkedAdd(
                        actorBinding->firstInstanceIndex,
                        index,
                        expectedInstanceIndex) ||
                    expectedInstanceIndex >= authoredInstances.size() ||
                    static_cast<std::size_t>(
                        poseOverride.instanceIndex) !=
                        expectedInstanceIndex) {
                    return failure(
                        PlayerActorPoseRuntimePreparationStatus::
                            invalidPayload);
                }
                const auto& authored =
                    authoredInstances[expectedInstanceIndex];
                if (!sameMatBits(
                        poseOverride.modelLinear,
                        authored.modelLinear) ||
                    !sameVecBits(
                        poseOverride.modelTranslation,
                        authored.modelTranslation)) {
                    return failure(
                        PlayerActorPoseRuntimePreparationStatus::
                            invalidPayload);
                }
            }
        }

        std::shared_ptr<PlayerActorPoseRuntime> runtime(
            std::move(built.runtime));
        return {
            .status =
                PlayerActorPoseRuntimePreparationStatus::ready,
            .runtime = std::move(runtime),
        };
    } catch (const std::bad_alloc&) {
        return failure(
            PlayerActorPoseRuntimePreparationStatus::resourceLimit);
    } catch (const std::length_error&) {
        return failure(
            PlayerActorPoseRuntimePreparationStatus::resourceLimit);
    } catch (...) {
        return failure(
            PlayerActorPoseRuntimePreparationStatus::invalidPayload);
    }
}

} // namespace airfix::render
