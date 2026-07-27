#pragma once

#include "airfix/assets/AssetResolver.hpp"
#include "airfix/assets/CcfPlacedScene.hpp"
#include "airfix/assets/MissionWorldRooms.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::assets {

struct MissionWorldRoomMeshPlanEntry {
    std::size_t sourceIndex{};
    std::size_t physicalMeshIndex{};

    friend bool operator==(
        const MissionWorldRoomMeshPlanEntry&,
        const MissionWorldRoomMeshPlanEntry&) = default;
};

struct MissionWorldRoomPlacedPlanEntry {
    std::size_t sourceIndex{};
    std::size_t placedNodeIndex{};
    std::size_t meshSlot{};
    // Parsed-room instances retain their selected-room contributor. Receiver
    // fallback instances have neither field and are emitted once per source.
    std::optional<std::size_t> contributorIndex;
    std::optional<std::size_t> physicalRoomIndex;

    friend bool operator==(
        const MissionWorldRoomPlacedPlanEntry&,
        const MissionWorldRoomPlacedPlanEntry&) = default;
};

struct MissionWorldRoomMaterialPlanEntry {
    std::size_t sourceIndex{};
    std::size_t physicalMaterialIndex{};

    friend bool operator==(
        const MissionWorldRoomMaterialPlanEntry&,
        const MissionWorldRoomMaterialPlanEntry&) = default;
};

struct MissionWorldRoomTexturePlanEntry {
    std::size_t sourceIndex{};
    TextureDependency dependency;

    [[nodiscard]] friend bool operator==(
        const MissionWorldRoomTexturePlanEntry& left,
        const MissionWorldRoomTexturePlanEntry& right) {
        return left.sourceIndex == right.sourceIndex &&
            left.dependency.role == right.dependency.role &&
            left.dependency.materialReference ==
                right.dependency.materialReference &&
            left.dependency.materialIndex ==
                right.dependency.materialIndex &&
            left.dependency.sourceText ==
                right.dependency.sourceText;
    }
};

enum class MissionWorldRoomDrawPlanIssueKind : std::uint8_t {
    catalogIncomplete,
    sourceCountMismatch,
    invalidSource,
    invalidRoomSectionLayout,
    invalidTopLevelOrder,
    worldRoomIndexOutOfRange,
    invalidContributor,
    invalidRoomReferenceMap,
    placedSceneDependency,
    invalidPlacedNode,
    invalidMeshIndex,
    materialNotFound,
    materialAmbiguous,
    limitExceeded,
    integerOverflow,
};

struct MissionWorldRoomDrawPlanIssue {
    MissionWorldRoomDrawPlanIssueKind kind{
        MissionWorldRoomDrawPlanIssueKind::catalogIncomplete};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> contributorIndex;
    std::optional<std::size_t> physicalRoomIndex;
    std::optional<std::size_t> placedNodeIndex;
    std::optional<std::size_t> physicalMeshIndex;
    std::optional<std::uint32_t> materialReference;
    std::optional<std::uint32_t> roomReference;
    std::optional<PlacedSceneIssueKind> placedSceneIssue;
};

struct MissionWorldRoomDrawPlanLimits {
    PlacedSceneLimits placedScenePerSource;
    std::size_t maximumSources{65'536U};
    std::size_t maximumRuntimeRooms{100'000U};
    std::size_t maximumContributors{262'144U};
    std::size_t maximumRoomSections{65'536U};
    std::size_t maximumTopLevelSections{262'144U};
    std::size_t maximumPhysicalRooms{262'144U};
    std::size_t maximumScannedMeshes{262'144U};
    std::size_t maximumScannedPlacedNodes{1'000'000U};
    std::size_t maximumScannedMaterials{250'000U};
    std::size_t maximumInstances{1'000'000U};
    std::size_t maximumUniqueMeshes{100'000U};
    std::size_t maximumMaterialReferences{262'144U};
    std::size_t maximumTextureEdges{262'144U};
    std::size_t maximumRetainedSourceTextBytes{
        16U * 1024U * 1024U};
};

struct MissionWorldRoomDrawPlan {
    std::size_t sourceCount{};
    std::optional<std::size_t> worldRoomIndex;
    // Global first-use mesh order across sources.
    std::vector<MissionWorldRoomMeshPlanEntry> meshes;
    // Source load order, then physical placed-node order within each source.
    std::vector<MissionWorldRoomPlacedPlanEntry> placedNodes;
    // Global mesh order, then physical triangle first-use order.
    std::vector<MissionWorldRoomMaterialPlanEntry> materials;
    std::vector<MissionWorldRoomTexturePlanEntry> textures;
    std::vector<MissionWorldRoomDrawPlanIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return worldRoomIndex.has_value() && issues.empty();
    }
};

// Resolves one runtime room across the exact ordered LoadSceneCcf source list.
// Parsed-room objects are selected through catalog contributors. An unresolved
// room reference is published to root exactly once while its source's placed
// scene is scanned, independent of how many primary physical records bind the
// receiver. Sources loaded with flag 0x2000 contribute rooms but no instances.
// Any issue clears the complete source-aware plan atomically.
[[nodiscard]] MissionWorldRoomDrawPlan resolveMissionWorldRoomDrawPlan(
    const MissionWorldRoomCatalog& catalog,
    std::span<const MissionCcfRoomLoadSource> sources,
    std::size_t worldRoomIndex,
    const MissionWorldRoomDrawPlanLimits& limits = {});

} // namespace airfix::assets
