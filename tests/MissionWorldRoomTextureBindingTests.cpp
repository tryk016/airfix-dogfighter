#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/MissionWorldRoomDrawAssembly.hpp"
#include "airfix/render/MissionWorldRoomTextureBindings.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using namespace airfix::assets;
using namespace airfix::render;

struct TestDirectory {
    std::string path;
    std::vector<std::string> files;
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

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

[[nodiscard]] Bytes makeArchive(std::vector<TestDirectory> directories) {
    std::stable_sort(
        directories.begin(),
        directories.end(),
        [](const auto& left, const auto& right) {
            return airfix::udsp::nameHash(left.path) <
                airfix::udsp::nameHash(right.path);
        });
    for (auto& directory : directories) {
        std::stable_sort(
            directory.files.begin(),
            directory.files.end(),
            [](const auto& left, const auto& right) {
                return airfix::udsp::nameHash(left) <
                    airfix::udsp::nameHash(right);
            });
    }

    Bytes strings;
    struct EncodedDirectory {
        std::uint32_t pathOffset{};
        std::uint32_t firstFileIndex{};
        std::vector<std::uint32_t> fileNameOffsets;
    };
    std::vector<EncodedDirectory> encodedDirectories;
    std::uint32_t fileCount = 0U;
    for (const auto& directory : directories) {
        EncodedDirectory encoded{
            .pathOffset = static_cast<std::uint32_t>(strings.size()),
            .firstFileIndex = fileCount,
        };
        strings.insert(
            strings.end(), directory.path.begin(), directory.path.end());
        strings.push_back(0U);
        for (const auto& file : directory.files) {
            encoded.fileNameOffsets.push_back(
                static_cast<std::uint32_t>(strings.size()));
            strings.insert(strings.end(), file.begin(), file.end());
            strings.push_back(0U);
            ++fileCount;
        }
        encodedDirectories.push_back(std::move(encoded));
    }

    Bytes result(airfix::udsp::kHeaderSize, 0U);
    const auto directoryOffset =
        static_cast<std::uint32_t>(result.size());
    for (std::size_t index = 0U; index < directories.size(); ++index) {
        appendRecord(
            result,
            airfix::udsp::nameHash(directories[index].path),
            encodedDirectories[index].pathOffset,
            0U,
            0U,
            static_cast<std::uint32_t>(
                directories[index].files.size()),
            encodedDirectories[index].firstFileIndex *
                static_cast<std::uint32_t>(
                    airfix::udsp::kRecordSize));
    }
    const auto fileOffset = static_cast<std::uint32_t>(result.size());
    for (std::size_t directoryIndex = 0U;
         directoryIndex < directories.size();
         ++directoryIndex) {
        for (std::size_t fileIndex = 0U;
             fileIndex < directories[directoryIndex].files.size();
             ++fileIndex) {
            appendRecord(
                result,
                airfix::udsp::nameHash(
                    directories[directoryIndex].files[fileIndex]),
                encodedDirectories[directoryIndex]
                    .fileNameOffsets[fileIndex],
                0U,
                0U,
                0U,
                static_cast<std::uint32_t>(
                    airfix::udsp::kHeaderSize));
        }
    }
    const auto stringOffset = static_cast<std::uint32_t>(result.size());
    result.insert(result.end(), strings.begin(), strings.end());
    result[0] = 'U';
    result[1] = 'D';
    result[2] = 'S';
    result[3] = 'P';
    writeU32(result, 4U, airfix::udsp::kVersion);
    writeU32(
        result,
        8U,
        static_cast<std::uint32_t>(
            directories.size() * airfix::udsp::kRecordSize));
    writeU32(result, 12U, directoryOffset);
    writeU32(
        result, 16U, static_cast<std::uint32_t>(strings.size()));
    writeU32(result, 20U, stringOffset);
    writeU32(
        result,
        24U,
        fileCount *
            static_cast<std::uint32_t>(airfix::udsp::kRecordSize));
    writeU32(result, 28U, fileOffset);
    return result;
}

[[nodiscard]] CcfRoomMetadata room(
    std::string name,
    const std::uint32_t reference,
    const bool primary) {
    return {
        .name = std::move(name),
        .prefix = "",
        .reference = reference,
        .primaryBinding = primary,
    };
}

[[nodiscard]] CcfMeshMetadata mesh(
    const std::uint32_t meshReference,
    const std::uint32_t materialReference) {
    CcfMeshMetadata result{
        .name = "mesh",
        .prefix = "",
        .reference = meshReference,
        .orientation = {
            CcfVector3{1.0F, 0.0F, 0.0F},
            CcfVector3{0.0F, 1.0F, 0.0F},
            CcfVector3{0.0F, 0.0F, 1.0F},
        },
    };
    result.vertices = {
        CcfMeshVertexMetadata{.position = {0.0F, 0.0F, 0.0F}},
        CcfMeshVertexMetadata{.position = {1.0F, 0.0F, 0.0F}},
        CcfMeshVertexMetadata{.position = {0.0F, 1.0F, 0.0F}},
    };
    result.triangles = {{
        .vertexIndices = {0U, 1U, 2U},
        .materialReference = materialReference,
    }};
    return result;
}

[[nodiscard]] CcfPlacedNodeMetadata placedObject(
    const std::uint32_t roomReference,
    const std::uint32_t meshReference,
    const float x) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "object",
        .prefix = "",
        .currentReference = 500U,
        .roomReference = roomReference,
        .transform = {
            .position = {x, 0.0F, 0.0F},
            .rawScalar = 1.0F,
            .orientation = std::array<CcfVector3, 3>{
                CcfVector3{1.0F, 0.0F, 0.0F},
                CcfVector3{0.0F, 1.0F, 0.0F},
                CcfVector3{0.0F, 0.0F, 1.0F},
            },
        },
        .data = CcfPlacedObjectMetadata{
            .meshReference = meshReference,
        },
    };
}

