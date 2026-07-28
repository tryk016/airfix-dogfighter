#pragma once

#include "airfix/render/MissionWorldRuntimeSphereCollision.hpp"
#include "airfix/render/MissionWorldRuntimeSpatialTrace.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::render {

struct LegacyGameplayCameraRoomState final {
    Vec3 runtimeWorldPosition{};
    std::size_t worldRoomIndex{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraRoomState&,
        const LegacyGameplayCameraRoomState&) noexcept = default;
};

struct LegacyGameplayCameraRoomUpdateOptions final {
    // This remains bounded by
    // assets::kMissionWorldSpatialMaximumPortalTransitions.
    std::size_t maximumPortalTransitions{64U};
};

enum class LegacyGameplayCameraRoomUpdateStatus : std::uint8_t {
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

struct LegacyGameplayCameraRoomUpdateResult final {
    LegacyGameplayCameraRoomUpdateStatus status{
        LegacyGameplayCameraRoomUpdateStatus::noTransition};
    // Present only for a complete valid trace. Applying this value is the
    // caller's later atomic commit; this function never mutates currentState.
    std::optional<LegacyGameplayCameraRoomState> proposedState;
    // Diagnostic only. It may be present for blocked portals and for failures
    // after one or more local hits.
    std::optional<MissionWorldRuntimeSpatialLineHit> diagnosticHit;
    std::size_t transitionCount{};

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                LegacyGameplayCameraRoomUpdateStatus::noTransition ||
            status ==
                LegacyGameplayCameraRoomUpdateStatus::transition;
    }
};

// Proposes the candidate position and the room resolved by strict portal
// tracing. A valid no-transition result keeps the current room; a valid
// transition uses the final target room. Every failure returns no proposed
// state, so a caller can retain its complete prior state without rollback.
//
// This boundary does not resolve static/sphere contacts, change camera
// orientation, or publish to a thread/runtime endpoint. The operation is
// read-only, allocation-free, and noexcept.
[[nodiscard]] LegacyGameplayCameraRoomUpdateResult
proposeLegacyGameplayCameraRoomState(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraRoomState& currentState,
    const Vec3& candidateRuntimeWorldPosition,
    const LegacyGameplayCameraRoomUpdateOptions& options = {}) noexcept;

struct LegacyGameplayCameraStaticCollisionState final {
    LegacyGameplayCameraRoomState roomState{};
    Vec3 axisFactors{1.0F, 1.0F, 1.0F};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraStaticCollisionState&,
        const LegacyGameplayCameraStaticCollisionState&) noexcept = default;
};

struct LegacyGameplayCameraStaticCollisionOptions final {
    // Includes the current room and remains bounded by the static sphere
    // resolver's hard 256-room ceiling.
    std::size_t maximumSpherePortalRooms{64U};
    // Remains bounded by
    // assets::kMissionWorldSpatialMaximumPortalTransitions.
    std::size_t maximumPortalTransitions{64U};
};

enum class LegacyGameplayCameraStaticCollisionStatus : std::uint8_t {
    noTransition,
    transition,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    invalidBasis,
    traversalDepthExceeded,
    spherePortalRoomLimitExceeded,
    candidateCapacityExceeded,
    constraintCapacityExceeded,
    transitionLimitExceeded,
    outOfSegmentHit,
};

struct LegacyGameplayCameraStaticCollisionResult final {
    LegacyGameplayCameraStaticCollisionStatus status{
        LegacyGameplayCameraStaticCollisionStatus::noTransition};
    // Present only after sphere resolution, factor reduction, and the complete
    // corrected-position portal trace all succeed.
    std::optional<LegacyGameplayCameraStaticCollisionState> proposedState;
    // Stage diagnostics are present only when that stage was reached. Their
    // internal proposal/result must not be published independently.
    std::optional<MissionWorldRuntimeSphereCollisionResult> sphereCollision;
    std::optional<LegacyGameplayCameraRoomUpdateResult> roomUpdate;

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                LegacyGameplayCameraStaticCollisionStatus::noTransition ||
            status ==
                LegacyGameplayCameraStaticCollisionStatus::transition;
    }
};

