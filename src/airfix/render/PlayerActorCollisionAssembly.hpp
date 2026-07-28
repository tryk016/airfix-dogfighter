#pragma once

#include "airfix/render/LegacyDynamicBsp.hpp"
#include "airfix/render/PlayerActorVisualDrawAssembly.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class PlayerActorCollisionIssueKind : std::uint8_t {
    actorVisualFailure,
    invalidActorVisualAssembly,
    invalidMeshProvenance,
    invalidInstanceProvenance,
    missingMaterialBinding,
    duplicateMaterialBinding,
    geometryFailure,
    dynamicBspFailure,
    invalidActorLocalTransform,
    limitExceeded,
    retainedByteLimitExceeded,
    integerOverflow,
    allocationFailure,
};

struct PlayerActorCollisionIssue final {
    PlayerActorCollisionIssueKind kind{
        PlayerActorCollisionIssueKind::invalidActorVisualAssembly};
    std::optional<PlayerActorVisualDrawIssueKind> actorVisualIssue;
    std::optional<LegacyDynamicBspBuildIssueKind> dynamicBspIssue;
    std::optional<GeometryErrorCode> geometryError;
    std::optional<std::size_t> meshIndex;
    std::optional<std::size_t> instanceIndex;
    std::optional<std::size_t> physicalMeshIndex;
    std::optional<std::uint32_t> materialReference;
};

struct PlayerActorCollisionMeshProvenance final {
    PlayerActorVisualProvenance actor;
    std::size_t collisionMeshIndex{};
    std::uint32_t sourceMeshReference{};

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerActorCollisionMeshProvenance&,
        const PlayerActorCollisionMeshProvenance&) noexcept = default;
};

struct PlayerActorCollisionInstance final {
    std::size_t collisionMeshIndex{};
    PlayerActorVisualProvenance actor;
    ConvertedNodeTransform actorLocal;

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerActorCollisionInstance& left,
        const PlayerActorCollisionInstance& right) noexcept {
        return left.collisionMeshIndex == right.collisionMeshIndex &&
            left.actor == right.actor &&
            left.actorLocal.linear == right.actorLocal.linear &&
            left.actorLocal.translation == right.actorLocal.translation &&
            left.actorLocal.rawScalar == right.actorLocal.rawScalar;
    }
};

struct PlayerActorCollisionLimits final {
    GeometryLimits geometryPerMesh{};
    LegacyDynamicBspBuildLimits dynamicBspPerMesh{};
    std::size_t maximumMeshes{65'536U};
    std::size_t maximumInstances{1'000'000U};
    std::size_t maximumMaterialBindings{65'536U};
    // Complete retained output: outer mesh/provenance/instance records plus
    // every nested LegacyDynamicBspMesh payload. Allocator capacity and
    // transient conversion/build memory are intentionally excluded.
    std::uint64_t maximumRetainedBytes{512U * 1024U * 1024U};
};

struct PlayerActorCollisionAssembly final {
    // Physical meshes retain PlayerActorVisualDrawAssembly first-use order.
    std::vector<LegacyDynamicBspMesh> meshes;
    std::vector<PlayerActorCollisionMeshProvenance> meshProvenance;
    // Actor-local instances retain blueprint DFS order.
    std::vector<PlayerActorCollisionInstance> instances;
    std::uint64_t retainedPayloadBytes{};
    std::vector<PlayerActorCollisionIssue> issues;

    [[nodiscard]] bool complete() const noexcept;
};

// Builds immutable collision assets from the same CCF and actor-local visual
// that the authenticated mission loader uses for rendering. Geometry and
// material collision properties are resolved by retained physical indices and
// references; names and filesystem paths never cross this boundary.
//
// The operation is a bounded mission-load step and may allocate. On every
// issue all publishable vectors are cleared atomically.
[[nodiscard]] PlayerActorCollisionAssembly
buildPlayerActorCollisionAssembly(
    const assets::CcfMetadata& ccf,
    const PlayerActorVisualDrawAssembly& actorVisual,
    const BasisTransform& basis = {},
    const PlayerActorCollisionLimits& limits = {});

enum class PlayerActorCollisionPublicationStatus : std::uint8_t {
    published,
    invalidAssembly,
    invalidInput,
    outputSizeMismatch,
    invalidTransform,
};

// Publishes one actor's current pose into exact-size caller-owned storage.
// Output object order remains the immutable actor-instance order. Exactly one
// room range contains the complete span; all other ranges are empty.
//
// Validation is performed in a complete first pass, so failure leaves both
// output spans untouched. The successful hot path is allocation-free,
// noexcept, and suitable for a simulation-frame publication boundary.
[[nodiscard]] PlayerActorCollisionPublicationStatus
publishPlayerActorCollisionFrame(
    const PlayerActorCollisionAssembly& assembly,
    const ConvertedNodeTransform& actorWorld,
    std::uint32_t actorObjectId,
    bool active,
    std::size_t worldRoomIndex,
    std::span<LegacyDynamicBspLineObject> outputObjects,
    std::span<LegacyDynamicBspRoomObjectRange> outputRoomRanges) noexcept;

} // namespace airfix::render