[[nodiscard]] CcfChunk section(
    const std::uint16_t id,
    const std::initializer_list<std::uint16_t> childIds) {
    CcfChunk result{.id = id};
    for (const auto childId : childIds) {
        result.directChildren.push_back({.id = childId});
    }
    return result;
}

[[nodiscard]] CcfMetadata source(
    const std::string& primaryTexture,
    const std::optional<std::string>& secondaryTexture,
    const std::optional<std::string>& environmentTexture,
    const std::uint32_t roomBase,
    const float x) {
    CcfMetadata ccf;
    ccf.topLevelChunks = {
        section(0x1000U, {0x1100U, 0x1100U}),
        section(0x2000U, {0x2100U}),
        section(0x3000U, {0x3100U}),
        section(0x4000U, {0x4100U}),
    };
    ccf.rooms = {
        room("", roomBase, true),
        room("shared", roomBase + 1U, false),
    };
    ccf.roomSections = {{
        .firstPhysicalRoomIndex = 0U,
        .physicalRoomCount = 2U,
        .firstDirectChildIsRoom = true,
    }};
    ccf.materials = {{
        .name = "material",
        .prefix = "",
        .reference = 50U,
        .primaryTexture = primaryTexture,
        .secondaryTexture = secondaryTexture,
        .environmentTexture = environmentTexture,
        .properties2140 = CcfMaterialProperties2140{
            .firstVector = {x, x + 1.0F, x + 2.0F},
            .secondVector = {x + 3.0F, x + 4.0F, x + 5.0F},
            .scalar = x + 0.5F,
        },
        .properties2150 = CcfMaterialProperties2150{
            .lightingMode = static_cast<std::uint8_t>(roomBase / 100U),
            .gouraudShading = roomBase == 100U,
            .blendMode = roomBase == 100U ? 0U : 3U,
        },
        .flag2151 = roomBase != 100U,
    }};
    ccf.meshes = {mesh(100U, 50U)};
    ccf.placedNodes = {
        placedObject(roomBase + 1U, 100U, x),
    };
    return ccf;
}

