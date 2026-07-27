#include "airfix/render/PlayerActorPoseRuntime.hpp"

#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace airfix::render {
namespace {

[[nodiscard]] PlayerActorPoseRuntimeBuildResult buildFailure(
    const PlayerActorPoseRuntimeBuildIssueKind kind,
    const std::optional<PlayerActorPoseFrameIssueKind> frameIssue =
        std::nullopt,
    const std::optional<DynamicInstancePosePublishResult> exchangeResult =
        std::nullopt) {
    return {
        .runtime = nullptr,
        .issue =
            PlayerActorPoseRuntimeBuildIssue{
                .kind = kind,
                .frameIssue = frameIssue,
                .exchangeResult = exchangeResult,
            },
        .retainedBytes = 0U,
    };
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

[[nodiscard]] ConvertedNodeTransform composeUnchecked(
    const ConvertedNodeTransform& actorWorld,
    const ConvertedNodeTransform& actorLocal) noexcept {
    const auto rotatedTranslation =
        applyRuntimeColumn(actorWorld.linear, actorLocal.translation);
    return {
        .linear = multiply(actorWorld.linear, actorLocal.linear),
        .translation =
            {
                rotatedTranslation.x + actorWorld.translation.x,
                rotatedTranslation.y + actorWorld.translation.y,
                rotatedTranslation.z + actorWorld.translation.z,
            },
        .rawScalar = actorLocal.rawScalar,
    };
}

[[nodiscard]] std::optional<std::size_t> retainedBytesFor(
    const std::size_t sourceCount,
    const std::size_t frameBytes) noexcept {
    std::size_t sourceBytes = 0U;
    std::size_t exchangeBytes = 0U;
    std::size_t retainedBytes = 0U;
    if (!checkedMultiply(
            sourceCount,
            sizeof(PlayerActorPoseRuntimeSource),
            sourceBytes) ||
        !checkedMultiply(frameBytes, 2U, exchangeBytes) ||
        !checkedAdd(sourceBytes, frameBytes, retainedBytes) ||
        !checkedAdd(retainedBytes, exchangeBytes, retainedBytes)) {
        return std::nullopt;
    }
    return retainedBytes;
}

} // namespace

PlayerActorPoseRuntime::PlayerActorPoseRuntime(
    std::vector<PlayerActorPoseRuntimeSource> sources,
    std::vector<DynamicInstancePoseOverride> scratch,
    const DynamicInstancePoseLimits& exactLimits,
    const std::size_t retainedBytes)
    : sources_(std::move(sources)),
      scratch_(std::move(scratch)),
      exchange_(exactLimits),
      retainedBytes_(retainedBytes) {}

PlayerActorPoseRuntimeBuildResult PlayerActorPoseRuntime::create(
    const std::optional<PlayerActorSceneBinding>& actorBinding,
    const std::span<const PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance,
    const std::size_t totalInstanceCount,
    const ConvertedNodeTransform& initialActorWorld,
    const std::uint64_t initialSimulationStep,
    const DynamicInstancePoseLimits& exactLimits) {
    if (!actorBinding.has_value()) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::missingActorBinding);
    }
    if (actorBinding->instanceCount == 0U ||
        actorInstanceProvenance.empty()) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::emptyActor);
    }
    if (totalInstanceCount >
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max())) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::
                totalInstanceCountNotRepresentable);
    }
    if (totalInstanceCount !=
        static_cast<std::size_t>(exactLimits.maximumInstances)) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::instanceLimitNotExact);
    }
    if (actorBinding->instanceCount != exactLimits.maximumOverrides) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::overrideLimitNotExact);
    }

    std::size_t frameBytes = 0U;
    if (!checkedMultiply(
            actorBinding->instanceCount,
            sizeof(DynamicInstancePoseOverride),
            frameBytes)) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::frameByteSizeOverflow);
    }
    if (frameBytes != exactLimits.maximumFrameBytes) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::frameByteLimitNotExact);
    }
    const auto retainedBytes = retainedBytesFor(
        actorBinding->instanceCount, frameBytes);
    if (!retainedBytes.has_value()) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::retainedByteSizeOverflow);
    }

    try {
        auto initialFrame = buildPlayerActorPoseFrame(
            actorBinding,
            actorInstanceProvenance,
            initialActorWorld,
            initialSimulationStep,
            exactLimits);
        if (!initialFrame.complete()) {
            return buildFailure(
                PlayerActorPoseRuntimeBuildIssueKind::initialFrameFailure,
                initialFrame.issues.empty()
                    ? std::nullopt
                    : std::optional<PlayerActorPoseFrameIssueKind>{
                          initialFrame.issues.front().kind});
        }

        std::vector<PlayerActorPoseRuntimeSource> sources;
        sources.reserve(actorInstanceProvenance.size());
        for (const auto& provenance : actorInstanceProvenance) {
            sources.push_back({
                .finalInstanceIndex = provenance.finalInstanceIndex,
                .actorLocal = provenance.actorLocal,
            });
        }

        auto runtime = std::unique_ptr<PlayerActorPoseRuntime>(
            new PlayerActorPoseRuntime(
                std::move(sources),
                std::move(initialFrame.overrides),
                exactLimits,
                *retainedBytes));
        auto scene =
            runtime->exchange_.tryBeginScene(exactLimits.maximumInstances);
        if (!scene.has_value()) {
            return buildFailure(
                PlayerActorPoseRuntimeBuildIssueKind::initialSceneFailure);
        }
        runtime->scene_.emplace(std::move(*scene));
        const auto initialPublish = runtime->exchange_.tryPublish(
            *runtime->scene_,
            {
                .simulationStep = initialSimulationStep,
                .overrides = runtime->scratch_,
            });
        if (initialPublish != DynamicInstancePosePublishResult::published) {
            return buildFailure(
                PlayerActorPoseRuntimeBuildIssueKind::initialPublishFailure,
                std::nullopt,
                initialPublish);
        }
        return {
            .runtime = std::move(runtime),
            .issue = std::nullopt,
            .retainedBytes = *retainedBytes,
        };
    } catch (const std::bad_alloc&) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::allocationFailure);
    } catch (const std::length_error&) {
        return buildFailure(
            PlayerActorPoseRuntimeBuildIssueKind::allocationFailure);
    }
}

