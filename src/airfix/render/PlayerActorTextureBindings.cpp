#include "airfix/render/PlayerActorTextureBindings.hpp"

#include "airfix/render/ObjectTextureBindings.hpp"

#include <algorithm>
#include <limits>

namespace airfix::render {
namespace {

[[nodiscard]] PlayerActorTextureBindingIssue
issue(const PlayerActorTextureBindingIssueKind kind) noexcept {
  return {
      .kind = kind,
      .baseImportIndex = std::nullopt,
      .archiveFileIndex = std::nullopt,
      .dependencyIssue = std::nullopt,
      .graphIssue = std::nullopt,
      .textureEntryIssue = std::nullopt,
      .textureBindingIssue = std::nullopt,
  };
}

[[nodiscard]] PlayerActorTextureBindingIssueKind
playerIssueKind(const ObjectTextureBindingIssueKind kind) noexcept {
  switch (kind) {
  case ObjectTextureBindingIssueKind::invalidBaseAssetId:
    return PlayerActorTextureBindingIssueKind::invalidBaseAssetId;
  case ObjectTextureBindingIssueKind::duplicateBaseArchiveFileIndex:
    return PlayerActorTextureBindingIssueKind::duplicateBaseArchiveFileIndex;
  case ObjectTextureBindingIssueKind::baseArchiveFileIndexOutOfRange:
    return PlayerActorTextureBindingIssueKind::baseArchiveFileIndexOutOfRange;
  case ObjectTextureBindingIssueKind::sceneDependencyFailure:
    return PlayerActorTextureBindingIssueKind::sceneDependencyFailure;
  case ObjectTextureBindingIssueKind::sceneGraphFailure:
    return PlayerActorTextureBindingIssueKind::sceneGraphFailure;
  case ObjectTextureBindingIssueKind::invalidSceneResolution:
    return PlayerActorTextureBindingIssueKind::invalidSceneResolution;
  case ObjectTextureBindingIssueKind::textureEntryFailure:
    return PlayerActorTextureBindingIssueKind::textureEntryFailure;
  case ObjectTextureBindingIssueKind::textureBindingFailure:
    return PlayerActorTextureBindingIssueKind::textureBindingFailure;
  case ObjectTextureBindingIssueKind::invalidLocalBinding:
  case ObjectTextureBindingIssueKind::invalidSource:
  case ObjectTextureBindingIssueKind::conflictingRepeatedDefinition:
  case ObjectTextureBindingIssueKind::sourceLimitExceeded:
    return PlayerActorTextureBindingIssueKind::invalidLocalBinding;
  case ObjectTextureBindingIssueKind::limitExceeded:
    return PlayerActorTextureBindingIssueKind::limitExceeded;
  case ObjectTextureBindingIssueKind::integerOverflow:
    return PlayerActorTextureBindingIssueKind::integerOverflow;
  }
  return PlayerActorTextureBindingIssueKind::invalidLocalBinding;
}

[[nodiscard]] std::size_t
genericByteLimit(const std::size_t playerLimit) noexcept {
  constexpr auto wrapperOverhead =
      sizeof(ObjectTextureMaterialBindings) + sizeof(std::size_t);
  if (playerLimit > std::numeric_limits<std::size_t>::max() - wrapperOverhead) {
    return std::numeric_limits<std::size_t>::max();
  }
  return playerLimit + wrapperOverhead;
}

} // namespace

PlayerActorTextureBindings buildPlayerActorTextureBindings(
    const std::span<const TextureImportRequest> baseImports,
    const assets::ObjectDefinition &object, const assets::CcfMetadata &ccf,
    const udsp::Archive &archive,
    const PlayerActorTextureBindingLimits &limits) {
  PlayerActorTextureBindings result;
  if (baseImports.size() > limits.maximumBaseImports ||
      baseImports.size() > limits.maximumGlobalImports) {
    result.issues.push_back(
        issue(PlayerActorTextureBindingIssueKind::limitExceeded));
    return result;
  }
  if (baseImports.size() >
      std::numeric_limits<std::size_t>::max() / sizeof(TextureImportRequest)) {
    result.issues.push_back(
        issue(PlayerActorTextureBindingIssueKind::integerOverflow));
    return result;
  }
  const auto baseBytes = baseImports.size() * sizeof(TextureImportRequest);
  if (baseBytes > limits.maximumTotalBytes) {
    result.issues.push_back(
        issue(PlayerActorTextureBindingIssueKind::limitExceeded));
    return result;
  }

  const ObjectTextureBindingSource source{
      .definitionIndex = 0U,
      .object = &object,
      .ccf = &ccf,
  };
  const ObjectTextureBindingLimits genericLimits{
      .dependencies = limits.dependencies,
      .textureEntries = limits.textureEntries,
      .binding = limits.binding,
      .maximumSources = 1U,
      .maximumUniqueDefinitions = 1U,
      .maximumBaseImports = limits.maximumBaseImports,
      .maximumMaterialsPerDefinition = limits.maximumActorMaterials,
      .maximumTextureEntriesPerDefinition = limits.maximumActorTextureEntries,
      .maximumAggregateMaterials = limits.maximumActorMaterials,
      .maximumAggregateTextureEntries = limits.maximumActorTextureEntries,
      .maximumGlobalImports = limits.maximumGlobalImports,
      .maximumTotalBytes = genericByteLimit(limits.maximumTotalBytes),
  };
  auto generic = buildObjectTextureBindings(baseImports, std::span{&source, 1U},
                                            archive, genericLimits);

  result.issues.reserve(generic.issues.size());
  for (const auto &issue : generic.issues) {
    result.issues.push_back({
        .kind = playerIssueKind(issue.kind),
        .baseImportIndex = issue.baseImportIndex,
        .archiveFileIndex = issue.archiveFileIndex,
        .dependencyIssue = issue.dependencyIssue,
        .graphIssue = issue.graphIssue,
        .textureEntryIssue = issue.textureEntryIssue,
        .textureBindingIssue = issue.textureBindingIssue,
    });
  }
  if (!result.issues.empty() || generic.definitions.size() != 1U ||
      generic.definitionBindingIndexBySource != std::vector<std::size_t>{0U}) {
    if (result.issues.empty()) {
      result.issues.push_back(
          issue(PlayerActorTextureBindingIssueKind::invalidLocalBinding));
    }
    return result;
  }

  result.materialBindings = std::move(generic.definitions.front().materials);
  result.imports = std::move(generic.imports);
  std::size_t totalBytes = 0U;
  if (result.materialBindings.size() >
          std::numeric_limits<std::size_t>::max() / sizeof(DrawMaterial) ||
      result.imports.size() > std::numeric_limits<std::size_t>::max() /
                                  sizeof(TextureImportRequest)) {
    result.materialBindings.clear();
    result.imports.clear();
    result.issues.push_back(
        issue(PlayerActorTextureBindingIssueKind::integerOverflow));
    return result;
  }
  const auto materialBytes =
      result.materialBindings.size() * sizeof(DrawMaterial);
  const auto importBytes = result.imports.size() * sizeof(TextureImportRequest);
  if (importBytes > std::numeric_limits<std::size_t>::max() - materialBytes) {
    result.materialBindings.clear();
    result.imports.clear();
    result.issues.push_back(
        issue(PlayerActorTextureBindingIssueKind::integerOverflow));
    return result;
  }
  totalBytes = materialBytes + importBytes;
  if (totalBytes > limits.maximumTotalBytes) {
    result.materialBindings.clear();
    result.imports.clear();
    result.issues.push_back(
        issue(PlayerActorTextureBindingIssueKind::limitExceeded));
    return result;
  }
  result.totalBytes = totalBytes;
  return result;
}

} // namespace airfix::render
