#include "airfix/render/LevelObjectSceneAssembly.hpp"

#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    LevelObjectSceneAssembly &result, const LevelObjectSceneIssueKind kind,
    const LevelObjectDrawSource *source = nullptr,
    const std::optional<std::size_t> blueprintIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt,
    const std::optional<ObjectVisualDrawIssueKind> visualIssue = std::nullopt,
    const std::optional<GeometryErrorCode> geometryError = std::nullopt) {
  result.issues.push_back({
      .kind = kind,
      .placementIndex = source == nullptr
                            ? std::nullopt
                            : std::optional{source->placementIndex},
      .sourceIndex =
          source == nullptr ? std::nullopt : std::optional{source->sourceIndex},
      .uniqueObjectDefinitionIndex =
          source == nullptr
              ? std::nullopt
              : std::optional{source->uniqueObjectDefinitionIndex},
      .blueprintIndex = blueprintIndex,
      .physicalMeshIndex = physicalMeshIndex,
      .objectVisualIssue = visualIssue,
      .geometryError = geometryError,
  });
}

[[nodiscard]] bool checkedAdd(const std::size_t left, const std::size_t right,
                              std::size_t &output) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  output = left + right;
  return true;
}

[[nodiscard]] bool account(std::size_t &total, const std::size_t addition,
                           const std::size_t limit,
                           LevelObjectSceneAssembly &result) {
  std::size_t next = 0U;
  if (!checkedAdd(total, addition, next)) {
    addIssue(result, LevelObjectSceneIssueKind::integerOverflow);
    return false;
  }
  if (next > limit) {
    addIssue(result, LevelObjectSceneIssueKind::limitExceeded);
    return false;
  }
  total = next;
  return true;
}

[[nodiscard]] bool accountBytes(std::size_t &total, const std::size_t count,
                                const std::size_t elementSize,
                                const std::size_t limit,
                                LevelObjectSceneAssembly &result) {
  if (elementSize != 0U &&
      count > std::numeric_limits<std::size_t>::max() / elementSize) {
    addIssue(result, LevelObjectSceneIssueKind::integerOverflow);
    return false;
  }
  return account(total, count * elementSize, limit, result);
}

[[nodiscard]] bool finite(const Vec3 &value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3 &value) noexcept {
  return finite(value.columns[0]) && finite(value.columns[1]) &&
         finite(value.columns[2]);
}

[[nodiscard]] std::optional<GeometryErrorCode>
transformError(const DrawMeshInstance &instance) noexcept {
  if (!finite(instance.modelLinear) || !finite(instance.modelTranslation)) {
    return GeometryErrorCode::nonFiniteValue;
  }
  const auto value = determinant(instance.modelLinear);
  if (!std::isfinite(value)) {
    return GeometryErrorCode::nonFiniteValue;
  }
  if (value == 0.0F) {
    return GeometryErrorCode::singularTransform;
  }
  return std::nullopt;
}

