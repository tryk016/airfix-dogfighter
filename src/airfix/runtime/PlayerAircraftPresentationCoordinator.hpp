#pragma once

#include "airfix/render/PlayerActorPoseRuntime.hpp"
#include "airfix/simulation/PlayerAircraftSimulation.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace airfix::runtime {

using PlayerActorPoseRuntimeEndpoint =
    std::optional<std::weak_ptr<render::PlayerActorPoseRuntime>>;

enum class PlayerAircraftPresentationStepStatus : std::uint8_t {
    advancedWithoutPoseEndpoint,
    advancedAndPosePublished,
    advancedWhilePoseBusy,
    simulationRejected,
    poseEndpointExpired,
    posePublicationRejected,
    unhealthy,
};

struct PlayerAircraftPresentationStepResult final {
    PlayerAircraftPresentationStepStatus status{
        PlayerAircraftPresentationStepStatus::unhealthy};
    simulation::PlayerAircraftAdvanceError simulationError{
        simulation::PlayerAircraftAdvanceError::none};
    std::optional<render::PlayerActorPoseRuntimePublishOutcome> poseOutcome;

    [[nodiscard]] constexpr bool accepted() const noexcept {
        return status ==
                PlayerAircraftPresentationStepStatus::
                    advancedWithoutPoseEndpoint ||
            status ==
                PlayerAircraftPresentationStepStatus::
                    advancedAndPosePublished ||
            status ==
                PlayerAircraftPresentationStepStatus::
                    advancedWhilePoseBusy;
    }

    [[nodiscard]] constexpr bool terminal() const noexcept {
        return !accepted();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return accepted();
    }
};

// Owns one deterministic player-aircraft state and coordinates its optional
// presentation publication. A rejected transition never changes the owned
// state. The coordinator is single-producer and does not own the pose runtime.
class PlayerAircraftPresentationCoordinator final {
public:
    PlayerAircraftPresentationCoordinator() noexcept = default;

    explicit PlayerAircraftPresentationCoordinator(
        simulation::PlayerAircraftState initialState) noexcept;

    [[nodiscard]] PlayerAircraftPresentationStepResult tryAdvance(
        const input::InputFrame& frame,
        const render::ConvertedNodeTransform& actorWorld,
        const PlayerActorPoseRuntimeEndpoint& poseEndpoint =
            std::nullopt) noexcept;

    [[nodiscard]] const simulation::PlayerAircraftState& state()
        const noexcept {
        return state_;
    }

    [[nodiscard]] std::uint64_t stateHash() const noexcept;

    [[nodiscard]] bool healthy() const noexcept {
        return healthy_;
    }

private:
    simulation::PlayerAircraftState state_{};
    bool healthy_{true};
};

} // namespace airfix::runtime
