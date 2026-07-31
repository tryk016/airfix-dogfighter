#include "airfix/render/MissionWorldRoomTextureBindings.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace airfix::render {
namespace {

void addIssue(
    MissionWorldRoomTextureBindings& result,
    const MissionWorldRoomTextureBindingIssueKind kind,
    const std::optional<std::size_t> sourceIndex = std::nullopt,
    const std::optional<std::size_t> textureEntryIndex = std::nullopt,
    const std::optional<assets::MissionWorldRoomDrawPlanIssueKind>
        drawPlanIssue = std::nullopt,
    const std::optional<assets::TextureEntryIssueKind>
        textureResolutionIssue = std::nullopt,
    const std::optional<TextureBindingIssueKind>
        textureBindingIssue = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .sourceIndex = sourceIndex,
        .textureEntryIndex = textureEntryIndex,
        .drawPlanIssue = drawPlanIssue,
        .textureResolutionIssue = textureResolutionIssue,
        .textureBindingIssue = textureBindingIssue,
    });
}

void failClosed(MissionWorldRoomTextureBindings& result) {
    result.worldRoomIndex.reset();
    result.materialBindingsBySource.clear();
    result.imports.clear();
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

[[nodiscard]] bool assignGlobal(
    std::optional<TextureAssetId>& role,
    const std::vector<std::optional<TextureAssetId>>& localToGlobal) {
    if (!role.has_value()) {
        return true;
    }
    const auto local = static_cast<std::size_t>(role->value);
    if (local >= localToGlobal.size() ||
        !localToGlobal[local].has_value()) {
        return false;
    }
    role = localToGlobal[local];
    return true;
}

} // namespace

