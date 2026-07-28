#pragma once

#include "airfix/render/LegacyGameplayCameraChase.hpp"
#include "airfix/render/LegacyGameplayCameraPoseSnapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyGameplayCameraClipConfig final {
    float logicalCanvasHeight{480.0F};
};

enum class LegacyGameplayCameraClipBuildIssueKind : std::uint8_t {
    invalidLogicalCanvasHeight,
};

struct LegacyGameplayCameraClipBuildIssue final {
    LegacyGameplayCameraClipBuildIssueKind kind{
        LegacyGameplayCameraClipBuildIssueKind::invalidLogicalCanvasHeight};
};

enum class LegacyGameplayCameraClipIssueKind : std::uint8_t {
    transformFailed,
    nonFiniteDerivedValue,
};

struct LegacyGameplayCameraClipIssue final {
    LegacyGameplayCameraClipIssueKind kind{
        LegacyGameplayCameraClipIssueKind::transformFailed};
    std::optional<LegacyCameraTransformIssue> transformIssue;
};

struct LegacyGameplayCameraHomogeneousClipPoint final {
    Vec3 cameraSpacePosition{};
    float x{};
    float y{};
    float z{};
    float w{};
    float farClipDistance{};
    bool withinRecoveredDepthRange{};
};

struct LegacyGameplayCameraClipResult final {
    std::optional<LegacyGameplayCameraHomogeneousClipPoint> point;
    std::optional<LegacyGameplayCameraClipIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return point.has_value() && !issue.has_value();
    }
};

class LegacyGameplayCameraClipPacket;

struct LegacyGameplayCameraClipBuildResult;

// Packages one immutable gameplay-camera pose for a homogeneous [0,1] clip
// backend without replacing the recovered scalar arithmetic with a generic
// projection matrix. For accepted camera Z, the resulting divide reproduces
// screen X/Y and reverse depth near/Z. z=1 and w=Z/near let the standard near
// plane clip Z<near; farClipDistance supplies the separate far plane.
[[nodiscard]] LegacyGameplayCameraClipBuildResult
buildLegacyGameplayCameraClipPacket(
    const LegacyGameplayCameraPoseSnapshot &pose,
    const LegacyGameplayCameraClipConfig &config = {}) noexcept;

class LegacyGameplayCameraClipPacket final {
  public:
    [[nodiscard]] const LegacyGameplayCameraPoseSnapshot &
    pose() const noexcept {
        return pose_;
    }

    [[nodiscard]] float logicalCanvasWidth() const noexcept {
        return pose_.projection().windowWidth();
    }

    [[nodiscard]] constexpr float logicalCanvasHeight() const noexcept {
        return logicalCanvasHeight_;
    }

    [[nodiscard]] LegacyGameplayCameraClipResult
    project(const Vec3 &worldPosition) const noexcept;

  private:
    friend LegacyGameplayCameraClipBuildResult
    buildLegacyGameplayCameraClipPacket(
        const LegacyGameplayCameraPoseSnapshot &pose,
        const LegacyGameplayCameraClipConfig &config) noexcept;

    LegacyGameplayCameraClipPacket(const LegacyGameplayCameraPoseSnapshot &pose,
                                   const float logicalCanvasHeight) noexcept
        : pose_(pose), logicalCanvasHeight_(logicalCanvasHeight) {}

    LegacyGameplayCameraPoseSnapshot pose_;
    const float logicalCanvasHeight_;
};

struct LegacyGameplayCameraClipBuildResult final {
    std::optional<LegacyGameplayCameraClipPacket> packet;
    std::optional<LegacyGameplayCameraClipBuildIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return packet.has_value() && !issue.has_value();
    }
};

struct LegacyGameplayCameraBootstrapInput final {
    Vec3 vehicleWorldPosition{};
    Mat3 vehicleWorldRotation{};
    std::size_t worldRoomIndex{};
};

enum class LegacyGameplayCameraBootstrapIssueKind : std::uint8_t {
    cameraPresetUnavailable,
    chaseFailed,
    poseFailed,
    clipPacketFailed,
};

struct LegacyGameplayCameraBootstrapIssue final {
    LegacyGameplayCameraBootstrapIssueKind kind{
        LegacyGameplayCameraBootstrapIssueKind::cameraPresetUnavailable};
    std::optional<LegacyGameplayCameraChaseIssue> chaseIssue;
    std::optional<LegacyGameplayCameraPoseBuildIssue> poseIssue;
    std::optional<LegacyGameplayCameraClipBuildIssue> clipPacketIssue;
};

struct LegacyGameplayCameraBootstrapResult final {
    std::optional<LegacyGameplayCameraClipPacket> packet;
    std::optional<LegacyGameplayCameraBootstrapIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return packet.has_value() && !issue.has_value();
    }
};

// Until the retained simulation publishes event-5 camera generations, the
// private-room renderer starts directly at the recovered camera0 target. This
// explicit port bootstrap has no fabricated predecessor or smoothing history.
// It is intentionally separate from the recovered steady-state recurrence.
[[nodiscard]] LegacyGameplayCameraBootstrapResult
buildLegacyGameplayCameraBootstrapClipPacket(
    const LegacyGameplayCameraBootstrapInput &input) noexcept;

} // namespace airfix::render
