#include "airfix/assets/AssetResolver.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void writeU32(Bytes& bytes, const std::size_t offset, const std::uint32_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 2U) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(offset + 3U) = static_cast<std::uint8_t>(value >> 24U);
}

void appendRecord(
    Bytes& bytes,
    const std::uint32_t hash,
    const std::uint32_t nameOffset,
    const std::uint32_t field08,
    const std::uint32_t field0C,
    const std::uint32_t field10,
    const std::uint32_t field14) {
    appendU32(bytes, hash);
    appendU32(bytes, nameOffset);
    appendU32(bytes, field08);
    appendU32(bytes, field0C);
    appendU32(bytes, field10);
    appendU32(bytes, field14);
}

[[nodiscard]] Bytes makeTextureArchive(const std::vector<std::string>& fileNames) {
    constexpr std::string_view directory = "Graphics\\Textures";
    auto sortedNames = fileNames;
    std::stable_sort(sortedNames.begin(), sortedNames.end(), [](const auto& left, const auto& right) {
        return airfix::udsp::nameHash(left) < airfix::udsp::nameHash(right);
    });
    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    appendRecord(
        archive,
        airfix::udsp::nameHash(directory),
        0U,
        0U,
        0U,
        static_cast<std::uint32_t>(sortedNames.size()),
        0U);

    const auto fileOffset = static_cast<std::uint32_t>(archive.size());
    std::uint32_t nameOffset = static_cast<std::uint32_t>(directory.size() + 1U);
    for (const auto& fileName : sortedNames) {
        appendRecord(
            archive,
            airfix::udsp::nameHash(fileName),
            nameOffset,
            0U,
            0U,
            0U,
            static_cast<std::uint32_t>(airfix::udsp::kHeaderSize));
        nameOffset += static_cast<std::uint32_t>(fileName.size() + 1U);
    }

    const auto stringOffset = static_cast<std::uint32_t>(archive.size());
    archive.insert(archive.end(), directory.begin(), directory.end());
    archive.push_back(0U);
    for (const auto& fileName : sortedNames) {
        archive.insert(archive.end(), fileName.begin(), fileName.end());
        archive.push_back(0U);
    }

    archive[0] = 'U';
    archive[1] = 'D';
    archive[2] = 'S';
    archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(archive, 8U, airfix::udsp::kRecordSize);
    writeU32(archive, 12U, directoryOffset);
    writeU32(archive, 16U, static_cast<std::uint32_t>(archive.size()) - stringOffset);
    writeU32(archive, 20U, stringOffset);
    writeU32(
        archive,
        24U,
        static_cast<std::uint32_t>(sortedNames.size() * airfix::udsp::kRecordSize));
    writeU32(archive, 28U, fileOffset);
    return archive;
}

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

[[nodiscard]] bool hasTextureIssue(
    const airfix::assets::ObjectTextureEntryResolution& result,
    const airfix::assets::TextureEntryIssueKind kind,
    const std::optional<std::size_t> dependencyIndex) {
    return std::any_of(
        result.issues.begin(), result.issues.end(),
        [kind, dependencyIndex](const auto& issue) {
            return issue.kind == kind && issue.dependencyIndex == dependencyIndex;
        });
}

