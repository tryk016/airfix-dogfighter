#include "airfix/runtime/PlayerAircraftPresentationCoordinator.hpp"

#include <utility>

namespace airfix::runtime {

PlayerAircraftPresentationCoordinator::
    PlayerAircraftPresentationCoordinator(
        simulation::PlayerAircraftState initialState) noexcept
    : state_(std::move(initialState)) {}

PlayerAircraftPresentationStepResult
PlayerAircraftPresentationCoordinator::tryAdvance(
    const input::InputFrame& frame,
    const render::ConvertedNodeTransform& actorWorld,
    const PlayerActorPoseRuntimeEndpoint& poseEndpoint) noexcept {
    if (!healthy_) {
        return {
            .status =
                PlayerAircraftPresentationStepStatus::unhealthy,
            .simulationError =
                simulation::PlayerAircraftAdvanceError::none,
            .poseOutcome = std::nullopt,
        };
    }

    const auto advanced = simulation::advance(state_, frame);
    if (!advanced.accepted()) {
        healthy_ = false;
        return {
            .status =
                PlayerAircraftPresentationStepStatus::
                    simulationRejected,
            .simulationError = advanced.error,
            .poseOutcome = std::nullopt,
        };
    }

    if (!poseEndpoint.has_value()) {
        state_ = advanced.state;
        return {
            .status =
                PlayerAircraftPresentationStepStatus::
                    advancedWithoutPoseEndpoint,
            .simulationError =
                simulation::PlayerAircraftAdvanceError::none,
            .poseOutcome = std::nullopt,
        };
    }

    const auto poseRuntime = poseEndpoint->lock();
    if (poseRuntime == nullptr) {
        healthy_ = false;
        return {
            .status =
                PlayerAircraftPresentationStepStatus::
                    poseEndpointExpired,
            .simulationError =
                simulation::PlayerAircraftAdvanceError::none,
            .poseOutcome = std::nullopt,
        };
    }

    const auto poseOutcome = poseRuntime->tryPublish(
        actorWorld, advanced.state.completedSteps);
    if (poseOutcome.result ==
            render::PlayerActorPoseRuntimePublishResult::published ||
        poseOutcome.result ==
            render::PlayerActorPoseRuntimePublishResult::busy) {
        state_ = advanced.state;
        return {
            .status = poseOutcome.result ==
                    render::PlayerActorPoseRuntimePublishResult::
                        published
                ? PlayerAircraftPresentationStepStatus::
                      advancedAndPosePublished
                : PlayerAircraftPresentationStepStatus::
                      advancedWhilePoseBusy,
            .simulationError =
                simulation::PlayerAircraftAdvanceError::none,
            .poseOutcome = poseOutcome,
        };
    }

    healthy_ = false;
    return {
        .status =
            PlayerAircraftPresentationStepStatus::
                posePublicationRejected,
        .simulationError =
            simulation::PlayerAircraftAdvanceError::none,
        .poseOutcome = poseOutcome,
    };
}

std::uint64_t
PlayerAircraftPresentationCoordinator::stateHash() const noexcept {
    return simulation::canonicalHash(state_);
}

} // namespace airfix::runtime
