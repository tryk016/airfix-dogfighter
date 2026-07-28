#pragma once

#include "airfix/assets/MissionWorldSpatialArena.hpp"
#include "airfix/render/LegacyGameplayCameraCollision.hpp"
#include "airfix/render/LegacyGeometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::render {

inline constexpr std::size_t
    kMissionWorldRuntimeSphereMaximumPortalRooms = 256U;

struct MissionWorldRuntimeSphereCandidate final {
    std::size_t ownerWorldRoomIndex{};
    std::size_t treeIndex{};
    std::size_t nodeIndex{};
    std::size_t polygonIndex{};
    bool active{};
};

struct MissionWorldRuntimeSphereContact final {
    float penetrationDepth{};
    Vec3 direction{};
    Vec3 contactPoint{};
    LegacyGameplayCameraSphereContactFeature feature{
        LegacyGameplayCameraSphereContactFeature::face};
    std::size_t ownerWorldRoomIndex{};
    std::size_t treeIndex{};
    std::size_t nodeIndex{};
    std::size_t polygonIndex{};
};

enum class MissionWorldRuntimeSphereCollisionStatus : std::uint8_t {
    noContact,
    touching,
    resolved,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    invalidBasis,
    traversalDepthExceeded,
    portalRoomLimitExceeded,
    candidateCapacityExceeded,
    constraintCapacityExceeded,
};

struct MissionWorldRuntimeSphereCollisionOptions final {
    // Includes the starting room. The native graph walk is unbounded; the
    // portable runtime imposes the same hard ceiling used by portal tracing.
    std::size_t maximumPortalRooms{64U};
};

struct MissionWorldRuntimeSphereCollisionResult final {
    MissionWorldRuntimeSphereCollisionStatus status{
        MissionWorldRuntimeSphereCollisionStatus::noContact};
    Vec3 originalCenter{};
    Vec3 resolvedCenter{};
    Vec3 correction{};
    std::size_t candidateCount{};
    std::size_t resolutionIterations{};
    std::size_t constraintPlaneCount{};
    std::optional<MissionWorldRuntimeSphereContact> lastSelectedContact;

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                MissionWorldRuntimeSphereCollisionStatus::noContact ||
            status ==
                MissionWorldRuntimeSphereCollisionStatus::touching ||
            status ==
                MissionWorldRuntimeSphereCollisionStatus::resolved;
    }
};

// Reconstructs the gameplay camera's static PhSphere path over the retained
// mission BSP: B-then-A traversal, candidate collection by
// PhSphere::GetCollision, type-zero visible portal-room discovery, collision
// material filtering, closest-feature selection, and iterative constrained
// correction.
//
// The operation is read-only with respect to the arena, allocation-free, and
// noexcept. The caller owns both workspaces. candidateWorkspace represents
// native collided-polygon records; constraintPlanesHeadFirst represents the
// native newest-first CcConstraint list and is overwritten during the call.
// Dynamic-object BSP is deliberately outside this static boundary.
//
// A source-to-runtime basis must be orthonormal; runtimeUnitsPerSourceUnit
// supplies the sole uniform scale. A general affine basis would turn a runtime
// sphere into a source-space ellipsoid and therefore fails closed.
[[nodiscard]] MissionWorldRuntimeSphereCollisionResult
resolveMissionWorldRuntimeStaticSphere(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    std::size_t worldRoomIndex,
    const Vec3& runtimeCenter,
    float runtimeRadius,
    std::span<MissionWorldRuntimeSphereCandidate> candidateWorkspace,
    std::span<Vec3> constraintPlanesHeadFirst,
    const MissionWorldRuntimeSphereCollisionOptions& options = {}) noexcept;

} // namespace airfix::render
