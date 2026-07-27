#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyScreenPoint final {
    float x{};
    float y{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyScreenPoint&,
        const LegacyScreenPoint&) noexcept = default;
};

struct LegacyScreenProjectionConfig final {
    float nearDistance{1.0F};
    float farDistance{100.0F};
    float horizontalFovDegrees{90.0F};
    float windowWidth{1.0F};
    LegacyScreenPoint centre{0.5F, 0.5F};
};

enum class LegacyScreenProjectionBuildIssueKind : std::uint8_t {
    nonFiniteConfig,
    nearDistanceNotPositive,
    farDistanceNotGreaterThanNear,
    windowWidthNotPositive,
    nonFiniteDerivedValue,
};

struct LegacyScreenProjectionBuildIssue final {
    LegacyScreenProjectionBuildIssueKind kind{
        LegacyScreenProjectionBuildIssueKind::nonFiniteConfig};
};

enum class LegacyScreenProjectionProjectIssueKind : std::uint8_t {
    nonFiniteInput,
    nonFiniteOutput,
};

struct LegacyScreenProjectionProjectIssue final {
    LegacyScreenProjectionProjectIssueKind kind{
        LegacyScreenProjectionProjectIssueKind::nonFiniteInput};
};

struct LegacyScreenProjectedPoint final {
    LegacyScreenPoint point{};
    float cameraSpaceZ{};
    float legacyRhw{};
    bool usedNearFallback{};
};

struct LegacyScreenProjectionProjectResult final {
    std::optional<LegacyScreenProjectedPoint> projected;
    std::optional<LegacyScreenProjectionProjectIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return projected.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

class LegacyScreenProjection;

struct LegacyScreenProjectionBuildResult;

[[nodiscard]] LegacyScreenProjectionBuildResult
buildLegacyScreenProjection(
    const LegacyScreenProjectionConfig& config = {}) noexcept;

// Immutable, backend-neutral reconstruction of the legacy screen projection.
// It deliberately performs no far clipping, view transform, NDC conversion,
// or graphics-API matrix construction.
class LegacyScreenProjection final {
public:
    [[nodiscard]] LegacyScreenProjectionProjectResult project(
        const Vec3& cameraSpacePosition) const noexcept;

    [[nodiscard]] constexpr float nearDistance() const noexcept {
        return nearDistance_;
    }

    [[nodiscard]] constexpr float farDistance() const noexcept {
        return farDistance_;
    }

    [[nodiscard]] constexpr float horizontalFovDegrees() const noexcept {
        return horizontalFovDegrees_;
    }

    [[nodiscard]] constexpr float windowWidth() const noexcept {
        return windowWidth_;
    }

    [[nodiscard]] constexpr LegacyScreenPoint centre() const noexcept {
        return centre_;
    }

    [[nodiscard]] constexpr float focalLength() const noexcept {
        return focalLength_;
    }

    [[nodiscard]] constexpr float projectScale() const noexcept {
        return projectScale_;
    }

private:
    friend LegacyScreenProjectionBuildResult
    buildLegacyScreenProjection(
        const LegacyScreenProjectionConfig& config) noexcept;

    constexpr LegacyScreenProjection(
        const float nearDistance,
        const float farDistance,
        const float horizontalFovDegrees,
        const float windowWidth,
        const LegacyScreenPoint centre,
        const float focalLength,
        const float projectScale) noexcept
        : nearDistance_(nearDistance),
          farDistance_(farDistance),
          horizontalFovDegrees_(horizontalFovDegrees),
          windowWidth_(windowWidth),
          centre_(centre),
          focalLength_(focalLength),
          projectScale_(projectScale) {}

    const float nearDistance_;
    const float farDistance_;
    const float horizontalFovDegrees_;
    const float windowWidth_;
    const LegacyScreenPoint centre_;
    const float focalLength_;
    const float projectScale_;
};

struct LegacyScreenProjectionBuildResult final {
    std::optional<LegacyScreenProjection> projection;
    std::optional<LegacyScreenProjectionBuildIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return projection.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

} // namespace airfix::render
