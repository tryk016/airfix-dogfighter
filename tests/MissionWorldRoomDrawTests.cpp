#include "airfix/assets/MissionWorldRoomDrawPlan.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/MissionWorldRoomDrawAssembly.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace airfix::assets;
using namespace airfix::render;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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
    const std::uint32_t reference,
    const std::uint32_t materialReference) {
    CcfMeshMetadata result{
        .name = "mesh",
        .prefix = "",
        .reference = reference,
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

[[nodiscard]] CcfPlacedNodeMetadata object(
    const std::uint32_t currentReference,
    const std::uint32_t roomReference,
    const std::uint32_t meshReference,
    const float x) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "object",
        .prefix = "",
        .currentReference = currentReference,
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

[[nodiscard]] CcfMetadata repeatedSectionSource() {
    CcfMetadata ccf;
    ccf.topLevelChunks = {
        section(0x1000U, {0x1100U, 0x1100U}),
        section(0x1000U, {0x1100U, 0x1100U}),
        section(0x2000U, {0x2100U}),
        section(0x3000U, {0x3100U}),
        section(
            0x4000U,
            {0x4100U, 0x4100U, 0x4100U,
             0x4100U, 0x4100U, 0x4100U}),
    };
    ccf.topLevelChunks[0].offset = 100U;
    ccf.topLevelChunks[0].directChildren[0].offset = 110U;
    ccf.topLevelChunks[0].directChildren[1].offset = 120U;
    ccf.topLevelChunks[1].offset = 200U;
    ccf.topLevelChunks[1].directChildren[0].offset = 210U;
    ccf.topLevelChunks[1].directChildren[1].offset = 220U;
    ccf.rooms = {
        room("", 10U, true),
        room("shared", 20U, false),
        room("", 30U, true),
        room("SHARED", 40U, false),
    };
    ccf.rooms[0].offset = 110U;
    ccf.rooms[1].offset = 120U;
    ccf.rooms[2].offset = 210U;
    ccf.rooms[3].offset = 220U;
    ccf.roomSections = {
        {
            .firstPhysicalRoomIndex = 0U,
            .physicalRoomCount = 2U,
            .firstDirectChildIsRoom = true,
            .offset = 100U,
        },
        {
            .firstPhysicalRoomIndex = 2U,
            .physicalRoomCount = 2U,
            .firstDirectChildIsRoom = true,
            .offset = 200U,
        },
    };
    ccf.materials = {{
        .name = "material",
        .prefix = "",
        .reference = 50U,
        .primaryTexture = "shared-texture",
    }};
    ccf.meshes = {mesh(100U, 50U)};
    ccf.placedNodes = {
        object(1000U, 40U, 100U, 0.0F),
        object(1001U, 30U, 100U, 1.0F),
        // Earlier wrappers for the same runtime rooms are no longer in the
        // current CcLoadedScene reference map and therefore fall back to root.
        object(1002U, 20U, 100U, 2.0F),
        object(1003U, 10U, 100U, 3.0F),
        object(1004U, 999U, 100U, 4.0F),
        object(1005U, 40U, 100U, 5.0F),
    };
    return ccf;
}

[[nodiscard]] CcfMetadata secondSource() {
    CcfMetadata ccf;
    ccf.topLevelChunks = {
        section(0x1000U, {0x1100U, 0x1100U}),
        section(0x2000U, {0x2100U}),
        section(0x3000U, {0x3100U}),
        section(0x4000U, {0x4100U, 0x4100U}),
    };
    ccf.rooms = {
        room("", 110U, true),
        room("shared", 120U, false),
    };
    ccf.roomSections = {{
        .firstPhysicalRoomIndex = 0U,
        .physicalRoomCount = 2U,
        .firstDirectChildIsRoom = true,
    }};
    // References deliberately collide with source zero. They are source-local.
    ccf.materials = {{
        .name = "material",
        .prefix = "",
        .reference = 50U,
        .primaryTexture = "shared-texture",
    }};
    ccf.meshes = {mesh(100U, 50U)};
    ccf.placedNodes = {
        object(1000U, 120U, 100U, 10.0F),
        object(1001U, 999U, 100U, 11.0F),
    };
    return ccf;
}

[[nodiscard]] CcfMetadata placedDisabledSource() {
    CcfMetadata ccf;
    ccf.topLevelChunks = {
        section(0x1000U, {0x1100U}),
        // Deliberately noncanonical placed/resource order is ignored because
        // flag 0x2000 suppresses the complete placed scene.
        section(0x4000U, {}),
        section(0x3000U, {0x3100U}),
    };
    ccf.rooms = {room("", 210U, true)};
    ccf.roomSections = {{
        .firstPhysicalRoomIndex = 0U,
        .physicalRoomCount = 1U,
        .firstDirectChildIsRoom = true,
    }};
    ccf.materials = {{
        .name = "material",
        .reference = 50U,
    }};
    ccf.meshes = {mesh(100U, 50U)};
    ccf.placedNodes = {
        object(2000U, 210U, 100U, 20.0F),
    };
    return ccf;
}

struct Fixture {
    CcfMetadata first{repeatedSectionSource()};
    CcfMetadata second{secondSource()};
    CcfMetadata disabled{placedDisabledSource()};
    std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &first},
        {.ccf = &second},
        {.ccf = &disabled, .placedSceneEnabled = false},
    };
    MissionWorldRoomCatalog catalog{
        buildMissionWorldRoomCatalog({
            .initialRootName = {},
            .sources = sources,
        })};
    std::size_t sharedRoomIndex{findSharedRoom()};

    [[nodiscard]] std::size_t findSharedRoom() const {
        for (std::size_t index = 1U;
             index < catalog.rooms.size();
             ++index) {
            if (catalog.rooms[index].ccName.name ==
                std::optional<std::string>{"shared"}) {
                return index;
            }
        }
        throw std::runtime_error("shared runtime room missing");
    }
};

