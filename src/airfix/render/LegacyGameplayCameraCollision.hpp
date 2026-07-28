#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <bit>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::render {

// Exact third arguments passed to SetPosAndTracePortals after the sphere and
// line stages. CcRoom::TracePortals does not read this parameter in the
// analyzed game build; the values remain part of the recovered caller ABI.
inline constexpr float legacyGameplayCameraSpherePortalTraceArgument =
    std::bit_cast<float>(std::uint32_t{0x3E4CCCCDU});
inline constexpr float legacyGameplayCameraLinePortalTraceArgument =
    std::bit_cast<float>(std::uint32_t{0x3DCCCCCDU});
inline constexpr double
    legacyGameplayCameraConstraintDuplicateDotThreshold =
        std::bit_cast<double>(std::uint64_t{0x3FEFF7CED916872BULL});
inline constexpr double
    legacyGameplayCameraConstraintCrossLengthSquaredThreshold =
        std::bit_cast<double>(std::uint64_t{0x3F1A36E2EB1C432DULL});

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

// Reconstructs CcConstraint::AddPlane's duplicate test. existingPlanesHeadFirst
// uses the native linked-list order (newest plane first). The candidate is
// retained unless one existing dot product is strictly greater than 0.999.
[[nodiscard]] std::optional<bool>
legacyGameplayCameraConstraintAcceptsPlane(
    std::span<const Vec3> existingPlanesHeadFirst,
    const Vec3& candidate) noexcept;

// Reconstructs CcConstraintPlane::Overrides. `plane` is the native receiver:
// it overrides `otherPlane` when movement projected onto the other plane still
// has a strictly negative component along this plane.
[[nodiscard]] std::optional<bool>
legacyGameplayCameraConstraintPlaneOverrides(
    const Vec3& plane,
    const Vec3& otherPlane,
    const Vec3& requestedMove) noexcept;

// Reconstructs CcConstraint::AttemptMove for a finite requested vector and
// finite plane normals in native head-first order. The original assumes the
// normals are normalized; this function preserves their supplied magnitudes.
[[nodiscard]] std::optional<Vec3>
legacyGameplayCameraAttemptConstrainedMove(
    const Vec3& requestedMove,
    std::span<const Vec3> planesHeadFirst) noexcept;

// Reconstructs PhSphere::GetCollision's seven-axis broad-phase test for one
// triangle. splitNormal is the owning BSP node plane normal, not the
// polygon's separately retained faceNormal. A finite positive sphere and
// finite triangle are required; exact tangency remains a candidate.
[[nodiscard]] std::optional<bool>
legacyGameplayCameraSphereTriangleCandidate(
    const Vec3& sphereCenter,
    float sphereRadius,
    const Vec3& point0,
    const Vec3& point1,
    const Vec3& point2,
    const Vec3& splitNormal) noexcept;

enum class LegacyGameplayCameraSphereContactFeature : std::uint8_t {
    face,
    edge01,
    edge12,
    edge20,
    vertex0,
    vertex1,
    vertex2,
};

struct LegacyGameplayCameraSphereContact final {
    float penetrationDepth{};
    // Native GetCollisionAndBestFree direction: unit faceNormal for a face,
    // but the raw contactPoint - sphereCenter vector for edges and vertices.
    Vec3 direction{};
    Vec3 contactPoint{};
    LegacyGameplayCameraSphereContactFeature feature{
        LegacyGameplayCameraSphereContactFeature::face};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraSphereContact&,
        const LegacyGameplayCameraSphereContact&) noexcept = default;
};

enum class LegacyGameplayCameraSphereContactStatus : std::uint8_t {
    noContact,
    contact,
    invalidInput,
};

struct LegacyGameplayCameraSphereContactResult final {
    LegacyGameplayCameraSphereContactStatus status{
        LegacyGameplayCameraSphereContactStatus::noContact};
    std::optional<LegacyGameplayCameraSphereContact> contact;

    [[nodiscard]] bool valid() const noexcept {
        return status !=
            LegacyGameplayCameraSphereContactStatus::invalidInput;
    }
};

// Reconstructs the closest-face/edge/vertex portion of
// PhCollidedPolyList::GetCollisionAndBestFree for one candidate. faceNormal
// uses the authored collision-facing orientation assumed by the native path.
// edge01 and edge02 are the native collided-polygon fields: point1 - point0
// and point2 - point0, respectively (edge02 is the binary32 sum of the two
// authored consecutive CCF edges).
// Face and edge tangencies are reported at zero depth; an exact vertex
// tangency is rejected by the original strict comparison.
[[nodiscard]] LegacyGameplayCameraSphereContactResult
legacyGameplayCameraSphereTriangleContact(
    const Vec3& sphereCenter,
    float sphereRadius,
    const Vec3& point0,
    const Vec3& edge01,
    const Vec3& edge02,
    const Vec3& faceNormal) noexcept;

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
