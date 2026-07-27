#include "airfix/render/LegacyScreenProjection.hpp"

#include <algorithm>
#include <cmath>

namespace airfix::render {
namespace {

constexpr float minimumHorizontalFovDegrees = 1.0F;
constexpr float maximumHorizontalFovDegrees = 175.0F;
constexpr float halfRadiansPerDegree = 0.008726646259971648F;

[[nodiscard]] bool finite(
    const LegacyScreenProjectionConfig& config) noexcept {
    return std::isfinite(config.nearDistance) &&
        std::isfinite(config.farDistance) &&
        std::isfinite(config.horizontalFovDegrees) &&
        std::isfinite(config.windowWidth) &&
        std::isfinite(config.centre.x) &&
        std::isfinite(config.centre.y);
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] LegacyScreenProjectionBuildResult buildFailure(
    const LegacyScreenProjectionBuildIssueKind kind) noexcept {
    return {
        .projection = std::nullopt,
        .issue = LegacyScreenProjectionBuildIssue{.kind = kind},
    };
}

[[nodiscard]] LegacyScreenProjectionProjectResult projectFailure(
    const LegacyScreenProjectionProjectIssueKind kind) noexcept {
    return {
        .projected = std::nullopt,
        .issue = LegacyScreenProjectionProjectIssue{.kind = kind},
    };
}

} // namespace

LegacyScreenProjectionBuildResult buildLegacyScreenProjection(
    const LegacyScreenProjectionConfig& config) noexcept {
    if (!finite(config)) {
        return buildFailure(
            LegacyScreenProjectionBuildIssueKind::nonFiniteConfig);
    }
    if (!(config.nearDistance > 0.0F)) {
        return buildFailure(
            LegacyScreenProjectionBuildIssueKind::
                nearDistanceNotPositive);
    }
    if (!(config.farDistance > config.nearDistance)) {
        return buildFailure(
            LegacyScreenProjectionBuildIssueKind::
                farDistanceNotGreaterThanNear);
    }
    if (!(config.windowWidth > 0.0F)) {
        return buildFailure(
            LegacyScreenProjectionBuildIssueKind::
                windowWidthNotPositive);
    }

    const float clampedHorizontalFov = std::clamp(
        config.horizontalFovDegrees,
        minimumHorizontalFovDegrees,
        maximumHorizontalFovDegrees);
    const float halfAngleRadians =
        clampedHorizontalFov * halfRadiansPerDegree;
    const float tangent = std::tan(halfAngleRadians);
    const float focalLength =
        (0.5F * config.windowWidth) / tangent;
    const float projectScale =
        focalLength / config.nearDistance;
    if (!std::isfinite(tangent) || !(tangent > 0.0F) ||
        !std::isfinite(focalLength) ||
        !std::isfinite(projectScale)) {
        return buildFailure(
            LegacyScreenProjectionBuildIssueKind::
                nonFiniteDerivedValue);
    }

    return {
        .projection = LegacyScreenProjection{
            config.nearDistance,
            config.farDistance,
            clampedHorizontalFov,
            config.windowWidth,
            config.centre,
            focalLength,
            projectScale},
        .issue = std::nullopt,
    };
}

LegacyScreenProjectionProjectResult LegacyScreenProjection::project(
    const Vec3& cameraSpacePosition) const noexcept {
    if (!finite(cameraSpacePosition)) {
        return projectFailure(
            LegacyScreenProjectionProjectIssueKind::nonFiniteInput);
    }

    const bool usedNearFallback =
        cameraSpacePosition.z <= nearDistance_;
    float factor = 1.0F;
    if (!usedNearFallback) {
        factor = nearDistance_ / cameraSpacePosition.z;
    }
    const float scale = projectScale_ * factor;
    const float screenX =
        scale * cameraSpacePosition.x + centre_.x;
    const float screenY =
        centre_.y - scale * cameraSpacePosition.y;
    if (!std::isfinite(scale) || !std::isfinite(screenX) ||
        !std::isfinite(screenY)) {
        return projectFailure(
            LegacyScreenProjectionProjectIssueKind::nonFiniteOutput);
    }

    return {
        .projected = LegacyScreenProjectedPoint{
            .point = {.x = screenX, .y = screenY},
            .usedNearFallback = usedNearFallback,
        },
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
