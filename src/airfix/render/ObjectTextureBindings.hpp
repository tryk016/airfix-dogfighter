#pragma once

#include "airfix/assets/AssetResolver.hpp"
#include "airfix/render/TextureRuntimePlan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace airfix::render {

enum class ObjectTextureBindingIssueKind : std::uint8_t {
  invalidBaseAssetId,
  duplicateBaseArchiveFileIndex,
  baseArchiveFileIndexOutOfRange,
  sourceLimitExceeded,
  invalidSource,
  conflictingRepeatedDefinition,
  sceneDependencyFailure,
  sceneGraphFailure,
  invalidSceneResolution,
  textureEntryFailure,
  textureBindingFailure,
  invalidLocalBinding,
  limitExceeded,
  integerOverflow,
};

struct ObjectTextureBindingIssue final {
  ObjectTextureBindingIssueKind kind{
      ObjectTextureBindingIssueKind::sceneDependencyFailure};
  std::optional<std::size_t> sourceIndex;
  std::optional<std::size_t> definitionIndex;
  std::optional<std::size_t> baseImportIndex;
  std::optional<std::size_t> archiveFileIndex;
  std::optional<assets::DependencyIssue> dependencyIssue;
  std::optional<assets::BlueprintGraphIssue> graphIssue;
  std::optional<assets::TextureEntryIssue> textureEntryIssue;
  std::optional<TextureBindingIssue> textureBindingIssue;
};

struct ObjectTextureBindingSource final {
  // Stable caller-owned identity of one authenticated object definition.
  // Repeated placements use the same index and pointer pair.
  std::size_t definitionIndex{};
  const assets::ObjectDefinition *object{};
  const assets::CcfMetadata *ccf{};
};

struct ObjectTextureMaterialBindings final {
  std::size_t definitionIndex{};
  // Stable first-use material order for the selected object subtree.
  std::vector<DrawMaterial> materials;
};

struct ObjectTextureBindingLimits final {
  assets::ObjectSceneDependencyLimits dependencies;
  assets::TextureEntryResolutionLimits textureEntries;
  TextureBindingPlanLimits binding;
  std::size_t maximumSources{100'000U};
  std::size_t maximumUniqueDefinitions{65'536U};
  std::size_t maximumBaseImports{262'144U};
  std::size_t maximumMaterialsPerDefinition{65'536U};
  std::size_t maximumTextureEntriesPerDefinition{262'144U};
  std::size_t maximumAggregateMaterials{262'144U};
  std::size_t maximumAggregateTextureEntries{1'048'576U};
  std::size_t maximumGlobalImports{262'144U};
  // Logical output vectors only. This excludes allocator overhead and
  // temporary dependency-resolution storage.
  std::size_t maximumTotalBytes{256U * 1024U * 1024U};
};

struct ObjectTextureBindings final {
  // One entry per unique definition in first source-use order.
  std::vector<ObjectTextureMaterialBindings> definitions;
  // Parallel to the input sources and indexing definitions.
  std::vector<std::size_t> definitionBindingIndexBySource;
  // One dense global namespace. A successful result begins with an exact
  // copy of baseImports and appends physical archive entries at first use.
  std::vector<TextureImportRequest> imports;
  std::size_t totalBytes{};
  std::vector<ObjectTextureBindingIssue> issues;

  [[nodiscard]] bool complete() const noexcept { return issues.empty(); }
};

// Resolves each unique authenticated object definition once, preserves an
// exact existing import prefix, and merges every local material binding into
// one deterministic global TextureAssetId namespace. Physical GTI entries are
// deduplicated by archive file index. Repeated definition indices must carry
// the identical object/CCF pointer pair. Every failure clears all
// renderer-facing vectors and totalBytes atomically.
[[nodiscard]] ObjectTextureBindings
buildObjectTextureBindings(std::span<const TextureImportRequest> baseImports,
                           std::span<const ObjectTextureBindingSource> sources,
                           const udsp::Archive &archive,
                           const ObjectTextureBindingLimits &limits = {});

} // namespace airfix::render
