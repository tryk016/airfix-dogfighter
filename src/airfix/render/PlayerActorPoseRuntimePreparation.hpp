#pragma once

#include "airfix/render/DrawModel.hpp"
#include "airfix/render/PlayerActorPoseRuntime.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace airfix::render {

enum class PlayerActorPoseRuntimePreparationStatus : std::uint8_t {
    noPlayer,
    ready,
    invalidPayload,
    resourceLimit,
};

struct PlayerActorPoseRuntimePlan final {
    PlayerActorPoseRuntimePreparationStatus status{
        PlayerActorPoseRuntimePreparationStatus::noPlayer};
    DynamicInstancePoseLimits exactLimits{};
    std::size_t retainedPoseBytes{};

    [[nodiscard]] constexpr bool ready() const noexcept {
        return status == PlayerActorPoseRuntimePreparationStatus::ready;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerActorPoseRuntimePlan&,
        const PlayerActorPoseRuntimePlan&) noexcept = default;
};

struct PlayerActorPoseRuntimePreparation final {
    PlayerActorPoseRuntimePreparationStatus status{
        PlayerActorPoseRuntimePreparationStatus::noPlayer};
    std::shared_ptr<PlayerActorPoseRuntime> runtime;

    [[nodiscard]] bool complete() const noexcept {
        return status == PlayerActorPoseRuntimePreparationStatus::ready &&
            runtime != nullptr;
    }
};

// Allocation-free preflight for the exact two-slot player-pose runtime. It
// returns the retained CPU byte count before either backend reserves the
// runtime. The authenticated no-player path is distinct from malformed
// player provenance.
[[nodiscard]] PlayerActorPoseRuntimePlan planPlayerActorPoseRuntime(
    const std::optional<PlayerActorSceneBinding>& actorBinding,
    std::span<const PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance,
    std::span<const DrawMeshInstance> authoredInstances) noexcept;

// Allocates and verifies the runtime only after the caller has admitted the
// plan into its platform budget. The supplied plan must exactly match a fresh
// preflight of the same immutable scene inputs. Step zero is acquired and
// checked bit-for-bit against the authored instance transforms before the
// runtime is returned.
[[nodiscard]] PlayerActorPoseRuntimePreparation
preparePlayerActorPoseRuntime(
    const std::optional<PlayerActorSceneBinding>& actorBinding,
    std::span<const PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance,
    std::span<const DrawMeshInstance> authoredInstances,
    const ConvertedNodeTransform& initialActorWorld,
    const PlayerActorPoseRuntimePlan& plan) noexcept;

} // namespace airfix::render