template <typename IssueKind, typename Result>
[[nodiscard]] bool hasIssue(
    const Result& result,
    const IssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) {
            return issue.kind == kind;
        });
}

void requirePlanFailClosed(
    const MissionWorldRoomDrawPlan& plan,
    const std::string& message) {
    require(
        !plan.worldRoomIndex.has_value() &&
            plan.meshes.empty() &&
            plan.placedNodes.empty() &&
            plan.materials.empty() &&
            plan.textures.empty(),
        message);
}

void requireAssemblyFailClosed(
    const MissionWorldRoomDrawAssembly& assembly,
    const std::string& message) {
    require(
        !assembly.worldRoomIndex.has_value() &&
            assembly.model.meshes.empty() &&
            assembly.model.instances.empty() &&
            assembly.meshProvenance.empty() &&
            assembly.instanceProvenance.empty(),
        message);
}

void testSourceAwarePlanOrderFlagsAndFallback() {
    const Fixture fixture;
    require(
        fixture.catalog.complete() &&
            fixture.catalog.rooms.size() == 2U,
        "mission room catalog fixture is incomplete");

    const auto shared = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        fixture.sources,
        fixture.sharedRoomIndex);
    require(shared.complete(), "shared room plan was rejected");
    require(
        shared.placedNodes ==
            std::vector<MissionWorldRoomPlacedPlanEntry>{
                {0U, 0U, 0U, 1U, 3U},
                {0U, 5U, 0U, 1U, 3U},
                {1U, 0U, 1U, 2U, 1U},
            },
        "multi-source shared-room order or transient reference map differs");
    require(
        shared.meshes ==
            std::vector<MissionWorldRoomMeshPlanEntry>{
                {0U, 0U},
                {1U, 0U},
            },
        "source-local mesh identities collided");
    require(
        shared.materials ==
            std::vector<MissionWorldRoomMaterialPlanEntry>{
                {0U, 0U},
                {1U, 0U},
            } &&
            shared.textures.size() == 2U &&
            shared.textures[0].sourceIndex == 0U &&
            shared.textures[1].sourceIndex == 1U,
        "source-aware material or texture order changed");

    const auto root = resolveMissionWorldRoomDrawPlan(
        fixture.catalog, fixture.sources, 0U);
    require(root.complete(), "root plan was rejected");
    require(
        root.placedNodes.size() == 5U &&
            root.placedNodes[0].sourceIndex == 0U &&
            root.placedNodes[0].placedNodeIndex == 1U &&
            root.placedNodes[0].contributorIndex == 1U &&
            root.placedNodes[0].physicalRoomIndex == 2U &&
            root.placedNodes[1].placedNodeIndex == 2U &&
            !root.placedNodes[1].contributorIndex.has_value() &&
            !root.placedNodes[1].physicalRoomIndex.has_value() &&
            root.placedNodes[2].placedNodeIndex == 3U &&
            root.placedNodes[3].placedNodeIndex == 4U &&
            root.placedNodes[4].sourceIndex == 1U &&
            root.placedNodes[4].placedNodeIndex == 1U,
        "root fallback was duplicated, lost, or grouped by contributor");
    require(
        std::ranges::none_of(
            root.placedNodes,
            [](const auto& entry) {
                return entry.sourceIndex == 2U;
            }),
        "flag 0x2000 source published placed geometry");
}

