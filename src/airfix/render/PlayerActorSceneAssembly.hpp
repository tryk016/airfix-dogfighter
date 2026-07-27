#pragma once

#include "airfix/render/PlayerActorVisualDrawAssembly.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::render {

enum class PlayerActorSceneIssueKind : std::uint8_t {
    actorVisualFailure,
    invalidActorVisualAssembly,
    emptyActorVisual,
    invalidStaticMeshSlot,
    invalidActorMeshSlot,
    invalidStaticTransform,
    invalidActorWorldTransform,
    invalidActorLocalTransform,
    invalidComposedTransform,
    invalidActorProvenance,
    meshSlotOverflow,
    instanceIndexOverflow,
    limitExceeded,
    integerOverflow,
};

struct PlayerActorSceneIssue {
    PlayerActorSceneIssueKind kind{
        PlayerActorSceneIssueKind::invalidActorVisualAssembly};
    std::optional<PlayerActorVisualDrawIssueKind> actorVisualIssue;
    std::optional<std::size_t> staticInstanceIndex;
    std::optional<std::size_t> actorInstanceIndex;
    std::optional<std::size_t> actorMeshSlot;
    std::optional<PlayerActorVisualProvenance> actorProvenance;
    std::optional<GeometryErrorCode> geometryError;
};

struct PlayerActorSceneMeshProvenance {
    PlayerActorVisualProvenance actor;
    std::uint32_t finalMeshSlot{};

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerActorSceneMeshProvenance&,
        const PlayerActorSceneMeshProvenance&) = default;
};

struct PlayerActorSceneInstanceProvenance {
    PlayerActorVisualProvenance actor;
    std::uint32_t finalInstanceIndex{};
    // Exact operand used on the right-hand side of the one world
    // composition; retained before any absolute placement is applied.
    ConvertedNodeTransform actorLocal;

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerActorSceneInstanceProvenance& left,
        const PlayerActorSceneInstanceProvenance& right) {
        return left.actor == right.actor &&
            left.finalInstanceIndex == right.finalInstanceIndex &&
            left.actorLocal.linear == right.actorLocal.linear &&
            left.actorLocal.translation == right.actorLocal.translation &&
            left.actorLocal.rawScalar == right.actorLocal.rawScalar;
    }
};

// Actor meshes and instances are appended as contiguous ranges. This binding
// therefore preserves every exact actor instance index without a redundant
// allocation: [firstInstanceIndex, firstInstanceIndex + instanceCount).
struct PlayerActorSceneBinding {
    std::size_t firstMeshSlot{};
    std::size_t meshCount{};
    std::size_t firstInstanceIndex{};
    std::size_t instanceCount{};

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerActorSceneBinding&,
        const PlayerActorSceneBinding&) = default;
};

struct PlayerActorSceneLimits {
    std::size_t maximumMeshes{65'536U};
    std::size_t maximumInstances{1'000'000U};
    std::size_t maximumTotalVertices{3'000'000U};
    std::size_t maximumTotalIndices{3'000'000U};
    std::size_t maximumTotalMaterials{65'536U};
    std::size_t maximumTotalRanges{1'000'000U};
    // Complete logical output only: model vector elements and nested mesh
    // elements, actor-only provenance, and one binding. This is not allocator
    // capacity/overhead, transient working memory, or process RSS.
    std::size_t maximumTotalBytes{512U * 1024U * 1024U};
};

struct PlayerActorSceneAssembly {
    DrawModelPayload model;
    // Parallel to the actor mesh and instance subranges, not to static data.
    std::vector<PlayerActorSceneMeshProvenance> actorMeshProvenance;
    std::vector<PlayerActorSceneInstanceProvenance>
        actorInstanceProvenance;
    std::optional<PlayerActorSceneBinding> actorBinding;
    std::vector<PlayerActorSceneIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return actorBinding.has_value() && issues.empty();
    }
};

// Atomically appends one already-authenticated actor visual to a caller-owned
// static renderer model. Neither input is resolved by names, references, or
// indices across the static/actor boundary. Actor physical meshes retain their
// upstream first-use order and actor-local instances retain their upstream
// order. Every actor instance receives exactly one world composition:
// composeNodeTransforms(actorWorld, actorLocal).
//
// Inputs are accepted by value so a caller can choose transactional copies or
// transfer ownership with std::move. Static transforms are never changed. On
// every issue the output model, provenance, and binding are all empty.
[[nodiscard]] PlayerActorSceneAssembly buildPlayerActorSceneAssembly(
    DrawModelPayload staticModel,
    PlayerActorVisualDrawAssembly actorVisual,
    const ConvertedNodeTransform& actorWorld,
    const PlayerActorSceneLimits& limits = {});

} // namespace airfix::render