[[nodiscard]] CcfMetadata disabledSource() {
    CcfMetadata ccf;
    ccf.topLevelChunks = {
        section(0x1000U, {0x1100U}),
        section(0x3000U, {0x3100U}),
    };
    ccf.rooms = {room("", 300U, true)};
    ccf.roomSections = {{
        .firstPhysicalRoomIndex = 0U,
        .physicalRoomCount = 1U,
        .firstDirectChildIsRoom = true,
    }};
    return ccf;
}

struct Fixture {
    airfix::udsp::Archive archive{
        airfix::udsp::Archive::parse(makeArchive({
            {.path = "Textures",
             .files = {"Other.gti", "Shared.gti"}},
            {.path = "Worlds",
             .files = {"Disabled.ccf", "First.ccf", "Second.ccf"}},
        }))};
    CcfMetadata first{
        source("Shared", "Shared", std::nullopt, 100U, 1.0F)};
    CcfMetadata second{
        source("Other", std::nullopt, "Shared", 200U, 2.0F)};
    CcfMetadata disabled{disabledSource()};
    std::vector<MissionCcfRoomLoadSource> loadSources{
        {.ccf = &first},
        {.ccf = &second},
        {.ccf = &disabled, .placedSceneEnabled = false},
    };
    MissionWorldRoomCatalog catalog{buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = loadSources,
    })};
    std::vector<MissionWorldRoomTextureSource> textureSources{
        {
            .ccf = &first,
            .textureRoot = "Textures",
            .ccfLogicalPath = "Worlds\\First.ccf",
            .ccfArchiveFileIndex =
                lookup("Worlds\\First.ccf").fileIndex,
        },
        {
            .ccf = &second,
            .textureRoot = "Textures",
            .ccfLogicalPath = "Worlds\\Second.ccf",
            .ccfArchiveFileIndex =
                lookup("Worlds\\Second.ccf").fileIndex,
        },
        {
            .ccf = &disabled,
            .textureRoot = std::nullopt,
            .ccfLogicalPath = "Worlds\\Disabled.ccf",
            .ccfArchiveFileIndex =
                lookup("Worlds\\Disabled.ccf").fileIndex,
        },
    };
    std::size_t sharedRoomIndex{findSharedRoom()};

    [[nodiscard]] airfix::udsp::FileLookupResult lookup(
        const std::string_view logicalPath) const {
        const auto result = archive.lookup(logicalPath);
        if (result.status != airfix::udsp::LookupStatus::unique) {
            throw std::runtime_error("fixture archive lookup failed");
        }
        return result;
    }

    [[nodiscard]] std::size_t findSharedRoom() const {
        for (std::size_t index = 1U; index < catalog.rooms.size(); ++index) {
            if (catalog.rooms[index].ccName.name ==
                std::optional<std::string>{"shared"}) {
                return index;
            }
        }
        throw std::runtime_error("shared room missing");
    }

    [[nodiscard]] MissionWorldRoomTextureBindings build(
        const MissionWorldRoomTextureBindingLimits& limits = {}) const {
        return buildMissionWorldRoomTextureBindings(
            catalog,
            loadSources,
            sharedRoomIndex,
            textureSources,
            archive,
            limits);
    }
};

