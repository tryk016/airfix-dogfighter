#include "airfix/assets/AssetResolver.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace airfix::assets {
namespace {

[[nodiscard]] std::uint8_t asciiLower(const std::uint8_t value) noexcept {
    return value >= static_cast<std::uint8_t>('A') &&
            value <= static_cast<std::uint8_t>('Z')
        ? static_cast<std::uint8_t>(value + ('a' - 'A'))
        : value;
}

[[nodiscard]] bool asciiCaseEqual(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (asciiLower(static_cast<std::uint8_t>(left[index])) !=
            asciiLower(static_cast<std::uint8_t>(right[index]))) {
            return false;
        }
    }
    return true;
}

void addIssue(
    ObjectDependencyResolution& result,
    const DependencyIssueKind kind,
    const std::optional<std::uint32_t> reference = std::nullopt) {
    result.issues.push_back({.kind = kind, .reference = reference});
}

void addIssue(
    ObjectSceneDependencyResolution& result,
    const DependencyIssueKind kind,
    const std::optional<std::uint32_t> reference = std::nullopt) {
    result.issues.push_back({.kind = kind, .reference = reference});
}

[[nodiscard]] std::string joinTextureLogicalPath(
    const std::string_view textureRoot,
    const std::string_view sourceText,
    const std::size_t pathLimit) {
    auto root = textureRoot;
    while (!root.empty() && (root.back() == '\\' || root.back() == '/')) {
        root.remove_suffix(1U);
    }
    if (root.empty()) {
        throw udsp::ParseError("texture root is empty");
    }

    constexpr std::string_view kTextureSuffix = ".gti";
    const auto normalizedRoot = udsp::normalizeLogicalPath(root, pathLimit);
    const auto normalizedSource = udsp::normalizeLogicalPath(sourceText, pathLimit);
    constexpr std::size_t kJoinSeparatorBytes = 1U;
    constexpr std::size_t kJoinFixedBytes =
        kJoinSeparatorBytes + kTextureSuffix.size();
    if (normalizedRoot.size() > pathLimit ||
        kJoinFixedBytes > pathLimit - normalizedRoot.size() ||
        normalizedSource.size() >
            pathLimit - normalizedRoot.size() - kJoinFixedBytes) {
        throw udsp::ParseError("joined texture path exceeds the configured limit");
    }
    return udsp::normalizeLogicalPath(
        normalizedRoot + '\\' + normalizedSource + std::string(kTextureSuffix),
        pathLimit);
}

[[nodiscard]] std::string archiveLogicalPath(
    const udsp::Archive& archive,
    const std::size_t directoryIndex,
    const std::size_t fileIndex) {
    std::string result = archive.directories().at(directoryIndex).path;
    if (!result.empty()) {
        result.push_back('\\');
    }
    result += archive.files().at(fileIndex).name;
    return result;
}

void addTextureIssue(
    ObjectTextureEntryResolution& result,
    const TextureEntryIssueKind kind,
    const std::optional<std::size_t> dependencyIndex) {
    result.issues.push_back({.kind = kind, .dependencyIndex = dependencyIndex});
}