MissionWorldRoomTextureBindings
buildMissionWorldRoomTextureBindings(
    const assets::MissionWorldRoomCatalog& catalog,
    const std::span<const assets::MissionCcfRoomLoadSource> loadSources,
    const std::size_t worldRoomIndex,
    const std::span<const MissionWorldRoomTextureSource> textureSources,
    const udsp::Archive& archive,
    const MissionWorldRoomTextureBindingLimits& limits) {
    MissionWorldRoomTextureBindings result;

    if (loadSources.size() > limits.maximumSources ||
        textureSources.size() > limits.maximumSources) {
        addIssue(
            result,
            MissionWorldRoomTextureBindingIssueKind::limitExceeded);
        return result;
    }
    if (textureSources.size() != loadSources.size()) {
        addIssue(
            result,
            MissionWorldRoomTextureBindingIssueKind::sourceCountMismatch);
        return result;
    }

    auto drawPlanLimits = limits.drawPlan;
    drawPlanLimits.maximumMaterialReferences = std::min(
        drawPlanLimits.maximumMaterialReferences,
        limits.maximumMaterials);
    drawPlanLimits.maximumTextureEdges = std::min(
        drawPlanLimits.maximumTextureEdges,
        limits.maximumTextureEntries);
    const auto drawPlan = assets::resolveMissionWorldRoomDrawPlan(
        catalog, loadSources, worldRoomIndex, drawPlanLimits);
    if (!drawPlan.complete()) {
        addIssue(
            result,
            MissionWorldRoomTextureBindingIssueKind::drawPlanDependency,
            std::nullopt,
            std::nullopt,
            drawPlan.issues.empty()
                ? std::nullopt
                : std::optional{drawPlan.issues.front().kind});
        return result;
    }
    if (drawPlan.sourceCount != loadSources.size()) {
        addIssue(
            result,
            MissionWorldRoomTextureBindingIssueKind::sourceCountMismatch);
        return result;
    }
    if (drawPlan.materials.size() > limits.maximumMaterials ||
        drawPlan.textures.size() > limits.maximumTextureEntries) {
        addIssue(
            result,
            MissionWorldRoomTextureBindingIssueKind::limitExceeded);
        return result;
    }

    for (std::size_t sourceIndex = 0U;
         sourceIndex < textureSources.size();
         ++sourceIndex) {
        const auto& source = textureSources[sourceIndex];
        if (source.ccf == nullptr ||
            source.ccf != loadSources[sourceIndex].ccf) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::invalidSource,
                sourceIndex);
            return result;
        }
        if (!udsp::isLogicalPathValid(
                source.ccfLogicalPath,
                limits.maximumCcfLogicalPathBytes)) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    invalidCcfLogicalPath,
                sourceIndex);
            return result;
        }

        try {
            const auto normalized = udsp::normalizeLogicalPath(
                source.ccfLogicalPath,
                limits.maximumCcfLogicalPathBytes);
            const auto lookup = archive.lookup(
                normalized, limits.maximumCcfLogicalPathBytes);
            switch (lookup.status) {
            case udsp::LookupStatus::notFound:
                addIssue(
                    result,
                    MissionWorldRoomTextureBindingIssueKind::ccfNotFound,
                    sourceIndex);
                return result;
            case udsp::LookupStatus::ambiguous:
                addIssue(
                    result,
                    MissionWorldRoomTextureBindingIssueKind::ccfAmbiguous,
                    sourceIndex);
                return result;
            case udsp::LookupStatus::unique:
                if (lookup.fileIndex != source.ccfArchiveFileIndex) {
                    addIssue(
                        result,
                        MissionWorldRoomTextureBindingIssueKind::
                            ccfIdentityMismatch,
                        sourceIndex);
                    return result;
                }
                break;
            }
        }
        catch (const udsp::ParseError&) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    invalidCcfLogicalPath,
                sourceIndex);
            return result;
        }
    }

    std::vector<std::vector<std::uint32_t>>
        materialReferences(loadSources.size());
    std::vector<std::vector<DrawMaterialState>>
        materialStates(loadSources.size());
    std::vector<std::vector<assets::TextureDependency>>
        dependencies(loadSources.size());
    for (const auto& material : drawPlan.materials) {
        if (material.sourceIndex >= loadSources.size() ||
            material.physicalMaterialIndex >=
                loadSources[material.sourceIndex].ccf->materials.size()) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::drawPlanDependency,
                material.sourceIndex);
            return result;
        }
        const auto& sourceMaterial =
            loadSources[material.sourceIndex]
                .ccf->materials[material.physicalMaterialIndex];
        materialReferences[material.sourceIndex].push_back(
            sourceMaterial.reference);
        materialStates[material.sourceIndex].push_back(
            makeDrawMaterialState(sourceMaterial));
    }
    for (const auto& texture : drawPlan.textures) {
        if (texture.sourceIndex >= dependencies.size()) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::drawPlanDependency,
                texture.sourceIndex);
            return result;
        }
        dependencies[texture.sourceIndex].push_back(texture.dependency);
    }

    std::vector<assets::TextureEntryResolution>
        resolutions(loadSources.size());
    std::vector<TextureBindingPlan> localBindings(loadSources.size());
    auto resolutionLimits = limits.textureEntriesPerSource;
    resolutionLimits.maximumDependencies = std::min(
        resolutionLimits.maximumDependencies,
        limits.maximumTextureEntries);
    auto bindingLimits = limits.bindingPerSource;
    bindingLimits.maximumMaterials = std::min(
        bindingLimits.maximumMaterials, limits.maximumMaterials);
    bindingLimits.maximumTextureEntries = std::min(
        bindingLimits.maximumTextureEntries,
        limits.maximumTextureEntries);
    bindingLimits.maximumImports = std::min(
        bindingLimits.maximumImports, limits.maximumImports);
    std::size_t aggregateMaterials = 0U;
    std::size_t aggregateEntries = 0U;
    for (std::size_t sourceIndex = 0U;
         sourceIndex < loadSources.size();
         ++sourceIndex) {
        std::size_t nextMaterials = 0U;
        std::size_t nextEntries = 0U;
        if (!checkedAdd(
                aggregateMaterials,
                materialReferences[sourceIndex].size(),
                nextMaterials) ||
            !checkedAdd(
                aggregateEntries,
                dependencies[sourceIndex].size(),
                nextEntries)) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::integerOverflow,
                sourceIndex);
            return result;
        }
        if (nextMaterials > limits.maximumMaterials ||
            nextEntries > limits.maximumTextureEntries) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::limitExceeded,
                sourceIndex);
            return result;
        }
        aggregateMaterials = nextMaterials;
        aggregateEntries = nextEntries;

        resolutions[sourceIndex] = assets::resolveTextureEntries(
            textureSources[sourceIndex].textureRoot,
            dependencies[sourceIndex],
            archive,
            resolutionLimits);
        if (!resolutions[sourceIndex].issues.empty()) {
            const auto& issue = resolutions[sourceIndex].issues.front();
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureResolutionDependency,
                sourceIndex,
                issue.dependencyIndex,
                std::nullopt,
                issue.kind);
            return result;
        }

    }

    std::unordered_map<std::size_t, bool> preflightImports;
    preflightImports.reserve(std::min(
        drawPlan.textures.size(), limits.maximumImports));
    std::vector<std::size_t> preflightCursor(loadSources.size());
    for (const auto& texture : drawPlan.textures) {
        const auto sourceIndex = texture.sourceIndex;
        const auto entryIndex = preflightCursor[sourceIndex]++;
        if (entryIndex >= resolutions[sourceIndex].entries.size() ||
            !resolutions[sourceIndex]
                 .entries[entryIndex]
                 .archiveFileIndex.has_value()) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureBindingDependency,
                sourceIndex,
                entryIndex);
            return result;
        }
        const auto archiveFileIndex =
            *resolutions[sourceIndex].entries[entryIndex].archiveFileIndex;
        if (preflightImports.contains(archiveFileIndex)) {
            continue;
        }
        if (preflightImports.size() >= limits.maximumImports) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::limitExceeded,
                sourceIndex,
                entryIndex);
            return result;
        }
        if (preflightImports.size() >
            std::numeric_limits<std::uint32_t>::max()) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::integerOverflow,
                sourceIndex,
                entryIndex);
            return result;
        }
        preflightImports.emplace(archiveFileIndex, true);
    }

    for (std::size_t sourceIndex = 0U;
         sourceIndex < loadSources.size();
         ++sourceIndex) {
        localBindings[sourceIndex] = buildTextureBindingPlan(
            materialReferences[sourceIndex],
            materialStates[sourceIndex],
            dependencies[sourceIndex],
            resolutions[sourceIndex],
            bindingLimits);
        if (!localBindings[sourceIndex].issues.empty()) {
            const auto& issue = localBindings[sourceIndex].issues.front();
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureBindingDependency,
                sourceIndex,
                issue.entryIndex,
                std::nullopt,
                std::nullopt,
                issue.kind);
            return result;
        }
    }

    std::vector<std::unordered_map<std::size_t, TextureAssetId>>
        localIdsByArchive(loadSources.size());
    for (std::size_t sourceIndex = 0U;
         sourceIndex < localBindings.size();
         ++sourceIndex) {
        auto& localIds = localIdsByArchive[sourceIndex];
        localIds.reserve(localBindings[sourceIndex].imports.size());
        for (std::size_t localIndex = 0U;
             localIndex < localBindings[sourceIndex].imports.size();
             ++localIndex) {
            const auto& request =
                localBindings[sourceIndex].imports[localIndex];
            if (request.assetId.value != localIndex ||
                !localIds.emplace(
                    request.archiveFileIndex, request.assetId).second) {
                addIssue(
                    result,
                    MissionWorldRoomTextureBindingIssueKind::
                        textureBindingDependency,
                    sourceIndex);
                return result;
            }
        }
    }

    std::unordered_map<std::size_t, TextureAssetId> globalIds;
    globalIds.reserve(std::min(
        drawPlan.textures.size(), limits.maximumImports));
    result.imports.reserve(std::min(
        drawPlan.textures.size(), limits.maximumImports));
    std::vector<std::vector<std::optional<TextureAssetId>>>
        localToGlobal(loadSources.size());
    for (std::size_t sourceIndex = 0U;
         sourceIndex < localBindings.size();
         ++sourceIndex) {
        localToGlobal[sourceIndex].resize(
            localBindings[sourceIndex].imports.size());
    }

    std::vector<std::size_t> entryCursor(loadSources.size());
    for (const auto& texture : drawPlan.textures) {
        const auto sourceIndex = texture.sourceIndex;
        const auto entryIndex = entryCursor[sourceIndex]++;
        if (entryIndex >= resolutions[sourceIndex].entries.size() ||
            !resolutions[sourceIndex]
                 .entries[entryIndex]
                 .archiveFileIndex.has_value()) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureBindingDependency,
                sourceIndex,
                entryIndex);
            failClosed(result);
            return result;
        }
        const auto archiveFileIndex =
            *resolutions[sourceIndex].entries[entryIndex].archiveFileIndex;
        const auto local = localIdsByArchive[sourceIndex].find(
            archiveFileIndex);
        if (local == localIdsByArchive[sourceIndex].end()) {
            addIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureBindingDependency,
                sourceIndex,
                entryIndex);
            failClosed(result);
            return result;
        }

        auto global = globalIds.find(archiveFileIndex);
        if (global == globalIds.end()) {
            if (result.imports.size() >= limits.maximumImports) {
                addIssue(
                    result,
                    MissionWorldRoomTextureBindingIssueKind::limitExceeded,
                    sourceIndex,
                    entryIndex);
                failClosed(result);
                return result;
            }
            if (result.imports.size() >
                std::numeric_limits<std::uint32_t>::max()) {
                addIssue(
                    result,
                    MissionWorldRoomTextureBindingIssueKind::
                        integerOverflow,
                    sourceIndex,
                    entryIndex);
                failClosed(result);
                return result;
            }
            const TextureAssetId globalId{
                static_cast<std::uint32_t>(result.imports.size())};
            result.imports.push_back({
                .assetId = globalId,
                .archiveFileIndex = archiveFileIndex,
            });
            global = globalIds.emplace(
                archiveFileIndex, globalId).first;
        }
        localToGlobal[sourceIndex][local->second.value] = global->second;
    }

    result.materialBindingsBySource.resize(loadSources.size());
    for (std::size_t sourceIndex = 0U;
         sourceIndex < localBindings.size();
         ++sourceIndex) {
        auto& output = result.materialBindingsBySource[sourceIndex];
        output = std::move(localBindings[sourceIndex].materials);
        for (auto& material : output) {
            if (!assignGlobal(
                    material.primary, localToGlobal[sourceIndex]) ||
                !assignGlobal(
                    material.secondary, localToGlobal[sourceIndex]) ||
                !assignGlobal(
                    material.environment, localToGlobal[sourceIndex])) {
                addIssue(
                    result,
                    MissionWorldRoomTextureBindingIssueKind::
                        textureBindingDependency,
                    sourceIndex);
                failClosed(result);
                return result;
            }
        }
    }

    result.worldRoomIndex = worldRoomIndex;
    return result;
}

} // namespace airfix::render