[[nodiscard]] bool hasIssue(
    const MissionWorldRoomTextureBindings& result,
    const MissionWorldRoomTextureBindingIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

void requireFailClosed(
    const MissionWorldRoomTextureBindings& result,
    const std::string& message) {
    require(
        !result.worldRoomIndex.has_value() &&
            result.materialBindingsBySource.empty() &&
            result.imports.empty(),
        message);
}

void testGlobalIdsSourceIsolationAndDeduplication() {
    const Fixture fixture;
    const auto result = fixture.build();
    require(result.complete(), "valid multi-source binding was rejected");
    require(
        result.materialBindingsBySource.size() == 3U &&
            result.materialBindingsBySource[0].size() == 1U &&
            result.materialBindingsBySource[1].size() == 1U &&
            result.materialBindingsBySource[2].empty(),
        "source-local material vectors or disabled source changed");
    const auto shared = fixture.lookup("Textures\\Shared.gti").fileIndex;
    const auto other = fixture.lookup("Textures\\Other.gti").fileIndex;
    require(
        result.imports ==
            std::vector<TextureImportRequest>{
                {TextureAssetId{0U}, shared},
                {TextureAssetId{1U}, other},
            },
        "global IDs are not dense in canonical archive-file first use");

    const auto& first = result.materialBindingsBySource[0][0];
    const auto& second = result.materialBindingsBySource[1][0];
    require(
        first.sourceReference == 50U &&
            second.sourceReference == 50U &&
            first.primary == TextureAssetId{0U} &&
            first.secondary == TextureAssetId{0U} &&
            second.primary == TextureAssetId{1U} &&
            second.environment == TextureAssetId{0U} &&
            first.state ==
                DrawMaterialState{
                    .lightingMode = 1U,
                    .gouraudShading = true,
                    .blendMode = 0U,
                    .flag2151 = false,
                    .scalar2140 = 1.5F,
                    .firstVector2140 = {1.0F, 2.0F, 3.0F},
                    .secondVector2140 = {4.0F, 5.0F, 6.0F},
                } &&
            second.state ==
                DrawMaterialState{
                    .lightingMode = 2U,
                    .gouraudShading = false,
                    .blendMode = 3U,
                    .flag2151 = true,
                    .scalar2140 = 2.5F,
                    .firstVector2140 = {2.0F, 3.0F, 4.0F},
                    .secondVector2140 = {5.0F, 6.0F, 7.0F},
                },
        "ID zero, cross-role dedup, source isolation, or material state changed");
}

void testOutputCrossesAssemblyAndSubmissionBoundaries() {
    const Fixture fixture;
    const auto bindings = fixture.build();
    require(bindings.complete(), "binding fixture failed");
    const std::vector<MissionWorldRoomDrawSource> drawSources{
        {
            .ccf = &fixture.first,
            .materialBindings =
                bindings.materialBindingsBySource[0],
        },
        {
            .ccf = &fixture.second,
            .materialBindings =
                bindings.materialBindingsBySource[1],
        },
        {
            .ccf = &fixture.disabled,
            .materialBindings =
                bindings.materialBindingsBySource[2],
        },
    };
    const auto assembly = buildMissionWorldRoomDrawAssembly(
        fixture.catalog,
        fixture.loadSources,
        fixture.sharedRoomIndex,
        drawSources);
    require(
        assembly.complete() && assembly.model.meshes.size() == 2U &&
            assembly.model.instances.size() == 2U,
        "global bindings did not cross aggregate assembly");
    const auto submission = buildDrawSubmissionPlan(
        assembly.model, bindings.imports.size());
    require(
        submission.plan.has_value() && submission.issues.empty() &&
            submission.plan->commands.size() == 2U &&
            submission.plan->commands[0].materialState ==
                bindings.materialBindingsBySource[0][0].state &&
            submission.plan->commands[1].materialState ==
                bindings.materialBindingsBySource[1][0].state,
        "global bindings or material states did not cross draw submission");
}

void testForgedIdentityCountAndCatalogFailAtomically() {
    const Fixture fixture;
    {
        auto sources = fixture.textureSources;
        sources.pop_back();
        const auto result = buildMissionWorldRoomTextureBindings(
            fixture.catalog,
            fixture.loadSources,
            fixture.sharedRoomIndex,
            sources,
            fixture.archive);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    sourceCountMismatch),
            "forged texture source count was accepted");
        requireFailClosed(result, "source count failure leaked payload");
    }
    {
        auto sources = fixture.textureSources;
        sources[1].ccf = &fixture.first;
        const auto result = buildMissionWorldRoomTextureBindings(
            fixture.catalog,
            fixture.loadSources,
            fixture.sharedRoomIndex,
            sources,
            fixture.archive);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::invalidSource),
            "forged CCF pointer identity was accepted");
        requireFailClosed(result, "CCF pointer failure leaked payload");
    }
    {
        auto sources = fixture.textureSources;
        sources[1].ccfArchiveFileIndex =
            fixture.textureSources[0].ccfArchiveFileIndex;
        const auto result = buildMissionWorldRoomTextureBindings(
            fixture.catalog,
            fixture.loadSources,
            fixture.sharedRoomIndex,
            sources,
            fixture.archive);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    ccfIdentityMismatch),
            "forged archive-entry identity was accepted");
        requireFailClosed(result, "archive identity failure leaked payload");
    }
    {
        auto catalog = fixture.catalog;
        ++catalog.sourceCount;
        const auto result = buildMissionWorldRoomTextureBindings(
            catalog,
            fixture.loadSources,
            fixture.sharedRoomIndex,
            fixture.textureSources,
            fixture.archive);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    drawPlanDependency),
            "forged catalog was accepted");
        requireFailClosed(result, "catalog failure leaked payload");
    }
}

