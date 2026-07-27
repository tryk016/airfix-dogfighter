#pragma once

#include "airfix/render/LegacyGameplayCameraPreset.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

// Inputs to the recovered per-refresh chase recurrence. vehicleRotation uses
// the runtime column-vector convention; the caller remains responsible for
// constructing it from the legacy vehicle quaternion.
struct LegacyGameplayCameraChaseInput final {
    Vec3 vehiclePosition{};
    Mat3 vehicleRotation{};
    Vec3 currentCameraPosition{};
    LegacyGameplayCameraPreset preset{};
    Vec3 axisFactors{1.0F, 1.0F, 1.0F};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraChaseInput&,
        const LegacyGameplayCameraChaseInput&) noexcept = default;
};

struct LegacyGameplayCameraChaseStep final {
    Vec3 worldOffset{};
    Vec3 target{};
    Vec3 unadjustedError{};

    // Present only when the strict legacy
    // errorSquared < offsetSquared branch was taken.
    std::optional<float> closeTargetErrorScale;

    Vec3 adjustedError{};
    Vec3 candidateCameraPosition{};
};

enum class LegacyGameplayCameraChaseIssueKind : std::uint8_t {
    nonFiniteInput,
    nonFiniteDerivedValue,
};

struct LegacyGameplayCameraChaseIssue final {
    LegacyGameplayCameraChaseIssueKind kind{
        LegacyGameplayCameraChaseIssueKind::nonFiniteInput};
};

struct LegacyGameplayCameraChaseResult final {
    std::optional<LegacyGameplayCameraChaseStep> step;
    std::optional<LegacyGameplayCameraChaseIssue> issue;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return step.has_value() && !issue.has_value();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return complete();
    }
};

// Reconstructs only the confirmed target and nonlinear smoothing stage. The
// returned candidate still requires portal tracing, collision correction, and
// look-at pose publication before it can become a rendered camera pose.
//
// The expression grouping follows the recovered x87 instructions, but this
// portable float implementation does not claim bit-identical extended-
// precision behavior on every target.
[[nodiscard]] LegacyGameplayCameraChaseResult
legacyGameplayCameraChaseStep(
    const LegacyGameplayCameraChaseInput& input) noexcept;

} // namespace airfix::render
