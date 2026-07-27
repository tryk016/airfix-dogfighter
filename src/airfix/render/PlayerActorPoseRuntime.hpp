#pragma once

#include "airfix/render/PlayerActorPoseFrame.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

struct PlayerActorPoseRuntimeSource {
    std::uint32_t finalInstanceIndex{};
    ConvertedNodeTransform actorLocal;
};

enum class PlayerActorPoseRuntimeBuildIssueKind : std::uint8_t {
    missingActorBinding,
    emptyActor,
    totalInstanceCountNotRepresentable,
    instanceLimitNotExact,
    overrideLimitNotExact,
    frameByteSizeOverflow,
    frameByteLimitNotExact,
    retainedByteSizeOverflow,
    initialFrameFailure,
    initialSceneFailure,
    initialPublishFailure,
    allocationFailure,
};

struct PlayerActorPoseRuntimeBuildIssue {
    PlayerActorPoseRuntimeBuildIssueKind kind{
        PlayerActorPoseRuntimeBuildIssueKind::missingActorBinding};
    std::optional<PlayerActorPoseFrameIssueKind> frameIssue;
    std::optional<DynamicInstancePosePublishResult> exchangeResult;
};

enum class PlayerActorPoseRuntimePublishResult : std::uint8_t {
    published,
    invalidActorWorld,
    invalidComposition,
    wrongOrStaleScene,
    simulationStepNotIncreasing,
    overrideLimitExceeded,
    frameByteLimitExceeded,
    instanceIndexOutOfRange,
    overridesNotStrictlyIncreasing,
    nonFiniteTransform,
    generationExhausted,
    busy,
};

struct PlayerActorPoseRuntimePublishOutcome {
    PlayerActorPoseRuntimePublishResult result{
        PlayerActorPoseRuntimePublishResult::wrongOrStaleScene};
    std::optional<std::size_t> actorInstanceIndex;
    std::optional<GeometryErrorCode> geometryError;
};

[[nodiscard]] constexpr PlayerActorPoseRuntimePublishResult
mapPlayerActorPoseRuntimePublishResult(
    const DynamicInstancePosePublishResult result) noexcept {
    switch (result) {
    case DynamicInstancePosePublishResult::published:
        return PlayerActorPoseRuntimePublishResult::published;
    case DynamicInstancePosePublishResult::wrongOrStaleScene:
        return PlayerActorPoseRuntimePublishResult::wrongOrStaleScene;
    case DynamicInstancePosePublishResult::simulationStepNotIncreasing:
        return PlayerActorPoseRuntimePublishResult::
            simulationStepNotIncreasing;
    case DynamicInstancePosePublishResult::overrideLimitExceeded:
        return PlayerActorPoseRuntimePublishResult::overrideLimitExceeded;
    case DynamicInstancePosePublishResult::frameByteLimitExceeded:
        return PlayerActorPoseRuntimePublishResult::frameByteLimitExceeded;
    case DynamicInstancePosePublishResult::instanceIndexOutOfRange:
        return PlayerActorPoseRuntimePublishResult::instanceIndexOutOfRange;
    case DynamicInstancePosePublishResult::overridesNotStrictlyIncreasing:
        return PlayerActorPoseRuntimePublishResult::
            overridesNotStrictlyIncreasing;
    case DynamicInstancePosePublishResult::nonFiniteTransform:
        return PlayerActorPoseRuntimePublishResult::nonFiniteTransform;
    case DynamicInstancePosePublishResult::generationExhausted:
        return PlayerActorPoseRuntimePublishResult::generationExhausted;
    case DynamicInstancePosePublishResult::busy:
        return PlayerActorPoseRuntimePublishResult::busy;
    }
    return PlayerActorPoseRuntimePublishResult::wrongOrStaleScene;
}

struct PlayerActorPoseRuntimeBuildResult;

// One runtime belongs to exactly one scene. Factory/preparation is the initial
// producer. After handoff, exactly one producer may call tryPublish while one
// render consumer calls tryAcquire. The object and its scene handle never
// move. Creating a fresh scene requires creating a fresh runtime.
class PlayerActorPoseRuntime final {
public:
    PlayerActorPoseRuntime(const PlayerActorPoseRuntime&) = delete;
    PlayerActorPoseRuntime& operator=(const PlayerActorPoseRuntime&) = delete;
    PlayerActorPoseRuntime(PlayerActorPoseRuntime&&) = delete;
    PlayerActorPoseRuntime& operator=(PlayerActorPoseRuntime&&) = delete;
    ~PlayerActorPoseRuntime() = default;

    [[nodiscard]] static PlayerActorPoseRuntimeBuildResult create(
        const std::optional<PlayerActorSceneBinding>& actorBinding,
        std::span<const PlayerActorSceneInstanceProvenance>
            actorInstanceProvenance,
        std::size_t totalInstanceCount,
        const ConvertedNodeTransform& initialActorWorld,
        std::uint64_t initialSimulationStep,
        const DynamicInstancePoseLimits& exactLimits);

    // Allocation-free and bounded. A failure before exchange publication may
    // update producer-only scratch, but cannot alter the last published slot.
    [[nodiscard]] PlayerActorPoseRuntimePublishOutcome tryPublish(
        const ConvertedNodeTransform& actorWorld,
        std::uint64_t simulationStep) noexcept;

    [[nodiscard]] std::optional<DynamicInstancePoseLease>
    tryAcquire() noexcept;

    [[nodiscard]] std::size_t retainedBytes() const noexcept {
        return retainedBytes_;
    }

private:
    PlayerActorPoseRuntime(
        std::vector<PlayerActorPoseRuntimeSource> sources,
        std::vector<DynamicInstancePoseOverride> scratch,
        const DynamicInstancePoseLimits& exactLimits,
        std::size_t retainedBytes);

    std::vector<PlayerActorPoseRuntimeSource> sources_;
    std::vector<DynamicInstancePoseOverride> scratch_;
    ScenePoseExchange exchange_;
    std::optional<ScenePoseHandle> scene_;
    std::size_t retainedBytes_{};
};

struct PlayerActorPoseRuntimeBuildResult {
    std::unique_ptr<PlayerActorPoseRuntime> runtime;
    std::optional<PlayerActorPoseRuntimeBuildIssue> issue;
    std::size_t retainedBytes{};

    [[nodiscard]] bool complete() const noexcept {
        return runtime != nullptr && !issue.has_value() &&
            retainedBytes != 0U;
    }
};

} // namespace airfix::render
