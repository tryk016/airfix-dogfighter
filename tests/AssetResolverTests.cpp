#include "airfix/assets/AssetResolver.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool hasIssue(
    const airfix::assets::ObjectDependencyResolution& result,
    const airfix::assets::DependencyIssueKind kind,
    const std::optional<std::uint32_t> reference = std::nullopt) {
    return std::any_of(
        result.issues.begin(), result.issues.end(),
        [kind, reference](const auto& issue) {
            return issue.kind == kind && issue.reference == reference;
        });
}

[[nodiscard]] airfix::assets::ObjectDefinition makeObject() {
    airfix::assets::ObjectDefinition object;
    object.ccfPath = "Graphics\\Model.ccf";
    object.meshName = "TARGET";
    object.textureRoot = "Graphics\\Textures";
    return object;
}

[[nodiscard]] airfix::assets::CcfMetadata makeCcf() {
    airfix::assets::CcfMetadata ccf;
    airfix::assets::CcfMeshMetadata mesh;
    mesh.name = "target";
    mesh.triangles.push_back({.materialReference = 7U});
    mesh.triangles.push_back({.materialReference = 7U});
    mesh.triangles.push_back({.materialReference = 42U});
    ccf.meshes.push_back(mesh);
    ccf.blueprints.push_back({
        .kind = airfix::assets::CcfBlueprintKind::mesh,
        .name = "target",
        .prefix = "scene",
        .reference = 3U,
        .meshIndex = 0U,
        .offset = 10U,
    });
    ccf.materials.push_back({
        .name = "first",
        .reference = 7U,
        .primaryTexture = "primary.gti",
        .environmentTexture = "environment.gti",
    });
    ccf.materials.push_back({
        .name = "second",
        .reference = 42U,
        .secondaryTexture = "secondary.gti",
    });
    return ccf;
}

void testMeshResolution() {
    const auto result = airfix::assets::resolveObjectDependencies(makeObject(), makeCcf());
    require(result.selectorStatus == airfix::assets::BlueprintSelectorStatus::unique,
        "blueprint selector status mismatch");
    require(result.blueprintIndex == 0U && result.meshIndex == 0U,
        "resolved blueprint/mesh index mismatch");
    require(result.materialIndices == std::vector<std::size_t>{0U, 1U},
        "material references were not stably deduplicated");
    require(result.textures.size() == 3U, "texture dependency count mismatch");
    require(result.textures[0].role == airfix::assets::TextureDependencyRole::primary &&
        result.textures[0].sourceText == "primary.gti",
        "primary texture edge mismatch");
    require(result.textures[1].role == airfix::assets::TextureDependencyRole::environment &&
        result.textures[1].sourceText == "environment.gti",
        "environment texture edge mismatch");
    require(result.textures[2].role == airfix::assets::TextureDependencyRole::secondary &&
        result.textures[2].sourceText == "secondary.gti",
        "secondary texture edge mismatch");
    require(result.issues.empty(), "unexpected dependency issues");
}

void testSelectorVariants() {
    auto object = makeObject();
    object.meshName = std::nullopt;
    const auto noSelector = airfix::assets::resolveObjectDependencies(object, makeCcf());
    require(noSelector.selectorStatus == airfix::assets::BlueprintSelectorStatus::noSelector &&
        noSelector.materialIndices.empty(),
        "no-selector resolution mismatch");

    object = makeObject();
    auto nullCcf = makeCcf();
    nullCcf.blueprints[0].kind = airfix::assets::CcfBlueprintKind::nullNode;
    nullCcf.blueprints[0].meshIndex = std::nullopt;
    const auto nullResult = airfix::assets::resolveObjectDependencies(object, nullCcf);
    require(nullResult.selectorStatus == airfix::assets::BlueprintSelectorStatus::unique &&
        nullResult.blueprintIndex == 0U && !nullResult.meshIndex.has_value() &&
        nullResult.materialIndices.empty(),
        "null blueprint resolution mismatch");

    auto missingCcf = makeObject();
    missingCcf.ccfPath = std::nullopt;
    const auto missingPath = airfix::assets::resolveObjectDependencies(missingCcf, makeCcf());
    require(hasIssue(missingPath, airfix::assets::DependencyIssueKind::missingCcfPath),
        "missing CCF path issue missing");
    require(missingPath.selectorStatus ==
            airfix::assets::BlueprintSelectorStatus::noSelector &&
        !missingPath.blueprintIndex.has_value() &&
        missingPath.materialIndices.empty() && missingPath.textures.empty(),
        "missing CCF path exposed unreachable dependencies");

    auto ambiguousCcf = makeCcf();
    ambiguousCcf.blueprints.push_back({
        .kind = airfix::assets::CcfBlueprintKind::light,
        .name = "Target",
    });
    const auto ambiguous = airfix::assets::resolveObjectDependencies(
        makeObject(), ambiguousCcf);
    require(ambiguous.selectorStatus ==
            airfix::assets::BlueprintSelectorStatus::ambiguous &&
        hasIssue(ambiguous, airfix::assets::DependencyIssueKind::blueprintAmbiguous),
        "ambiguous blueprint mismatch");
}

void testMaterialFailuresAndLimits() {
    auto missing = makeCcf();
    missing.materials.erase(missing.materials.begin());
    const auto missingResult = airfix::assets::resolveObjectDependencies(
        makeObject(), missing);
    require(hasIssue(
        missingResult, airfix::assets::DependencyIssueKind::materialNotFound, 7U),
        "missing material issue mismatch");

    auto duplicate = makeCcf();
    duplicate.materials.push_back({.name = "duplicate", .reference = 7U});
    const auto duplicateResult = airfix::assets::resolveObjectDependencies(
        makeObject(), duplicate);
    require(hasIssue(
            duplicateResult, airfix::assets::DependencyIssueKind::materialAmbiguous, 7U) &&
        !hasIssue(
            duplicateResult, airfix::assets::DependencyIssueKind::materialNotFound, 7U),
        "ambiguous material issue mismatch");

    const auto limited = airfix::assets::resolveObjectDependencies(
        makeObject(), makeCcf(), {.maximumBlueprints = 0U});
    require(hasIssue(limited, airfix::assets::DependencyIssueKind::limitExceeded),
        "blueprint limit issue mismatch");

    const auto materialLimited = airfix::assets::resolveObjectDependencies(
        makeObject(), makeCcf(), {.maximumMaterials = 1U});
    require(hasIssue(
        materialLimited, airfix::assets::DependencyIssueKind::limitExceeded),
        "material limit issue mismatch");

    auto invalidMesh = makeCcf();
    invalidMesh.blueprints[0].meshIndex = 99U;
    const auto invalidMeshResult = airfix::assets::resolveObjectDependencies(
        makeObject(), invalidMesh);
    require(hasIssue(invalidMeshResult, airfix::assets::DependencyIssueKind::invalidMeshIndex),
        "invalid mesh link issue mismatch");
}

} // namespace

int main() {
    try {
        testMeshResolution();
        testSelectorVariants();
        testMaterialFailuresAndLimits();
        std::cout << "all asset resolver tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "asset resolver test failure: " << error.what() << '\n';
        return 1;
    }
}
