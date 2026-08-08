#pragma once

#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/render/ObjectVisualDrawAssembly.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

struct LevelObjectTargetRoomIdentity {
  std::size_t worldRoomIndex{};

  [[nodiscard]] friend constexpr bool
  operator==(const LevelObjectTargetRoomIdentity &,
             const LevelObjectTargetRoomIdentity &) = default;
};

struct LevelObjectDrawSource {
  std::size_t placementIndex{};
  std::size_t sourceIndex{};
  std::size_t uniqueObjectDefinitionIndex{};
  const assets::LevelObjectPlacement *placement{};
  const assets::ObjectDefinition *objectDefinition{};
  const assets::CcfMetadata *ccf{};
  std::optional<LevelObjectTargetRoomIdentity> targetRoom;
};

enum class LevelObjectSceneIssueKind : std::uint8_t {
  invalidRoomModel,
  invalidSource,
  invalidRoom,
  missingSelector,
  objectVisualFailure,
  invalidObjectVisualAssembly,
  invalidAuthoredRootTransform,
  invalidAuthoredNodeTransform,
  invalidPlacementTransform,
  invalidLocalTransform,
  invalidComposedTransform,
  meshSlotOverflow,
  instanceIndexOverflow,
  limitExceeded,
  integerOverflow,
};

struct LevelObjectSceneIssue {
  LevelObjectSceneIssueKind kind{LevelObjectSceneIssueKind::invalidSource};
  std::optional<std::size_t> placementIndex;
  std::optional<std::size_t> sourceIndex;
  std::optional<std::size_t> uniqueObjectDefinitionIndex;
  std::optional<std::size_t> blueprintIndex;
  std::optional<std::size_t> physicalMeshIndex;
  std::optional<ObjectVisualDrawIssueKind> objectVisualIssue;
  std::optional<GeometryErrorCode> geometryError;
};

struct LevelObjectSceneMeshProvenance {
  std::size_t placementIndex{};
  std::size_t sourceIndex{};
  std::size_t uniqueObjectDefinitionIndex{};
  LevelObjectTargetRoomIdentity targetRoom;
  std::size_t blueprintIndex{};
  std::size_t physicalMeshIndex{};
  std::uint32_t finalMeshSlot{};

  [[nodiscard]] friend constexpr bool
  operator==(const LevelObjectSceneMeshProvenance &,
             const LevelObjectSceneMeshProvenance &) = default;
};

struct LevelObjectSceneInstanceProvenance {
  std::size_t placementIndex{};
  std::size_t sourceIndex{};
  std::size_t uniqueObjectDefinitionIndex{};
  LevelObjectTargetRoomIdentity targetRoom;
  std::size_t blueprintIndex{};
  std::size_t physicalMeshIndex{};
  std::uint32_t finalMeshSlot{};
  std::uint32_t finalInstanceIndex{};
  ConvertedNodeTransform local;

  [[nodiscard]] friend constexpr bool
  operator==(const LevelObjectSceneInstanceProvenance &left,
             const LevelObjectSceneInstanceProvenance &right) {
    return left.placementIndex == right.placementIndex &&
           left.sourceIndex == right.sourceIndex &&
           left.uniqueObjectDefinitionIndex ==
               right.uniqueObjectDefinitionIndex &&
           left.targetRoom == right.targetRoom &&
           left.blueprintIndex == right.blueprintIndex &&
           left.physicalMeshIndex == right.physicalMeshIndex &&
           left.finalMeshSlot == right.finalMeshSlot &&
           left.finalInstanceIndex == right.finalInstanceIndex &&
           left.local.linear == right.local.linear &&
           left.local.translation == right.local.translation &&
           left.local.rawScalar == right.local.rawScalar;
  }
};

struct LevelObjectScenePlacementRange {
  std::size_t placementIndex{};
  std::size_t sourceIndex{};
  std::size_t uniqueObjectDefinitionIndex{};
  LevelObjectTargetRoomIdentity targetRoom;
  std::size_t firstMeshSlot{};
  std::size_t meshCount{};
  std::size_t firstInstanceIndex{};
  std::size_t instanceCount{};

  [[nodiscard]] friend constexpr bool
  operator==(const LevelObjectScenePlacementRange &,
             const LevelObjectScenePlacementRange &) = default;
};

struct LevelObjectSceneLimits {
  ObjectVisualDrawLimits visualPerPlacement;
  std::size_t maximumPlacements{65'536U};
  std::size_t maximumMeshes{65'536U};
  std::size_t maximumInstances{1'000'000U};
  std::size_t maximumTotalVertices{3'000'000U};
  std::size_t maximumTotalIndices{3'000'000U};
  std::size_t maximumTotalMaterials{65'536U};
  std::size_t maximumTotalRanges{1'000'000U};
  std::size_t maximumTotalBytes{512U * 1024U * 1024U};
};

struct LevelObjectSceneAssembly {
  DrawModelPayload model;
  // These vectors describe only the appended object suffix. The caller's
  // room model remains an exact prefix and has no duplicated provenance.
  std::vector<LevelObjectSceneMeshProvenance> objectMeshProvenance;
  std::vector<LevelObjectSceneInstanceProvenance> objectInstanceProvenance;
  std::vector<LevelObjectScenePlacementRange> placementRanges;
  std::vector<LevelObjectSceneIssue> issues;

  [[nodiscard]] bool complete() const noexcept { return issues.empty(); }
};

// Atomically appends authenticated Level OBJE visuals to an already-built
// room model. Sources are consumed exactly in their supplied physical order;
// each selected subtree retains ObjectVisualDrawAssembly DFS order. The
// selected authored root is overwritten by position plus Z-X-Y radians, and
// descendants are derived as authored-root-relative transforms before that
// placement is composed. No name resolution, skip policy, or CCF 0x4000
// publication occurs here. On any failure every output payload is empty.
[[nodiscard]] LevelObjectSceneAssembly buildLevelObjectSceneAssembly(
    DrawModelPayload roomModel, std::span<const LevelObjectDrawSource> sources,
    std::span<const DrawMaterial> globalMaterialBindings,
    const BasisTransform &basis = {}, UvPolicy uvPolicy = UvPolicy::preserveRaw,
    const LevelObjectSceneLimits &limits = {});

} // namespace airfix::render
