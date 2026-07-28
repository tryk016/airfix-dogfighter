#include "airfix/render/LegacyGameplayCameraRoomState.hpp"

#include <cmath>

namespace airfix::render {
namespace {

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool validStaticState(
    const LegacyGameplayCameraStaticCollisionState& state) noexcept {
    return finite(state.roomState.runtimeWorldPosition) &&
        finite(state.axisFactors) &&
        state.axisFactors.x >= 0.0F &&
        state.axisFactors.y >= 0.0F &&
        state.axisFactors.z >= 0.0F;
}

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

[[nodiscard]] LegacyGameplayCameraRetainedStaticFrameStatus
frameStatus(
    const MissionWorldRuntimeSpatialLineTraceStatus status) noexcept {
    switch (status) {
    case MissionWorldRuntimeSpatialLineTraceStatus::noHit:
        return LegacyGameplayCameraRetainedStaticFrameStatus::clear;
    case MissionWorldRuntimeSpatialLineTraceStatus::hit:
        return LegacyGameplayCameraRetainedStaticFrameStatus::occluded;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidArena:
        return LegacyGameplayCameraRetainedStaticFrameStatus::invalidArena;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidWorldRoom:
        return LegacyGameplayCameraRetainedStaticFrameStatus::
            invalidWorldRoom;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidInput:
        return LegacyGameplayCameraRetainedStaticFrameStatus::invalidInput;
    case MissionWorldRuntimeSpatialLineTraceStatus::invalidBasis:
        return LegacyGameplayCameraRetainedStaticFrameStatus::invalidBasis;
    case MissionWorldRuntimeSpatialLineTraceStatus::
        traversalDepthExceeded:
        return LegacyGameplayCameraRetainedStaticFrameStatus::
            traversalDepthExceeded;
    case MissionWorldRuntimeSpatialLineTraceStatus::outOfSegmentHit:
        return LegacyGameplayCameraRetainedStaticFrameStatus::
            outOfSegmentHit;
    }
    return LegacyGameplayCameraRetainedStaticFrameStatus::invalidArena;
}

[[nodiscard]] LegacyGameplayCameraRetainedStaticFrameStatus
frameStatus(
    const LegacyGameplayCameraRoomUpdateStatus status) noexcept {
    switch (status) {
    case LegacyGameplayCameraRoomUpdateStatus::noTransition:
    case LegacyGameplayCameraRoomUpdateStatus::transition:
        return LegacyGameplayCameraRetainedStaticFrameStatus::occluded;
    case LegacyGameplayCameraRoomUpdateStatus::invalidArena:
        return LegacyGameplayCameraRetainedStaticFrameStatus::invalidArena;
    case LegacyGameplayCameraRoomUpdateStatus::invalidWorldRoom:
        return LegacyGameplayCameraRetainedStaticFrameStatus::
            invalidWorldRoom;
    case LegacyGameplayCameraRoomUpdateStatus::invalidInput:
        return LegacyGameplayCameraRetainedStaticFrameStatus::invalidInput;
    case LegacyGameplayCameraRoomUpdateStatus::invalidBasis:
        return LegacyGameplayCameraRetainedStaticFrameStatus::invalidBasis;
    case LegacyGameplayCameraRoomUpdateStatus::traversalDepthExceeded:
        return LegacyGameplayCameraRetainedStaticFrameStatus::
            traversalDepthExceeded;
    case LegacyGameplayCameraRoomUpdateStatus::transitionLimitExceeded:
        return LegacyGameplayCameraRetainedStaticFrameStatus::
            transitionLimitExceeded;
    case LegacyGameplayCameraRoomUpdateStatus::outOfSegmentHit:
        return LegacyGameplayCameraRetainedStaticFrameStatus::
            outOfSegmentHit;
    }
    return LegacyGameplayCameraRetainedStaticFrameStatus::invalidArena;
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

LegacyGameplayCameraRetainedStaticFrameResult
completeLegacyGameplayCameraRetainedStaticFrameState(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStaticCollisionResult&
        intermediateProposal,
    const Vec3& vehicleWorldAnchor,
    const LegacyGameplayCameraRetainedStaticFrameOptions&
        options) noexcept {
    if (!intermediateProposal.valid() ||
        !intermediateProposal.proposedState.has_value() ||
        !validStaticState(*intermediateProposal.proposedState)) {
        return {
            .status = LegacyGameplayCameraRetainedStaticFrameStatus::
                invalidIntermediateProposal,
            .proposedState = std::nullopt,
            .lineTrace = std::nullopt,
            .lineRoomUpdate = std::nullopt,
        };
    }

    const auto& intermediateState =
        *intermediateProposal.proposedState;
    const auto line = traceMissionWorldRuntimeSpatialLine(
        arena,
        runtimeBasis,
        intermediateState.roomState.worldRoomIndex,
        assets::CcfBspTreeKind::staticTree,
        vehicleWorldAnchor,
        intermediateState.roomState.runtimeWorldPosition);
    if (line.status ==
        MissionWorldRuntimeSpatialLineTraceStatus::noHit) {
        if (line.hit.has_value()) {
            return {
                .status =
                    LegacyGameplayCameraRetainedStaticFrameStatus::
                        invalidArena,
                .proposedState = std::nullopt,
                .lineTrace = line,
                .lineRoomUpdate = std::nullopt,
            };
        }
        return {
            .status =
                LegacyGameplayCameraRetainedStaticFrameStatus::clear,
            .proposedState = intermediateState,
            .lineTrace = line,
            .lineRoomUpdate = std::nullopt,
        };
    }
    if (line.status !=
            MissionWorldRuntimeSpatialLineTraceStatus::hit ||
        !line.hit.has_value()) {
        return {
            .status = line.status ==
                    MissionWorldRuntimeSpatialLineTraceStatus::hit
                ? LegacyGameplayCameraRetainedStaticFrameStatus::
                      invalidArena
                : frameStatus(line.status),
            .proposedState = std::nullopt,
            .lineTrace = line,
            .lineRoomUpdate = std::nullopt,
        };
    }

    const auto roomUpdate = proposeLegacyGameplayCameraRoomState(
        arena,
        runtimeBasis,
        intermediateState.roomState,
        line.hit->runtimePoint,
        {
            .maximumPortalTransitions =
                options.maximumPortalTransitions,
        });
    if (!roomUpdate.valid() ||
        !roomUpdate.proposedState.has_value()) {
        return {
            .status = roomUpdate.valid()
                ? LegacyGameplayCameraRetainedStaticFrameStatus::
                      invalidArena
                : frameStatus(roomUpdate.status),
            .proposedState = std::nullopt,
            .lineTrace = line,
            .lineRoomUpdate = roomUpdate,
        };
    }

    return {
        .status =
            LegacyGameplayCameraRetainedStaticFrameStatus::occluded,
        .proposedState =
            LegacyGameplayCameraStaticCollisionState{
                .roomState = *roomUpdate.proposedState,
                .axisFactors = intermediateState.axisFactors,
            },
        .lineTrace = line,
        .lineRoomUpdate = roomUpdate,
    };
}

} // namespace airfix::render