[[nodiscard]] bool hasGraphIssue(
    const airfix::assets::ObjectSceneDependencyResolution& result,
    const airfix::assets::BlueprintGraphIssueKind kind) {
    return std::ranges::any_of(
        result.graphIssues,
        [kind](const auto& issue) { return issue.kind == kind; });
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
        .primaryTexture = "primary",
        .environmentTexture = "environment",
    });
    ccf.materials.push_back({
        .name = "second",
        .reference = 42U,
        .secondaryTexture = "secondary",
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
        result.textures[0].sourceText == "primary",
        "primary texture edge mismatch");
    require(result.textures[1].role == airfix::assets::TextureDependencyRole::environment &&
        result.textures[1].sourceText == "environment",
        "environment texture edge mismatch");
    require(result.textures[2].role == airfix::assets::TextureDependencyRole::secondary &&
        result.textures[2].sourceText == "secondary",
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

void testSceneSubtreeResolution() {
    auto object = makeObject();
    object.meshName = "GROUP";
    airfix::assets::CcfMetadata ccf;
    ccf.meshes.resize(3U);
    ccf.meshes[0].triangles = {
        {.materialReference = 7U},
        {.materialReference = 8U},
    };
    ccf.meshes[1].triangles = {{.materialReference = 8U}};
    ccf.meshes[2].triangles = {{.materialReference = 99U}};
    ccf.blueprints = {
        {
            .kind = airfix::assets::CcfBlueprintKind::nullNode,
            .name = "group",
            .reference = 10U,
            .parentReference = 0U,
        },
        {
            .kind = airfix::assets::CcfBlueprintKind::mesh,
            .name = "child-a",
            .reference = 20U,
            .parentReference = 10U,
            .meshIndex = 0U,
        },
        {
            .kind = airfix::assets::CcfBlueprintKind::nullNode,
            .name = "joint",
            .reference = 30U,
            .parentReference = 10U,
        },
        {
            .kind = airfix::assets::CcfBlueprintKind::mesh,
            .name = "child-b",
            .reference = 40U,
            .parentReference = 30U,
            .meshIndex = 1U,
        },
        {
            .kind = airfix::assets::CcfBlueprintKind::mesh,
            .name = "other-root",
            .reference = 50U,
            .parentReference = 0U,
            .meshIndex = 2U,
        },
    };
    ccf.materials = {
        {.name = "first", .reference = 7U, .primaryTexture = "first"},
        {.name = "second", .reference = 8U, .primaryTexture = "second"},
        {.name = "unreachable", .reference = 99U, .primaryTexture = "other"},
    };

    const auto result = airfix::assets::resolveObjectSceneDependencies(object, ccf);
    require(result.selectorStatus == airfix::assets::BlueprintSelectorStatus::unique &&
            result.rootBlueprintIndex == 0U && result.issues.empty() &&
            result.graphIssues.empty(),
        "valid null-root scene selection failed");
    require(result.blueprintIndices == std::vector<std::size_t>{0U, 1U, 2U, 3U},
        "selected scene subtree order mismatch");
    require(result.meshes.size() == 2U &&
            result.meshes[0].blueprintIndex == 1U &&
            result.meshes[0].meshIndex == 0U &&
            result.meshes[1].blueprintIndex == 3U &&
            result.meshes[1].meshIndex == 1U,
        "scene mesh preorder mismatch");
    require(result.materialIndices == std::vector<std::size_t>{0U, 1U} &&
            result.textures.size() == 2U,
        "scene materials were not deduplicated in first-use order");

    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({"first.gti", "second.gti"}));
    const auto textureEntries = airfix::assets::resolveObjectTextureEntries(
        object, result, archive);
    require(textureEntries.entries.size() == 2U && textureEntries.issues.empty(),
        "scene texture-entry overload did not resolve all dependencies");

    auto malformed = ccf;
    malformed.blueprints[3].parentReference = 777U;
    const auto missingParent = airfix::assets::resolveObjectSceneDependencies(
        object, malformed);
    require(missingParent.meshes.empty() &&
            hasGraphIssue(
                missingParent, airfix::assets::BlueprintGraphIssueKind::missingParent),
        "malformed scene graph exposed a partial model");

    auto limits = airfix::assets::ObjectSceneDependencyLimits{};
    limits.graph.maximumSelectedNodes = 3U;
    const auto limited = airfix::assets::resolveObjectSceneDependencies(
        object, ccf, limits);
    require(limited.meshes.empty() &&
            hasGraphIssue(limited, airfix::assets::BlueprintGraphIssueKind::limitExceeded),
        "scene subtree limit did not fail closed");
}

void testTextureEntryResolution() {
    auto object = makeObject();
    object.textureRoot = "graphics/Textures/";
    auto dependencies = airfix::assets::resolveObjectDependencies(object, makeCcf());
    dependencies.textures[0].sourceText = "PRIMARY";
    const auto archive = airfix::udsp::Archive::parse(makeTextureArchive({
        "Environment.gti",
        "primary.gti",
        "Secondary.gti",
    }));
    const auto result = airfix::assets::resolveObjectTextureEntries(
        object, dependencies, archive);
    const auto fileIndex = [&](const std::string_view name) {
        const auto found = std::find_if(
            archive.files().begin(), archive.files().end(),
            [name](const auto& file) { return file.name == name; });
        require(found != archive.files().end(), "synthetic texture entry missing");
        return static_cast<std::size_t>(found - archive.files().begin());
    };

    require(result.entries.size() == 3U && result.issues.empty(),
        "valid texture entries did not resolve uniquely");
    require(result.entries[0].role == airfix::assets::TextureDependencyRole::primary &&
            result.entries[0].materialReference == 7U &&
            result.entries[0].materialIndex == 0U &&
            result.entries[0].sourceText == "PRIMARY",
        "resolved primary texture lost dependency metadata");
    require(result.entries[0].status == airfix::assets::TextureEntryStatus::unique &&
            result.entries[0].logicalPath == "graphics\\Textures\\PRIMARY.gti" &&
            result.entries[0].archiveDirectoryIndex == 0U &&
            result.entries[0].archiveFileIndex == fileIndex("primary.gti") &&
            result.entries[0].archiveLogicalPath ==
                std::optional<std::string>{"Graphics\\Textures\\primary.gti"},
        "separator/case-normalized primary lookup metadata mismatch");
    require(result.entries[1].role == airfix::assets::TextureDependencyRole::environment &&
            result.entries[1].archiveFileIndex == fileIndex("Environment.gti") &&
            result.entries[2].role == airfix::assets::TextureDependencyRole::secondary &&
            result.entries[2].archiveFileIndex == fileIndex("Secondary.gti"),
        "texture dependency order was not deterministic");

    auto alreadyExtended = dependencies;
    alreadyExtended.textures.resize(1U);
    alreadyExtended.textures[0].sourceText = "primary.gti";
    const auto doubleSuffixArchive = airfix::udsp::Archive::parse(
        makeTextureArchive({"primary.gti.gti"}));
    const auto doubleSuffix = airfix::assets::resolveObjectTextureEntries(
        object, alreadyExtended, doubleSuffixArchive);
    require(doubleSuffix.entries.size() == 1U && doubleSuffix.issues.empty() &&
            doubleSuffix.entries[0].status == airfix::assets::TextureEntryStatus::unique &&
            doubleSuffix.entries[0].logicalPath ==
                "graphics\\Textures\\primary.gti.gti" &&
            doubleSuffix.entries[0].archiveLogicalPath ==
                std::optional<std::string>{"Graphics\\Textures\\primary.gti.gti"},
        "legacy resolver did not append .gti to an already extended source name");
}

void testTextureEntryFailures() {
    const auto archive = airfix::udsp::Archive::parse(makeTextureArchive({
        "primary.gti",
        "primary.gti",
        "environment.gti",
        "secondary.gti",
    }));
    auto object = makeObject();
    auto dependencies = airfix::assets::resolveObjectDependencies(object, makeCcf());
    auto ambiguous = airfix::assets::resolveObjectTextureEntries(
        object, dependencies, archive);
    require(ambiguous.entries[0].status == airfix::assets::TextureEntryStatus::ambiguous &&
            !ambiguous.entries[0].archiveFileIndex.has_value() &&
            hasTextureIssue(
                ambiguous, airfix::assets::TextureEntryIssueKind::ambiguous, 0U),
        "ambiguous texture lookup was not rejected");

    const auto missingArchive = airfix::udsp::Archive::parse(
        makeTextureArchive({"unrelated.gti"}));
    const auto missing = airfix::assets::resolveObjectTextureEntries(
        object, dependencies, missingArchive);
    require(missing.entries[0].status == airfix::assets::TextureEntryStatus::notFound &&
            hasTextureIssue(missing, airfix::assets::TextureEntryIssueKind::notFound, 0U),
        "missing texture lookup was not reported");

    object.textureRoot = std::nullopt;
    const auto missingRoot = airfix::assets::resolveObjectTextureEntries(
        object, dependencies, missingArchive);
    require(missingRoot.entries.size() == dependencies.textures.size() &&
            missingRoot.entries[0].status ==
                airfix::assets::TextureEntryStatus::missingTextureRoot &&
            hasTextureIssue(
                missingRoot,
                airfix::assets::TextureEntryIssueKind::missingTextureRoot,
                0U),
        "missing texture root was not reported per dependency");

    object.textureRoot = "";
    const auto emptyRoot = airfix::assets::resolveObjectTextureEntries(
        object, dependencies, missingArchive);
    require(emptyRoot.entries[0].status ==
            airfix::assets::TextureEntryStatus::missingTextureRoot,
        "empty texture root was accepted");
}

void testTexturePathValidationAndLimits() {
    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({"primary.gti"}));
    const auto verifyInvalid = [&](const std::string& root, const std::string& source) {
        auto object = makeObject();
        object.textureRoot = root;
        auto dependencies = airfix::assets::resolveObjectDependencies(object, makeCcf());
        dependencies.textures.resize(1U);
        dependencies.textures[0].sourceText = source;
        const auto result = airfix::assets::resolveObjectTextureEntries(
            object, dependencies, archive);
        require(result.entries.size() == 1U &&
                result.entries[0].status ==
                    airfix::assets::TextureEntryStatus::invalidLogicalPath &&
                hasTextureIssue(
                    result,
                    airfix::assets::TextureEntryIssueKind::invalidLogicalPath,
                    0U),
            "unsafe texture path was accepted");
    };
    verifyInvalid("Graphics\\..\\Textures", "primary");
    verifyInvalid("C:\\Graphics", "primary");
    verifyInvalid("Graphics\\Textures", "..\\primary");
    verifyInvalid("Graphics\\Textures", "\\primary");
    verifyInvalid("Graphics\\Textures", "");

    auto object = makeObject();
    auto dependencies = airfix::assets::resolveObjectDependencies(object, makeCcf());
    dependencies.textures.resize(1U);
    const auto exactPathLimit = airfix::assets::resolveObjectTextureEntries(
        object,
        dependencies,
        archive,
        {.maximumDependencies = 1U, .maximumLogicalPathBytes = 29U});
    require(exactPathLimit.entries[0].status ==
            airfix::assets::TextureEntryStatus::unique,
        "exact texture path limit did not include the .gti suffix correctly");

    const auto suffixLimited = airfix::assets::resolveObjectTextureEntries(
        object,
        dependencies,
        archive,
        {.maximumDependencies = 1U, .maximumLogicalPathBytes = 28U});
    require(suffixLimited.entries[0].status ==
            airfix::assets::TextureEntryStatus::invalidLogicalPath,
        "texture path limit omitted bytes from the .gti suffix");

    const auto pathLimited = airfix::assets::resolveObjectTextureEntries(
        object,
        dependencies,
        archive,
        {.maximumDependencies = 1U, .maximumLogicalPathBytes = 8U});
    require(pathLimited.entries[0].status ==
            airfix::assets::TextureEntryStatus::invalidLogicalPath,
        "texture logical-path limit was not enforced");

    dependencies.textures[0].sourceText = std::string(5'000U, 'x');
    const auto oversizedSource = airfix::assets::resolveObjectTextureEntries(
        object, dependencies, archive);
    require(oversizedSource.entries[0].status ==
                airfix::assets::TextureEntryStatus::invalidLogicalPath &&
            oversizedSource.entries[0].sourceText.empty(),
        "oversized texture source was copied into the resolution result");

    dependencies.textures[0].sourceText = "primary";
    const auto dependencyLimited = airfix::assets::resolveObjectTextureEntries(
        object,
        dependencies,
        archive,
        {.maximumDependencies = 0U, .maximumLogicalPathBytes = 4'096U});
    require(dependencyLimited.entries.empty() && dependencyLimited.issues.size() == 1U &&
            hasTextureIssue(
                dependencyLimited,
                airfix::assets::TextureEntryIssueKind::limitExceeded,
                std::nullopt),
        "texture dependency limit did not fail closed");
}

} // namespace

int main() {
    try {
        testMeshResolution();
        testSelectorVariants();
        testMaterialFailuresAndLimits();
        testSceneSubtreeResolution();
        testTextureEntryResolution();
        testTextureEntryFailures();
        testTexturePathValidationAndLimits();
        std::cout << "all asset resolver tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "asset resolver test failure: " << error.what() << '\n';
        return 1;
    }
}