[[nodiscard]] TextureEntryResolution resolveTextureEntriesImpl(
    const std::optional<std::string_view> textureRoot,
    const std::span<const TextureDependency> dependencies,
    const udsp::Archive& archive,
    const TextureEntryResolutionLimits& limits) {
    TextureEntryResolution result;
    if (dependencies.size() > limits.maximumDependencies) {
        addTextureIssue(result, TextureEntryIssueKind::limitExceeded, std::nullopt);
        return result;
    }

    result.entries.reserve(dependencies.size());
    result.issues.reserve(dependencies.size());
    for (std::size_t index = 0U; index < dependencies.size(); ++index) {
        const auto& dependency = dependencies[index];
        ResolvedTextureEntry entry{
            .role = dependency.role,
            .materialReference = dependency.materialReference,
            .materialIndex = dependency.materialIndex,
            .sourceText = {},
            .status = TextureEntryStatus::notFound,
            .logicalPath = {},
            .archiveDirectoryIndex = std::nullopt,
            .archiveFileIndex = std::nullopt,
            .archiveLogicalPath = std::nullopt,
        };

        if (!textureRoot.has_value() || textureRoot->empty()) {
            entry.status = TextureEntryStatus::missingTextureRoot;
            addTextureIssue(result, TextureEntryIssueKind::missingTextureRoot, index);
            result.entries.push_back(std::move(entry));
            continue;
        }

        try {
            entry.logicalPath = joinTextureLogicalPath(
                *textureRoot,
                dependency.sourceText,
                limits.maximumLogicalPathBytes);
            entry.sourceText = dependency.sourceText;
        }
        catch (const udsp::ParseError&) {
            entry.status = TextureEntryStatus::invalidLogicalPath;
            addTextureIssue(result, TextureEntryIssueKind::invalidLogicalPath, index);
            result.entries.push_back(std::move(entry));
            continue;
        }

        const auto lookup = archive.lookup(
            entry.logicalPath,
            limits.maximumLogicalPathBytes);
        switch (lookup.status) {
        case udsp::LookupStatus::notFound:
            entry.status = TextureEntryStatus::notFound;
            addTextureIssue(result, TextureEntryIssueKind::notFound, index);
            break;
        case udsp::LookupStatus::unique:
            entry.status = TextureEntryStatus::unique;
            entry.archiveDirectoryIndex = lookup.directoryIndex;
            entry.archiveFileIndex = lookup.fileIndex;
            entry.archiveLogicalPath = archiveLogicalPath(
                archive, lookup.directoryIndex, lookup.fileIndex);
            break;
        case udsp::LookupStatus::ambiguous:
            entry.status = TextureEntryStatus::ambiguous;
            addTextureIssue(result, TextureEntryIssueKind::ambiguous, index);
            break;
        }
        result.entries.push_back(std::move(entry));
    }
    return result;
}

} // namespace

TextureEntryResolution resolveTextureEntries(
    const std::optional<std::string_view> textureRoot,
    const std::span<const TextureDependency> dependencies,
    const udsp::Archive& archive,
    const TextureEntryResolutionLimits& limits) {
    return resolveTextureEntriesImpl(textureRoot, dependencies, archive, limits);
}

