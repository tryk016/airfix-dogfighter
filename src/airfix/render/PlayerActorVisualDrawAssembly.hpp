#pragma once

#include "airfix/render/ObjectVisualDrawAssembly.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class PlayerActorVisualDrawIssueKind : std::uint8_t {
    objectVisualFailure,
    invalidObjectVisualAssembly,
    invalidSelectedRootTransform,
    invalidActorLocalTransform,
    limitExceeded,
    integerOverflow,
};

struct PlayerActorVisualDrawIssue {
    PlayerActorVisualDrawIssueKind kind{
        PlayerActorVisualDrawIssueKind::objectVisualFailure};
    std::optional<ObjectVisualDrawIssueKind> objectVisualIssue;
    std::optional<std::size_t> blueprintIndex;
    std::optional<std::size_t> physicalMeshIndex;
    std::optional<std::uint32_t> sourceReference;
    std::optional<assets::DependencyIssueKind> dependencyIssue;
    std::optional<assets::BlueprintGraphIssueKind> graphIssue;
    std::optional<GeometryErrorCode> geometryError;
    std::optional<DrawMeshErrorCode> drawMeshError;
};

struct PlayerActorVisualProvenance {
    // AfVehicle::SetSkin's currently recovered visible third-person slot.
    // Slots 1 and 2 are deliberately not inferred or published here.
    std::uint8_t legacySkinSlot{};
    std::size_t blueprintIndex{};
    std::uint32_t blueprintReference{};
    std::size_t physicalMeshIndex{};

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerActorVisualProvenance&,
        const PlayerActorVisualProvenance&) = default;
};

struct PlayerActorVisualDrawAssembly {
    // Retained even on failure so diagnostics can identify the authenticated
    // dependency selection without reading from any side channel.
    assets::ObjectSceneDependencyResolution resolution;
    DrawModelPayload model;
    // Parallel to model.meshes and model.instances respectively.
    std::vector<PlayerActorVisualProvenance> meshProvenance;
    std::vector<PlayerActorVisualProvenance> instanceProvenance;
    std::vector<PlayerActorVisualDrawIssue> issues;
};

// Adapts the selected ObjectDefinition subtree to the recovered visible
// AfVehicle::SetSkin slot 0. buildObjectVisualDrawAssembly first produces
// converted authored-world blueprint transforms. This adapter derives every
// mesh node relative to the selected authored root and replaces that root
// with the recovered actor-local zero-translation, legacy Y(pi) pose.
//
// The resulting transforms are actor-local only. A later, separately
// authenticated publication step may compose absolute spawn world exactly
// once as composeNodeTransforms(spawnWorld, actorLocal). This function does
// not choose a player object, assign final scene indices, publish hidden skin
// slots, or perform any archive/filesystem reads.
//
// On every issue the model and both provenance vectors remain empty.
[[nodiscard]] PlayerActorVisualDrawAssembly
buildPlayerActorVisualDrawAssembly(
    const assets::ObjectDefinition& object,
    const assets::CcfMetadata& ccf,
    std::span<const DrawMaterial> materialBindings,
    const BasisTransform& basis = {},
    UvPolicy uvPolicy = UvPolicy::preserveRaw,
    const ObjectVisualDrawLimits& limits = {});

} // namespace airfix::render
