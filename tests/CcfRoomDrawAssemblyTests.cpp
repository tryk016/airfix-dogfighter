#include "airfix/render/CcfRoomDrawAssembly.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
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
    const std::uint32_t reference,
    const bool primary = false) {
    return {
        .name = "room",
        .reference = reference,
        .primaryBinding = primary,
    };
}

[[nodiscard]] CcfMeshMetadata mesh(
    const std::uint32_t reference,
    const std::uint32_t materialReference) {
    CcfMeshMetadata result{
        .name = "mesh",
        .reference = reference,
        // This prototype transform must not replace a placed object's world.
        .position = {900.0F, 901.0F, 902.0F},
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
    const CcfVector3 position,
    const std::uint32_t parentReference = 0U) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "object",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .parentReference = parentReference,
        .transform = {
            .position = position,
            .rawScalar = 1.0F,
            .orientation = std::array<CcfVector3, 3>{
                CcfVector3{1.0F, 0.0F, 0.0F},
                CcfVector3{0.0F, 1.0F, 0.0F},
                CcfVector3{0.0F, 0.0F, 1.0F},
            },
        },
        .data = CcfPlacedObjectMetadata{.meshReference = meshReference},
    };
}

[[nodiscard]] bool hasIssue(
    const CcfRoomDrawAssembly& result,
    const CcfRoomDrawIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

[[nodiscard]] CcfMetadata sharedMeshScene() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U, true), room(20U)};
    ccf.materials = {{
        .name = "material",
        .reference = 50U,
        .primaryTexture = "primary",
        .secondaryTexture = "secondary",
        .environmentTexture = "environment",
    }};
    ccf.meshes = {mesh(100U, 50U)};
    // Child before its cross-room parent. The authored child world transform
    // must remain unchanged after deferred parent attachment.
    ccf.placedNodes = {
        object(3U, 10U, 100U, {1.0F, 2.0F, 3.0F}, 9U),
        object(4U, 10U, 100U, {4.0F, 5.0F, 6.0F}),
        object(9U, 20U, 100U, {100.0F, 101.0F, 102.0F}),
        object(5U, 999U, 100U, {7.0F, 8.0F, 9.0F}),
    };

    // Deliberately incomplete spatial coverage: draw-all must not use it.
    CcfBspTreeMetadata spatial{
        .kind = CcfBspTreeKind::staticTree,
        .rootNodeIndex = 0U,
    };
    spatial.nodes = {{
        .polygonIndices = {0U},
    }};
    spatial.polygons = {{
        .polygonIndex = 0U,
        .placedObjectReference = 3U,
    }};
    ccf.rooms[0].staticBspTrees.push_back(std::move(spatial));
    return ccf;
}

void testDrawAllUsesPlacedWorldAndSharesMeshes() {
    const auto ccf = sharedMeshScene();
    const std::vector<DrawMaterial> bindings{{
        .sourceReference = 50U,
        .primary = TextureAssetId{1U},
        .secondary = TextureAssetId{2U},
        .environment = TextureAssetId{3U},
    }};
    const auto result = buildFirstRoomDrawAssembly(ccf, bindings);
    require(result.plan.issues.empty() && result.issues.empty(),
        "valid first-room assembly was rejected");
    require(result.plan.placedNodeIndices ==
            std::vector<std::size_t>{0U, 1U, 3U},
        "room selection did not preserve physical order or receiver fallback");
    require(result.model.meshes.size() == 1U &&
            result.model.instances.size() == 3U,
        "shared mesh was duplicated or an instance was lost");
    require(
        result.model.instances[0].sourceNodeReference == 3U &&
            result.model.instances[1].sourceNodeReference == 4U &&
            result.model.instances[2].sourceNodeReference == 5U,
        "instance order does not match physical placed order");
    require(
        result.model.instances[0].modelTranslation ==
            Vec3{1.0F, 2.0F, 3.0F} &&
            result.model.instances[1].modelTranslation ==
                Vec3{4.0F, 5.0F, 6.0F} &&
            result.model.instances[2].modelTranslation ==
                Vec3{7.0F, 8.0F, 9.0F},
        "placed authored-world transforms were recomposed or replaced");
    require(result.model.meshes[0].materials == bindings,
        "primary/secondary/environment texture roles changed");
}

void testFirstUseMeshSlots() {
    auto ccf = sharedMeshScene();
    ccf.materials.push_back({
        .name = "second material",
        .reference = 60U,
    });
    ccf.meshes.push_back(mesh(200U, 60U));
    ccf.placedNodes = {
        object(1U, 10U, 200U, {1.0F, 0.0F, 0.0F}),
        object(2U, 10U, 100U, {2.0F, 0.0F, 0.0F}),
        object(3U, 10U, 200U, {3.0F, 0.0F, 0.0F}),
    };
    const std::vector<DrawMaterial> bindings{
        {.sourceReference = 50U},
        {.sourceReference = 60U},
    };
    const auto result = buildFirstRoomDrawAssembly(ccf, bindings);
    require(result.issues.empty(), "valid two-mesh room was rejected");
    require(result.plan.meshIndices == std::vector<std::size_t>{1U, 0U},
        "mesh plan did not retain first-use order");
    require(
        result.model.instances[0].meshSlot == 0U &&
            result.model.instances[1].meshSlot == 1U &&
            result.model.instances[2].meshSlot == 0U,
        "first-use mesh slots are unstable");
}