void testPlanLimitsAndForgedInputsFailClosed() {
    const Fixture fixture;
    const auto exact = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        fixture.sources,
        fixture.sharedRoomIndex,
        MissionWorldRoomDrawPlanLimits{
            .maximumSources = 3U,
            .maximumRuntimeRooms = 2U,
            .maximumContributors = 7U,
            .maximumRoomSections = 4U,
            .maximumTopLevelSections = 12U,
            .maximumPhysicalRooms = 7U,
            .maximumScannedMeshes = 2U,
            .maximumScannedPlacedNodes = 8U,
            .maximumScannedMaterials = 2U,
            .maximumInstances = 3U,
            .maximumUniqueMeshes = 2U,
            .maximumMaterialReferences = 2U,
            .maximumTextureEdges = 2U,
            .maximumRetainedSourceTextBytes = 28U,
        });
    require(exact.complete(), "exact mission plan limits were rejected");

    const auto requirePlanLimit = [&](auto configure) {
        auto oneUnder = MissionWorldRoomDrawPlanLimits{};
        configure(oneUnder);
        const auto rejected = resolveMissionWorldRoomDrawPlan(
            fixture.catalog,
            fixture.sources,
            fixture.sharedRoomIndex,
            oneUnder);
        require(
            hasIssue(
                rejected,
                MissionWorldRoomDrawPlanIssueKind::limitExceeded),
            "one-under mission plan aggregate limit was ignored");
        requirePlanFailClosed(
            rejected,
            "one-under mission plan limit leaked output");
    };
    requirePlanLimit([](auto& value) {
        value.maximumSources = 2U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumRuntimeRooms = 1U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumContributors = 6U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumRoomSections = 3U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumTopLevelSections = 11U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumPhysicalRooms = 6U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumScannedMeshes = 1U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumScannedPlacedNodes = 7U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumScannedMaterials = 1U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumInstances = 2U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumUniqueMeshes = 1U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumMaterialReferences = 1U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumTextureEdges = 1U;
    });
    requirePlanLimit([](auto& value) {
        value.maximumRetainedSourceTextBytes = 27U;
    });

    auto limits = MissionWorldRoomDrawPlanLimits{};
    limits.maximumInstances = 2U;
    const auto limited = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        fixture.sources,
        fixture.sharedRoomIndex,
        limits);
    require(
        hasIssue(
            limited,
            MissionWorldRoomDrawPlanIssueKind::limitExceeded),
        "aggregate instance limit was ignored");
    requirePlanFailClosed(limited, "limited plan leaked partial output");

    limits = {};
    limits.maximumPhysicalRooms = 6U;
    const auto roomLimited = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        fixture.sources,
        fixture.sharedRoomIndex,
        limits);
    require(
        hasIssue(
            roomLimited,
            MissionWorldRoomDrawPlanIssueKind::limitExceeded),
        "aggregate physical-room limit was ignored");
    requirePlanFailClosed(
        roomLimited, "physical-room limit leaked a plan");

    auto scannedRoomSources = fixture.sources;
    scannedRoomSources[0].roomSectionEnabled = false;
    const auto scannedRoomCatalog = buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = scannedRoomSources,
    });
    limits = {};
    limits.maximumPhysicalRooms = 6U;
    const auto scannedRoomLimited = resolveMissionWorldRoomDrawPlan(
        scannedRoomCatalog, scannedRoomSources, 0U, limits);
    require(
        hasIssue(
            scannedRoomLimited,
            MissionWorldRoomDrawPlanIssueKind::limitExceeded),
        "0x20 source escaped the aggregate physical-room limit");
    requirePlanFailClosed(
        scannedRoomLimited,
        "0x20 physical-room limit leaked a plan");

    auto unsupportedSources = fixture.sources;
    unsupportedSources[0].roomSectionEnabled = false;
    const auto rootOnlyCatalog = buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = unsupportedSources,
    });
    const auto roomsDisabled = resolveMissionWorldRoomDrawPlan(
        rootOnlyCatalog, unsupportedSources, 0U);
    require(
        roomsDisabled.complete() &&
            std::ranges::count_if(
                roomsDisabled.placedNodes,
                [](const auto& entry) {
                    return entry.sourceIndex == 0U;
                }) == 6,
        "flag 0x20 did not route placed objects once to root");

    auto forgedCatalog = fixture.catalog;
    forgedCatalog.rooms[0].contributors.push_back(
        forgedCatalog.rooms[1].contributors.front());
    const auto forged = resolveMissionWorldRoomDrawPlan(
        forgedCatalog, fixture.sources, 0U);
    require(
        hasIssue(
            forged,
            MissionWorldRoomDrawPlanIssueKind::catalogIncomplete),
        "duplicate forged contributor was accepted");
    requirePlanFailClosed(forged, "forged catalog leaked a plan");

    auto swappedCatalog = fixture.catalog;
    std::swap(
        swappedCatalog.rooms[0].contributors.front(),
        swappedCatalog.rooms[1].contributors.front());
    const auto swapped = resolveMissionWorldRoomDrawPlan(
        swappedCatalog, fixture.sources, 0U);
    require(
        hasIssue(
            swapped,
            MissionWorldRoomDrawPlanIssueKind::catalogIncomplete),
        "forged primary/ordinary mapping was accepted");

    auto reorderedFirst = fixture.first;
    std::swap(
        reorderedFirst.topLevelChunks[0],
        reorderedFirst.topLevelChunks[4]);
    auto reorderedSources = fixture.sources;
    reorderedSources[0].ccf = &reorderedFirst;
    const auto reordered = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        reorderedSources,
        fixture.sharedRoomIndex);
    require(
        hasIssue(
            reordered,
            MissionWorldRoomDrawPlanIssueKind::
                invalidTopLevelOrder),
        "time-dependent top-level order was accepted");

    auto materialAfterMesh = fixture.first;
    std::swap(
        materialAfterMesh.topLevelChunks[2],
        materialAfterMesh.topLevelChunks[3]);
    auto materialAfterMeshSources = fixture.sources;
    materialAfterMeshSources[0].ccf = &materialAfterMesh;
    const auto lateMaterial = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        materialAfterMeshSources,
        fixture.sharedRoomIndex);
    require(
        hasIssue(
            lateMaterial,
            MissionWorldRoomDrawPlanIssueKind::
                invalidTopLevelOrder),
        "material section after mesh section was accepted");
    requirePlanFailClosed(
        lateMaterial,
        "late-material source leaked a plan");

    auto forgedRoomSections = fixture.first;
    forgedRoomSections.roomSections[0].physicalRoomCount = 1U;
    auto forgedRoomSectionSources = fixture.sources;
    forgedRoomSectionSources[0].ccf = &forgedRoomSections;
    const auto forgedSection = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        forgedRoomSectionSources,
        fixture.sharedRoomIndex);
    require(
        hasIssue(
            forgedSection,
            MissionWorldRoomDrawPlanIssueKind::
                invalidRoomSectionLayout) ||
            hasIssue(
                forgedSection,
                MissionWorldRoomDrawPlanIssueKind::
                    invalidTopLevelOrder),
        "forged room-section metadata was accepted");
    requirePlanFailClosed(
        forgedSection,
        "forged room-section metadata leaked a plan");

    auto swappedRoomSections = fixture.first;
    std::swap(
        swappedRoomSections.topLevelChunks[0],
        swappedRoomSections.topLevelChunks[1]);
    auto swappedRoomSectionSources = fixture.sources;
    swappedRoomSectionSources[0].ccf = &swappedRoomSections;
    const auto swappedSections = resolveMissionWorldRoomDrawPlan(
        fixture.catalog,
        swappedRoomSectionSources,
        fixture.sharedRoomIndex);
    require(
        hasIssue(
            swappedSections,
            MissionWorldRoomDrawPlanIssueKind::
                invalidTopLevelOrder),
        "swapped physical room sections were accepted");
    requirePlanFailClosed(
        swappedSections,
        "swapped room sections leaked a plan");
}

