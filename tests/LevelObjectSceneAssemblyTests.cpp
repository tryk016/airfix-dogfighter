#include "airfix/render/LevelObjectSceneAssembly.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace airfix::assets;
using namespace airfix::render;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] std::array<CcfVector3, 3> identity() {
  return {
      CcfVector3{1.0F, 0.0F, 0.0F},
      CcfVector3{0.0F, 1.0F, 0.0F},
      CcfVector3{0.0F, 0.0F, 1.0F},
  };
}

[[nodiscard]] CcfMeshMetadata mesh(const std::uint32_t reference,
                                   const std::uint32_t materialReference) {
  CcfMeshMetadata result{
      .name = "mesh",
      .reference = reference,
      .orientation = identity(),
  };
  result.vertices = {
      {.position = {0.0F, 0.0F, 0.0F}},
      {.position = {1.0F, 0.0F, 0.0F}},
      {.position = {0.0F, 1.0F, 0.0F}},
  };
  result.triangles = {{
      .vertexIndices = {0U, 1U, 2U},
      .materialReference = materialReference,
  }};
  return result;
}

[[nodiscard]] CcfBlueprintMetadata
node(const CcfBlueprintKind kind, const std::string &name,
     const std::uint32_t reference, const std::uint32_t parentReference,
     const std::optional<std::size_t> meshIndex, const CcfVector3 position,
     const std::array<CcfVector3, 3> &orientation, const float scalar = 1.0F) {
  return {
      .kind = kind,
      .name = name,
      .reference = reference,
      .parentReference = parentReference,
      .authoredTransform =
          {
              .position = position,
              .rawScalar = scalar,
              .orientation = orientation,
          },
      .meshIndex = meshIndex,
  };
}

[[nodiscard]] CcfMetadata nullRootScene(const float rootScalar = 1.0F) {
  const std::array<CcfVector3, 3> rootLinear{
      CcfVector3{0.0F, 1.0F, 0.0F},
      CcfVector3{-1.0F, 0.0F, 0.0F},
      CcfVector3{0.0F, 0.0F, 1.0F},
  };
  const std::array<CcfVector3, 3> childLinear{
      CcfVector3{1.0F, 0.0F, 0.0F},
      CcfVector3{0.0F, 0.0F, 1.0F},
      CcfVector3{0.0F, -1.0F, 0.0F},
  };
  CcfMetadata ccf;
  ccf.materials = {{.name = "material", .reference = 501U}};
  ccf.meshes = {mesh(101U, 501U), mesh(102U, 501U)};
  // Physical order is root, first sibling, its child, second sibling.
  // Mesh-bearing DFS is therefore [1, 2, 3].
  ccf.blueprints = {
      node(CcfBlueprintKind::nullNode, "Root", 10U, 0U, std::nullopt,
           {3.0F, 4.0F, 5.0F}, rootLinear, rootScalar),
      node(CcfBlueprintKind::mesh, "first", 20U, 10U, 0U, {7.0F, 8.0F, 9.0F},
           childLinear),
      node(CcfBlueprintKind::mesh, "grandchild", 30U, 20U, 1U,
           {11.0F, 12.0F, 13.0F}, identity()),
      node(CcfBlueprintKind::mesh, "second", 40U, 10U, 0U, {-2.0F, 6.0F, 1.0F},
           identity()),
  };
  return ccf;
}

[[nodiscard]] CcfMetadata meshRootScene(const float rootScalar) {
  CcfMetadata ccf;
  ccf.materials = {{.name = "material", .reference = 501U}};
  ccf.meshes = {mesh(201U, 501U)};
  ccf.blueprints = {node(CcfBlueprintKind::mesh, "Root", 50U, 0U, 0U,
                         {9.0F, 8.0F, 7.0F}, identity(), rootScalar)};
  return ccf;
}

[[nodiscard]] ObjectDefinition
definition(const std::optional<std::string> selector = std::string{"Root"}) {
  return {.ccfPath = "logical/object.ccf", .meshName = selector};
}

[[nodiscard]] LevelObjectPlacement
placement(const std::array<float, 3> position = {17.0F, -5.0F, 23.0F},
          const std::array<float, 3> rotation = {0.31F, -0.47F, 0.83F},
          const std::string &room = "room-a") {
  return {
      .position = position,
      .axisRotation = rotation,
      .room = room,
      .objectPath = "logical/object.type",
  };
}

[[nodiscard]] std::vector<DrawMaterial> bindings() {
  return {{.sourceReference = 501U, .primary = TextureAssetId{7U}}};
}

