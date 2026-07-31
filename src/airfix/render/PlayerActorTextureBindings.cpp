#include "airfix/render/PlayerActorTextureBindings.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    PlayerActorTextureBindings& result,
    const PlayerActorTextureBindingIssueKind kind,
    const std::optional<std::size_t> baseImportIndex = std::nullopt,
    const std::optional<std::size_t> archiveFileIndex = std::nullopt,
    const std::optional<assets::DependencyIssue> dependencyIssue =
        std::nullopt,
    const std::optional<assets::BlueprintGraphIssue> graphIssue =
        std::nullopt,
    const std::optional<assets::TextureEntryIssue> textureEntryIssue =
        std::nullopt,
    const std::optional<TextureBindingIssue> textureBindingIssue =
        std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .baseImportIndex = baseImportIndex,
        .archiveFileIndex = archiveFileIndex,
        .dependencyIssue = dependencyIssue,
        .graphIssue = graphIssue,
        .textureEntryIssue = textureEntryIssue,
        .textureBindingIssue = textureBindingIssue,
    });
}

void failClosed(PlayerActorTextureBindings& result) noexcept {
    result.materialBindings.clear();
    result.imports.clear();
    result.totalBytes = 0U;
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checkedBytes(
    const std::size_t count,
    const std::size_t elementSize,
    std::size_t& total) noexcept {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        return false;
    }
    return checkedAdd(total, count * elementSize, total);
}

[[nodiscard]] bool assignGlobal(
    std::optional<TextureAssetId>& role,
    const std::vector<std::optional<TextureAssetId>>& localToGlobal) noexcept {
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

} // namespace

