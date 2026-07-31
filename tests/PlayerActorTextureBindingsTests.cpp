#include "airfix/render/PlayerActorTextureBindings.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using namespace airfix::assets;
using namespace airfix::render;

void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void writeU32(
    Bytes& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
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

[[nodiscard]] Bytes makeTextureArchive(
    const std::vector<std::string>& fileNames) {
    constexpr std::string_view directory = "Graphics\\Textures";
    auto sortedNames = fileNames;
    std::stable_sort(
        sortedNames.begin(),
        sortedNames.end(),
        [](const auto& left, const auto& right) {
            return airfix::udsp::nameHash(left) <
                airfix::udsp::nameHash(right);
        });
    Bytes bytes(airfix::udsp::kHeaderSize, 0U);
    const auto directoryOffset =
        static_cast<std::uint32_t>(bytes.size());
    appendRecord(
        bytes,
        airfix::udsp::nameHash(directory),
        0U,
        0U,
        0U,
        static_cast<std::uint32_t>(sortedNames.size()),
        0U);

    const auto fileOffset = static_cast<std::uint32_t>(bytes.size());
    std::uint32_t nameOffset =
        static_cast<std::uint32_t>(directory.size() + 1U);
    for (const auto& name : sortedNames) {
        appendRecord(
            bytes,
            airfix::udsp::nameHash(name),
            nameOffset,
            0U,
            0U,
            0U,
            static_cast<std::uint32_t>(airfix::udsp::kHeaderSize));
        nameOffset += static_cast<std::uint32_t>(name.size() + 1U);
    }

    const auto stringOffset = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(), directory.begin(), directory.end());
    bytes.push_back(0U);
    for (const auto& name : sortedNames) {
        bytes.insert(bytes.end(), name.begin(), name.end());
        bytes.push_back(0U);
    }

    bytes[0] = 'U';
    bytes[1] = 'D';
    bytes[2] = 'S';
    bytes[3] = 'P';
    writeU32(bytes, 4U, airfix::udsp::kVersion);
    writeU32(bytes, 8U, airfix::udsp::kRecordSize);
    writeU32(bytes, 12U, directoryOffset);
    writeU32(
        bytes,
        16U,
        static_cast<std::uint32_t>(bytes.size()) - stringOffset);
    writeU32(bytes, 20U, stringOffset);
    writeU32(
        bytes,
        24U,
        static_cast<std::uint32_t>(
            sortedNames.size() * airfix::udsp::kRecordSize));
    writeU32(bytes, 28U, fileOffset);
    return bytes;
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] ObjectDefinition objectDefinition() {
    ObjectDefinition object;
    object.ccfPath = "Graphics\\Actor.ccf";
    object.meshName = "actor";
    object.textureRoot = "Graphics\\Textures";
    return object;
}

[[nodiscard]] CcfMetadata actorCcf(
    const std::optional<std::string>& primary = std::nullopt,
    const std::optional<std::string>& secondary = std::nullopt,
    const std::optional<std::string>& environment = std::nullopt) {
    CcfMetadata ccf;
    CcfMeshMetadata mesh;
    mesh.name = "actor";
    CcfMeshTriangleMetadata triangle;
    triangle.materialReference = 10U;
    mesh.triangles.push_back(triangle);
    ccf.meshes.push_back(std::move(mesh));
    CcfBlueprintMetadata blueprint;
    blueprint.kind = CcfBlueprintKind::mesh;
    blueprint.name = "actor";
    blueprint.reference = 20U;
    blueprint.meshIndex = 0U;
    ccf.blueprints.push_back(std::move(blueprint));
    CcfMaterialMetadata material;
    material.name = "actor-material";
    material.reference = 10U;
    material.primaryTexture = primary;
    material.secondaryTexture = secondary;
    material.environmentTexture = environment;
    material.properties2140 = CcfMaterialProperties2140{
        .firstVector = {1.25F, 2.5F, 3.75F},
        .secondVector = {4.5F, 5.25F, 6.75F},
        .scalar = 0.625F,
    };
    material.properties2150 = CcfMaterialProperties2150{
        .lightingMode = 2U,
        .gouraudShading = true,
        .blendMode = 3U,
    };
    material.flag2151 = true;
    ccf.materials.push_back(std::move(material));
    return ccf;
}