void testExplicitRoomAssemblyExcludesReceiverFallback() {
    const auto ccf = sharedMeshScene();
    const std::vector<DrawMaterial> bindings{{
        .sourceReference = 50U,
    }};
    const auto result =
        buildRoomDrawAssembly(ccf, 1U, bindings);
    require(
        result.issues.empty() && result.plan.roomIndex == 1U &&
            result.plan.placedNodeIndices ==
                std::vector<std::size_t>{2U} &&
            result.model.instances.size() == 1U &&
            result.model.instances[0].sourceNodeReference == 9U &&
            result.model.instances[0].modelTranslation ==
                Vec3{100.0F, 101.0F, 102.0F},
        "explicit ordinary-room assembly included another room or fallback");

    const auto outOfRange =
        buildRoomDrawAssembly(ccf, 2U, bindings);
    require(
        hasIssue(outOfRange, CcfRoomDrawIssueKind::planDependency) &&
            outOfRange.plan.issues.size() == 1U &&
            outOfRange.plan.issues[0].kind ==
                CcfRoomDrawPlanIssueKind::roomIndexOutOfRange &&
            outOfRange.model.meshes.empty() &&
            outOfRange.model.instances.empty(),
        "out-of-range room assembly did not fail atomically");
}

void testTypedFailuresAreAtomic() {
    auto ccf = sharedMeshScene();
    auto result = buildFirstRoomDrawAssembly(
        ccf, std::span<const DrawMaterial>{});
    require(
        hasIssue(result, CcfRoomDrawIssueKind::missingMaterialBinding) &&
            result.model.meshes.empty() &&
            result.model.instances.empty(),
        "missing material binding published a partial model");

    const std::vector<DrawMaterial> duplicateBindings{
        {.sourceReference = 50U},
        {.sourceReference = 50U},
    };
    result = buildFirstRoomDrawAssembly(ccf, duplicateBindings);
    require(
        hasIssue(result, CcfRoomDrawIssueKind::invalidMaterialBinding) &&
            result.model.meshes.empty(),
        "duplicate material binding was accepted");

    const std::vector<DrawMaterial> bindings{{.sourceReference = 50U}};
    ccf.placedNodes[0].transform.orientation =
        CcfVector3{1.0F, 0.0F, 0.0F};
    result = buildFirstRoomDrawAssembly(ccf, bindings);
    require(
        hasIssue(
            result,
            CcfRoomDrawIssueKind::unsupportedPlacedOrientation) &&
            result.model.meshes.empty() &&
            result.model.instances.empty(),
        "alternate placed orientation was guessed or partially published");

    ccf = sharedMeshScene();
    ccf.placedNodes[0].transform.orientation =
        std::array<CcfVector3, 3>{
            CcfVector3{1.0F, 0.0F, 0.0F},
            CcfVector3{2.0F, 0.0F, 0.0F},
            CcfVector3{0.0F, 0.0F, 1.0F},
        };
    result = buildFirstRoomDrawAssembly(ccf, bindings);
    require(
        hasIssue(result, CcfRoomDrawIssueKind::invalidTransform) &&
            result.model.meshes.empty(),
        "singular placed transform published a model");

    auto limits = CcfRoomDrawLimits{};
    limits.maximumInstances = 2U;
    result = buildFirstRoomDrawAssembly(sharedMeshScene(), bindings, {}, {}, limits);
    require(
        hasIssue(result, CcfRoomDrawIssueKind::limitExceeded) &&
            result.model.instances.empty(),
        "instance limit left partial output");

    limits = {};
    limits.maximumTotalVertices = 0U;
    result = buildFirstRoomDrawAssembly(sharedMeshScene(), bindings, {}, {}, limits);
    require(
        hasIssue(result, CcfRoomDrawIssueKind::limitExceeded) &&
            result.model.meshes.empty(),
        "aggregate vertex limit left partial output");

    const auto requireLimit = [&bindings](
        auto configure,
        const std::string& message) {
        auto configured = CcfRoomDrawLimits{};
        configure(configured);
        const auto limited = buildFirstRoomDrawAssembly(
            sharedMeshScene(), bindings, {}, {}, configured);
        require(
            hasIssue(limited, CcfRoomDrawIssueKind::limitExceeded) &&
                limited.model.meshes.empty() &&
                limited.model.instances.empty(),
            message);
    };
    requireLimit(
        [](auto& configured) { configured.maximumMeshes = 0U; },
        "unique-mesh limit left partial output");
    requireLimit(
        [](auto& configured) {
            configured.maximumMaterialBindings = 0U;
        },
        "material-binding limit left partial output");
    requireLimit(
        [](auto& configured) {
            configured.maximumTotalIndices = 2U;
        },
        "aggregate index limit left partial output");
    requireLimit(
        [](auto& configured) {
            configured.maximumTotalMaterials = 0U;
        },
        "aggregate material limit left partial output");
    requireLimit(
        [](auto& configured) {
            configured.maximumTotalRanges = 0U;
        },
        "aggregate range limit left partial output");
    requireLimit(
        [](auto& configured) {
            configured.maximumTotalBytes = 0U;
        },
        "aggregate byte limit left partial output");

    const Mat3 singularBasis{{
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    result = buildFirstRoomDrawAssembly(
        sharedMeshScene(),
        bindings,
        BasisTransform{singularBasis, 1.0F});
    require(
        hasIssue(result, CcfRoomDrawIssueKind::geometryFailure) &&
            result.model.meshes.empty(),
        "invalid conversion basis published a model");

    CcfMetadata noRoom;
    result = buildFirstRoomDrawAssembly(noRoom, bindings);
    require(
        hasIssue(result, CcfRoomDrawIssueKind::planDependency) &&
            result.model.meshes.empty(),
        "invalid first-room plan was traversed");
}

} // namespace

int main() {
    try {
        testDrawAllUsesPlacedWorldAndSharesMeshes();
        testFirstUseMeshSlots();
        testExplicitRoomAssemblyExcludesReceiverFallback();
        testTypedFailuresAreAtomic();
        std::cout << "CcfRoomDrawAssembly tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "CcfRoomDrawAssembly tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
