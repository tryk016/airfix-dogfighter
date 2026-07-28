#include "airfix/render/LegacyGameplayCameraRoomState.hpp"

namespace airfix::render {
namespace {

[[nodiscard]] LegacyGameplayCameraRoomUpdateStatus updateStatus(
    const MissionWorldRuntimePortalTraceStatus status) noexcept {
    switch (status) {
    case MissionWorldRuntimePortalTraceStatus::noTransition:
        return LegacyGameplayCameraRoomUpdateStatus::noTransition;
    case MissionWorldRuntimePortalTraceStatus::transition:
        return LegacyGameplayCameraRoomUpdateStatus::transition;
    case MissionWorldRuntimePortalTraceStatus::invalidArena:
        return LegacyGameplayCameraRoomUpdateStatus::invalidArena;
    case MissionWorldRuntimePortalTraceStatus::invalidWorldRoom:
        return LegacyGameplayCameraRoomUpdateStatus::invalidWorldRoom;
    case MissionWorldRuntimePortalTraceStatus::invalidInput:
        return LegacyGameplayCameraRoomUpdateStatus::invalidInput;
    case MissionWorldRuntimePortalTraceStatus::invalidBasis:
        return LegacyGameplayCameraRoomUpdateStatus::invalidBasis;
    case MissionWorldRuntimePortalTraceStatus::traversalDepthExceeded:
        return LegacyGameplayCameraRoomUpdateStatus::
            traversalDepthExceeded;
    case MissionWorldRuntimePortalTraceStatus::transitionLimitExceeded:
        return LegacyGameplayCameraRoomUpdateStatus::
            transitionLimitExceeded;
    case MissionWorldRuntimePortalTraceStatus::outOfSegmentHit:
        return LegacyGameplayCameraRoomUpdateStatus::outOfSegmentHit;
    }
    return LegacyGameplayCameraRoomUpdateStatus::invalidArena;
}

[[nodiscard]] LegacyGameplayCameraRoomUpdateResult failure(
    const LegacyGameplayCameraRoomUpdateStatus status,
    const MissionWorldRuntimePortalTraceResult& trace) noexcept {
    return {
        .status = status,
        .proposedState = std::nullopt,
        .diagnosticHit = trace.hit,
        .transitionCount = trace.transitionCount,
    };
}

[[nodiscard]] LegacyGameplayCameraStaticCollisionStatus collisionStatus(
    const MissionWorldRuntimeSphereCollisionStatus status) noexcept {
    switch (status) {
    case MissionWorldRuntimeSphereCollisionStatus::noContact:
    case MissionWorldRuntimeSphereCollisionStatus::touching:
    case MissionWorldRuntimeSphereCollisionStatus::resolved:
        return LegacyGameplayCameraStaticCollisionStatus::noTransition;
    case MissionWorldRuntimeSphereCollisionStatus::invalidArena:
        return LegacyGameplayCameraStaticCollisionStatus::invalidArena;
    case MissionWorldRuntimeSphereCollisionStatus::invalidWorldRoom:
        return LegacyGameplayCameraStaticCollisionStatus::invalidWorldRoom;
    case MissionWorldRuntimeSphereCollisionStatus::invalidInput:
        return LegacyGameplayCameraStaticCollisionStatus::invalidInput;
    case MissionWorldRuntimeSphereCollisionStatus::invalidBasis:
        return LegacyGameplayCameraStaticCollisionStatus::invalidBasis;
    case MissionWorldRuntimeSphereCollisionStatus::traversalDepthExceeded:
        return LegacyGameplayCameraStaticCollisionStatus::
            traversalDepthExceeded;
    case MissionWorldRuntimeSphereCollisionStatus::portalRoomLimitExceeded:
        return LegacyGameplayCameraStaticCollisionStatus::
            spherePortalRoomLimitExceeded;
    case MissionWorldRuntimeSphereCollisionStatus::
        candidateCapacityExceeded:
        return LegacyGameplayCameraStaticCollisionStatus::
            candidateCapacityExceeded;
    case MissionWorldRuntimeSphereCollisionStatus::
        constraintCapacityExceeded:
        return LegacyGameplayCameraStaticCollisionStatus::
            constraintCapacityExceeded;
    }
    return LegacyGameplayCameraStaticCollisionStatus::invalidArena;
}

[[nodiscard]] LegacyGameplayCameraStaticCollisionStatus roomStatus(
    const LegacyGameplayCameraRoomUpdateStatus status) noexcept {
    switch (status) {
    case LegacyGameplayCameraRoomUpdateStatus::noTransition:
        return LegacyGameplayCameraStaticCollisionStatus::noTransition;
    case LegacyGameplayCameraRoomUpdateStatus::transition:
        return LegacyGameplayCameraStaticCollisionStatus::transition;
    case LegacyGameplayCameraRoomUpdateStatus::invalidArena:
        return LegacyGameplayCameraStaticCollisionStatus::invalidArena;
    case LegacyGameplayCameraRoomUpdateStatus::invalidWorldRoom:
        return LegacyGameplayCameraStaticCollisionStatus::invalidWorldRoom;
    case LegacyGameplayCameraRoomUpdateStatus::invalidInput:
        return LegacyGameplayCameraStaticCollisionStatus::invalidInput;
    case LegacyGameplayCameraRoomUpdateStatus::invalidBasis:
        return LegacyGameplayCameraStaticCollisionStatus::invalidBasis;
    case LegacyGameplayCameraRoomUpdateStatus::traversalDepthExceeded:
        return LegacyGameplayCameraStaticCollisionStatus::
            traversalDepthExceeded;
    case LegacyGameplayCameraRoomUpdateStatus::transitionLimitExceeded:
        return LegacyGameplayCameraStaticCollisionStatus::
            transitionLimitExceeded;
    case LegacyGameplayCameraRoomUpdateStatus::outOfSegmentHit:
        return LegacyGameplayCameraStaticCollisionStatus::outOfSegmentHit;
    }
    return LegacyGameplayCameraStaticCollisionStatus::invalidArena;
}

} // namespace

LegacyGameplayCameraRoomUpdateResult
proposeLegacyGameplayCameraRoomState(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraRoomState& currentState,
    const Vec3& candidateRuntimeWorldPosition,
    const LegacyGameplayCameraRoomUpdateOptions& options) noexcept {
    const auto trace = traceMissionWorldRuntimePortalTransition(
        arena,
        runtimeBasis,
        currentState.worldRoomIndex,
        currentState.runtimeWorldPosition,
        candidateRuntimeWorldPosition,
        {
            .maximumTransitions =
                options.maximumPortalTransitions,
        });
    const auto status = updateStatus(trace.status);
    if (trace.status ==
        MissionWorldRuntimePortalTraceStatus::noTransition) {
        if (trace.targetWorldRoomIndex.has_value()) {
            return failure(
                LegacyGameplayCameraRoomUpdateStatus::invalidArena,
                trace);
        }
        return {
            .status = status,
            .proposedState =
                LegacyGameplayCameraRoomState{
                    .runtimeWorldPosition =
                        candidateRuntimeWorldPosition,
                    .worldRoomIndex =
                        currentState.worldRoomIndex,
                },
            .diagnosticHit = trace.hit,
            .transitionCount = trace.transitionCount,
        };
    }
    if (trace.status ==
        MissionWorldRuntimePortalTraceStatus::transition) {
        if (!trace.targetWorldRoomIndex.has_value() ||
            *trace.targetWorldRoomIndex >= arena.rooms.size()) {
            return failure(
                LegacyGameplayCameraRoomUpdateStatus::invalidArena,
                trace);
        }
        return {
            .status = status,
            .proposedState =
                LegacyGameplayCameraRoomState{
                    .runtimeWorldPosition =
                        candidateRuntimeWorldPosition,
                    .worldRoomIndex =
                        *trace.targetWorldRoomIndex,
                },
            .diagnosticHit = trace.hit,
            .transitionCount = trace.transitionCount,
        };
    }
    return failure(status, trace);
}

LegacyGameplayCameraStaticCollisionResult
proposeLegacyGameplayCameraStaticCollisionState(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStaticCollisionState& currentState,
    const Vec3& candidateRuntimeWorldPosition,
    const float nearClipping,
    const std::span<MissionWorldRuntimeSphereCandidate>
        candidateWorkspace,
    const std::span<Vec3> constraintPlanesHeadFirst,
    const LegacyGameplayCameraStaticCollisionOptions& options) noexcept {
    const auto radius =
        legacyGameplayCameraCollisionSphereRadius(nearClipping);
    if (!radius.has_value()) {
        return {
            .status =
                LegacyGameplayCameraStaticCollisionStatus::invalidInput,
            .proposedState = std::nullopt,
            .sphereCollision = std::nullopt,
            .roomUpdate = std::nullopt,
        };
    }

    const auto collision = resolveMissionWorldRuntimeStaticSphere(
        arena,
        runtimeBasis,
        currentState.roomState.worldRoomIndex,
        candidateRuntimeWorldPosition,
        *radius,
        candidateWorkspace,
        constraintPlanesHeadFirst,
        {
            .maximumPortalRooms =
                options.maximumSpherePortalRooms,
        });
    if (!collision.valid()) {
        return {
            .status = collisionStatus(collision.status),
            .proposedState = std::nullopt,
            .sphereCollision = collision,
            .roomUpdate = std::nullopt,
        };
    }

    // Axis factors follow runtime axis order, but their recovered subtraction
    // is measured in legacy world-distance units. Remove only the uniform unit
    // scale; the orthonormal basis has already mapped the correction into the
    // runtime axis convention.
    const Vec3 factorCorrection{
        collision.correction.x /
            runtimeBasis.runtimeUnitsPerSourceUnit,
        collision.correction.y /
            runtimeBasis.runtimeUnitsPerSourceUnit,
        collision.correction.z /
            runtimeBasis.runtimeUnitsPerSourceUnit,
    };
    const auto reducedFactors =
        legacyGameplayCameraReduceCollisionAxisFactors(
            currentState.axisFactors,
            Vec3{},
            factorCorrection);
    if (!reducedFactors.has_value()) {
        return {
            .status =
                LegacyGameplayCameraStaticCollisionStatus::invalidInput,
            .proposedState = std::nullopt,
            .sphereCollision = collision,
            .roomUpdate = std::nullopt,
        };
    }

    const auto roomUpdate = proposeLegacyGameplayCameraRoomState(
        arena,
        runtimeBasis,
        currentState.roomState,
        collision.resolvedCenter,
        {
            .maximumPortalTransitions =
                options.maximumPortalTransitions,
        });
    const auto status = roomStatus(roomUpdate.status);
    if (!roomUpdate.valid() ||
        !roomUpdate.proposedState.has_value()) {
        return {
            .status = roomUpdate.valid()
                ? LegacyGameplayCameraStaticCollisionStatus::invalidArena
                : status,
            .proposedState = std::nullopt,
            .sphereCollision = collision,
            .roomUpdate = roomUpdate,
        };
    }

    return {
        .status = status,
        .proposedState =
            LegacyGameplayCameraStaticCollisionState{
                .roomState = *roomUpdate.proposedState,
                .axisFactors = *reducedFactors,
            },
        .sphereCollision = collision,
        .roomUpdate = roomUpdate,
    };
}

} // namespace airfix::render