[[nodiscard]] DrawMeshPayload prefixMesh() {
  DrawMeshPayload result;
  result.vertices = {{.position = {99.0F, 98.0F, 97.0F}}};
  result.indices = {0U};
  result.materials = {{.sourceReference = 999U}};
  result.ranges = {{.firstIndex = 0U, .indexCount = 1U}};
  result.localBounds = {{99.0F, 98.0F, 97.0F}, {99.0F, 98.0F, 97.0F}};
  return result;
}

[[nodiscard]] DrawModelPayload roomPrefix() {
  DrawModelPayload result;
  result.meshes = {prefixMesh()};
  result.instances = {{
      .meshSlot = 0U,
      .sourceNodeReference = 999U,
      .modelLinear = {},
      .modelTranslation = {-9.0F, -8.0F, -7.0F},
  }};
  return result;
}

[[nodiscard]] LevelObjectDrawSource
source(const std::size_t placementIndex, const std::size_t sourceIndex,
       const std::size_t definitionIndex, const LevelObjectPlacement &value,
       const ObjectDefinition &object, const CcfMetadata &ccf,
       const std::size_t roomIndex,
       const std::span<const DrawMaterial> materialBindings,
       const std::int32_t legacyRoomId = 0) {
  return {
      .placementIndex = placementIndex,
      .sourceIndex = sourceIndex,
      .uniqueObjectDefinitionIndex = definitionIndex,
      .placement = &value,
      .objectDefinition = &object,
      .ccf = &ccf,
      .materialBindings = materialBindings,
      .targetRoom = LevelObjectTargetRoomIdentity{roomIndex, legacyRoomId},
  };
}

[[nodiscard]] bool close(const float left, const float right) {
  return std::fabs(left - right) <= 1.0e-4F;
}

[[nodiscard]] bool close(const Vec3 &left, const Vec3 &right) {
  return close(left.x, right.x) && close(left.y, right.y) &&
         close(left.z, right.z);
}

[[nodiscard]] bool close(const Mat3 &left, const Mat3 &right) {
  return close(left.columns[0], right.columns[0]) &&
         close(left.columns[1], right.columns[1]) &&
         close(left.columns[2], right.columns[2]);
}

[[nodiscard]] ConvertedNodeTransform
transformOf(const DrawMeshInstance &value) {
  return {value.modelLinear, value.modelTranslation, 1.0F};
}

[[nodiscard]] bool hasIssue(const LevelObjectSceneAssembly &result,
                            const LevelObjectSceneIssueKind kind) {
  return std::ranges::any_of(
      result.issues, [kind](const auto &issue) { return issue.kind == kind; });
}

void requireAtomicFailure(const LevelObjectSceneAssembly &result,
                          const LevelObjectSceneIssueKind kind,
                          const std::string &message) {
  require(hasIssue(result, kind) && result.model.meshes.empty() &&
              result.model.instances.empty() &&
              result.objectMeshProvenance.empty() &&
              result.objectInstanceProvenance.empty() &&
              result.placementRanges.empty(),
          message);
}