void testLateSourceFailuresAreAtomic() {
    const Fixture fixture;
    {
        auto sources = fixture.textureSources;
        sources[1].ccfLogicalPath = "Worlds\\Missing.ccf";
        const auto result = buildMissionWorldRoomTextureBindings(
            fixture.catalog,
            fixture.loadSources,
            fixture.sharedRoomIndex,
            sources,
            fixture.archive);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::ccfNotFound) &&
                result.issues[0].sourceIndex == 1U,
            "late missing CCF was not typed");
        requireFailClosed(result, "late CCF failure leaked early output");
    }
    {
        auto sources = fixture.textureSources;
        sources[1].textureRoot = "Missing";
        const auto result = buildMissionWorldRoomTextureBindings(
            fixture.catalog,
            fixture.loadSources,
            fixture.sharedRoomIndex,
            sources,
            fixture.archive);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureResolutionDependency) &&
                result.issues[0].sourceIndex == 1U,
            "late texture failure was not typed");
        requireFailClosed(result, "late texture failure leaked early output");
    }
}

void testAmbiguousTextureEntryFailsAtomically() {
    const Fixture fixture;
    const auto ambiguousArchive = airfix::udsp::Archive::parse(makeArchive({
        {.path = "Textures", .files = {"Other.gti", "Shared.gti"}},
        {.path = "Textures", .files = {"Shared.gti"}},
        {.path = "Worlds",
         .files = {"Disabled.ccf", "First.ccf", "Second.ccf"}},
    }));
    auto sources = fixture.textureSources;
    for (auto& source : sources) {
        const auto lookup = ambiguousArchive.lookup(source.ccfLogicalPath);
        require(
            lookup.status == airfix::udsp::LookupStatus::unique,
            "ambiguous-texture fixture broke CCF identity");
        source.ccfArchiveFileIndex = lookup.fileIndex;
    }
    const auto result = buildMissionWorldRoomTextureBindings(
        fixture.catalog,
        fixture.loadSources,
        fixture.sharedRoomIndex,
        sources,
        ambiguousArchive);
    require(
        hasIssue(
            result,
            MissionWorldRoomTextureBindingIssueKind::
                textureResolutionDependency) &&
            result.issues[0].sourceIndex == 0U &&
            result.issues[0].textureResolutionIssue ==
                TextureEntryIssueKind::ambiguous,
        "ambiguous GTI lookup was not retained as an upstream issue");
    requireFailClosed(result, "ambiguous GTI lookup leaked payload");
}