PlayerActorTextureBindings buildPlayerActorTextureBindings(
    const std::span<const TextureImportRequest> baseImports,
    const assets::ObjectDefinition& object,
    const assets::CcfMetadata& ccf,
    const udsp::Archive& archive,
    const PlayerActorTextureBindingLimits& limits) {
    PlayerActorTextureBindings result;

    if (baseImports.size() > limits.maximumBaseImports ||
        baseImports.size() > limits.maximumGlobalImports) {
        addIssue(
            result, PlayerActorTextureBindingIssueKind::limitExceeded);
        return result;
    }
    std::size_t baseBytes = 0U;
    if (!checkedBytes(
            baseImports.size(),
            sizeof(TextureImportRequest),
            baseBytes)) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::integerOverflow);
        return result;
    }
    if (baseBytes > limits.maximumTotalBytes) {
        addIssue(
            result, PlayerActorTextureBindingIssueKind::limitExceeded);
        return result;
    }

    std::unordered_map<std::size_t, TextureAssetId> globalIds;
    globalIds.reserve(baseImports.size());
    for (std::size_t index = 0U; index < baseImports.size(); ++index) {
        const auto& request = baseImports[index];
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::integerOverflow,
                index,
                request.archiveFileIndex);
            return result;
        }
        if (request.assetId.value != static_cast<std::uint32_t>(index)) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::invalidBaseAssetId,
                index,
                request.archiveFileIndex);
            return result;
        }
        if (request.archiveFileIndex >= archive.files().size()) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::
                    baseArchiveFileIndexOutOfRange,
                index,
                request.archiveFileIndex);
            return result;
        }
        if (!globalIds.emplace(
                request.archiveFileIndex, request.assetId).second) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::
                    duplicateBaseArchiveFileIndex,
                index,
                request.archiveFileIndex);
            return result;
        }
    }

    auto dependencyLimits = limits.dependencies;
    dependencyLimits.dependencies.maximumMaterialReferences = std::min(
        dependencyLimits.dependencies.maximumMaterialReferences,
        limits.maximumActorMaterials);
    dependencyLimits.dependencies.maximumTextureEdges = std::min(
        dependencyLimits.dependencies.maximumTextureEdges,
        limits.maximumActorTextureEntries);
    const auto resolution = assets::resolveObjectSceneDependencies(
        object, ccf, dependencyLimits);
    for (const auto& issue : resolution.issues) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::sceneDependencyFailure,
            std::nullopt,
            std::nullopt,
            issue);
    }
    for (const auto& issue : resolution.graphIssues) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::sceneGraphFailure,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            issue);
    }
    if (!result.issues.empty()) {
        return result;
    }
    if (resolution.materialIndices.size() >
            limits.maximumActorMaterials ||
        resolution.textures.size() >
            limits.maximumActorTextureEntries) {
        addIssue(
            result, PlayerActorTextureBindingIssueKind::limitExceeded);
        return result;
    }
    std::size_t minimumOutputBytes = baseBytes;
    if (!checkedBytes(
            resolution.materialIndices.size(),
            sizeof(DrawMaterial),
            minimumOutputBytes)) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::integerOverflow);
        return result;
    }
    if (minimumOutputBytes > limits.maximumTotalBytes) {
        addIssue(
            result, PlayerActorTextureBindingIssueKind::limitExceeded);
        return result;
    }

    std::vector<std::uint32_t> materialReferences;
    materialReferences.reserve(resolution.materialIndices.size());
    std::vector<DrawMaterialState> materialStates;
    materialStates.reserve(resolution.materialIndices.size());
    for (const auto materialIndex : resolution.materialIndices) {
        if (materialIndex >= ccf.materials.size()) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::
                    invalidSceneResolution);
            return result;
        }
        const auto& material = ccf.materials[materialIndex];
        materialReferences.push_back(material.reference);
        materialStates.push_back(makeDrawMaterialState(material));
    }

    auto textureEntryLimits = limits.textureEntries;
    textureEntryLimits.maximumDependencies = std::min(
        textureEntryLimits.maximumDependencies,
        limits.maximumActorTextureEntries);
    const auto textureResolution = assets::resolveObjectTextureEntries(
        object, resolution, archive, textureEntryLimits);
    for (const auto& issue : textureResolution.issues) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::textureEntryFailure,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            issue);
    }
    if (!result.issues.empty()) {
        return result;
    }
    if (textureResolution.entries.size() >
        limits.maximumActorTextureEntries) {
        addIssue(
            result, PlayerActorTextureBindingIssueKind::limitExceeded);
        return result;
    }

    auto bindingLimits = limits.binding;
    bindingLimits.maximumMaterials = std::min(
        bindingLimits.maximumMaterials,
        limits.maximumActorMaterials);
    bindingLimits.maximumTextureEntries = std::min(
        bindingLimits.maximumTextureEntries,
        limits.maximumActorTextureEntries);
    bindingLimits.maximumImports = std::min(
        bindingLimits.maximumImports,
        limits.maximumGlobalImports);
    auto local = buildTextureBindingPlan(
        materialReferences,
        materialStates,
        resolution.textures,
        textureResolution,
        bindingLimits);
    for (const auto& issue : local.issues) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::textureBindingFailure,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            issue);
    }
    if (!result.issues.empty()) {
        return result;
    }
    if (local.materials.size() != materialReferences.size() ||
        local.imports.size() > limits.maximumGlobalImports) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::invalidLocalBinding);
        return result;
    }

    std::unordered_set<std::size_t> localArchiveIndices;
    localArchiveIndices.reserve(local.imports.size());
    std::size_t appendCount = 0U;
    for (std::size_t localIndex = 0U;
         localIndex < local.imports.size();
         ++localIndex) {
        const auto& request = local.imports[localIndex];
        if (localIndex > std::numeric_limits<std::uint32_t>::max() ||
            request.assetId.value !=
                static_cast<std::uint32_t>(localIndex) ||
            request.archiveFileIndex >= archive.files().size()) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::invalidLocalBinding,
                std::nullopt,
                request.archiveFileIndex);
            return result;
        }
        if (!localArchiveIndices.insert(
                request.archiveFileIndex).second) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::invalidLocalBinding,
                std::nullopt,
                request.archiveFileIndex);
            return result;
        }
        if (globalIds.contains(request.archiveFileIndex)) {
            continue;
        }
        std::size_t nextAppendCount = 0U;
        if (!checkedAdd(appendCount, 1U, nextAppendCount)) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::integerOverflow);
            return result;
        }
        appendCount = nextAppendCount;
    }

    std::size_t finalImportCount = 0U;
    if (!checkedAdd(
            baseImports.size(), appendCount, finalImportCount)) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::integerOverflow);
        return result;
    }
    if (finalImportCount > limits.maximumGlobalImports) {
        addIssue(
            result, PlayerActorTextureBindingIssueKind::limitExceeded);
        return result;
    }
    if (finalImportCount != 0U &&
        finalImportCount - 1U >
            std::numeric_limits<std::uint32_t>::max()) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::integerOverflow);
        return result;
    }

    std::size_t totalBytes = 0U;
    if (!checkedBytes(
            local.materials.size(),
            sizeof(DrawMaterial),
            totalBytes) ||
        !checkedBytes(
            finalImportCount,
            sizeof(TextureImportRequest),
            totalBytes)) {
        addIssue(
            result,
            PlayerActorTextureBindingIssueKind::integerOverflow);
        return result;
    }
    if (totalBytes > limits.maximumTotalBytes) {
        addIssue(
            result, PlayerActorTextureBindingIssueKind::limitExceeded);
        return result;
    }

    std::vector<std::optional<TextureAssetId>> localToGlobal(
        local.imports.size());
    result.imports.assign(baseImports.begin(), baseImports.end());
    result.imports.reserve(finalImportCount);
    for (std::size_t localIndex = 0U;
         localIndex < local.imports.size();
         ++localIndex) {
        if (localToGlobal[localIndex].has_value()) {
            continue;
        }
        const auto& request = local.imports[localIndex];
        if (const auto existing =
                globalIds.find(request.archiveFileIndex);
            existing != globalIds.end()) {
            localToGlobal[localIndex] = existing->second;
            continue;
        }
        const TextureAssetId globalId{
            static_cast<std::uint32_t>(result.imports.size())};
        result.imports.push_back({
            .assetId = globalId,
            .archiveFileIndex = request.archiveFileIndex,
        });
        globalIds.emplace(request.archiveFileIndex, globalId);
        localToGlobal[localIndex] = globalId;
    }

    result.materialBindings = std::move(local.materials);
    for (auto& material : result.materialBindings) {
        if (!assignGlobal(material.primary, localToGlobal) ||
            !assignGlobal(material.secondary, localToGlobal) ||
            !assignGlobal(material.environment, localToGlobal)) {
            addIssue(
                result,
                PlayerActorTextureBindingIssueKind::invalidLocalBinding);
            failClosed(result);
            return result;
        }
    }
    result.totalBytes = totalBytes;
    return result;
}

} // namespace airfix::render
