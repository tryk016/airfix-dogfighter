#include "airfix/render/PlayerActorPoseFrame.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    PlayerActorPoseFrame& result,
    const PlayerActorPoseFrameIssueKind kind,
    const std::optional<std::size_t> actorInstanceIndex = std::nullopt,
    const std::optional<std::size_t> expectedFinalInstanceIndex =
        std::nullopt,
    const std::optional<std::uint32_t> actualFinalInstanceIndex =
        std::nullopt,
    const std::optional<GeometryErrorCode> geometryError = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .actorInstanceIndex = actorInstanceIndex,
        .expectedFinalInstanceIndex = expectedFinalInstanceIndex,
        .actualFinalInstanceIndex = actualFinalInstanceIndex,
        .geometryError = geometryError,
    });
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] std::optional<GeometryErrorCode> transformError(
    const ConvertedNodeTransform& transform) noexcept {
    if (!finite(transform.linear) || !finite(transform.translation) ||
        !std::isfinite(transform.rawScalar)) {
        return GeometryErrorCode::nonFiniteValue;
    }
    const auto linearDeterminant = determinant(transform.linear);
    if (!std::isfinite(linearDeterminant)) {
        return GeometryErrorCode::nonFiniteValue;
    }
    if (linearDeterminant == 0.0F) {
        return GeometryErrorCode::singularTransform;
    }
    return std::nullopt;
}

} // namespace

PlayerActorPoseFrame buildPlayerActorPoseFrame(
    const std::optional<PlayerActorSceneBinding>& actorBinding,
    const std::span<const PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance,
    const ConvertedNodeTransform& actorWorld,
    const std::uint64_t simulationStep,
    const DynamicInstancePoseLimits& limits) {
    PlayerActorPoseFrame result;
    result.simulationStep = simulationStep;

    if (!actorBinding.has_value()) {
        addIssue(
            result, PlayerActorPoseFrameIssueKind::missingActorBinding);
        return result;
    }

    const auto& binding = *actorBinding;
    if (binding.meshCount == 0U || binding.instanceCount == 0U) {
        addIssue(result, PlayerActorPoseFrameIssueKind::emptyActorBinding);
        return result;
    }
    if (binding.instanceCount != actorInstanceProvenance.size()) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::bindingInstanceCountMismatch);
        return result;
    }

    std::size_t meshRangeEnd = 0U;
    if (!checkedAdd(
            binding.firstMeshSlot, binding.meshCount, meshRangeEnd)) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::meshBindingRangeOverflow);
        return result;
    }

    std::size_t instanceRangeEnd = 0U;
    if (!checkedAdd(
            binding.firstInstanceIndex,
            binding.instanceCount,
            instanceRangeEnd)) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::instanceBindingRangeOverflow);
        return result;
    }

    const auto maximumUint32 =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (binding.firstInstanceIndex > maximumUint32 ||
        instanceRangeEnd == 0U ||
        instanceRangeEnd - 1U > maximumUint32) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::instanceIndexNotRepresentable);
        return result;
    }
    if (instanceRangeEnd >
        static_cast<std::size_t>(limits.maximumInstances)) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::instanceCeilingExceeded);
        return result;
    }
    if (binding.instanceCount > limits.maximumOverrides) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::overrideLimitExceeded);
        return result;
    }

    std::size_t frameBytes = 0U;
    if (!checkedMultiply(
            binding.instanceCount,
            sizeof(DynamicInstancePoseOverride),
            frameBytes)) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::frameByteSizeOverflow);
        return result;
    }
    if (frameBytes > limits.maximumFrameBytes) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::frameByteLimitExceeded);
        return result;
    }

    if (const auto error = transformError(actorWorld);
        error.has_value()) {
        addIssue(
            result,
            PlayerActorPoseFrameIssueKind::invalidActorWorldTransform,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            error);
        return result;
    }

    for (std::size_t index = 0U;
         index < actorInstanceProvenance.size();
         ++index) {
        std::size_t expectedFinalInstanceIndex = 0U;
        if (!checkedAdd(
                binding.firstInstanceIndex,
                index,
                expectedFinalInstanceIndex)) {
            addIssue(
                result,
                PlayerActorPoseFrameIssueKind::instanceBindingRangeOverflow,
                index);
            return result;
        }

        const auto& provenance = actorInstanceProvenance[index];
        if (static_cast<std::size_t>(provenance.finalInstanceIndex) !=
            expectedFinalInstanceIndex) {
            addIssue(
                result,
                PlayerActorPoseFrameIssueKind::
                    provenanceInstanceIndexMismatch,
                index,
                expectedFinalInstanceIndex,
                provenance.finalInstanceIndex);
            return result;
        }
        if (const auto error = transformError(provenance.actorLocal);
            error.has_value()) {
            addIssue(
                result,
                PlayerActorPoseFrameIssueKind::invalidActorLocalTransform,
                index,
                expectedFinalInstanceIndex,
                provenance.finalInstanceIndex,
                error);
            return result;
        }
    }

    std::vector<DynamicInstancePoseOverride> overrides;
    overrides.reserve(binding.instanceCount);
    for (std::size_t index = 0U;
         index < actorInstanceProvenance.size();
         ++index) {
        const auto& provenance = actorInstanceProvenance[index];
        try {
            const auto absolute = composeNodeTransforms(
                actorWorld, provenance.actorLocal);
            overrides.push_back({
                .instanceIndex = provenance.finalInstanceIndex,
                .modelLinear = absolute.linear,
                .modelTranslation = absolute.translation,
            });
        } catch (const GeometryError& error) {
            addIssue(
                result,
                PlayerActorPoseFrameIssueKind::invalidComposedTransform,
                index,
                static_cast<std::size_t>(
                    provenance.finalInstanceIndex),
                provenance.finalInstanceIndex,
                error.code());
            return result;
        }
    }

    result.overrides = std::move(overrides);
    return result;
}

} // namespace airfix::render