void testTransformsPrefixOrderAndProvenance() {
  const auto ccf = nullRootScene();
  const auto object = definition();
  const auto placed = placement();
  const auto input = roomPrefix();
  const auto materials = bindings();
  const std::array sources{
      source(7U, 11U, 13U, placed, object, ccf, 5U, materials, 17)};
  const auto result = buildLevelObjectSceneAssembly(input, sources);
  require(result.complete(), "valid Level object assembly was rejected");
  require(result.model.meshes.size() == 3U &&
              result.model.instances.size() == 4U,
          "object suffix count is wrong");
  require(result.model.meshes[0].vertices == input.meshes[0].vertices &&
              result.model.meshes[0].indices == input.meshes[0].indices &&
              result.model.meshes[0].materials == input.meshes[0].materials &&
              result.model.meshes[0].ranges == input.meshes[0].ranges &&
              result.model.meshes[0].localBounds ==
                  input.meshes[0].localBounds &&
              result.model.instances[0] == input.instances[0],
          "room model was not retained as an exact prefix");
  require(result.model.instances[1].sourceNodeReference == 20U &&
              result.model.instances[2].sourceNodeReference == 30U &&
              result.model.instances[3].sourceNodeReference == 40U,
          "physical sibling/DFS order changed");
  require(result.model.instances[1].meshSlot == 1U &&
              result.model.instances[2].meshSlot == 2U &&
              result.model.instances[3].meshSlot == 1U,
          "per-placement first-use mesh slots were not remapped");

  const auto authoredRoot =
      convertLegacyTransform(ccf.blueprints[0].authoredTransform);
  const auto placementRoot = convertLegacyAxisRotationWorldPose(
      {placed.position[0], placed.position[1], placed.position[2]},
      placed.axisRotation);
  for (std::size_t index = 0U; index < 3U; ++index) {
    const auto blueprintIndex = index + 1U;
    const auto authoredNode = convertLegacyTransform(
        ccf.blueprints[blueprintIndex].authoredTransform);
    const auto local = deriveLocalTransform(authoredRoot, authoredNode);
    const auto expected = composeNodeTransforms(placementRoot, local);
    require(
        close(transformOf(result.model.instances[index + 1U]).linear,
              expected.linear) &&
            close(transformOf(result.model.instances[index + 1U]).translation,
                  expected.translation),
        "P * inverse(A0) * Ai transform contract changed");
  }
  require(result.placementRanges ==
              std::vector<LevelObjectScenePlacementRange>{
                  {7U, 11U, 13U, {5U, 17}, 1U, 2U, 1U, 3U}},
          "placement range lost exact absolute indices");
  require(
      result.objectMeshProvenance.size() == 2U &&
          result.objectMeshProvenance[0].finalMeshSlot == 1U &&
          result.objectMeshProvenance[1].finalMeshSlot == 2U &&
          result.objectInstanceProvenance[2].finalInstanceIndex == 3U &&
          result.objectInstanceProvenance[2].targetRoom.worldRoomIndex == 5U &&
          result.objectInstanceProvenance[2].targetRoom.legacyCcRoomId == 17,
      "mesh/instance provenance is incomplete");
}

void testRootScalarPolicy() {
  const auto object = definition();
  const auto placed = placement({}, {}, "room");
  const auto materials = bindings();

  const auto meshCcf = meshRootScene(2.0F);
  const std::array meshSources{
      source(0U, 0U, 0U, placed, object, meshCcf, 1U, materials)};
  const auto meshResult = buildLevelObjectSceneAssembly({}, meshSources);
  require(meshResult.complete() &&
              close(meshResult.model.instances[0].modelLinear, Mat3{}) &&
              std::bit_cast<std::uint32_t>(
                  meshResult.objectInstanceProvenance[0].local.rawScalar) ==
                  0x3F800000U,
          "mesh-root scalar was not reset to exact one");

  const auto nullCcf = nullRootScene(2.0F);
  const std::array nullSources{
      source(0U, 0U, 0U, placed, object, nullCcf, 1U, materials)};
  const auto nullResult = buildLevelObjectSceneAssembly({}, nullSources);
  require(nullResult.complete(), "null-root scalar fixture failed");
  const auto root =
      convertLegacyTransform(nullCcf.blueprints[0].authoredTransform);
  const auto child =
      convertLegacyTransform(nullCcf.blueprints[1].authoredTransform);
  auto placementWithScale = convertLegacyAxisRotationWorldPose({}, {});
  for (auto &column : placementWithScale.linear.columns) {
    column.x *= 2.0F;
    column.y *= 2.0F;
    column.z *= 2.0F;
  }
  const auto expected = composeNodeTransforms(
      placementWithScale, deriveLocalTransform(root, child));
  require(close(nullResult.model.instances[0].modelLinear, expected.linear) &&
              close(nullResult.model.instances[0].modelTranslation,
                    expected.translation),
          "null/light root scalar did not affect the axis-root relation once");

  auto lightCcf = nullRootScene(2.0F);
  lightCcf.blueprints[0].kind = CcfBlueprintKind::light;
  const std::array lightSources{
      source(0U, 0U, 0U, placed, object, lightCcf, 1U, materials)};
  const auto lightResult = buildLevelObjectSceneAssembly({}, lightSources);
  require(
      lightResult.complete() &&
          close(lightResult.model.instances[0].modelLinear, expected.linear) &&
          close(lightResult.model.instances[0].modelTranslation,
                expected.translation),
      "light-root scalar diverged from the recovered null/light policy");
}

