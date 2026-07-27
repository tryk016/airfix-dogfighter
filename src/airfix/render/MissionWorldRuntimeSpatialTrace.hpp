#pragma once

#include "airfix/assets/MissionWorldSpatialLineTrace.hpp"
#include "airfix/render/LegacyGeometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::render {

struct MissionWorldRuntimeSpatialLineHit final {
    // Preserved exactly from the source-world tracer. The recovered epsilon
    // endpoint gate can admit a finite value just outside [0, 1].
    float legacyFraction{};
    Vec3 runtimePoint{};
    // Plane covector transformed by transpose(inverse(sourceToRuntime)).
    // Uniform positive unit scale is intentionally omitted because a plane
    // normal is homogeneous. The vector is not normalized.
    Vec3 runtimePlaneNormal{};
    std::size_t ownerWorldRoomIndex{};
    std::size_t treeIndex{};
    std::size_t nodeIndex{};
    std::size_t polygonIndex{};
    std::optional<std::size_t> portalWorldRoomIndex;
    bool reverseFacing{};
    bool withinRequestedSegment{};

    [[nodiscard]] friend constexpr bool operator==(
        const MissionWorldRuntimeSpatialLineHit&,
        const MissionWorldRuntimeSpatialLineHit&) noexcept = default;
};

enum class MissionWorldRuntimeSpatialLineTraceStatus : std::uint8_t {
    noHit,
    hit,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    invalidBasis,
    traversalDepthExceeded,
    outOfSegmentHit,
};

struct MissionWorldRuntimeSpatialLineTraceResult final {
    MissionWorldRuntimeSpatialLineTraceStatus status{
        MissionWorldRuntimeSpatialLineTraceStatus::noHit};
    std::optional<MissionWorldRuntimeSpatialLineHit> hit;

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                MissionWorldRuntimeSpatialLineTraceStatus::noHit ||
            status ==
                MissionWorldRuntimeSpatialLineTraceStatus::hit;
    }
};

// Runtime-space boundary for the retained source-world BSP arena. Conversion,
// tracing, and result adaptation are read-only, allocation-free, and noexcept.
// Invalid/non-finite/singular basis data fails closed before the arena is
// queried. A hit point is transformed from the tracer's authored result and is
// never reconstructed from a clamped fraction. A recovered epsilon hit outside
// [0, 1] is retained for diagnostics but returns outOfSegmentHit so a camera
// state publisher cannot mistake it for a valid segment collision.
[[nodiscard]] MissionWorldRuntimeSpatialLineTraceResult
traceMissionWorldRuntimeSpatialLine(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    std::size_t worldRoomIndex,
    assets::CcfBspTreeKind treeKind,
    const Vec3& runtimeStart,
    const Vec3& runtimeEnd,
    const assets::MissionWorldSpatialLineTraceOptions& options = {}) noexcept;

enum class MissionWorldRuntimePortalTraceStatus : std::uint8_t {
    noTransition,
    transition,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    invalidBasis,
    traversalDepthExceeded,
    transitionLimitExceeded,
    outOfSegmentHit,
};

struct MissionWorldRuntimePortalTraceResult final {
    MissionWorldRuntimePortalTraceStatus status{
        MissionWorldRuntimePortalTraceStatus::noTransition};
    // Present when a portal polygon was hit even if its recovered follow gate
    // blocked the transition.
    std::optional<MissionWorldRuntimeSpatialLineHit> hit;
    std::optional<std::size_t> targetWorldRoomIndex;
    std::size_t transitionCount{};

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                MissionWorldRuntimePortalTraceStatus::noTransition ||
            status ==
                MissionWorldRuntimePortalTraceStatus::transition;
    }
};

// Runtime-space boundary for the recovered portal-room transition chain. The
// same mission-wide basis is used for every room; only the logical room index
// changes while the segment remains in common source-world coordinates. This
// boundary always enables per-hop [0, 1] validation before a room can change.
[[nodiscard]] MissionWorldRuntimePortalTraceResult
traceMissionWorldRuntimePortalTransition(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    std::size_t worldRoomIndex,
    const Vec3& runtimeStart,
    const Vec3& runtimeEnd,
    const assets::MissionWorldPortalTraceOptions& options = {}) noexcept;

} // namespace airfix::render