void testPerSourceLimitsFailAtomically() {
    const Fixture fixture;
    MissionWorldRoomTextureBindingLimits exact;
    exact.textureEntriesPerSource.maximumDependencies = 2U;
    exact.bindingPerSource.maximumMaterials = 1U;
    exact.bindingPerSource.maximumTextureEntries = 2U;
    exact.bindingPerSource.maximumImports = 2U;
    require(
        fixture.build(exact).complete(),
        "exact per-source resolver/binding limits were rejected");

    {
        auto limits = exact;
        limits.textureEntriesPerSource.maximumDependencies = 1U;
        const auto result = fixture.build(limits);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureResolutionDependency),
            "one-under per-source resolver limit was accepted");
        requireFailClosed(
            result, "per-source resolver limit leaked payload");
    }
    {
        auto limits = exact;
        limits.bindingPerSource.maximumMaterials = 0U;
        const auto result = fixture.build(limits);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureBindingDependency),
            "one-under per-source material limit was accepted");
        requireFailClosed(
            result, "per-source material limit leaked payload");
    }
    {
        auto limits = exact;
        limits.bindingPerSource.maximumTextureEntries = 1U;
        const auto result = fixture.build(limits);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureBindingDependency),
            "one-under per-source texture-entry limit was accepted");
        requireFailClosed(
            result, "per-source texture-entry limit leaked payload");
    }
    {
        auto limits = exact;
        limits.bindingPerSource.maximumImports = 1U;
        const auto result = fixture.build(limits);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    textureBindingDependency) &&
                result.issues[0].sourceIndex == 1U,
            "late one-under per-source import limit was accepted");
        requireFailClosed(
            result, "per-source import limit leaked early payload");
    }
}

void testAggregateLimitsAcceptExactAndRejectOneUnder() {
    const Fixture fixture;
    MissionWorldRoomTextureBindingLimits exact{
        .maximumSources = 3U,
        .maximumMaterials = 2U,
        .maximumTextureEntries = 4U,
        .maximumImports = 2U,
        .maximumCcfLogicalPathBytes =
            std::string_view{"Worlds\\Disabled.ccf"}.size(),
    };
    require(
        fixture.build(exact).complete(),
        "exact aggregate texture-binding limits were rejected");

    const auto reject = [&](auto configure, const std::string& message) {
        auto limits = exact;
        configure(limits);
        const auto result = fixture.build(limits);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::limitExceeded) ||
                hasIssue(
                    result,
                    MissionWorldRoomTextureBindingIssueKind::
                        invalidCcfLogicalPath),
            message);
        requireFailClosed(result, message + " leaked payload");
    };
    reject(
        [](auto& limits) { limits.maximumSources = 2U; },
        "one-under source limit was accepted");
    {
        auto limits = exact;
        limits.maximumMaterials = 1U;
        const auto result = fixture.build(limits);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    drawPlanDependency) &&
                result.issues[0].drawPlanIssue ==
                    MissionWorldRoomDrawPlanIssueKind::limitExceeded,
            "outer material limit was not enforced by the draw plan");
        requireFailClosed(
            result, "outer draw-plan material limit leaked payload");
    }
    {
        auto limits = exact;
        limits.maximumTextureEntries = 3U;
        const auto result = fixture.build(limits);
        require(
            hasIssue(
                result,
                MissionWorldRoomTextureBindingIssueKind::
                    drawPlanDependency) &&
                result.issues[0].drawPlanIssue ==
                    MissionWorldRoomDrawPlanIssueKind::limitExceeded,
            "outer texture-entry limit was not enforced by the draw plan");
        requireFailClosed(
            result, "outer draw-plan texture limit leaked payload");
    }
    reject(
        [](auto& limits) { limits.maximumImports = 1U; },
        "one-under global import limit was accepted");
    reject(
        [](auto& limits) {
            --limits.maximumCcfLogicalPathBytes;
        },
        "one-under logical-path limit was accepted");
}

} // namespace

int main() {
    try {
        testGlobalIdsSourceIsolationAndDeduplication();
        testOutputCrossesAssemblyAndSubmissionBoundaries();
        testForgedIdentityCountAndCatalogFailAtomically();
        testLateSourceFailuresAreAtomic();
        testAmbiguousTextureEntryFailsAtomically();
        testPerSourceLimitsFailAtomically();
        testAggregateLimitsAcceptExactAndRejectOneUnder();
        std::cout << "Mission world room texture binding tests passed\n";
        return 0;
    }
    catch (const std::exception& exception) {
        std::cerr << "Mission world room texture binding tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
