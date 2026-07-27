#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <bit>
#include <cstdint>
#include <optional>

namespace airfix::render {

// Exact third arguments passed to SetPosAndTracePortals after the sphere and
// line stages. CcRoom::TracePortals does not read this parameter in the
// analyzed game build; the values remain part of the recovered caller ABI.
inline constexpr float legacyGameplayCameraSpherePortalTraceArgument =
    std::bit_cast<float>(std::uint32_t{0x3E4CCCCDU});
inline constexpr float legacyGameplayCameraLinePortalTraceArgument =
    std::bit_cast<float>(std::uint32_t{0x3DCCCCCDU});

// Returns nearClipping * 1.1 using the recovered binary32 multiplier.
// A non-positive/non-finite near plane or non-finite result fails closed.
[[nodiscard]] std::optional<float>
legacyGameplayCameraCollisionSphereRadius(
    float nearClipping) noexcept;

// Applies the post-sphere collision response independently per axis:
// max(0, factor - 2 * abs(resolved - original)). The original path has no
// upper clamp at this stage.
[[nodiscard]] std::optional<Vec3>
legacyGameplayCameraReduceCollisionAxisFactors(
    const Vec3& currentFactors,
    const Vec3& originalCameraPosition,
    const Vec3& resolvedCameraPosition) noexcept;

// Reconstructs the AirCraft.type vtable-slot +0xB0 recovery. rawRefreshArgument
// is intentionally not labelled as dt until its physical unit is proven.
// This contract is aircraft-specific; other vehicle types remain unaudited.
[[nodiscard]] std::optional<Vec3>
legacyAircraftRecoverGameplayCameraAxisFactors(
    const Vec3& currentFactors,
    float rawRefreshArgument,
    float vehicleField98,
    bool vehicleFlag460) noexcept;

// Reconstructs the point selected by the legacy vehicle-to-camera line trace:
// anchor + hitFraction * (camera - anchor). The collision backend is expected
// to supply a normalized fraction, so values outside [0,1] are rejected.
[[nodiscard]] std::optional<Vec3>
legacyGameplayCameraLineHitPoint(
    const Vec3& vehicleWorldAnchor,
    const Vec3& cameraPosition,
    float hitFraction) noexcept;

struct LegacyGameplayCameraLookAt final {
    Vec3 direction{};
    Vec3 axisRotationRadians{};

    // Raw camera-world CcMatrixRot/SRT linear matrix. It is consumed directly
    // by LegacyCameraTransformConfig::linear, whose row-vector inverse maps
    // the look direction onto positive camera-space Z.
    Mat3 cameraWorldLinear{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraLookAt&,
        const LegacyGameplayCameraLookAt&) noexcept = default;
};

// Reconstructs CcAxisRot::FromDirection followed by mode-zero
// CcMatrixRot::RotateByAxisRot (Z, X, Y; effectively X then Y because Z=0).
// A zero direction is rejected because the original degenerate atan2(0,0)
// behavior has not yet been established by a controlled runtime trace.
[[nodiscard]] std::optional<LegacyGameplayCameraLookAt>
legacyGameplayCameraLookAt(
    const Vec3& vehicleWorldAnchor,
    const Vec3& finalCameraPosition) noexcept;

} // namespace airfix::render