[[nodiscard]] bool validateRoomModel(const DrawModelPayload &roomModel,
                                     LevelObjectSceneAssembly &result) {
  for (const auto &instance : roomModel.instances) {
    if (static_cast<std::size_t>(instance.meshSlot) >=
            roomModel.meshes.size() ||
        transformError(instance).has_value()) {
      addIssue(result, LevelObjectSceneIssueKind::invalidRoomModel);
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool
validateAddressableCounts(const std::size_t meshCount,
                          const std::size_t instanceCount,
                          LevelObjectSceneAssembly &result,
                          const LevelObjectDrawSource *source = nullptr) {
  if constexpr (std::numeric_limits<std::size_t>::max() >
                std::numeric_limits<std::uint32_t>::max()) {
    constexpr auto maximumUint32Count =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) +
        std::size_t{1U};
    if (meshCount > maximumUint32Count) {
      addIssue(result, LevelObjectSceneIssueKind::meshSlotOverflow, source);
      return false;
    }
    if (instanceCount > maximumUint32Count) {
      addIssue(result, LevelObjectSceneIssueKind::instanceIndexOverflow,
               source);
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool accountModel(const DrawModelPayload &model,
                                const LevelObjectSceneLimits &limits,
                                std::size_t &vertices, std::size_t &indices,
                                std::size_t &materials, std::size_t &ranges,
                                std::size_t &bytes,
                                LevelObjectSceneAssembly &result) {
  for (const auto &mesh : model.meshes) {
    if (!account(vertices, mesh.vertices.size(), limits.maximumTotalVertices,
                 result) ||
        !account(indices, mesh.indices.size(), limits.maximumTotalIndices,
                 result) ||
        !account(materials, mesh.materials.size(), limits.maximumTotalMaterials,
                 result) ||
        !account(ranges, mesh.ranges.size(), limits.maximumTotalRanges,
                 result) ||
        !accountBytes(bytes, mesh.vertices.size(), sizeof(DrawVertex),
                      limits.maximumTotalBytes, result) ||
        !accountBytes(bytes, mesh.indices.size(), sizeof(std::uint32_t),
                      limits.maximumTotalBytes, result) ||
        !accountBytes(bytes, mesh.materials.size(), sizeof(DrawMaterial),
                      limits.maximumTotalBytes, result) ||
        !accountBytes(bytes, mesh.ranges.size(), sizeof(DrawRange),
                      limits.maximumTotalBytes, result)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Mat3 scaled(const Mat3 &matrix, const float scalar) noexcept {
  auto result = matrix;
  for (auto &column : result.columns) {
    column.x *= scalar;
    column.y *= scalar;
    column.z *= scalar;
  }
  return result;
}

} // namespace

LevelObjectSceneAssembly buildLevelObjectSceneAssembly(
    DrawModelPayload roomModel,
    const std::span<const LevelObjectDrawSource> sources,
    const std::span<const DrawMaterial> globalMaterialBindings,
    const BasisTransform &basis, const UvPolicy uvPolicy,
    const LevelObjectSceneLimits &limits) {
  LevelObjectSceneAssembly result;
  if (sources.size() > limits.maximumPlacements) {
    addIssue(result, LevelObjectSceneIssueKind::limitExceeded);
    return result;
  }
  if (!validateRoomModel(roomModel, result)) {
    return result;
  }

  std::size_t finalMeshes = roomModel.meshes.size();
  std::size_t finalInstances = roomModel.instances.size();
  if (finalMeshes > limits.maximumMeshes ||
      finalInstances > limits.maximumInstances) {
    addIssue(result, LevelObjectSceneIssueKind::limitExceeded);
    return result;
  }
  if (!validateAddressableCounts(finalMeshes, finalInstances, result)) {
    return result;
  }
  std::size_t vertices = 0U;
  std::size_t indices = 0U;
  std::size_t materials = 0U;
  std::size_t ranges = 0U;
  std::size_t bytes = 0U;
  if (!accountBytes(bytes, finalMeshes, sizeof(DrawMeshPayload),
                    limits.maximumTotalBytes, result) ||
      !accountBytes(bytes, finalInstances, sizeof(DrawMeshInstance),
                    limits.maximumTotalBytes, result) ||
      !accountBytes(bytes, sources.size(),
                    sizeof(LevelObjectScenePlacementRange),
                    limits.maximumTotalBytes, result) ||
      !accountModel(roomModel, limits, vertices, indices, materials, ranges,
                    bytes, result)) {
    return result;
  }

  struct PreparedPlacement {
    const LevelObjectDrawSource *source{};
    ObjectVisualDrawAssembly visual;
    ConvertedNodeTransform authoredRoot;
    ConvertedNodeTransform placementRoot;
  };
  std::vector<PreparedPlacement> prepared;
  prepared.reserve(sources.size());

  for (const auto &source : sources) {
    if (source.placement == nullptr || source.objectDefinition == nullptr ||
        source.ccf == nullptr) {
      addIssue(result, LevelObjectSceneIssueKind::invalidSource, &source);
      return result;
    }
    if (!source.targetRoom.has_value() || source.placement->room.empty()) {
      addIssue(result, LevelObjectSceneIssueKind::invalidRoom, &source);
      return result;
    }
    if (!source.objectDefinition->meshName.has_value() ||
        source.objectDefinition->meshName->empty()) {
      addIssue(result, LevelObjectSceneIssueKind::missingSelector, &source);
      return result;
    }

    auto visual = buildObjectVisualDrawAssembly(
        *source.objectDefinition, *source.ccf, globalMaterialBindings, basis,
        uvPolicy, limits.visualPerPlacement);
    if (!visual.issues.empty()) {
      const auto &issue = visual.issues.front();
      addIssue(result, LevelObjectSceneIssueKind::objectVisualFailure, &source,
               issue.blueprintIndex, issue.physicalMeshIndex, issue.kind,
               issue.geometryError);
      return result;
    }
    if (!visual.resolution.rootBlueprintIndex.has_value() ||
        *visual.resolution.rootBlueprintIndex >=
            source.ccf->blueprints.size() ||
        visual.model.meshes.size() != visual.meshProvenance.size() ||
        visual.model.instances.size() != visual.instanceProvenance.size()) {
      addIssue(result, LevelObjectSceneIssueKind::invalidObjectVisualAssembly,
               &source);
      return result;
    }

    const auto rootIndex = *visual.resolution.rootBlueprintIndex;
    ConvertedNodeTransform authoredRoot;
    try {
      authoredRoot = convertLegacyTransform(
          source.ccf->blueprints[rootIndex].authoredTransform, basis);
    } catch (const GeometryError &error) {
      addIssue(result, LevelObjectSceneIssueKind::invalidAuthoredRootTransform,
               &source, rootIndex, std::nullopt, std::nullopt, error.code());
      return result;
    }

    const auto &placement = *source.placement;
    auto placementRoot = tryConvertLegacyAxisRotationWorldPose(
        Vec3{placement.position[0], placement.position[1],
             placement.position[2]},
        placement.axisRotation, basis);
    if (!placementRoot.has_value()) {
      addIssue(result, LevelObjectSceneIssueKind::invalidPlacementTransform,
               &source);
      return result;
    }
    const auto rootKind = source.ccf->blueprints[rootIndex].kind;
    const float rootScalar = rootKind == assets::CcfBlueprintKind::mesh
                                 ? 1.0F
                                 : authoredRoot.rawScalar;
    if (!std::isfinite(rootScalar) || rootScalar == 0.0F) {
      addIssue(result, LevelObjectSceneIssueKind::invalidPlacementTransform,
               &source, rootIndex, std::nullopt, std::nullopt,
               GeometryErrorCode::invalidScale);
      return result;
    }
    placementRoot->linear = scaled(placementRoot->linear, rootScalar);
    placementRoot->rawScalar = rootScalar;
    if (!std::isfinite(determinant(placementRoot->linear)) ||
        determinant(placementRoot->linear) == 0.0F) {
      addIssue(result, LevelObjectSceneIssueKind::invalidPlacementTransform,
               &source);
      return result;
    }

    std::size_t nextMeshes = 0U;
    std::size_t nextInstances = 0U;
    if (!checkedAdd(finalMeshes, visual.model.meshes.size(), nextMeshes) ||
        !checkedAdd(finalInstances, visual.model.instances.size(),
                    nextInstances)) {
      addIssue(result, LevelObjectSceneIssueKind::integerOverflow, &source);
      return result;
    }
    if (nextMeshes > limits.maximumMeshes ||
        nextInstances > limits.maximumInstances) {
      addIssue(result, LevelObjectSceneIssueKind::limitExceeded, &source);
      return result;
    }
    if (!validateAddressableCounts(nextMeshes, nextInstances, result,
                                   &source)) {
      return result;
    }
    if (!accountBytes(bytes, visual.model.meshes.size(),
                      sizeof(DrawMeshPayload), limits.maximumTotalBytes,
                      result) ||
        !accountBytes(bytes, visual.model.instances.size(),
                      sizeof(DrawMeshInstance), limits.maximumTotalBytes,
                      result) ||
        !accountBytes(bytes, visual.model.meshes.size(),
                      sizeof(LevelObjectSceneMeshProvenance),
                      limits.maximumTotalBytes, result) ||
        !accountBytes(bytes, visual.model.instances.size(),
                      sizeof(LevelObjectSceneInstanceProvenance),
                      limits.maximumTotalBytes, result) ||
        !accountModel(visual.model, limits, vertices, indices, materials,
                      ranges, bytes, result)) {
      return result;
    }
    finalMeshes = nextMeshes;
    finalInstances = nextInstances;
    prepared.push_back(
        {&source, std::move(visual), authoredRoot, *placementRoot});
  }

  DrawModelPayload candidate = std::move(roomModel);
  candidate.meshes.reserve(finalMeshes);
  candidate.instances.reserve(finalInstances);
  std::vector<LevelObjectSceneMeshProvenance> meshProvenance;
  std::vector<LevelObjectSceneInstanceProvenance> instanceProvenance;
  std::vector<LevelObjectScenePlacementRange> placementRanges;
  meshProvenance.reserve(finalMeshes - candidate.meshes.size());
  instanceProvenance.reserve(finalInstances - candidate.instances.size());
  placementRanges.reserve(prepared.size());

  for (auto &placement : prepared) {
    const auto &source = *placement.source;
    const auto firstMesh = candidate.meshes.size();
    const auto firstInstance = candidate.instances.size();
    for (std::size_t meshIndex = 0U;
         meshIndex < placement.visual.model.meshes.size(); ++meshIndex) {
      const auto finalSlot = firstMesh + meshIndex;
      const auto &provenance = placement.visual.meshProvenance[meshIndex];
      meshProvenance.push_back({
          source.placementIndex,
          source.sourceIndex,
          source.uniqueObjectDefinitionIndex,
          *source.targetRoom,
          provenance.blueprintIndex,
          provenance.physicalMeshIndex,
          static_cast<std::uint32_t>(finalSlot),
      });
    }
    candidate.meshes.insert(
        candidate.meshes.end(),
        std::make_move_iterator(placement.visual.model.meshes.begin()),
        std::make_move_iterator(placement.visual.model.meshes.end()));

    for (std::size_t instanceIndex = 0U;
         instanceIndex < placement.visual.model.instances.size();
         ++instanceIndex) {
      const auto &visualInstance =
          placement.visual.model.instances[instanceIndex];
      const auto &visualProvenance =
          placement.visual.instanceProvenance[instanceIndex];
      if (visualProvenance.blueprintIndex >= source.ccf->blueprints.size() ||
          visualProvenance.physicalMeshIndex >= source.ccf->meshes.size() ||
          static_cast<std::size_t>(visualInstance.meshSlot) >=
              placement.visual.model.meshes.size()) {
        addIssue(result, LevelObjectSceneIssueKind::invalidObjectVisualAssembly,
                 &source);
        return result;
      }
      ConvertedNodeTransform local;
      ConvertedNodeTransform absolute;
      try {
        const auto authoredNode = convertLegacyTransform(
            source.ccf->blueprints[visualProvenance.blueprintIndex]
                .authoredTransform,
            basis);
        local = deriveLocalTransform(placement.authoredRoot, authoredNode);
        absolute = composeNodeTransforms(placement.placementRoot, local);
      } catch (const GeometryError &error) {
        addIssue(result, LevelObjectSceneIssueKind::invalidComposedTransform,
                 &source, visualProvenance.blueprintIndex,
                 visualProvenance.physicalMeshIndex, std::nullopt,
                 error.code());
        return result;
      }
      const auto finalMeshSlot =
          firstMesh + static_cast<std::size_t>(visualInstance.meshSlot);
      const auto finalInstanceIndex = candidate.instances.size();
      if (finalMeshSlot > std::numeric_limits<std::uint32_t>::max()) {
        addIssue(result, LevelObjectSceneIssueKind::meshSlotOverflow, &source);
        return result;
      }
      if (finalInstanceIndex > std::numeric_limits<std::uint32_t>::max()) {
        addIssue(result, LevelObjectSceneIssueKind::instanceIndexOverflow,
                 &source);
        return result;
      }
      candidate.instances.push_back({
          .meshSlot = static_cast<std::uint32_t>(finalMeshSlot),
          .sourceNodeReference = visualInstance.sourceNodeReference,
          .modelLinear = absolute.linear,
          .modelTranslation = absolute.translation,
      });
      instanceProvenance.push_back({
          source.placementIndex,
          source.sourceIndex,
          source.uniqueObjectDefinitionIndex,
          *source.targetRoom,
          visualProvenance.blueprintIndex,
          visualProvenance.physicalMeshIndex,
          static_cast<std::uint32_t>(finalMeshSlot),
          static_cast<std::uint32_t>(finalInstanceIndex),
          local,
      });
    }
    placementRanges.push_back({
        source.placementIndex,
        source.sourceIndex,
        source.uniqueObjectDefinitionIndex,
        *source.targetRoom,
        firstMesh,
        placement.visual.model.meshes.size(),
        firstInstance,
        placement.visual.model.instances.size(),
    });
  }

  result.model = std::move(candidate);
  result.objectMeshProvenance = std::move(meshProvenance);
  result.objectInstanceProvenance = std::move(instanceProvenance);
  result.placementRanges = std::move(placementRanges);
  return result;
}

} // namespace airfix::render
