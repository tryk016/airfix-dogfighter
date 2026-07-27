#include "airfix/render/LegacyGameplayCameraCollision.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace airfix::render {
namespace {

constexpr float collisionRadiusScale =
    std::bit_cast<float>(std::uint32_t{0x3F8CCCCDU});
constexpr float aircraftAxisRecoveryRate = 0.25F;

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] std::optional<float> reduceFactor(
    const float factor,
    const float correction) noexcept {
    const float reduced = factor - correction;
    if (!std::isfinite(reduced)) {
        return std::nullopt;
    }
    return std::max(0.0F, reduced);
}

[[nodiscard]] std::optional<float> recoverFactor(
    float factor,
    const float rawRefreshArgument) noexcept {
    if (factor < 1.0F) {
        const float increment =
            rawRefreshArgument *
            aircraftAxisRecoveryRate;
        if (!std::isfinite(increment)) {
            return std::nullopt;
        }
        factor += increment;
        if (!std::isfinite(factor)) {
            return std::nullopt;
        }
    }
    return std::clamp(factor, 0.0F, 1.0F);
}

} // namespace

std::optional<float> legacyGameplayCameraCollisionSphereRadius(
    const float nearClipping) noexcept {
    if (!std::isfinite(nearClipping) || !(nearClipping > 0.0F)) {
        return std::nullopt;
    }

    const float radius =
        nearClipping * collisionRadiusScale;
    if (!std::isfinite(radius)) {
        return std::nullopt;
    }
    return radius;
}

std::optional<Vec3> legacyGameplayCameraReduceCollisionAxisFactors(
    const Vec3& currentFactors,
    const Vec3& originalCameraPosition,
    const Vec3& resolvedCameraPosition) noexcept {
    if (!finite(currentFactors) || !finite(originalCameraPosition) ||
        !finite(resolvedCameraPosition)) {
        return std::nullopt;
    }

    const Vec3 delta{
        resolvedCameraPosition.x - originalCameraPosition.x,
        resolvedCameraPosition.y - originalCameraPosition.y,
        resolvedCameraPosition.z - originalCameraPosition.z,
    };
    const Vec3 correction{
        2.0F * std::abs(delta.x),
        2.0F * std::abs(delta.y),
        2.0F * std::abs(delta.z),
    };
    if (!finite(delta) || !finite(correction)) {
        return std::nullopt;
    }

    const auto reducedX =
        reduceFactor(currentFactors.x, correction.x);
    const auto reducedY =
        reduceFactor(currentFactors.y, correction.y);
    const auto reducedZ =
        reduceFactor(currentFactors.z, correction.z);
    if (!reducedX.has_value() || !reducedY.has_value() ||
        !reducedZ.has_value()) {
        return std::nullopt;
    }
    const Vec3 reduced{*reducedX, *reducedY, *reducedZ};
    if (!finite(reduced)) {
        return std::nullopt;
    }
    return reduced;
}

std::optional<Vec3> legacyAircraftRecoverGameplayCameraAxisFactors(
    const Vec3& currentFactors,
    const float rawRefreshArgument,
    const float vehicleField98,
    const bool vehicleFlag460) noexcept {
    if (!finite(currentFactors) ||
        !std::isfinite(rawRefreshArgument) ||
        !std::isfinite(vehicleField98)) {
        return std::nullopt;
    }

    if (!(vehicleField98 > 0.0F) || vehicleFlag460) {
        return Vec3{};
    }

    const auto recoveredX =
        recoverFactor(currentFactors.x, rawRefreshArgument);
    const auto recoveredY =
        recoverFactor(currentFactors.y, rawRefreshArgument);
    const auto recoveredZ =
        recoverFactor(currentFactors.z, rawRefreshArgument);
    if (!recoveredX.has_value() || !recoveredY.has_value() ||
        !recoveredZ.has_value()) {
        return std::nullopt;
    }
    const Vec3 recovered{*recoveredX, *recoveredY, *recoveredZ};
    if (!finite(recovered)) {
        return std::nullopt;
    }
    return recovered;
}

std::optional<Vec3> legacyGameplayCameraLineHitPoint(
    const Vec3& vehicleWorldAnchor,
    const Vec3& cameraPosition,
    const float hitFraction) noexcept {
    if (!finite(vehicleWorldAnchor) || !finite(cameraPosition) ||
        !std::isfinite(hitFraction) || hitFraction < 0.0F ||
        hitFraction > 1.0F) {
        return std::nullopt;
    }

    const Vec3 delta{
        cameraPosition.x - vehicleWorldAnchor.x,
        cameraPosition.y - vehicleWorldAnchor.y,
        cameraPosition.z - vehicleWorldAnchor.z,
    };
    const Vec3 hitPoint{
        vehicleWorldAnchor.x + hitFraction * delta.x,
        vehicleWorldAnchor.y + hitFraction * delta.y,
        vehicleWorldAnchor.z + hitFraction * delta.z,
    };
    if (!finite(delta) || !finite(hitPoint)) {
        return std::nullopt;
    }
    return hitPoint;
}

std::optional<LegacyGameplayCameraLookAt>
legacyGameplayCameraLookAt(
    const Vec3& vehicleWorldAnchor,
    const Vec3& finalCameraPosition) noexcept {
    if (!finite(vehicleWorldAnchor) ||
        !finite(finalCameraPosition)) {
        return std::nullopt;
    }

    const Vec3 direction{
        vehicleWorldAnchor.x - finalCameraPosition.x,
        vehicleWorldAnchor.y - finalCameraPosition.y,
        vehicleWorldAnchor.z - finalCameraPosition.z,
    };
    if (!finite(direction) ||
        (direction.x == 0.0F && direction.y == 0.0F &&
         direction.z == 0.0F)) {
        return std::nullopt;
    }

    // FromDirection evaluates these operations with x87 intermediates in the
    // legacy binary. Double staging avoids premature binary32 overflow while
    // remaining an explicit portable approximation, not a bit-parity claim.
    const double x = direction.x;
    const double y = direction.y;
    const double z = direction.z;
    const double horizontal =
        std::sqrt(z * z + x * x);
    const double axisX = std::atan2(y, horizontal);
    const double axisY = std::atan2(x, z);
    if (!std::isfinite(horizontal) || !std::isfinite(axisX) ||
        !std::isfinite(axisY)) {
        return std::nullopt;
    }

    const Vec3 axisRotationRadians{
        static_cast<float>(axisX),
        static_cast<float>(axisY),
        0.0F,
    };
    if (!finite(axisRotationRadians)) {
        return std::nullopt;
    }

    // The existing axis-rotation reconstruction publishes transpose(raw) as
    // a runtime column matrix. Transpose it back to the raw CcMatrixRot/SRT
    // linear matrix expected by the legacy camera world-to-view relation.
    const auto runtimePose =
        tryConvertLegacyAxisRotationWorldPose(
            {},
            std::array<float, 3>{
                axisRotationRadians.x,
                axisRotationRadians.y,
                axisRotationRadians.z,
            });
    if (!runtimePose.has_value()) {
        return std::nullopt;
    }
    const Mat3 cameraWorldLinear =
        transpose(runtimePose->linear);
    if (!finite(cameraWorldLinear)) {
        return std::nullopt;
    }

    return LegacyGameplayCameraLookAt{
        .direction = direction,
        .axisRotationRadians = axisRotationRadians,
        .cameraWorldLinear = cameraWorldLinear,
    };
}

} // namespace airfix::render