ObjectDependencyResolution resolveObjectDependencies(
    const ObjectDefinition& object,
    const CcfMetadata& ccf,
    const ObjectDependencyLimits& limits) {
    ObjectDependencyResolution result;
    if (!object.ccfPath.has_value()) {
        addIssue(result, DependencyIssueKind::missingCcfPath);
        return result;
    }
    if (!object.meshName.has_value()) {
        return result;
    }
    if (ccf.blueprints.size() > limits.maximumBlueprints ||
        ccf.materials.size() > limits.maximumMaterials) {
        addIssue(result, DependencyIssueKind::limitExceeded);
        return result;
    }

    std::optional<std::size_t> matchingBlueprint;
    for (std::size_t index = 0U; index < ccf.blueprints.size(); ++index) {
        if (!asciiCaseEqual(ccf.blueprints[index].name, *object.meshName)) {
            continue;
        }
        if (matchingBlueprint.has_value()) {
            result.selectorStatus = BlueprintSelectorStatus::ambiguous;
            addIssue(result, DependencyIssueKind::blueprintAmbiguous);
            return result;
        }
        matchingBlueprint = index;
    }
    if (!matchingBlueprint.has_value()) {
        result.selectorStatus = BlueprintSelectorStatus::notFound;
        addIssue(result, DependencyIssueKind::blueprintNotFound);
        return result;
    }

    result.selectorStatus = BlueprintSelectorStatus::unique;
    result.blueprintIndex = matchingBlueprint;
    const auto& blueprint = ccf.blueprints[*matchingBlueprint];
    if (blueprint.kind != CcfBlueprintKind::mesh) {
        return result;
    }
    if (!blueprint.meshIndex.has_value() || *blueprint.meshIndex >= ccf.meshes.size()) {
        addIssue(result, DependencyIssueKind::invalidMeshIndex);
        return result;
    }
    result.meshIndex = blueprint.meshIndex;
    const auto& mesh = ccf.meshes[*blueprint.meshIndex];

    std::vector<std::uint32_t> materialReferences;
    materialReferences.reserve(std::min(mesh.triangles.size(), limits.maximumMaterialReferences));
    std::unordered_set<std::uint32_t> seenMaterialReferences;
    seenMaterialReferences.reserve(
        std::min(mesh.triangles.size(), limits.maximumMaterialReferences));
    for (const auto& triangle : mesh.triangles) {
        if (seenMaterialReferences.contains(triangle.materialReference)) {
            continue;
        }
        if (materialReferences.size() >= limits.maximumMaterialReferences) {
            addIssue(result, DependencyIssueKind::limitExceeded);
            return result;
        }
        seenMaterialReferences.insert(triangle.materialReference);
        materialReferences.push_back(triangle.materialReference);
    }

    struct MaterialMatch {
        std::size_t index{};
        bool ambiguous{};
    };
    std::unordered_map<std::uint32_t, MaterialMatch> materialByReference;
    materialByReference.reserve(ccf.materials.size());
    for (std::size_t index = 0U; index < ccf.materials.size(); ++index) {
        const auto [iterator, inserted] = materialByReference.emplace(
            ccf.materials[index].reference, MaterialMatch{.index = index});
        if (!inserted) {
            iterator->second.ambiguous = true;
        }
    }

    const auto addTexture = [&](
        const TextureDependencyRole role,
        const std::uint32_t materialReference,
        const std::size_t materialIndex,
        const std::optional<std::string>& source) {
        if (!source.has_value()) {
            return true;
        }
        if (result.textures.size() >= limits.maximumTextureEdges) {
            addIssue(result, DependencyIssueKind::limitExceeded);
            return false;
        }
        result.textures.push_back({
            .role = role,
            .materialReference = materialReference,
            .materialIndex = materialIndex,
            .sourceText = *source,
        });
        return true;
    };

    for (const auto reference : materialReferences) {
        const auto match = materialByReference.find(reference);
        if (match == materialByReference.end()) {
            addIssue(result, DependencyIssueKind::materialNotFound, reference);
            continue;
        }
        if (match->second.ambiguous) {
            addIssue(result, DependencyIssueKind::materialAmbiguous, reference);
            continue;
        }
        const auto materialIndex = match->second.index;
        const auto& material = ccf.materials[materialIndex];
        result.materialIndices.push_back(materialIndex);
        if (!addTexture(
                TextureDependencyRole::primary, reference, materialIndex,
                material.primaryTexture) ||
            !addTexture(
                TextureDependencyRole::secondary, reference, materialIndex,
                material.secondaryTexture) ||
            !addTexture(
                TextureDependencyRole::environment, reference, materialIndex,
                material.environmentTexture)) {
            return result;
        }
    }
    return result;
}

