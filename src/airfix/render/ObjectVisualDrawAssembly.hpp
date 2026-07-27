#pragma once

#include "airfix/assets/AssetResolver.hpp"
#include "airfix/render/DrawModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class ObjectVisualDrawIssueKind : std::uint8_t {
    missingBlueprintSelector,
    dependencyFailure,
    graphFailure,
    invalidResolution,
    invalidMaterialBinding,
    missingMaterialBinding,
    invalidTransform,
    geometryFailure,
    drawMeshFailure,
    limitExceeded,
    integerOverflow,
};

struct ObjectVisualDrawIssue {
    ObjectVisualDrawIssueKind kind{
        ObjectVisualDrawIssueKind::dependencyFailure};
    std::optional<std::size_t> blueprintIndex;
    std::optional<std::size_t> physicalMeshIndex;
    std::optional<std::uint32_t> sourceReference;
    std::optional<assets::DependencyIssueKind> dependencyIssue;
    std::optional<assets::BlueprintGraphIssueKind> graphIssue;
    std::optional<GeometryErrorCode> geometryError;
    std::optional<DrawMeshErrorCode> drawMeshError;
};

struct ObjectVisualDrawLimits {
    assets::ObjectSceneDependencyLimits dependencies;
    GeometryLimits geometryPerMesh;
    DrawMeshLimits drawMeshPerMesh;
    std::size_t maximumMeshes{65'536U};
    std::size_t maximumInstances{100'000U};
    std::size_t maximumMaterialBindings{65'536U};
    std::size_t maximumTotalVertices{3'000'000U};
    std::size_t maximumTotalIndices{3'000'000U};
    std::size_t maximumTotalMaterials{65'536U};
    std::size_t maximumTotalRanges{1'000'000U};
    // Logical renderer model plus parallel provenance payload bytes only
    // (element count * sizeof element). Dependency-resolution diagnostics
    // have their own count limits. This is not allocator capacity, allocator
    // overhead, or process RSS.
    std::size_t maximumTotalBytes{512U * 1024U * 1024U};
};

struct ObjectVisualProvenance {
    std::size_t blueprintIndex{};
    std::size_t physicalMeshIndex{};

    [[nodiscard]] friend constexpr bool operator==(
        const ObjectVisualProvenance&,
        const ObjectVisualProvenance&) = default;
};

struct ObjectVisualDrawAssembly {
    // Resolution is produced by this builder and retained for diagnostics.
    // On any issue the model and both provenance vectors remain empty.
    assets::ObjectSceneDependencyResolution resolution;
    DrawModelPayload model;
    // Parallel to model.meshes. The blueprint is the first DFS use that
    // assigned the physical mesh its stable slot.
    std::vector<ObjectVisualProvenance> meshProvenance;
    // Parallel to model.instances in stable mesh-bearing blueprint DFS order.
    std::vector<ObjectVisualProvenance> instanceProvenance;
    std::vector<ObjectVisualDrawIssue> issues;
};

// Builds the complete drawable subtree selected by ObjectDefinition::meshName.
// The dependency resolution is deliberately repeated internally. Physical
// meshes are shared in first-use order, while instances retain stable
// mesh-bearing blueprint DFS order. Blueprint transforms remain converted
// authored-world values; parent transforms are not recomposed here. A later
// actor adapter must derive descendants relative to the selected root and
// apply the SetSkin root-slot pose override.
[[nodiscard]] ObjectVisualDrawAssembly buildObjectVisualDrawAssembly(
    const assets::ObjectDefinition& object,
    const assets::CcfMetadata& ccf,
    std::span<const DrawMaterial> materialBindings,
    const BasisTransform& basis = {},
    UvPolicy uvPolicy = UvPolicy::preserveRaw,
    const ObjectVisualDrawLimits& limits = {});

} // namespace airfix::render
