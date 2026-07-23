#include "airfix/assets/CcfRoomDrawPlan.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using airfix::assets::CcfMetadata;
using airfix::assets::CcfPlacedNodeKind;
using airfix::assets::CcfPlacedNodeMetadata;
using airfix::assets::CcfRoomDrawPlan;
using airfix::assets::CcfRoomDrawPlanIssueKind;
using airfix::assets::CcfRoomDrawPlanLimits;
using airfix::assets::TextureDependencyRole;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] airfix::assets::CcfRoomMetadata room(
    const std::uint32_t reference,
    const bool primaryBinding) {
    return {
        .name = "room",
        .prefix = "",
        .reference = reference,
        .primaryBinding = primaryBinding,
    };
}

[[nodiscard]] airfix::assets::CcfMaterialMetadata material(
    const std::uint32_t reference,
    const std::optional<std::string>& primary = std::nullopt,
    const std::optional<std::string>& secondary = std::nullopt,
    const std::optional<std::string>& environment = std::nullopt) {
    return {
        .name = "material",
        .prefix = "",
        .reference = reference,
        .primaryTexture = primary,
        .secondaryTexture = secondary,
        .environmentTexture = environment,
    };
}

[[nodiscard]] airfix::assets::CcfMeshMetadata mesh(
    const std::uint32_t reference,
    const std::vector<std::uint32_t>& materialReferences) {
    airfix::assets::CcfMeshMetadata result{
        .name = "mesh",
        .prefix = "",
        .reference = reference,
    };
    for (const auto materialReference : materialReferences) {
        result.triangles.push_back({
            .materialReference = materialReference,
        });
    }
    return result;
}

[[nodiscard]] CcfPlacedNodeMetadata object(
    const std::uint32_t currentReference,
    const std::uint32_t roomReference,
    const std::uint32_t meshReference,
    const std::uint32_t parentReference = 0U) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "object",
        .prefix = "",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .parentReference = parentReference,
        .data = airfix::assets::CcfPlacedObjectMetadata{
            .meshReference = meshReference,
        },
    };
}

[[nodiscard]] CcfPlacedNodeMetadata nullNode(
    const std::uint32_t currentReference,
    const std::uint32_t roomReference) {
    return {
        .kind = CcfPlacedNodeKind::nullNode,
        .name = "null",
        .prefix = "",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .data = airfix::assets::CcfPlacedNullMetadata{},
    };
}

[[nodiscard]] CcfPlacedNodeMetadata light(
    const std::uint32_t currentReference,
    const std::uint32_t roomReference) {
    return {
        .kind = CcfPlacedNodeKind::light,
        .name = "light",
        .prefix = "",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .data = airfix::assets::CcfPlacedLightMetadata{},
    };
}

[[nodiscard]] bool hasIssue(
    const CcfRoomDrawPlan& plan,
    const CcfRoomDrawPlanIssueKind kind,
    const std::optional<std::uint32_t> reference = std::nullopt) {
    return std::ranges::any_of(
        plan.issues,
        [kind, reference](const auto& issue) {
            return issue.kind == kind &&
                (!reference.has_value() || issue.reference == reference);
        });
}

void requireFailClosed(
    const CcfRoomDrawPlan& plan,
    const std::string& message) {
    require(
        !plan.roomIndex.has_value() &&
            plan.placedNodeIndices.empty() &&
            plan.meshIndices.empty() &&
            plan.materialIndices.empty() &&
            plan.textures.empty(),
        message);
}

[[nodiscard]] CcfMetadata makeOrderedCcf() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U, true), room(20U, false)};
    ccf.meshes = {
        mesh(100U, {7U, 8U}),
        mesh(200U, {8U, 9U}),
        mesh(300U, {10U}),
    };
    ccf.materials = {
        material(8U, "m8-primary"),
        material(7U, "m7-primary", "m7-secondary", "m7-environment"),
        material(9U, std::nullopt, "m9-secondary"),
        material(10U, "other-room"),
    };
    ccf.placedNodes = {
        object(1U, 10U, 200U),
        nullNode(2U, 10U),
        object(3U, 20U, 300U),
        object(4U, 10U, 100U),
        light(5U, 10U),
        object(6U, 10U, 200U),
    };
    return ccf;
}