PlayerActorPoseRuntimePublishOutcome
PlayerActorPoseRuntime::tryPublish(
    const ConvertedNodeTransform& actorWorld,
    const std::uint64_t simulationStep) noexcept {
    if (const auto error = transformError(actorWorld);
        error.has_value()) {
        return {
            .result =
                PlayerActorPoseRuntimePublishResult::invalidActorWorld,
            .actorInstanceIndex = std::nullopt,
            .geometryError = error,
        };
    }

    for (std::size_t index = 0U; index < sources_.size(); ++index) {
        const auto composed =
            composeUnchecked(actorWorld, sources_[index].actorLocal);
        if (const auto error = transformError(composed);
            error.has_value()) {
            return {
                .result =
                    PlayerActorPoseRuntimePublishResult::
                        invalidComposition,
                .actorInstanceIndex = index,
                .geometryError = error,
            };
        }
        scratch_[index] = {
            .instanceIndex = sources_[index].finalInstanceIndex,
            .modelLinear = composed.linear,
            .modelTranslation = composed.translation,
        };
    }

    if (!scene_.has_value()) {
        return {
            .result =
                PlayerActorPoseRuntimePublishResult::wrongOrStaleScene,
            .actorInstanceIndex = std::nullopt,
            .geometryError = std::nullopt,
        };
    }
    const auto exchangeResult = exchange_.tryPublish(
        *scene_,
        {
            .simulationStep = simulationStep,
            .overrides = scratch_,
        });
    return {
        .result =
            mapPlayerActorPoseRuntimePublishResult(exchangeResult),
        .actorInstanceIndex = std::nullopt,
        .geometryError = std::nullopt,
    };
}

std::optional<DynamicInstancePoseLease>
PlayerActorPoseRuntime::tryAcquire() noexcept {
    if (!scene_.has_value()) {
        return std::nullopt;
    }
    return exchange_.tryAcquire(*scene_);
}

} // namespace airfix::render