void testRepeatedDefinitionRemainsTwoRanges() {
  const auto ccf = meshRootScene(1.0F);
  const auto object = definition();
  const auto first = placement({1.0F, 0.0F, 0.0F}, {}, "a");
  const auto second = placement({2.0F, 0.0F, 0.0F}, {}, "b");
  const auto materials = bindings();
  const std::array sources{
      source(4U, 9U, 2U, first, object, ccf, 3U, materials),
      source(5U, 10U, 2U, second, object, ccf, 4U, materials),
  };
  const auto result = buildLevelObjectSceneAssembly({}, sources);
  require(result.complete() && result.model.meshes.size() == 2U &&
              result.model.instances.size() == 2U &&
              result.placementRanges.size() == 2U &&
              result.placementRanges[0].firstMeshSlot == 0U &&
              result.placementRanges[1].firstMeshSlot == 1U &&
              result.model.instances[0].modelTranslation.x == 1.0F &&
              result.model.instances[1].modelTranslation.x == 2.0F,
          "repeated definition was deduplicated across placements");
}

void testPerSourceMaterialBindingsDoNotCrossCcfBoundaries() {
  auto firstCcf = meshRootScene(1.0F);
  auto secondCcf = meshRootScene(1.0F);
  secondCcf.materials[0].reference = 601U;
  secondCcf.meshes[0].triangles[0].materialReference = 601U;
  const auto object = definition();
  const auto firstPlacement = placement({1.0F, 0.0F, 0.0F}, {}, "a");
  const auto secondPlacement = placement({2.0F, 0.0F, 0.0F}, {}, "b");
  const std::vector<DrawMaterial> firstBindings{{
      .sourceReference = 501U,
      .primary = TextureAssetId{70U},
  }};
  const std::vector<DrawMaterial> secondBindings{{
      .sourceReference = 601U,
      .primary = TextureAssetId{80U},
  }};
  const std::array sources{
      source(1U, 20U, 4U, firstPlacement, object, firstCcf, 2U, firstBindings,
             3),
      source(2U, 21U, 5U, secondPlacement, object, secondCcf, 3U,
             secondBindings, 4),
  };
  const auto result = buildLevelObjectSceneAssembly({}, sources);
  require(result.complete() && result.model.meshes.size() == 2U &&
              result.model.meshes[0].materials[0].sourceReference == 501U &&
              result.model.meshes[0].materials[0].primary ==
                  std::optional<TextureAssetId>{TextureAssetId{70U}} &&
              result.model.meshes[1].materials[0].sourceReference == 601U &&
              result.model.meshes[1].materials[0].primary ==
                  std::optional<TextureAssetId>{TextureAssetId{80U}},
          "material bindings crossed authenticated CCF source boundaries");
}

void testPhysicalSourceOrderFailsClosed() {
  const auto ccf = meshRootScene(1.0F);
  const auto object = definition();
  const auto first = placement({1.0F, 0.0F, 0.0F}, {}, "a");
  const auto second = placement({2.0F, 0.0F, 0.0F}, {}, "b");
  const auto materials = bindings();

  const std::array duplicateSourceIndex{
      source(2U, 7U, 0U, first, object, ccf, 1U, materials),
      source(3U, 7U, 0U, second, object, ccf, 1U, materials),
  };
  requireAtomicFailure(buildLevelObjectSceneAssembly({}, duplicateSourceIndex),
                       LevelObjectSceneIssueKind::invalidSourceOrder,
                       "duplicate source index did not fail-clear");

  const std::array reversedPlacementIndex{
      source(5U, 8U, 0U, first, object, ccf, 1U, materials),
      source(4U, 9U, 0U, second, object, ccf, 1U, materials),
  };
  requireAtomicFailure(
      buildLevelObjectSceneAssembly({}, reversedPlacementIndex),
      LevelObjectSceneIssueKind::invalidSourceOrder,
      "reversed placement index did not fail-clear");

  const std::array reversedSourceIndex{
      source(5U, 9U, 0U, first, object, ccf, 1U, materials),
      source(6U, 8U, 0U, second, object, ccf, 1U, materials),
  };
  requireAtomicFailure(buildLevelObjectSceneAssembly({}, reversedSourceIndex),
                       LevelObjectSceneIssueKind::invalidSourceOrder,
                       "reversed source index did not fail-clear");
}