void testPhysicalOrderSharedMeshesAndDependencies() {
    const auto plan =
        airfix::assets::resolveFirstRoomDrawPlan(makeOrderedCcf());
    require(plan.issues.empty() && plan.roomIndex == 0U,
        "valid first room draw plan was rejected");
    require(plan.placedNodeIndices ==
            std::vector<std::size_t>{0U, 3U, 5U},
        "instance order did not retain physical placed order");
    require(plan.meshIndices == std::vector<std::size_t>{1U, 0U},
        "mesh slots were not stably deduplicated at first use");
    require(plan.materialIndices ==
            std::vector<std::size_t>{0U, 2U, 1U},
        "materials did not retain mesh/triangle first-use order");
    require(plan.textures.size() == 5U,
        "texture edge count mismatch");
    require(
        plan.textures[0].role == TextureDependencyRole::primary &&
            plan.textures[0].materialReference == 8U &&
            plan.textures[0].sourceText == "m8-primary" &&
            plan.textures[1].role == TextureDependencyRole::secondary &&
            plan.textures[1].materialReference == 9U &&
            plan.textures[2].role == TextureDependencyRole::primary &&
            plan.textures[2].materialReference == 7U &&
            plan.textures[3].role == TextureDependencyRole::secondary &&
            plan.textures[4].role == TextureDependencyRole::environment,
        "texture roles or stable material ordering mismatch");
}

void testFallbackAndBspAreIndependent() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U, true)};
    ccf.meshes = {mesh(100U, {7U})};
    ccf.materials = {material(7U)};
    ccf.placedNodes = {
        object(1U, 999U, 100U),
        object(2U, 10U, 100U),
    };

    airfix::assets::CcfBspTreeMetadata partial;
    partial.kind = airfix::assets::CcfBspTreeKind::staticTree;
    partial.rootNodeIndex = 99U;
    partial.nodes.push_back({
        .childAPresenceRaw = 1U,
        .childAIndex = 500U,
        .polygonIndices = {900U},
    });
    ccf.rooms[0].staticBspTrees.push_back(std::move(partial));
    ccf.rooms[0].portalBspTrees.push_back({
        .kind = airfix::assets::CcfBspTreeKind::staticTree,
    });

    const auto plan = airfix::assets::resolveFirstRoomDrawPlan(ccf);
    require(plan.issues.empty() &&
            plan.placedNodeIndices ==
                std::vector<std::size_t>{0U, 1U},
        "external receiver fallback or irrelevant partial BSP was rejected");
}

void testMaterialFailuresAreAtomic() {
    auto missing = makeOrderedCcf();
    missing.materials.erase(missing.materials.begin() + 2);
    const auto missingPlan =
        airfix::assets::resolveFirstRoomDrawPlan(missing);
    require(
        hasIssue(
            missingPlan,
            CcfRoomDrawPlanIssueKind::materialNotFound,
            9U),
        "missing material was not reported");
    requireFailClosed(
        missingPlan,
        "missing material left a partial plan");

    auto ambiguous = makeOrderedCcf();
    ambiguous.materials.push_back(material(7U));
    const auto ambiguousPlan =
        airfix::assets::resolveFirstRoomDrawPlan(ambiguous);
    require(
        hasIssue(
            ambiguousPlan,
            CcfRoomDrawPlanIssueKind::materialAmbiguous,
            7U),
        "ambiguous material was not reported");
    requireFailClosed(
        ambiguousPlan,
        "ambiguous material left a partial plan");
}

void testPlacedDependencyIsAtomic() {
    auto ccf = makeOrderedCcf();
    ccf.placedNodes[0].parentReference = 777U;
    const auto plan = airfix::assets::resolveFirstRoomDrawPlan(ccf);
    require(
        hasIssue(
            plan,
            CcfRoomDrawPlanIssueKind::placedSceneDependency),
        "invalid placed graph did not surface as a dependency failure");
    requireFailClosed(
        plan,
        "placed graph failure left a partial plan");

    ccf = makeOrderedCcf();
    ccf.placedNodes[0].data =
        airfix::assets::CcfPlacedNullMetadata{};
    const auto invalidNodePlan =
        airfix::assets::resolveFirstRoomDrawPlan(ccf);
    require(
        hasIssue(
            invalidNodePlan,
            CcfRoomDrawPlanIssueKind::placedSceneDependency),
        "invalid placed-node variant was not rejected");
    requireFailClosed(
        invalidNodePlan,
        "invalid placed node left a partial plan");
}

