#pragma once

#include "airfix/render/DynamicInstancePose.hpp"
#include "airfix/render/PlayerActorSceneAssembly.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class PlayerActorPoseFrameIssueKind : std::uint8_t {
    missingActorBinding,
    emptyActorBinding,
    bindingInstanceCountMismatch,
    meshBindingRangeOverflow,
    instanceBindingRangeOverflow,
    instanceIndexNotRepresentable,
    instanceCeilingExceeded,
    overrideLimitExceeded,
    frameByteSizeOverflow,
    frameByteLimitExceeded,
    provenanceInstanceIndexMismatch,
    invalidActorWorldTransform,
    invalidActorLocalTransform,
    invalidComposedTransform,
};

struct PlayerActorPoseFrameIssue {
    PlayerActorPoseFrameIssueKind kind{
        PlayerActorPoseFrameIssueKind::missingActorBinding};
    std::optional<std::size_t> actorInstanceIndex;
    std::optional<std::size_t> expectedFinalInstanceIndex;
    std::optional<std::uint32_t> actualFinalInstanceIndex;
    std::optional<GeometryErrorCode> geometryError;
};

// Owns the complete override payload. A view remains valid only while this
// object is alive and its override vector has not been modified or moved.
struct PlayerActorPoseFrame {
    std::uint64_t simulationStep{};
    std::vector<DynamicInstancePoseOverride> overrides;
    std::vector<PlayerActorPoseFrameIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return !overrides.empty() && issues.empty();
    }

    [[nodiscard]] DynamicInstancePoseFrameView frameView() const& noexcept {
        return {
            .simulationStep = simulationStep,
            .overrides = overrides,
        };
    }
    [[nodiscard]] DynamicInstancePoseFrameView frameView() const&& = delete;
};

// Builds one bounded dynamic pose frame for the authenticated actor subrange.
// The provenance order is authoritative: entry i must name exactly
// binding.firstInstanceIndex + i. Every accepted entry is composed exactly
// once as composeNodeTransforms(actorWorld, actorLocal). On every issue the
// returned override vector is empty.
[[nodiscard]] PlayerActorPoseFrame buildPlayerActorPoseFrame(
    const std::optional<PlayerActorSceneBinding>& actorBinding,
    std::span<const PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance,
    const ConvertedNodeTransform& actorWorld,
    std::uint64_t simulationStep,
    const DynamicInstancePoseLimits& limits = {});

} // namespace airfix::render