// Reconstructs the static portion of the gameplay camera's event-5 collision
// order: build the near-plane-scaled sphere at the candidate position, resolve
// retained static BSP contacts in the current room graph, reduce all three
// axis factors by the accepted correction in legacy distance units, then trace
// the corrected segment through portals. Factors retain runtime axis order;
// only the basis's uniform unit scale is removed. Dynamic-object BSP remains
// outside this boundary.
//
// The returned state joins corrected position, final room, and reduced axis
// factors. It is present only if every stage succeeds, allowing a later
// single-writer owner to publish the complete value atomically. The operation
// is read-only with respect to currentState and the arena, allocation-free,
// and noexcept; both workspaces are caller-owned and overwritten.
[[nodiscard]] LegacyGameplayCameraStaticCollisionResult
proposeLegacyGameplayCameraStaticCollisionState(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStaticCollisionState& currentState,
    const Vec3& candidateRuntimeWorldPosition,
    float nearClipping,
    std::span<MissionWorldRuntimeSphereCandidate> candidateWorkspace,
    std::span<Vec3> constraintPlanesHeadFirst,
    const LegacyGameplayCameraStaticCollisionOptions& options = {}) noexcept;

struct LegacyGameplayCameraRetainedStaticFrameOptions final {
    // Remains bounded by
    // assets::kMissionWorldSpatialMaximumPortalTransitions.
    std::size_t maximumPortalTransitions{64U};
};

enum class LegacyGameplayCameraRetainedStaticFrameStatus :
    std::uint8_t {
    clear,
    occluded,
    invalidIntermediateProposal,
    invalidArena,
    invalidWorldRoom,
    invalidInput,
    invalidBasis,
    traversalDepthExceeded,
    transitionLimitExceeded,
    outOfSegmentHit,
};

struct LegacyGameplayCameraRetainedStaticFrameResult final {
    LegacyGameplayCameraRetainedStaticFrameStatus status{
        LegacyGameplayCameraRetainedStaticFrameStatus::
            invalidIntermediateProposal};
    // Present only after the preceding sphere/factor/portal proposal and this
    // complete retained-static line stage succeed.
    std::optional<LegacyGameplayCameraStaticCollisionState> proposedState;
    // Present after the line stage is reached, including a valid miss.
    std::optional<MissionWorldRuntimeSpatialLineTraceResult> lineTrace;
    // Present only when a static line hit required the native second portal
    // update from the intermediate camera position to the hit point.
    std::optional<LegacyGameplayCameraRoomUpdateResult> lineRoomUpdate;

    [[nodiscard]] bool valid() const noexcept {
        return status ==
                LegacyGameplayCameraRetainedStaticFrameStatus::clear ||
            status ==
                LegacyGameplayCameraRetainedStaticFrameStatus::occluded;
    }
};

// Completes the retained-static portion of native event 5. A complete valid
// sphere/factor/portal proposal is mandatory. The vehicle-to-camera segment is
// then queried against the static BSP of that proposal's camera room. A miss
// preserves the intermediate state. A hit uses the tracer-authored runtime
// point and performs the native second portal update before proposing the
// final position/room; axis factors remain those from the sphere stage.
//
// The native PhLine path can additionally inspect moving-object BSP and follow
// transparent portal polygons inside that collision query. Those inputs are
// not retained yet, so this boundary deliberately claims only the current-room
// static query plus the separately recovered room transition. It is read-only,
// allocation-free, bounded, and noexcept. Every failure exposes diagnostics
// but no proposed state.
[[nodiscard]] LegacyGameplayCameraRetainedStaticFrameResult
completeLegacyGameplayCameraRetainedStaticFrameState(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStaticCollisionResult&
        intermediateProposal,
    const Vec3& vehicleWorldAnchor,
    const LegacyGameplayCameraRetainedStaticFrameOptions& options =
        {}) noexcept;

} // namespace airfix::render
