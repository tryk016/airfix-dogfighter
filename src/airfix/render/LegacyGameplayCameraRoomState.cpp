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

} // namespace airfix::render
