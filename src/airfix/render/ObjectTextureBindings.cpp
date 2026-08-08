#include "airfix/render/ObjectTextureBindings.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    ObjectTextureBindings &result, const ObjectTextureBindingIssueKind kind,
    const std::optional<std::size_t> sourceIndex = std::nullopt,
    const std::optional<std::size_t> definitionIndex = std::nullopt,
    const std::optional<std::size_t> baseImportIndex = std::nullopt,
    const std::optional<std::size_t> archiveFileIndex = std::nullopt,
    const std::optional<assets::DependencyIssue> dependencyIssue = std::nullopt,
    const std::optional<assets::BlueprintGraphIssue> graphIssue = std::nullopt,
    const std::optional<assets::TextureEntryIssue> textureEntryIssue =
        std::nullopt,
    const std::optional<TextureBindingIssue> textureBindingIssue =
        std::nullopt) {
  result.issues.push_back({
      .kind = kind,
      .sourceIndex = sourceIndex,
      .definitionIndex = definitionIndex,
      .baseImportIndex = baseImportIndex,
      .archiveFileIndex = archiveFileIndex,
      .dependencyIssue = dependencyIssue,
      .graphIssue = graphIssue,
      .textureEntryIssue = textureEntryIssue,
      .textureBindingIssue = textureBindingIssue,
  });
}

void failClosed(ObjectTextureBindings &result) noexcept {
  result.definitions.clear();
  result.definitionBindingIndexBySource.clear();
  result.imports.clear();
  result.totalBytes = 0U;
}

[[nodiscard]] bool checkedAdd(const std::size_t left, const std::size_t right,
                              std::size_t &result) noexcept {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

[[nodiscard]] bool checkedBytes(const std::size_t count,
                                const std::size_t elementSize,
                                std::size_t &total) noexcept {
  if (elementSize != 0U &&
      count > std::numeric_limits<std::size_t>::max() / elementSize) {
    return false;
  }
  return checkedAdd(total, count * elementSize, total);
}

[[nodiscard]] bool assignGlobal(
    std::optional<TextureAssetId> &role,
    const std::vector<std::optional<TextureAssetId>> &localToGlobal) noexcept {
  if (!role.has_value()) {
    return true;
  }
  const auto localIndex = static_cast<std::size_t>(role->value);
  if (localIndex >= localToGlobal.size() ||
      !localToGlobal[localIndex].has_value()) {
    return false;
  }
  role = localToGlobal[localIndex];
  return true;
}

enum class ByteAccountResult : std::uint8_t {
  success,
  limitExceeded,
  integerOverflow,
};

[[nodiscard]] ByteAccountResult
accountOutputBytes(const ObjectTextureBindings &result,
                   const ObjectTextureBindingLimits &limits,
                   std::size_t &total) noexcept {
  total = 0U;
  if (!checkedBytes(result.definitions.size(),
                    sizeof(ObjectTextureMaterialBindings), total) ||
      !checkedBytes(result.definitionBindingIndexBySource.size(),
                    sizeof(std::size_t), total) ||
      !checkedBytes(result.imports.size(), sizeof(TextureImportRequest),
                    total)) {
    return ByteAccountResult::integerOverflow;
  }
  for (const auto &definition : result.definitions) {
    if (!checkedBytes(definition.materials.size(), sizeof(DrawMaterial),
                      total)) {
      return ByteAccountResult::integerOverflow;
    }
  }
  return total <= limits.maximumTotalBytes ? ByteAccountResult::success
                                           : ByteAccountResult::limitExceeded;
}

void addByteAccountIssue(ObjectTextureBindings &result,
                         const ByteAccountResult account,
                         const std::optional<std::size_t> sourceIndex,
                         const std::optional<std::size_t> definitionIndex) {
  addIssue(result,
           account == ByteAccountResult::integerOverflow
               ? ObjectTextureBindingIssueKind::integerOverflow
               : ObjectTextureBindingIssueKind::limitExceeded,
           sourceIndex, definitionIndex);
}

} // namespace

