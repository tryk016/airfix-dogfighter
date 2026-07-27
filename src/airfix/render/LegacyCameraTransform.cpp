#include "airfix/render/LegacyCameraTransform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

constexpr double relativeTolerance = 1.0e-4;
constexpr double absoluteTolerance = 1.0e-6;

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] bool finite(
    const LegacyCameraTransformConfig& config) noexcept {
    return finite(config.linear) && finite(config.translation) &&
        std::isfinite(config.uniformScale) &&
        std::isfinite(config.inverseScaleSquared);
}

[[nodiscard]] double dotDouble(
    const Vec3& left,
    const Vec3& right) noexcept {
    return static_cast<double>(left.x) *
            static_cast<double>(right.x) +
        static_cast<double>(left.y) *
            static_cast<double>(right.y) +
        static_cast<double>(left.z) *
            static_cast<double>(right.z);
}

[[nodiscard]] bool withinTolerance(
    const double actual,
    const double expected) noexcept {
    const double tolerance = std::max(
        absoluteTolerance,
        relativeTolerance *
            std::max(std::abs(actual), std::abs(expected)));
    return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyCameraTransformBuildResult buildFailure(
    const LegacyCameraTransformBuildIssueKind kind) noexcept {
    return {
        .transform = std::nullopt,
        .issue = LegacyCameraTransformBuildIssue{.kind = kind},
    };
}

[[nodiscard]] LegacyCameraTransformResult transformFailure(
    const LegacyCameraTransformIssueKind kind) noexcept {
    return {
        .cameraSpacePosition = std::nullopt,
        .issue = LegacyCameraTransformIssue{.kind = kind},
    };
}

} // namespace

LegacyCameraTransformBuildResult buildLegacyCameraTransform(
    const LegacyCameraTransformConfig& config) noexcept {
    if (!finite(config)) {
        return buildFailure(
            LegacyCameraTransformBuildIssueKind::nonFiniteConfig);
    }
    if (config.uniformScale == 0.0F) {
        return buildFailure(
            LegacyCameraTransformBuildIssueKind::zeroUniformScale);
    }
    if (!(config.inverseScaleSquared > 0.0F)) {
        return buildFailure(
            LegacyCameraTransformBuildIssueKind::
                inverseScaleSquaredNotPositive);
    }

    std::array<std::array<double, 3U>, 3U> gram{};
    for (std::size_t row = 0U; row < gram.size(); ++row) {
        for (std::size_t column = 0U;
             column < gram[row].size();
             ++column) {
            gram[row][column] = dotDouble(
                config.linear.columns[row],
                config.linear.columns[column]);
        }
    }

    const double scale = static_cast<double>(config.uniformScale);
    const double scaleSquared = scale * scale;
    if (!std::isfinite(scaleSquared) || !(scaleSquared > 0.0)) {
        return buildFailure(
            LegacyCameraTransformBuildIssueKind::
                nonFiniteDerivedValue);
    }
    const double mathematicalInverseScaleSquared =
        1.0 / scaleSquared;
    constexpr double minimumNormalFloat =
        static_cast<double>(std::numeric_limits<float>::min());
    constexpr double maximumFloat =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(mathematicalInverseScaleSquared) ||
        mathematicalInverseScaleSquared < minimumNormalFloat ||
        mathematicalInverseScaleSquared > maximumFloat) {
        return buildFailure(
            LegacyCameraTransformBuildIssueKind::
                scaleOutsideValidatedRange);
    }

    std::array<std::array<double, 3U>, 3U> normalizedGram{};
    for (std::size_t row = 0U;
         row < normalizedGram.size();
         ++row) {
        for (std::size_t column = 0U;
             column < normalizedGram[row].size();
             ++column) {
            normalizedGram[row][column] =
                gram[row][column] / scaleSquared;
            if (!std::isfinite(normalizedGram[row][column])) {
                return buildFailure(
                    LegacyCameraTransformBuildIssueKind::
                        nonFiniteDerivedValue);
            }
        }
    }

    for (std::size_t row = 0U;
         row < normalizedGram.size();
         ++row) {
        for (std::size_t column = row + 1U;
             column < normalizedGram[row].size();
             ++column) {
            if (!withinTolerance(
                    normalizedGram[row][column], 0.0)) {
                return buildFailure(
                    LegacyCameraTransformBuildIssueKind::
                        nonOrthogonalLinearColumns);
            }
        }
    }

    for (std::size_t index = 0U;
         index < normalizedGram.size();
         ++index) {
        if (!withinTolerance(
                normalizedGram[index][index], 1.0)) {
            return buildFailure(
                LegacyCameraTransformBuildIssueKind::
                    linearColumnLengthMismatch);
        }
    }

    const double recoveredUnitScale =
        static_cast<double>(config.inverseScaleSquared) *
        scaleSquared;
    if (!std::isfinite(recoveredUnitScale)) {
        return buildFailure(
            LegacyCameraTransformBuildIssueKind::
                nonFiniteDerivedValue);
    }
    if (!withinTolerance(recoveredUnitScale, 1.0)) {
        return buildFailure(
            LegacyCameraTransformBuildIssueKind::
                inverseScaleSquaredMismatch);
    }

    return {
        .transform = LegacyCameraTransform{
            config.linear,
            config.translation,
            config.uniformScale,
            config.inverseScaleSquared},
        .issue = std::nullopt,
    };
}

LegacyCameraTransformResult LegacyCameraTransform::transform(
    const Vec3& worldPosition) const noexcept {
    if (!finite(worldPosition)) {
        return transformFailure(
            LegacyCameraTransformIssueKind::nonFiniteInput);
    }

    const Vec3 delta{
        worldPosition.x - translation_.x,
        worldPosition.y - translation_.y,
        worldPosition.z - translation_.z,
    };
    const Vec3 rotated = applyLegacyRow(linear_, delta);
    const Vec3 cameraSpace{
        rotated.x * inverseScaleSquared_,
        rotated.y * inverseScaleSquared_,
        rotated.z * inverseScaleSquared_,
    };
    if (!finite(delta) || !finite(rotated) || !finite(cameraSpace)) {
        return transformFailure(
            LegacyCameraTransformIssueKind::nonFiniteOutput);
    }

    return {
        .cameraSpacePosition = cameraSpace,
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