void testInvalidInputsFailAtomically() {
  const auto validCcf = meshRootScene(1.0F);
  const auto validObject = definition();
  const auto validPlacement = placement();
  const auto materials = bindings();

  auto invalidSource =
      source(0U, 0U, 0U, validPlacement, validObject, validCcf, 0U, materials);
  invalidSource.ccf = nullptr;
  requireAtomicFailure(buildLevelObjectSceneAssembly(
                           roomPrefix(), std::span{&invalidSource, 1U}),
                       LevelObjectSceneIssueKind::invalidSource,
                       "null CCF did not fail-clear");

  invalidSource =
      source(0U, 0U, 0U, validPlacement, validObject, validCcf, 0U, materials);
  invalidSource.placement = nullptr;
  requireAtomicFailure(buildLevelObjectSceneAssembly(
                           roomPrefix(), std::span{&invalidSource, 1U}),
                       LevelObjectSceneIssueKind::invalidSource,
                       "null placement did not fail-clear");

  invalidSource =
      source(0U, 0U, 0U, validPlacement, validObject, validCcf, 0U, materials);
  invalidSource.objectDefinition = nullptr;
  requireAtomicFailure(buildLevelObjectSceneAssembly(
                           roomPrefix(), std::span{&invalidSource, 1U}),
                       LevelObjectSceneIssueKind::invalidSource,
                       "null object definition did not fail-clear");

  const auto noSelector = definition(std::nullopt);
  const std::array selectorSources{
      source(0U, 0U, 0U, validPlacement, noSelector, validCcf, 0U, materials)};
  requireAtomicFailure(
      buildLevelObjectSceneAssembly(roomPrefix(), selectorSources),
      LevelObjectSceneIssueKind::missingSelector,
      "missing selector did not fail-clear");

  const auto noRoom = placement({}, {}, "");
  const std::array roomSources{
      source(0U, 0U, 0U, noRoom, validObject, validCcf, 0U, materials)};
  requireAtomicFailure(buildLevelObjectSceneAssembly(roomPrefix(), roomSources),
                       LevelObjectSceneIssueKind::invalidRoom,
                       "missing room did not fail-clear");

  auto absentRoomSource =
      source(0U, 0U, 0U, validPlacement, validObject, validCcf, 0U, materials);
  absentRoomSource.targetRoom.reset();
  requireAtomicFailure(buildLevelObjectSceneAssembly(
                           roomPrefix(), std::span{&absentRoomSource, 1U}),
                       LevelObjectSceneIssueKind::invalidRoom,
                       "absent target identity did not fail-clear");

  auto negativeRoomSource = source(0U, 0U, 0U, validPlacement, validObject,
                                   validCcf, 0U, materials, -1);
  requireAtomicFailure(buildLevelObjectSceneAssembly(
                           roomPrefix(), std::span{&negativeRoomSource, 1U}),
                       LevelObjectSceneIssueKind::invalidRoom,
                       "negative signed legacy room ID did not fail-clear");

  auto nonfinite = validPlacement;
  nonfinite.position[1] = std::numeric_limits<float>::infinity();
  const std::array nonfiniteSources{
      source(0U, 0U, 0U, nonfinite, validObject, validCcf, 0U, materials)};
  requireAtomicFailure(
      buildLevelObjectSceneAssembly(roomPrefix(), nonfiniteSources),
      LevelObjectSceneIssueKind::invalidPlacementTransform,
      "non-finite placement did not fail-clear");

  auto singularCcf = validCcf;
  singularCcf.blueprints[0].authoredTransform.orientation = {};
  const std::array singularSources{source(
      0U, 0U, 0U, validPlacement, validObject, singularCcf, 0U, materials)};
  requireAtomicFailure(
      buildLevelObjectSceneAssembly(roomPrefix(), singularSources),
      LevelObjectSceneIssueKind::objectVisualFailure,
      "singular authored transform did not fail-clear");

  auto overflowPlacement = placement({}, {}, "room");
  overflowPlacement.position = {std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max()};
  auto overflowCcf = nullRootScene();
  overflowCcf.blueprints[0].authoredTransform.position = {
      -std::numeric_limits<float>::max(), 0.0F, 0.0F};
  const std::array overflowSources{source(
      0U, 0U, 0U, overflowPlacement, validObject, overflowCcf, 0U, materials)};
  const auto overflowResult =
      buildLevelObjectSceneAssembly(roomPrefix(), overflowSources);
  require(!overflowResult.complete() && overflowResult.model.meshes.empty(),
          "finite transform overflow published partial output");
}

[[nodiscard]] std::size_t
exactLogicalBytes(const LevelObjectSceneAssembly &result) {
  std::size_t bytes =
      result.model.meshes.size() * sizeof(DrawMeshPayload) +
      result.model.instances.size() * sizeof(DrawMeshInstance) +
      result.objectMeshProvenance.size() *
          sizeof(LevelObjectSceneMeshProvenance) +
      result.objectInstanceProvenance.size() *
          sizeof(LevelObjectSceneInstanceProvenance) +
      result.placementRanges.size() * sizeof(LevelObjectScenePlacementRange);
  for (const auto &meshValue : result.model.meshes) {
    bytes += meshValue.vertices.size() * sizeof(DrawVertex);
    bytes += meshValue.indices.size() * sizeof(std::uint32_t);
    bytes += meshValue.materials.size() * sizeof(DrawMaterial);
    bytes += meshValue.ranges.size() * sizeof(DrawRange);
  }
  return bytes;
}