void testRepeatedCcfPointerRetainsLoadIdentity() {
    auto ccf = secondSource();
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
        {.ccf = &ccf},
    };
    const auto catalog = buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = sources,
    });
    require(
        catalog.complete() && catalog.rooms.size() == 2U,
        "repeated-pointer catalog was rejected");
    const auto plan = resolveMissionWorldRoomDrawPlan(
        catalog, sources, 1U);
    require(
        plan.complete() &&
            plan.meshes ==
                std::vector<MissionWorldRoomMeshPlanEntry>{
                    {0U, 0U},
                    {1U, 0U},
                } &&
            plan.placedNodes.size() == 2U &&
            plan.placedNodes[0].sourceIndex == 0U &&
            plan.placedNodes[1].sourceIndex == 1U,
        "two loads of one CCF pointer lost source identity");
}

[[nodiscard]] std::vector<MissionWorldRoomDrawSource>
drawSources(
    const Fixture& fixture,
    const std::vector<DrawMaterial>& firstBindings,
    const std::vector<DrawMaterial>& secondBindings,
    const std::vector<DrawMaterial>& disabledBindings = {}) {
    return {
        {.ccf = &fixture.first, .materialBindings = firstBindings},
        {.ccf = &fixture.second, .materialBindings = secondBindings},
        {.ccf = &fixture.disabled, .materialBindings = disabledBindings},
    };
}