ObjectSceneDependencyResolution resolveObjectSceneDependencies(
    const ObjectDefinition& object,
    const CcfMetadata& ccf,
    const ObjectSceneDependencyLimits& limits) {
    ObjectSceneDependencyResolution result;
    if (!object.ccfPath.has_value()) {
        addIssue(result, DependencyIssueKind::missingCcfPath);
        return result;
    }
    if (!object.meshName.has_value()) {
        return result;
    }
    if (ccf.blueprints.size() > limits.dependencies.maximumBlueprints ||
        ccf.materials.size() > limits.dependencies.maximumMaterials) {
        addIssue(result, DependencyIssueKind::limitExceeded);
        return result;
    }

    for (std::size_t index = 0U; index < ccf.blueprints.size(); ++index) {
        if (!asciiCaseEqual(ccf.blueprints[index].name, *object.meshName)) {
            continue;
        }
        if (result.rootBlueprintIndex.has_value()) {
            result.selectorStatus = BlueprintSelectorStatus::ambiguous;
            result.rootBlueprintIndex.reset();
            addIssue(result, DependencyIssueKind::blueprintAmbiguous);
            return result;
        }
        result.rootBlueprintIndex = index;
    }
    if (!result.rootBlueprintIndex.has_value()) {
        result.selectorStatus = BlueprintSelectorStatus::notFound;
        addIssue(result, DependencyIssueKind::blueprintNotFound);
        return result;
    }
    result.selectorStatus = BlueprintSelectorStatus::unique;

    auto subtree = resolveBlueprintSubtree(ccf, *result.rootBlueprintIndex, limits.graph);
    if (!subtree.issues.empty()) {
        result.graphIssues = std::move(subtree.issues);
        return result;
    }
    result.blueprintIndices = std::move(subtree.blueprintIndices);
    result.meshes.reserve(result.blueprintIndices.size());
    for (const auto blueprintIndex : result.blueprintIndices) {
        const auto& blueprint = ccf.blueprints[blueprintIndex];
        if (blueprint.kind != CcfBlueprintKind::mesh) {
            continue;
        }
        if (!blueprint.meshIndex.has_value() || *blueprint.meshIndex >= ccf.meshes.size()) {
            addIssue(result, DependencyIssueKind::invalidMeshIndex, blueprint.reference);
            return result;
        }
        result.meshes.push_back({
            .blueprintIndex = blueprintIndex,
            .meshIndex = *blueprint.meshIndex,
        });
    }

    std::vector<std::uint32_t> materialReferences;
    std::unordered_set<std::uint32_t> seenMaterialReferences;
    for (const auto& meshDependency : result.meshes) {
        const auto& mesh = ccf.meshes[meshDependency.meshIndex];
        for (const auto& triangle : mesh.triangles) {
            if (seenMaterialReferences.contains(triangle.materialReference)) {
                continue;
            }
            if (materialReferences.size() >=
                limits.dependencies.maximumMaterialReferences) {
                addIssue(result, DependencyIssueKind::limitExceeded);
                return result;
            }
            seenMaterialReferences.insert(triangle.materialReference);
            materialReferences.push_back(triangle.materialReference);
        }
    }

    struct MaterialMatch {
        std::size_t index{};
        bool ambiguous{};
    };
    std::unordered_map<std::uint32_t, MaterialMatch> materialByReference;
    materialByReference.reserve(ccf.materials.size());
    for (std::size_t index = 0U; index < ccf.materials.size(); ++index) {
        const auto [iterator, inserted] = materialByReference.emplace(
            ccf.materials[index].reference, MaterialMatch{.index = index});
        if (!inserted) {
            iterator->second.ambiguous = true;
        }
    }

    const auto addTexture = [&result, &limits](
        const TextureDependencyRole role,
        const std::uint32_t reference,
        const std::size_t materialIndex,
        const std::optional<std::string>& source) {
        if (!source.has_value()) {
            return true;
        }
        if (result.textures.size() >= limits.dependencies.maximumTextureEdges) {
            addIssue(result, DependencyIssueKind::limitExceeded);
            return false;
        }
        result.textures.push_back({
            .role = role,
            .materialReference = reference,
            .materialIndex = materialIndex,
            .sourceText = *source,
        });
        return true;
    };

    for (const auto reference : materialReferences) {
        const auto match = materialByReference.find(reference);
        if (match == materialByReference.end()) {
            addIssue(result, DependencyIssueKind::materialNotFound, reference);
            continue;
        }
        if (match->second.ambiguous) {
            addIssue(result, DependencyIssueKind::materialAmbiguous, reference);
            continue;
        }
        const auto materialIndex = match->second.index;
        const auto& material = ccf.materials[materialIndex];
        result.materialIndices.push_back(materialIndex);
        if (!addTexture(
                TextureDependencyRole::primary, reference, materialIndex,
                material.primaryTexture) ||
            !addTexture(
                TextureDependencyRole::secondary, reference, materialIndex,
                material.secondaryTexture) ||
            !addTexture(
                TextureDependencyRole::environment, reference, materialIndex,
                material.environmentTexture)) {
            return result;
        }
    }
    return result;
}

ObjectTextureEntryResolution resolveObjectTextureEntries(
    const ObjectDefinition& object,
    const ObjectDependencyResolution& dependencies,
    const udsp::Archive& archive,
    const TextureEntryResolutionLimits& limits) {
    const auto textureRoot = object.textureRoot.has_value()
        ? std::optional<std::string_view>{*object.textureRoot}
        : std::nullopt;
    return resolveTextureEntries(textureRoot, dependencies.textures, archive, limits);
}

ObjectTextureEntryResolution resolveObjectTextureEntries(
    const ObjectDefinition& object,
    const ObjectSceneDependencyResolution& dependencies,
    const udsp::Archive& archive,
    const TextureEntryResolutionLimits& limits) {
    const auto textureRoot = object.textureRoot.has_value()
        ? std::optional<std::string_view>{*object.textureRoot}
        : std::nullopt;
    return resolveTextureEntries(textureRoot, dependencies.textures, archive, limits);
}

} // namespace airfix::assets
