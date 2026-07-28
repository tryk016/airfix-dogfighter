#pragma once

#include "airfix/render/LegacyCameraTransform.hpp"
#include "airfix/render/LegacyGameplayCameraStateOwner.hpp"
#include "airfix/render/LegacyScreenProjection.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyGameplayCameraPoseConfig final {
    LegacyScreenProjectionConfig projection{
        .nearDistance = 0.25F,
        .farDistance = 200.0F,
        .horizontalFovDegrees = 90.0F,
        .windowWidth = 640.0F,
        .centre = {320.0F, 240.0F},
    };
};

enum class LegacyGameplayCameraPoseBuildIssueKind : std::uint8_t {
    invalidFrameSnapshot,
    invalidVehicleWorldAnchor,
    lookAtFailed,
    transformBuildFailed,
    projectionBuildFailed,
    anchorTransformFailed,
};

struct LegacyGameplayCameraPoseBuildIssue final {
    LegacyGameplayCameraPoseBuildIssueKind kind{
        LegacyGameplayCameraPoseBuildIssueKind::invalidFrameSnapshot};
    std::optional<LegacyCameraTransformBuildIssue> transformIssue;
    std::optional<LegacyScreenProjectionBuildIssue> projectionIssue;
};

enum class LegacyGameplayCameraWorldProjectionIssueKind : std::uint8_t {
    transformFailed,
    projectionFailed,
};

struct LegacyGameplayCameraWorldProjectionIssue final {
    LegacyGameplayCameraWorldProjectionIssueKind kind{
        LegacyGameplayCameraWorldProjectionIssueKind::transformFailed};
    std::optional<LegacyCameraTransformIssue> transformIssue;
    std::optional<LegacyScreenProjectionProjectIssue> projectionIssue;
};

struct LegacyGameplayCameraProjectedWorldPoint final {
    Vec3 cameraSpacePosition{};
    LegacyScreenProjectedPoint projected{};
};

struct LegacyGameplayCameraWorldProjectionResult final {
    std::optional<LegacyGameplayCameraProjectedWorldPoint> point;
    std::optional<LegacyGameplayCameraWorldProjectionIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return point.has_value() && !issue.has_value();
    }
};

class LegacyGameplayCameraPoseSnapshot;

struct LegacyGameplayCameraPoseBuildResult;

// Creates one immutable, backend-neutral render snapshot from one acquired
// camera-state generation and the matching vehicle anchor. The recovered
// event-5 look-at resets the camera SRT to unit scale/q, so no caller-supplied
// transform scale is accepted. Projection remains explicit and defaults to
// the gameplay camera's 0.25/200/90 and logical 640x480 full-screen values.
[[nodiscard]] LegacyGameplayCameraPoseBuildResult
buildLegacyGameplayCameraPoseSnapshot(
    const LegacyGameplayCameraFrameSnapshot& frame,
    const Vec3& vehicleWorldAnchor,
    const LegacyGameplayCameraPoseConfig& config = {}) noexcept;

class LegacyGameplayCameraPoseSnapshot final {
public:
    [[nodiscard]] const LegacyGameplayCameraFrameSnapshot&
    frame() const noexcept {
        return frame_;
    }

    [[nodiscard]] constexpr Vec3 vehicleWorldAnchor() const noexcept {
        return vehicleWorldAnchor_;
    }

    [[nodiscard]] const LegacyGameplayCameraLookAt& lookAt() const noexcept {
        return lookAt_;
    }

    [[nodiscard]] const LegacyCameraTransform& worldToView() const noexcept {
        return worldToView_;
    }

    [[nodiscard]] const LegacyScreenProjection& projection() const noexcept {
        return projection_;
    }

    [[nodiscard]] constexpr Vec3 anchorCameraSpacePosition() const noexcept {
        return anchorCameraSpacePosition_;
    }

    // Preserves the reconstructed operation boundary: subtract/rotate/q first,
    // then legacy screen projection. It does not add clipping, NDC, Metal
    // matrices, or presentation aspect fitting.
    [[nodiscard]] LegacyGameplayCameraWorldProjectionResult project(
        const Vec3& worldPosition) const noexcept;

private:
    friend LegacyGameplayCameraPoseBuildResult
    buildLegacyGameplayCameraPoseSnapshot(
        const LegacyGameplayCameraFrameSnapshot& frame,
        const Vec3& vehicleWorldAnchor,
        const LegacyGameplayCameraPoseConfig& config) noexcept;

    LegacyGameplayCameraPoseSnapshot(
        const LegacyGameplayCameraFrameSnapshot& frame,
        const Vec3 vehicleWorldAnchor,
        const LegacyGameplayCameraLookAt& lookAt,
        const LegacyCameraTransform& worldToView,
        const LegacyScreenProjection& projection,
        const Vec3 anchorCameraSpacePosition) noexcept
        : frame_(frame), vehicleWorldAnchor_(vehicleWorldAnchor),
          lookAt_(lookAt), worldToView_(worldToView), projection_(projection),
          anchorCameraSpacePosition_(anchorCameraSpacePosition) {
    }

    LegacyGameplayCameraFrameSnapshot frame_;
    Vec3 vehicleWorldAnchor_;
    LegacyGameplayCameraLookAt lookAt_;
    LegacyCameraTransform worldToView_;
    LegacyScreenProjection projection_;
    Vec3 anchorCameraSpacePosition_;
};

struct LegacyGameplayCameraPoseBuildResult final {
    std::optional<LegacyGameplayCameraPoseSnapshot> snapshot;
    std::optional<LegacyGameplayCameraPoseBuildIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return snapshot.has_value() && !issue.has_value();
    }
};

} // namespace airfix::render