template <typename Configure>
void requireLimitFailure(
    const CcfMetadata& ccf,
    Configure configure,
    const std::string& message) {
    auto limits = CcfRoomDrawPlanLimits{};
    configure(limits);
    const auto plan =
        airfix::assets::resolveFirstRoomDrawPlan(ccf, limits);
    require(
        hasIssue(plan, CcfRoomDrawPlanIssueKind::limitExceeded),
        message + " was not reported");
    requireFailClosed(plan, message + " left a partial plan");
}

void testLimits() {
    const auto ccf = makeOrderedCcf();
    requireLimitFailure(
        ccf,
        [](auto& limits) { limits.maximumPlacedNodes = 5U; },
        "placed-node limit");
    requireLimitFailure(
        ccf,
        [](auto& limits) { limits.maximumInstances = 2U; },
        "instance limit");
    requireLimitFailure(
        ccf,
        [](auto& limits) { limits.maximumUniqueMeshes = 1U; },
        "unique-mesh limit");
    requireLimitFailure(
        ccf,
        [](auto& limits) { limits.maximumMaterials = 3U; },
        "material catalog limit");
    requireLimitFailure(
        ccf,
        [](auto& limits) {
            limits.maximumMaterialReferences = 2U;
        },
        "material-reference limit");
    requireLimitFailure(
        ccf,
        [](auto& limits) { limits.maximumTextureEdges = 4U; },
        "texture-edge limit");

    auto noInstances = ccf;
    noInstances.placedNodes = {
        nullNode(1U, 10U),
        light(2U, 10U),
        object(3U, 20U, 300U),
    };
    auto zeroLimits = CcfRoomDrawPlanLimits{};
    zeroLimits.maximumInstances = 0U;
    zeroLimits.maximumUniqueMeshes = 0U;
    zeroLimits.maximumMaterialReferences = 0U;
    zeroLimits.maximumTextureEdges = 0U;
    const auto emptyPlan =
        airfix::assets::resolveFirstRoomDrawPlan(
            noInstances, zeroLimits);
    require(emptyPlan.issues.empty() && emptyPlan.roomIndex == 0U &&
            emptyPlan.placedNodeIndices.empty() &&
            emptyPlan.meshIndices.empty() &&
            emptyPlan.materialIndices.empty() &&
            emptyPlan.textures.empty(),
        "zero growth limits rejected an empty first-room plan");
}

void testNoRoomAndInvalidPrimary() {
    CcfMetadata noRoom;
    const auto noRoomPlan =
        airfix::assets::resolveFirstRoomDrawPlan(noRoom);
    require(
        hasIssue(noRoomPlan, CcfRoomDrawPlanIssueKind::noRoom),
        "missing first room was not reported");
    requireFailClosed(noRoomPlan, "missing room produced a plan");

    auto invalid = makeOrderedCcf();
    invalid.rooms[0].primaryBinding = false;
    invalid.rooms[1].primaryBinding = true;
    const auto invalidPlan =
        airfix::assets::resolveFirstRoomDrawPlan(invalid);
    require(
        hasIssue(
            invalidPlan,
            CcfRoomDrawPlanIssueKind::firstRoomNotPrimary),
        "non-primary first room was accepted");
    requireFailClosed(
        invalidPlan,
        "invalid first-room binding produced a plan");
}

} // namespace

int main() {
    try {
        testPhysicalOrderSharedMeshesAndDependencies();
        testFallbackAndBspAreIndependent();
        testMaterialFailuresAreAtomic();
        testPlacedDependencyIsAtomic();
        testLimits();
        testNoRoomAndInvalidPrimary();
        std::cout << "CcfRoomDrawPlan tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "CcfRoomDrawPlan tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