[[nodiscard]] DrawMaterialState actorMaterialState() {
    return {
        .lightingMode = 2U,
        .gouraudShading = true,
        .blendMode = 3U,
        .flag2151 = true,
        .scalar2140 = 0.625F,
        .firstVector2140 = {1.25F, 2.5F, 3.75F},
        .secondVector2140 = {4.5F, 5.25F, 6.75F},
    };
}

[[nodiscard]] std::size_t fileIndex(
    const airfix::udsp::Archive& archive,
    const std::string_view name) {
    const auto found = std::ranges::find(
        archive.files(), name, &airfix::udsp::FileEntry::name);
    require(found != archive.files().end(), "archive fixture file missing");
    return static_cast<std::size_t>(found - archive.files().begin());
}

[[nodiscard]] bool hasIssue(
    const PlayerActorTextureBindings& result,
    const PlayerActorTextureBindingIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

void requireAtomicFailure(
    const PlayerActorTextureBindings& result,
    const std::string& message) {
    require(
        !result.issues.empty() &&
            result.materialBindings.empty() &&
            result.imports.empty() &&
            result.totalBytes == 0U,
        message);
}

void testEmptyAndUntexturedActor() {
    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({"base.gti"}));
    const std::vector<TextureImportRequest> base{{
        .assetId = TextureAssetId{0U},
        .archiveFileIndex = fileIndex(archive, "base.gti"),
    }};

    auto emptyObject = objectDefinition();
    emptyObject.meshName.reset();
    auto result = buildPlayerActorTextureBindings(
        base, emptyObject, actorCcf(), archive);
    require(
        result.complete() && result.materialBindings.empty() &&
            result.imports == base,
        "empty actor changed the base texture namespace");

    result = buildPlayerActorTextureBindings(
        base, objectDefinition(), actorCcf(), archive);
    require(
        result.complete() && result.materialBindings.size() == 1U &&
            result.imports == base &&
            !result.materialBindings[0].primary.has_value() &&
            !result.materialBindings[0].secondary.has_value() &&
            !result.materialBindings[0].environment.has_value() &&
            result.materialBindings[0].state == actorMaterialState(),
        "untextured actor did not retain its material and recovered state");
}

void testActorOnlyAndFirstUseAppendOrder() {
    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({
            "base.gti",
            "primary.gti",
            "secondary.gti",
            "environment.gti",
        }));
    auto result = buildPlayerActorTextureBindings(
        {},
        objectDefinition(),
        actorCcf("primary", "secondary", "primary"),
        archive);
    require(
        result.complete() && result.imports.size() == 2U &&
            result.imports[0].archiveFileIndex ==
                fileIndex(archive, "primary.gti") &&
            result.imports[1].archiveFileIndex ==
                fileIndex(archive, "secondary.gti"),
        "actor-only imports lost role-first-use order or deduplication");
    require(
        result.materialBindings[0].primary == TextureAssetId{0U} &&
            result.materialBindings[0].secondary == TextureAssetId{1U} &&
            result.materialBindings[0].environment == TextureAssetId{0U} &&
            result.materialBindings[0].state == actorMaterialState(),
        "actor-only material IDs or recovered state were not retained");

    const std::vector<TextureImportRequest> base{{
        .assetId = TextureAssetId{0U},
        .archiveFileIndex = fileIndex(archive, "primary.gti"),
    }};
    result = buildPlayerActorTextureBindings(
        base,
        objectDefinition(),
        actorCcf("primary", "secondary", "environment"),
        archive);
    require(
        result.complete() && result.imports.size() == 3U &&
            result.imports[0] == base[0] &&
            result.imports[1].archiveFileIndex ==
                fileIndex(archive, "secondary.gti") &&
            result.imports[2].archiveFileIndex ==
                fileIndex(archive, "environment.gti"),
        "new actor imports were not appended after an exact base prefix");
}