void testExactAndOneUnderBudgets() {
  const auto ccf = meshRootScene(1.0F);
  const auto object = definition();
  const auto placed = placement();
  const auto materials = bindings();
  const std::array sources{
      source(0U, 0U, 0U, placed, object, ccf, 0U, materials)};
  const auto baseline = buildLevelObjectSceneAssembly(roomPrefix(), sources);
  require(baseline.complete(), "budget baseline failed");
  LevelObjectSceneLimits exact;
  exact.maximumPlacements = sources.size();
  exact.maximumMeshes = baseline.model.meshes.size();
  exact.maximumInstances = baseline.model.instances.size();
  exact.maximumTotalVertices = 0U;
  exact.maximumTotalIndices = 0U;
  exact.maximumTotalMaterials = 0U;
  exact.maximumTotalRanges = 0U;
  for (const auto &meshValue : baseline.model.meshes) {
    exact.maximumTotalVertices += meshValue.vertices.size();
    exact.maximumTotalIndices += meshValue.indices.size();
    exact.maximumTotalMaterials += meshValue.materials.size();
    exact.maximumTotalRanges += meshValue.ranges.size();
  }
  exact.maximumTotalBytes = exactLogicalBytes(baseline);
  require(buildLevelObjectSceneAssembly(roomPrefix(), sources, {},
                                        UvPolicy::preserveRaw, exact)
              .complete(),
          "exact byte budget was rejected");
  auto byteOneUnder = exact;
  --byteOneUnder.maximumTotalBytes;
  requireAtomicFailure(buildLevelObjectSceneAssembly(roomPrefix(), sources, {},
                                                     UvPolicy::preserveRaw,
                                                     byteOneUnder),
                       LevelObjectSceneIssueKind::limitExceeded,
                       "one-under byte budget did not fail-clear");

  const auto requireOneUnder = [&](LevelObjectSceneLimits oneUnder,
                                   const std::string &label) {
    const auto failed = buildLevelObjectSceneAssembly(
        roomPrefix(), sources, {}, UvPolicy::preserveRaw, oneUnder);
    requireAtomicFailure(failed, LevelObjectSceneIssueKind::limitExceeded,
                         label + " one-under budget did not fail-clear");
  };
  auto oneUnder = exact;
  --oneUnder.maximumPlacements;
  requireOneUnder(oneUnder, "placement");
  oneUnder = exact;
  --oneUnder.maximumMeshes;
  requireOneUnder(oneUnder, "mesh");
  oneUnder = exact;
  --oneUnder.maximumInstances;
  requireOneUnder(oneUnder, "instance");
  oneUnder = exact;
  --oneUnder.maximumTotalVertices;
  requireOneUnder(oneUnder, "vertex");
  oneUnder = exact;
  --oneUnder.maximumTotalIndices;
  requireOneUnder(oneUnder, "index");
  oneUnder = exact;
  --oneUnder.maximumTotalMaterials;
  requireOneUnder(oneUnder, "material");
  oneUnder = exact;
  --oneUnder.maximumTotalRanges;
  requireOneUnder(oneUnder, "range");

  static_assert(LevelObjectSceneIssueKind::integerOverflow !=
                    LevelObjectSceneIssueKind::limitExceeded &&
                LevelObjectSceneIssueKind::meshSlotOverflow !=
                    LevelObjectSceneIssueKind::instanceIndexOverflow);
}

} // namespace

int main() {
  try {
    testTransformsPrefixOrderAndProvenance();
    testRootScalarPolicy();
    testRepeatedDefinitionRemainsTwoRanges();
    testPerSourceMaterialBindingsDoNotCrossCcfBoundaries();
    testPhysicalSourceOrderFailsClosed();
    testInvalidInputsFailAtomically();
    testExactAndOneUnderBudgets();
  } catch (const std::exception &error) {
    std::cerr << "LevelObjectSceneAssemblyTests failed: " << error.what()
              << '\n';
    return 1;
  }
  std::cout << "LevelObjectSceneAssemblyTests passed\n";
  return 0;
}
