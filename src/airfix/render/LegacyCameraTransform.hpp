#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyCameraTransformConfig final {
    Mat3 linear{};
    Vec3 translation{};
    float uniformScale{1.0F};
    float inverseScaleSquared{1.0F};
};

enum class LegacyCameraTransformBuildIssueKind : std::uint8_t {
    nonFiniteConfig,
    zeroUniformScale,
    scaleOutsideValidatedRange,
    inverseScaleSquaredNotPositive,
    nonOrthogonalLinearColumns,
    linearColumnLengthMismatch,
    inverseScaleSquaredMismatch,
    nonFiniteDerivedValue,
};

struct LegacyCameraTransformBuildIssue final {
    LegacyCameraTransformBuildIssueKind kind{
        LegacyCameraTransformBuildIssueKind::nonFiniteConfig};
};

enum class LegacyCameraTransformIssueKind : std::uint8_t {
    nonFiniteInput,
    nonFiniteOutput,
};

struct LegacyCameraTransformIssue final {
    LegacyCameraTransformIssueKind kind{
        LegacyCameraTransformIssueKind::nonFiniteInput};
};

struct LegacyCameraTransformResult final {
    std::optional<Vec3> cameraSpacePosition;
    std::optional<LegacyCameraTransformIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return cameraSpacePosition.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

class LegacyCameraTransform;

struct LegacyCameraTransformBuildResult;

[[nodiscard]] LegacyCameraTransformBuildResult
buildLegacyCameraTransform(
    const LegacyCameraTransformConfig& config = {}) noexcept;

// Immutable reconstruction of the legacy world-to-camera relation:
// delta = world - translation, rotated = applyLegacyRow(linear, delta),
// camera = rotated * inverseScaleSquared. It intentionally retains those
// operations instead of publishing a precomposed affine transform.
class LegacyCameraTransform final {
public:
    [[nodiscard]] LegacyCameraTransformResult transform(
        const Vec3& worldPosition) const noexcept;

    [[nodiscard]] constexpr Mat3 linear() const noexcept {
        return linear_;
    }

    [[nodiscard]] constexpr Vec3 translation() const noexcept {
        return translation_;
    }

    [[nodiscard]] constexpr float uniformScale() const noexcept {
        return uniformScale_;
    }

    [[nodiscard]] constexpr float inverseScaleSquared() const noexcept {
        return inverseScaleSquared_;
    }

private:
    friend LegacyCameraTransformBuildResult buildLegacyCameraTransform(
        const LegacyCameraTransformConfig& config) noexcept;

    constexpr LegacyCameraTransform(
        const Mat3& linear,
        const Vec3 translation,
        const float uniformScale,
        const float inverseScaleSquared) noexcept
        : linear_(linear),
          translation_(translation),
          uniformScale_(uniformScale),
          inverseScaleSquared_(inverseScaleSquared) {}

    const Mat3 linear_;
    const Vec3 translation_;
    const float uniformScale_;
    const float inverseScaleSquared_;
};

struct LegacyCameraTransformBuildResult final {
    std::optional<LegacyCameraTransform> transform;
    std::optional<LegacyCameraTransformBuildIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return transform.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

} // namespace airfix::render
