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

} // namespace

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

} // namespace airfix::assets