void testAggregateAssemblyAndSubmission() {
    const Fixture fixture;
    const std::vector<DrawMaterial> firstBindings{{
        .sourceReference = 50U,
        .primary = TextureAssetId{0U},
    }};
    const std::vector<DrawMaterial> secondBindings{{
        .sourceReference = 50U,
        .primary = TextureAssetId{1U},
    }};
    const auto sources = drawSources(
        fixture, firstBindings, secondBindings);
    const auto assembly =
        buildMissionWorldRoomDrawAssembly(
            fixture.catalog,
            fixture.sources,
            fixture.sharedRoomIndex,
            sources);
    require(assembly.complete(), "aggregate assembly was rejected");
    require(
        assembly.model.meshes.size() == 2U &&
            assembly.model.instances.size() == 3U &&
            assembly.meshProvenance ==
                std::vector<MissionWorldRoomMeshProvenance>{
                    {0U, 0U},
                    {1U, 0U},
                } &&
            assembly.instanceProvenance.size() == 3U,
        "aggregate model or parallel provenance mismatch");
    require(
        assembly.model.instances[0].meshSlot == 0U &&
            assembly.model.instances[1].meshSlot == 0U &&
            assembly.model.instances[2].meshSlot == 1U &&
            assembly.model.instances[0].modelTranslation.x == 0.0F &&
            assembly.model.instances[1].modelTranslation.x == 5.0F &&
            assembly.model.instances[2].modelTranslation.x == 10.0F,
        "instance order, source-local mesh reuse, or transform changed");
    require(
        assembly.model.meshes[0].materials[0].primary ==
                std::optional<TextureAssetId>{TextureAssetId{0U}} &&
            assembly.model.meshes[1].materials[0].primary ==
                std::optional<TextureAssetId>{TextureAssetId{1U}},
        "source-local material references collided");

    const auto submission =
        buildDrawSubmissionPlan(assembly.model, 2U);
    require(
        submission.plan.has_value() &&
            submission.issues.empty() &&
            submission.plan->meshUploads.size() == 2U &&
            submission.plan->commands.size() == 3U,
        "aggregate model did not cross the existing submission boundary");
}