void testEveryRoleReusesBaseNamespace() {
    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({
            "primary.gti",
            "secondary.gti",
            "environment.gti",
        }));
    const std::vector<TextureImportRequest> base{
        {
            .assetId = TextureAssetId{0U},
            .archiveFileIndex = fileIndex(archive, "environment.gti"),
        },
        {
            .assetId = TextureAssetId{1U},
            .archiveFileIndex = fileIndex(archive, "secondary.gti"),
        },
        {
            .assetId = TextureAssetId{2U},
            .archiveFileIndex = fileIndex(archive, "primary.gti"),
        },
    };
    const auto result = buildPlayerActorTextureBindings(
        base,
        objectDefinition(),
        actorCcf("primary", "secondary", "environment"),
        archive);
    require(
        result.complete() && result.imports == base &&
            result.materialBindings[0].primary == TextureAssetId{2U} &&
            result.materialBindings[0].secondary == TextureAssetId{1U} &&
            result.materialBindings[0].environment == TextureAssetId{0U},
        "primary/secondary/environment did not reuse global base IDs");
}

void testInvalidBaseImportsFailClosed() {
    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({"a.gti", "b.gti"}));
    const auto a = fileIndex(archive, "a.gti");
    const auto b = fileIndex(archive, "b.gti");

    const std::vector<TextureImportRequest> nonDense{
        {TextureAssetId{1U}, a},
    };
    auto result = buildPlayerActorTextureBindings(
        nonDense,
        objectDefinition(),
        actorCcf(),
        archive);
    requireAtomicFailure(result, "non-dense base ID was not atomic");
    require(
        hasIssue(
            result,
            PlayerActorTextureBindingIssueKind::invalidBaseAssetId),
        "non-dense base ID issue was not typed");

    const std::vector<TextureImportRequest> duplicate{
        {TextureAssetId{0U}, a},
        {TextureAssetId{1U}, a},
    };
    result = buildPlayerActorTextureBindings(
        duplicate,
        objectDefinition(),
        actorCcf(),
        archive);
    requireAtomicFailure(result, "duplicate base archive entry was not atomic");
    require(
        hasIssue(
            result,
            PlayerActorTextureBindingIssueKind::
                duplicateBaseArchiveFileIndex) &&
            result.issues[0].baseImportIndex == 1U &&
            result.issues[0].archiveFileIndex == a,
        "duplicate base archive issue lost its index");

    const std::vector<TextureImportRequest> outOfRange{
        {TextureAssetId{0U}, archive.files().size()},
    };
    result = buildPlayerActorTextureBindings(
        outOfRange,
        objectDefinition(),
        actorCcf(),
        archive);
    requireAtomicFailure(result, "out-of-range base import was not atomic");
    require(
        hasIssue(
            result,
            PlayerActorTextureBindingIssueKind::
                baseArchiveFileIndexOutOfRange),
        "out-of-range base archive issue was not typed");

    PlayerActorTextureBindingLimits limits;
    limits.maximumBaseImports = 1U;
    const std::vector<TextureImportRequest> overCount{
        {TextureAssetId{0U}, a},
        {TextureAssetId{1U}, b},
    };
    result = buildPlayerActorTextureBindings(
        overCount,
        objectDefinition(),
        actorCcf(),
        archive,
        limits);
    requireAtomicFailure(result, "base count limit was not atomic");
}

