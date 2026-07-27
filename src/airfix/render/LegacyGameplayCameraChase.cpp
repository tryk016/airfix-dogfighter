#include "airfix/render/LegacyGameplayCameraChase.hpp"

#include <bit>
#include <cmath>

namespace airfix::render {
namespace {

constexpr float closeTargetBias =
    std::bit_cast<float>(std::uint32_t{0x3A83126FU});
constexpr float closeTargetDamping =
    std::bit_cast<float>(std::uint32_t{0x3F7D70A4U});

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] bool finite(
    const LegacyGameplayCameraChaseInput& input) noexcept {
    return finite(input.vehiclePosition) &&
        finite(input.vehicleRotation) &&
        finite(input.currentCameraPosition) &&
        std::isfinite(input.preset.speed) &&
        finite(input.preset.offset) &&
        finite(input.axisFactors);
}

[[nodiscard]] LegacyGameplayCameraChaseResult failure(
    const LegacyGameplayCameraChaseIssueKind kind) noexcept {
    return {
        .step = std::nullopt,
        .issue = LegacyGameplayCameraChaseIssue{.kind = kind},
    };
}

} // namespace

LegacyGameplayCameraChaseResult legacyGameplayCameraChaseStep(
    const LegacyGameplayCameraChaseInput& input) noexcept {
    if (!finite(input)) {
        return failure(
            LegacyGameplayCameraChaseIssueKind::nonFiniteInput);
    }

    const Vec3 rotatedOffset = applyRuntimeColumn(
        input.vehicleRotation,
        Vec3{input.preset.offset.x, 0.0F, input.preset.offset.z});
    const Vec3 worldOffset{
        rotatedOffset.x,
        rotatedOffset.y + input.preset.offset.y,
        rotatedOffset.z,
    };
    if (!finite(worldOffset)) {
        return failure(
            LegacyGameplayCameraChaseIssueKind::
                nonFiniteDerivedValue);
    }

    // target is diagnostic. The recurrence below deliberately preserves the
    // recovered (vehicle - camera) + worldOffset rounding order.
    const Vec3 target{
        input.vehiclePosition.x + worldOffset.x,
        input.vehiclePosition.y + worldOffset.y,
        input.vehiclePosition.z + worldOffset.z,
    };
    const Vec3 unadjustedError{
        (input.vehiclePosition.x - input.currentCameraPosition.x) +
            worldOffset.x,
        (input.vehiclePosition.y - input.currentCameraPosition.y) +
            worldOffset.y,
        (input.vehiclePosition.z - input.currentCameraPosition.z) +
            worldOffset.z,
    };
    if (!finite(target) || !finite(unadjustedError)) {
        return failure(
            LegacyGameplayCameraChaseIssueKind::
                nonFiniteDerivedValue);
    }

    const float errorSquared =
        unadjustedError.x * unadjustedError.x +
        (unadjustedError.y * unadjustedError.y +
         unadjustedError.z * unadjustedError.z);
    const float offsetSquared =
        (input.preset.offset.x * input.preset.offset.x +
         input.preset.offset.y * input.preset.offset.y) +
        input.preset.offset.z * input.preset.offset.z;
    if (!std::isfinite(errorSquared) ||
        !std::isfinite(offsetSquared)) {
        return failure(
            LegacyGameplayCameraChaseIssueKind::
                nonFiniteDerivedValue);
    }

    Vec3 adjustedError = unadjustedError;
    std::optional<float> closeTargetErrorScale;
    if (errorSquared < offsetSquared) {
        const float scale =
            (std::sqrt(errorSquared) + closeTargetBias) *
            closeTargetDamping;
        adjustedError = Vec3{
            adjustedError.x * scale,
            adjustedError.y * scale,
            adjustedError.z * scale,
        };
        closeTargetErrorScale = scale;
        if (!std::isfinite(scale) || !finite(adjustedError)) {
            return failure(
                LegacyGameplayCameraChaseIssueKind::
                    nonFiniteDerivedValue);
        }
    }

    const Vec3 factorSpeed{
        input.axisFactors.x * input.preset.speed,
        input.axisFactors.y * input.preset.speed,
        input.axisFactors.z * input.preset.speed,
    };
    const Vec3 candidate{
        input.currentCameraPosition.x +
            factorSpeed.x * adjustedError.x,
        input.currentCameraPosition.y +
            factorSpeed.y * adjustedError.y,
        input.currentCameraPosition.z +
            factorSpeed.z * adjustedError.z,
    };
    if (!finite(factorSpeed) || !finite(candidate)) {
        return failure(
            LegacyGameplayCameraChaseIssueKind::
                nonFiniteDerivedValue);
    }

    return {
        .step = LegacyGameplayCameraChaseStep{
            .worldOffset = worldOffset,
            .target = target,
            .unadjustedError = unadjustedError,
            .closeTargetErrorScale = closeTargetErrorScale,
            .adjustedError = adjustedError,
            .candidateCameraPosition = candidate,
        },
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