ObjectTextureBindings buildObjectTextureBindings(
    const std::span<const TextureImportRequest> baseImports,
    const std::span<const ObjectTextureBindingSource> sources,
    const udsp::Archive &archive, const ObjectTextureBindingLimits &limits) {
  ObjectTextureBindings result;

  if (sources.size() > limits.maximumSources) {
    addIssue(result, ObjectTextureBindingIssueKind::sourceLimitExceeded);
    return result;
  }
  if (baseImports.size() > limits.maximumBaseImports ||
      baseImports.size() > limits.maximumGlobalImports) {
    addIssue(result, ObjectTextureBindingIssueKind::limitExceeded);
    return result;
  }
  std::size_t baseBytes = 0U;
  if (!checkedBytes(baseImports.size(), sizeof(TextureImportRequest),
                    baseBytes)) {
    addIssue(result, ObjectTextureBindingIssueKind::integerOverflow);
    return result;
  }
  if (baseBytes > limits.maximumTotalBytes) {
    addIssue(result, ObjectTextureBindingIssueKind::limitExceeded);
    return result;
  }

  std::unordered_map<std::size_t, TextureAssetId> globalIds;
  globalIds.reserve(baseImports.size());
  for (std::size_t index = 0U; index < baseImports.size(); ++index) {
    const auto &request = baseImports[index];
    if (index > std::numeric_limits<std::uint32_t>::max()) {
      addIssue(result, ObjectTextureBindingIssueKind::integerOverflow,
               std::nullopt, std::nullopt, index, request.archiveFileIndex);
      return result;
    }
    if (request.assetId.value != static_cast<std::uint32_t>(index)) {
      addIssue(result, ObjectTextureBindingIssueKind::invalidBaseAssetId,
               std::nullopt, std::nullopt, index, request.archiveFileIndex);
      return result;
    }
    if (request.archiveFileIndex >= archive.files().size()) {
      addIssue(result,
               ObjectTextureBindingIssueKind::baseArchiveFileIndexOutOfRange,
               std::nullopt, std::nullopt, index, request.archiveFileIndex);
      return result;
    }
    if (!globalIds.emplace(request.archiveFileIndex, request.assetId).second) {
      addIssue(result,
               ObjectTextureBindingIssueKind::duplicateBaseArchiveFileIndex,
               std::nullopt, std::nullopt, index, request.archiveFileIndex);
      return result;
    }
  }

  result.imports.assign(baseImports.begin(), baseImports.end());
  const auto appendHint =
      std::min(sources.size(), limits.maximumUniqueDefinitions);
  const auto importReserve =
      appendHint > limits.maximumGlobalImports - baseImports.size()
          ? limits.maximumGlobalImports
          : baseImports.size() + appendHint;
  result.imports.reserve(importReserve);
  result.definitionBindingIndexBySource.reserve(sources.size());
  result.definitions.reserve(
      std::min(sources.size(), limits.maximumUniqueDefinitions));

  struct SeenDefinition final {
    const assets::ObjectDefinition *object{};
    const assets::CcfMetadata *ccf{};
    std::size_t bindingIndex{};
  };
  std::unordered_map<std::size_t, SeenDefinition> seenDefinitions;
  seenDefinitions.reserve(
      std::min(sources.size(), limits.maximumUniqueDefinitions));

  std::size_t aggregateMaterials = 0U;
  std::size_t aggregateTextureEntries = 0U;
  for (std::size_t sourceIndex = 0U; sourceIndex < sources.size();
       ++sourceIndex) {
    const auto &source = sources[sourceIndex];
    if (source.object == nullptr || source.ccf == nullptr) {
      addIssue(result, ObjectTextureBindingIssueKind::invalidSource,
               sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }

    if (const auto found = seenDefinitions.find(source.definitionIndex);
        found != seenDefinitions.end()) {
      if (found->second.object != source.object ||
          found->second.ccf != source.ccf) {
        addIssue(result,
                 ObjectTextureBindingIssueKind::conflictingRepeatedDefinition,
                 sourceIndex, source.definitionIndex);
        failClosed(result);
        return result;
      }
      result.definitionBindingIndexBySource.push_back(
          found->second.bindingIndex);
      std::size_t totalBytes = 0U;
      const auto account = accountOutputBytes(result, limits, totalBytes);
      if (account != ByteAccountResult::success) {
        addByteAccountIssue(result, account, sourceIndex,
                            source.definitionIndex);
        failClosed(result);
        return result;
      }
      result.totalBytes = totalBytes;
      continue;
    }
    if (result.definitions.size() >= limits.maximumUniqueDefinitions) {
      addIssue(result, ObjectTextureBindingIssueKind::limitExceeded,
               sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }

    auto dependencyLimits = limits.dependencies;
    dependencyLimits.dependencies.maximumMaterialReferences =
        std::min(dependencyLimits.dependencies.maximumMaterialReferences,
                 limits.maximumMaterialsPerDefinition);
    dependencyLimits.dependencies.maximumTextureEdges =
        std::min(dependencyLimits.dependencies.maximumTextureEdges,
                 limits.maximumTextureEntriesPerDefinition);
    const auto resolution = assets::resolveObjectSceneDependencies(
        *source.object, *source.ccf, dependencyLimits);
    for (const auto &issue : resolution.issues) {
      addIssue(result, ObjectTextureBindingIssueKind::sceneDependencyFailure,
               sourceIndex, source.definitionIndex, std::nullopt, std::nullopt,
               issue);
    }
    for (const auto &issue : resolution.graphIssues) {
      addIssue(result, ObjectTextureBindingIssueKind::sceneGraphFailure,
               sourceIndex, source.definitionIndex, std::nullopt, std::nullopt,
               std::nullopt, issue);
    }
    if (!result.issues.empty()) {
      failClosed(result);
      return result;
    }
    if (resolution.materialIndices.size() >
            limits.maximumMaterialsPerDefinition ||
        resolution.textures.size() >
            limits.maximumTextureEntriesPerDefinition) {
      addIssue(result, ObjectTextureBindingIssueKind::limitExceeded,
               sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }
    std::size_t nextAggregateMaterials = 0U;
    std::size_t nextAggregateTextureEntries = 0U;
    if (!checkedAdd(aggregateMaterials, resolution.materialIndices.size(),
                    nextAggregateMaterials) ||
        !checkedAdd(aggregateTextureEntries, resolution.textures.size(),
                    nextAggregateTextureEntries)) {
      addIssue(result, ObjectTextureBindingIssueKind::integerOverflow,
               sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }
    aggregateMaterials = nextAggregateMaterials;
    aggregateTextureEntries = nextAggregateTextureEntries;
    if (aggregateMaterials > limits.maximumAggregateMaterials ||
        aggregateTextureEntries > limits.maximumAggregateTextureEntries) {
      addIssue(result, ObjectTextureBindingIssueKind::limitExceeded,
               sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }

    std::vector<std::uint32_t> materialReferences;
    materialReferences.reserve(resolution.materialIndices.size());
    std::vector<DrawMaterialState> materialStates;
    materialStates.reserve(resolution.materialIndices.size());
    for (const auto materialIndex : resolution.materialIndices) {
      if (materialIndex >= source.ccf->materials.size()) {
        addIssue(result, ObjectTextureBindingIssueKind::invalidSceneResolution,
                 sourceIndex, source.definitionIndex);
        failClosed(result);
        return result;
      }
      const auto &material = source.ccf->materials[materialIndex];
      materialReferences.push_back(material.reference);
      materialStates.push_back(makeDrawMaterialState(material));
    }

    auto textureEntryLimits = limits.textureEntries;
    textureEntryLimits.maximumDependencies =
        std::min(textureEntryLimits.maximumDependencies,
                 limits.maximumTextureEntriesPerDefinition);
    const auto textureResolution = assets::resolveObjectTextureEntries(
        *source.object, resolution, archive, textureEntryLimits);
    for (const auto &issue : textureResolution.issues) {
      addIssue(result, ObjectTextureBindingIssueKind::textureEntryFailure,
               sourceIndex, source.definitionIndex, std::nullopt, std::nullopt,
               std::nullopt, std::nullopt, issue);
    }
    if (!result.issues.empty()) {
      failClosed(result);
      return result;
    }
    if (textureResolution.entries.size() >
        limits.maximumTextureEntriesPerDefinition) {
      addIssue(result, ObjectTextureBindingIssueKind::limitExceeded,
               sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }

    auto bindingLimits = limits.binding;
    bindingLimits.maximumMaterials = std::min(
        bindingLimits.maximumMaterials, limits.maximumMaterialsPerDefinition);
    bindingLimits.maximumTextureEntries =
        std::min(bindingLimits.maximumTextureEntries,
                 limits.maximumTextureEntriesPerDefinition);
    bindingLimits.maximumImports =
        std::min(bindingLimits.maximumImports, limits.maximumGlobalImports);
    auto local = buildTextureBindingPlan(materialReferences, materialStates,
                                         resolution.textures, textureResolution,
                                         bindingLimits);
    for (const auto &issue : local.issues) {
      addIssue(result, ObjectTextureBindingIssueKind::textureBindingFailure,
               sourceIndex, source.definitionIndex, std::nullopt, std::nullopt,
               std::nullopt, std::nullopt, std::nullopt, issue);
    }
    if (!result.issues.empty()) {
      failClosed(result);
      return result;
    }
    if (local.materials.size() != materialReferences.size()) {
      addIssue(result, ObjectTextureBindingIssueKind::invalidLocalBinding,
               sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }

    std::unordered_set<std::size_t> localArchiveIndices;
    localArchiveIndices.reserve(local.imports.size());
    std::vector<std::optional<TextureAssetId>> localToGlobal(
        local.imports.size());
    for (std::size_t localIndex = 0U; localIndex < local.imports.size();
         ++localIndex) {
      const auto &request = local.imports[localIndex];
      if (localIndex > std::numeric_limits<std::uint32_t>::max() ||
          request.assetId.value != static_cast<std::uint32_t>(localIndex) ||
          request.archiveFileIndex >= archive.files().size() ||
          !localArchiveIndices.insert(request.archiveFileIndex).second) {
        addIssue(result, ObjectTextureBindingIssueKind::invalidLocalBinding,
                 sourceIndex, source.definitionIndex, std::nullopt,
                 request.archiveFileIndex);
        failClosed(result);
        return result;
      }

      if (const auto existing = globalIds.find(request.archiveFileIndex);
          existing != globalIds.end()) {
        if (existing->second.value >= result.imports.size()) {
          addIssue(result, ObjectTextureBindingIssueKind::invalidLocalBinding,
                   sourceIndex, source.definitionIndex, std::nullopt,
                   request.archiveFileIndex);
          failClosed(result);
          return result;
        }
        const auto &globalRequest = result.imports[existing->second.value];
        if (!globalRequest.logicalPath.empty() &&
            globalRequest.logicalPath != request.logicalPath) {
          addIssue(result, ObjectTextureBindingIssueKind::invalidLocalBinding,
                   sourceIndex, source.definitionIndex, std::nullopt,
                   request.archiveFileIndex);
          failClosed(result);
          return result;
        }
        localToGlobal[localIndex] = existing->second;
        continue;
      }

      if (result.imports.size() >= limits.maximumGlobalImports) {
        addIssue(result, ObjectTextureBindingIssueKind::limitExceeded,
                 sourceIndex, source.definitionIndex, std::nullopt,
                 request.archiveFileIndex);
        failClosed(result);
        return result;
      }
      if (result.imports.size() > std::numeric_limits<std::uint32_t>::max()) {
        addIssue(result, ObjectTextureBindingIssueKind::integerOverflow,
                 sourceIndex, source.definitionIndex, std::nullopt,
                 request.archiveFileIndex);
        failClosed(result);
        return result;
      }
      const TextureAssetId globalId{
          static_cast<std::uint32_t>(result.imports.size())};
      result.imports.push_back({
          .assetId = globalId,
          .archiveFileIndex = request.archiveFileIndex,
          .logicalPath = request.logicalPath,
      });
      globalIds.emplace(request.archiveFileIndex, globalId);
      localToGlobal[localIndex] = globalId;
    }

    for (auto &material : local.materials) {
      if (!assignGlobal(material.primary, localToGlobal) ||
          !assignGlobal(material.secondary, localToGlobal) ||
          !assignGlobal(material.environment, localToGlobal)) {
        addIssue(result, ObjectTextureBindingIssueKind::invalidLocalBinding,
                 sourceIndex, source.definitionIndex);
        failClosed(result);
        return result;
      }
    }

    const auto bindingIndex = result.definitions.size();
    result.definitions.push_back({
        .definitionIndex = source.definitionIndex,
        .materials = std::move(local.materials),
    });
    result.definitionBindingIndexBySource.push_back(bindingIndex);
    seenDefinitions.emplace(source.definitionIndex,
                            SeenDefinition{
                                .object = source.object,
                                .ccf = source.ccf,
                                .bindingIndex = bindingIndex,
                            });

    std::size_t totalBytes = 0U;
    const auto account = accountOutputBytes(result, limits, totalBytes);
    if (account != ByteAccountResult::success) {
      addByteAccountIssue(result, account, sourceIndex, source.definitionIndex);
      failClosed(result);
      return result;
    }
    result.totalBytes = totalBytes;
  }

  if (sources.empty()) {
    std::size_t totalBytes = 0U;
    const auto account = accountOutputBytes(result, limits, totalBytes);
    if (account != ByteAccountResult::success) {
      addByteAccountIssue(result, account, std::nullopt, std::nullopt);
      failClosed(result);
      return result;
    }
    result.totalBytes = totalBytes;
  }
  return result;
}

} // namespace airfix::render