void testUpstreamFailuresPreserveDiagnostics() {
    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({"present.gti"}));

    auto object = objectDefinition();
    object.ccfPath.reset();
    auto result = buildPlayerActorTextureBindings(
        {}, object, actorCcf(), archive);
    requireAtomicFailure(result, "dependency failure was not atomic");
    require(
        result.issues[0].kind ==
                PlayerActorTextureBindingIssueKind::
                    sceneDependencyFailure &&
            result.issues[0].dependencyIssue.has_value() &&
            result.issues[0].dependencyIssue->kind ==
                DependencyIssueKind::missingCcfPath,
        "dependency cause was not preserved");

    auto malformed = actorCcf();
    malformed.blueprints[0].parentReference = 777U;
    result = buildPlayerActorTextureBindings(
        {}, objectDefinition(), malformed, archive);
    requireAtomicFailure(result, "graph failure was not atomic");
    require(
        result.issues[0].kind ==
                PlayerActorTextureBindingIssueKind::sceneGraphFailure &&
            result.issues[0].graphIssue.has_value() &&
            result.issues[0].graphIssue->kind ==
                BlueprintGraphIssueKind::missingParent &&
            result.issues[0].graphIssue->blueprintIndex == 0U,
        "graph cause/index was not preserved");

    result = buildPlayerActorTextureBindings(
        {},
        objectDefinition(),
        actorCcf("missing"),
        archive);
    requireAtomicFailure(result, "texture entry failure was not atomic");
    require(
        result.issues[0].kind ==
                PlayerActorTextureBindingIssueKind::textureEntryFailure &&
            result.issues[0].textureEntryIssue.has_value() &&
            result.issues[0].textureEntryIssue->kind ==
                TextureEntryIssueKind::notFound &&
            result.issues[0].textureEntryIssue->dependencyIndex == 0U,
        "texture entry cause/index was not preserved");

    auto limits = PlayerActorTextureBindingLimits{};
    limits.binding.maximumImports = 0U;
    result = buildPlayerActorTextureBindings(
        {},
        objectDefinition(),
        actorCcf("present"),
        archive,
        limits);
    requireAtomicFailure(result, "binding failure was not atomic");
    require(
        result.issues[0].kind ==
                PlayerActorTextureBindingIssueKind::
                    textureBindingFailure &&
            result.issues[0].textureBindingIssue.has_value() &&
            result.issues[0].textureBindingIssue->kind ==
                TextureBindingIssueKind::limitExceeded &&
            result.issues[0].textureBindingIssue->entryIndex == 0U,
        "binding cause/index was not preserved");
}

void testExactAndLimitMinusOneBudgets() {
    const auto archive = airfix::udsp::Archive::parse(
        makeTextureArchive({"base.gti", "actor.gti"}));
    const std::vector<TextureImportRequest> base{{
        .assetId = TextureAssetId{0U},
        .archiveFileIndex = fileIndex(archive, "base.gti"),
    }};
    const auto object = objectDefinition();
    const auto ccf = actorCcf("actor");
    const auto baseline = buildPlayerActorTextureBindings(
        base, object, ccf, archive);
    require(baseline.complete(), "budget baseline failed");

    PlayerActorTextureBindingLimits exact;
    exact.maximumBaseImports = base.size();
    exact.maximumActorMaterials = baseline.materialBindings.size();
    exact.maximumActorTextureEntries = 1U;
    exact.maximumGlobalImports = baseline.imports.size();
    exact.maximumTotalBytes = baseline.totalBytes;
    auto result = buildPlayerActorTextureBindings(
        base, object, ccf, archive, exact);
    require(
        result.complete() && result.totalBytes == baseline.totalBytes,
        "exact count/byte budgets were rejected");

    auto limited = exact;
    --limited.maximumBaseImports;
    result = buildPlayerActorTextureBindings(
        base, object, ccf, archive, limited);
    requireAtomicFailure(result, "base limit minus one was accepted");

    limited = exact;
    --limited.maximumActorMaterials;
    result = buildPlayerActorTextureBindings(
        base, object, ccf, archive, limited);
    requireAtomicFailure(result, "material limit minus one was accepted");

    limited = exact;
    --limited.maximumActorTextureEntries;
    result = buildPlayerActorTextureBindings(
        base, object, ccf, archive, limited);
    requireAtomicFailure(result, "texture-entry limit minus one was accepted");

    limited = exact;
    --limited.maximumGlobalImports;
    result = buildPlayerActorTextureBindings(
        base, object, ccf, archive, limited);
    requireAtomicFailure(result, "global import limit minus one was accepted");

    limited = exact;
    --limited.maximumTotalBytes;
    result = buildPlayerActorTextureBindings(
        base, object, ccf, archive, limited);
    requireAtomicFailure(result, "logical byte limit minus one was accepted");
}

} // namespace

int main() {
    try {
        testEmptyAndUntexturedActor();
        testActorOnlyAndFirstUseAppendOrder();
        testEveryRoleReusesBaseNamespace();
        testInvalidBaseImportsFailClosed();
        testUpstreamFailuresPreserveDiagnostics();
        testExactAndLimitMinusOneBudgets();
        std::cout << "all player actor texture binding tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "player actor texture binding test failure: "
            << error.what() << '\n';
        return 1;
    }
}
