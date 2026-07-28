#include "airfix/render/LegacyGameplayCameraPoseSnapshot.hpp"

#include <cmath>

namespace airfix::render {
namespace {

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool validFrame(
    const LegacyGameplayCameraFrameSnapshot& frame) noexcept {
    return frame.publicationGeneration != 0U &&
           finite(frame.state.roomState.runtimeWorldPosition) &&
           finite(frame.state.axisFactors) &&
           frame.state.axisFactors.x >= 0.0F &&
           frame.state.axisFactors.y >= 0.0F &&
           frame.state.axisFactors.z >= 0.0F;
}

[[nodiscard]] LegacyGameplayCameraPoseBuildResult failure(
    const LegacyGameplayCameraPoseBuildIssueKind kind,
    const std::optional<LegacyCameraTransformBuildIssue> transformIssue =
        std::nullopt,
    const std::optional<LegacyScreenProjectionBuildIssue> projectionIssue =
        std::nullopt) noexcept {
    return {
        .snapshot = std::nullopt,
        .issue =
            LegacyGameplayCameraPoseBuildIssue{
                .kind = kind,
                .transformIssue = transformIssue,
                .projectionIssue = projectionIssue,
            },
    };
}

} // namespace

LegacyGameplayCameraPoseBuildResult buildLegacyGameplayCameraPoseSnapshot(
    const LegacyGameplayCameraFrameSnapshot& frame,
    const Vec3& vehicleWorldAnchor,
    const LegacyGameplayCameraPoseConfig& config) noexcept {
    if (!validFrame(frame)) {
        return failure(
            LegacyGameplayCameraPoseBuildIssueKind::invalidFrameSnapshot);
    }
    if (!finite(vehicleWorldAnchor)) {
        return failure(
            LegacyGameplayCameraPoseBuildIssueKind::invalidVehicleWorldAnchor);
    }

    const auto lookAt = legacyGameplayCameraLookAt(
        vehicleWorldAnchor, frame.state.roomState.runtimeWorldPosition);
    if (!lookAt.has_value()) {
        return failure(LegacyGameplayCameraPoseBuildIssueKind::lookAtFailed);
    }

    const auto transform = buildLegacyCameraTransform({
        .linear = lookAt->cameraWorldLinear,
        .translation = frame.state.roomState.runtimeWorldPosition,
        .uniformScale = 1.0F,
        .inverseScaleSquared = 1.0F,
    });
    if (!transform.complete()) {
        return failure(
            LegacyGameplayCameraPoseBuildIssueKind::transformBuildFailed,
            transform.issue);
    }

    const auto projection = buildLegacyScreenProjection(config.projection);
    if (!projection.complete()) {
        return failure(
            LegacyGameplayCameraPoseBuildIssueKind::projectionBuildFailed,
            std::nullopt,
            projection.issue);
    }

    const auto anchorCameraSpace =
        transform.transform->transform(vehicleWorldAnchor);
    if (!anchorCameraSpace.complete() ||
        !(anchorCameraSpace.cameraSpacePosition->z > 0.0F)) {
        return failure(
            LegacyGameplayCameraPoseBuildIssueKind::anchorTransformFailed);
    }

    return {
        .snapshot =
            LegacyGameplayCameraPoseSnapshot{
                frame,
                vehicleWorldAnchor,
                *lookAt,
                *transform.transform,
                *projection.projection,
                *anchorCameraSpace.cameraSpacePosition,
            },
        .issue = std::nullopt,
    };
}

LegacyGameplayCameraWorldProjectionResult
LegacyGameplayCameraPoseSnapshot::project(
    const Vec3& worldPosition) const noexcept {
    const auto transformed = worldToView_.transform(worldPosition);
    if (!transformed.complete()) {
        return {
            .point = std::nullopt,
            .issue =
                LegacyGameplayCameraWorldProjectionIssue{
                    .kind = LegacyGameplayCameraWorldProjectionIssueKind::
                        transformFailed,
                    .transformIssue = transformed.issue,
                    .projectionIssue = std::nullopt,
                },
        };
    }

    const auto projected =
        projection_.project(*transformed.cameraSpacePosition);
    if (!projected.complete()) {
        return {
            .point = std::nullopt,
            .issue =
                LegacyGameplayCameraWorldProjectionIssue{
                    .kind = LegacyGameplayCameraWorldProjectionIssueKind::
                        projectionFailed,
                    .transformIssue = std::nullopt,
                    .projectionIssue = projected.issue,
                },
        };
    }

    return {
        .point =
            LegacyGameplayCameraProjectedWorldPoint{
                .cameraSpacePosition = *transformed.cameraSpacePosition,
                .projected = *projected.projected,
            },
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
