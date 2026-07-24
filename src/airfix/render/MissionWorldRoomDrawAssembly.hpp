#pragma once

#include "airfix/assets/MissionWorldRoomDrawPlan.hpp"
#include "airfix/render/DrawModel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

struct MissionWorldRoomDrawSource {
    // Metadata and bindings must outlive buildMissionWorldRoomDrawAssembly.
    const assets::CcfMetadata* ccf{};
    // Every source uses the caller's one global TextureAssetId namespace.
    std::span<const DrawMaterial> materialBindings;
};

enum class MissionWorldRoomDrawIssueKind : std::uint8_t {
    planDependency,
    sourceCountMismatch,
    invalidSource,
    invalidPlan,
    invalidMaterialBinding,
    missingMaterialBinding,
    invalidPlacedNode,
    unsupportedPlacedOrientation,
    invalidTransform,
    geometryFailure,
    drawMeshFailure,
    limitExceeded,
    integerOverflow,
};

struct MissionWorldRoomDrawIssue {
    MissionWorldRoomDrawIssueKind kind{
        MissionWorldRoomDrawIssueKind::planDependency};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> placedNodeIndex;
    std::optional<std::size_t> physicalMeshIndex;
    std::optional<std::uint32_t> materialReference;
    std::optional<assets::MissionWorldRoomDrawPlanIssueKind>
        planIssue;
    std::optional<GeometryErrorCode> geometryError;
    std::optional<DrawMeshErrorCode> drawMeshError;
};

struct MissionWorldRoomDrawLimits {
    assets::MissionWorldRoomDrawPlanLimits plan;
    GeometryLimits geometryPerMesh;
    DrawMeshLimits drawMeshPerMesh;
    std::size_t maximumSources{65'536U};
    std::size_t maximumMeshes{65'536U};
    std::size_t maximumInstances{1'000'000U};
    std::size_t maximumMaterialBindings{262'144U};
    std::size_t maximumTotalVertices{3'000'000U};
    std::size_t maximumTotalIndices{3'000'000U};
    std::size_t maximumTotalMaterials{65'536U};
    std::size_t maximumTotalRanges{1'000'000U};
    std::size_t maximumTotalBytes{512U * 1024U * 1024U};
};

struct MissionWorldRoomMeshProvenance {
    std::size_t sourceIndex{};
    std::size_t physicalMeshIndex{};

    friend bool operator==(
        const MissionWorldRoomMeshProvenance&,
        const MissionWorldRoomMeshProvenance&) = default;
};

struct MissionWorldRoomInstanceProvenance {
    std::size_t sourceIndex{};
    std::size_t placedNodeIndex{};
    std::optional<std::size_t> contributorIndex;
    std::optional<std::size_t> physicalRoomIndex;

    friend bool operator==(
        const MissionWorldRoomInstanceProvenance&,
        const MissionWorldRoomInstanceProvenance&) = default;
};

struct MissionWorldRoomDrawAssembly {
    std::optional<std::size_t> worldRoomIndex;
    DrawModelPayload model;
    // Parallel to model.meshes and model.instances on complete success.
    std::vector<MissionWorldRoomMeshProvenance> meshProvenance;
    std::vector<MissionWorldRoomInstanceProvenance>
        instanceProvenance;
    std::vector<MissionWorldRoomDrawIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return worldRoomIndex.has_value() && issues.empty();
    }
};

// Re-resolves the source-aware plan from the canonical catalog and exact load
// sources before producing one atomic renderer-facing model. Draw-source CCF
// identity must match the load list. Mesh identity is
// {sourceIndex, physicalMeshIndex}; references and bindings never cross source
// boundaries. Provenance distinguishes parsed-room contributors from receiver
// fallback records.
[[nodiscard]] MissionWorldRoomDrawAssembly
buildMissionWorldRoomDrawAssembly(
    const assets::MissionWorldRoomCatalog& catalog,
    std::span<const assets::MissionCcfRoomLoadSource> loadSources,
    std::size_t worldRoomIndex,
    std::span<const MissionWorldRoomDrawSource> drawSources,
    const BasisTransform& basis = {},
    UvPolicy uvPolicy = UvPolicy::preserveRaw,
    const MissionWorldRoomDrawLimits& limits = {});

} // namespace airfix::render