void testAssemblyLimitsAndLateFailureAreAtomic() {
    const Fixture fixture;
    const std::vector<DrawMaterial> firstBindings{{
        .sourceReference = 50U,
    }};
    const std::vector<DrawMaterial> secondBindings{{
        .sourceReference = 50U,
    }};
    const auto sources = drawSources(
        fixture, firstBindings, secondBindings);

    constexpr std::size_t meshCount = 2U;
    constexpr std::size_t instanceCount = 3U;
    constexpr std::size_t verticesPerMesh = 3U;
    constexpr std::size_t indicesPerMesh = 3U;
    constexpr std::size_t materialsPerMesh = 1U;
    constexpr std::size_t rangesPerMesh = 1U;
    const std::size_t exactBytes =
        meshCount *
            (sizeof(DrawMeshPayload) +
             sizeof(MissionWorldRoomMeshProvenance) +
             verticesPerMesh * sizeof(DrawVertex) +
             indicesPerMesh * sizeof(std::uint32_t) +
             materialsPerMesh * sizeof(DrawMaterial) +
             rangesPerMesh * sizeof(DrawRange)) +
        instanceCount *
            (sizeof(DrawMeshInstance) +
             sizeof(MissionWorldRoomInstanceProvenance));
    MissionWorldRoomDrawLimits exact{
        .maximumSources = 3U,
        .maximumMeshes = meshCount,
        .maximumInstances = instanceCount,
        .maximumMaterialBindings = 2U,
        .maximumTotalVertices = meshCount * verticesPerMesh,
        .maximumTotalIndices = meshCount * indicesPerMesh,
        .maximumTotalMaterials =
            meshCount * materialsPerMesh,
        .maximumTotalRanges = meshCount * rangesPerMesh,
        .maximumTotalBytes = exactBytes,
    };
    require(
        buildMissionWorldRoomDrawAssembly(
            fixture.catalog,
            fixture.sources,
            fixture.sharedRoomIndex,
            sources,
            {},
            UvPolicy::preserveRaw,
            exact)
            .complete(),
        "exact aggregate assembly limits were rejected");

    const auto requireAssemblyLimit = [&](auto configure) {
        auto oneUnder = MissionWorldRoomDrawLimits{};
        configure(oneUnder);
        const auto rejected =
            buildMissionWorldRoomDrawAssembly(
                fixture.catalog,
                fixture.sources,
                fixture.sharedRoomIndex,
                sources,
                {},
                UvPolicy::preserveRaw,
                oneUnder);
        require(
            hasIssue(
                rejected,
                MissionWorldRoomDrawIssueKind::limitExceeded) ||
                hasIssue(
                    rejected,
                    MissionWorldRoomDrawIssueKind::planDependency),
            "one-under assembly aggregate limit was ignored");
        requireAssemblyFailClosed(
            rejected,
            "one-under assembly limit leaked output");
    };
    requireAssemblyLimit([](auto& value) {
        value.maximumSources = 2U;
    });
    requireAssemblyLimit([](auto& value) {
        value.maximumMeshes = 1U;
    });
    requireAssemblyLimit([](auto& value) {
        value.maximumInstances = 2U;
    });
    requireAssemblyLimit([](auto& value) {
        value.maximumMaterialBindings = 1U;
    });
    requireAssemblyLimit([](auto& value) {
        value.maximumTotalVertices = 5U;
    });
    requireAssemblyLimit([](auto& value) {
        value.maximumTotalIndices = 5U;
    });
    requireAssemblyLimit([](auto& value) {
        value.maximumTotalMaterials = 1U;
    });
    requireAssemblyLimit([](auto& value) {
        value.maximumTotalRanges = 1U;
    });
    requireAssemblyLimit([](auto& value) {
        value.plan.maximumPhysicalRooms = 6U;
    });

    --exact.maximumTotalBytes;
    const auto byteLimited =
        buildMissionWorldRoomDrawAssembly(
            fixture.catalog,
            fixture.sources,
            fixture.sharedRoomIndex,
            sources,
            {},
            UvPolicy::preserveRaw,
            exact);
    require(
        hasIssue(
            byteLimited,
            MissionWorldRoomDrawIssueKind::limitExceeded),
        "one-under aggregate byte limit was ignored");
    requireAssemblyFailClosed(
        byteLimited, "byte-limited assembly leaked output");

    const auto missingLater = drawSources(
        fixture, firstBindings, {});
    const auto failed =
        buildMissionWorldRoomDrawAssembly(
            fixture.catalog,
            fixture.sources,
            fixture.sharedRoomIndex,
            missingLater);
    require(
        hasIssue(
            failed,
            MissionWorldRoomDrawIssueKind::
                missingMaterialBinding) &&
            failed.issues.front().sourceIndex == 1U,
        "later-source material failure lost provenance");
    requireAssemblyFailClosed(
        failed, "later-source failure published a partial model");

    auto staleSources = sources;
    staleSources[0].ccf = &fixture.second;
    const auto forged = buildMissionWorldRoomDrawAssembly(
        fixture.catalog,
        fixture.sources,
        fixture.sharedRoomIndex,
        staleSources);
    require(
        hasIssue(
            forged,
            MissionWorldRoomDrawIssueKind::invalidSource),
        "stale source identity was accepted");
    requireAssemblyFailClosed(
        forged, "stale source identity published a model");
}

} // namespace

int main() {
    try {
        testSourceAwarePlanOrderFlagsAndFallback();
        testPlanLimitsAndForgedInputsFailClosed();
        testRepeatedCcfPointerRetainsLoadIdentity();
        testAggregateAssemblyAndSubmission();
        testAssemblyLimitsAndLateFailureAreAtomic();
        std::cout << "Mission world room draw tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Mission world room draw tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
