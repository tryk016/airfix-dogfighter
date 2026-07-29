#include "airfix/render/MissionPlacedDynamicBspAssembly.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace airfix::assets;
using namespace airfix::render;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] CcfMeshMetadata mesh(
    const std::uint32_t reference,
    const std::uint32_t materialReference) {
    CcfMeshMetadata result{
        .name = "mesh",
        .reference = reference,
        .scalar = 1.0F,
    };
    result.vertices = {
        {.position = {-1.0F, -1.0F, 0.0F}},
        {.position = {1.0F, -1.0F, 0.0F}},
        {.position = {0.0F, 1.0F, 0.0F}},
    };
    result.triangles.push_back({
        .vertexIndices = {0U, 1U, 2U},
        .materialReference = materialReference,
    });
    return result;
}

[[nodiscard]] CcfBspPolygonMetadata polygon(
    const std::uint32_t ownerReference,
    const float marker = -1.0F) {
    return {
        .faceCross = {0.0F, 0.0F, 4.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {marker, -1.0F, 0.0F},
        .edge01 = {2.0F, 0.0F, 0.0F},
        .edge12 = {-1.0F, 2.0F, 0.0F},
        .polygonIndex = 0U,
        .placedObjectReference = ownerReference,
    };
}

[[nodiscard]] CcfBspTreeMetadata tree(
    const std::uint32_t ownerReference,
    const float firstMarker = -1.0F,
    const bool twoPolygons = false) {
    CcfBspTreeMetadata result{
        .kind = CcfBspTreeKind::dynamicObjectTree,
        .source = CcfBspTreeSource::placedObject4101,
        .rootNodeIndex = 0U,
    };
    result.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, 0.0F},
        .polygonIndices = twoPolygons
            ? std::vector<std::size_t>{0U, 1U}
            : std::vector<std::size_t>{0U},
    });
    result.polygons.push_back(
        polygon(ownerReference, firstMarker));
    if (twoPolygons) {
        result.polygons.push_back(
            polygon(ownerReference, firstMarker + 0.25F));
    }
    return result;
}

[[nodiscard]] CcfPlacedSrtMetadata placedTransform(
    const CcfVector3 position = {0.0F, 0.0F, 0.0F},
    const float scalar = 1.0F) {
    return {
        .position = position,
        .rawScalar = scalar,
        .orientation = std::array<CcfVector3, 3>{
            CcfVector3{1.0F, 0.0F, 0.0F},
            CcfVector3{0.0F, 1.0F, 0.0F},
            CcfVector3{0.0F, 0.0F, 1.0F},
        },
    };
}

[[nodiscard]] CcfPlacedNodeMetadata object(
    const std::uint32_t currentReference,
    const std::uint32_t meshReference,
    const std::uint32_t roomReference,
    const CcfVector3 position,
    std::vector<CcfBspTreeMetadata> trees,
    const std::uint32_t portalType = 0xFFFFFFFFU,
    const std::uint32_t portalRoomReference = 0U) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "placed",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .transform = placedTransform(position),
        .data = CcfPlacedObjectMetadata{
            .meshReference = meshReference,
            .rawFlag = 1U,
            .portalType = portalType,
            .portalRoomReference = portalRoomReference,
            .dynamicBspTrees = std::move(trees),
        },
    };
}

[[nodiscard]] CcfMetadata baseSource() {
    CcfMetadata ccf;
    ccf.roomSections.push_back({
        .firstPhysicalRoomIndex = 0U,
        .physicalRoomCount = 2U,
        .firstDirectChildIsRoom = true,
    });
    ccf.rooms = {
        {
            .name = "",
            .reference = 10U,
            .primaryBinding = true,
        },
        {
            .name = "next",
            .reference = 20U,
        },
    };
    ccf.materials.push_back({
        .name = "solid",
        .reference = 50U,
        .collisionMode2152 = 8U,
    });
    return ccf;
}

[[nodiscard]] MissionWorldRoomCatalog catalogFor(
    const std::vector<MissionCcfRoomLoadSource>& sources) {
    return buildMissionWorldRoomCatalog({
        .initialRootName = {},
        .sources = sources,
    });
}

[[nodiscard]] MissionWorldSpatialArena emptyStaticArena(
    const std::size_t roomCount) {
    MissionWorldSpatialArena result;
    result.rooms.resize(roomCount);
    result.retainedPayloadBytes =
        roomCount * sizeof(MissionWorldSpatialRoom);
    return result;
}

void testFirstUseCacheMaterialBindingAndNativeOrder() {
    auto ccf = baseSource();
    ccf.meshes = {mesh(7U, 50U), mesh(8U, 50U)};
    auto firstTrees = std::vector<CcfBspTreeMetadata>{
        tree(100U, -1.0F, true),
        tree(100U),
    };
    auto cachedTree = tree(300U);
    // The cached branch skips the complete nested 0x4101 payload. A forged
    // invalid kind proves this assembly does not reinterpret ignored data.
    cachedTree.kind = CcfBspTreeKind::staticTree;
    ccf.placedNodes = {
        object(100U, 7U, 10U, {0.0F, 0.0F, 0.0F},
            std::move(firstTrees)),
        object(200U, 8U, 10U, {10.0F, 0.0F, 0.0F},
            {tree(200U)}),
        object(300U, 7U, 10U, {20.0F, 0.0F, 0.0F},
            {std::move(cachedTree)}),
    };
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    const auto catalog = catalogFor(sources);
    const auto assembly =
        buildMissionPlacedDynamicBspAssembly(sources, catalog);

    require(assembly.complete(), "valid placed dynamic BSP was rejected");
    require(
        assembly.meshes.size() == 2U &&
            assembly.meshProvenance.size() == 2U &&
            assembly.objects.size() == 2U &&
            assembly.objectProvenance.size() == 2U &&
            assembly.roomObjectRanges.size() == catalog.rooms.size(),
        "placed dynamic BSP output counts are wrong");
    require(
        assembly.meshProvenance[0] ==
            MissionPlacedDynamicBspMeshProvenance{
                .sourceIndex = 0U,
                .physicalMeshIndex = 0U,
                .firstPlacedNodeIndex = 0U,
                .sourceMeshReference = 7U,
            } &&
            assembly.meshProvenance[1].physicalMeshIndex == 1U,
        "physical mesh first-use cache order was not retained");
    require(
        assembly.objectProvenance[0].sourceNodeReference == 200U &&
            assembly.objectProvenance[1].sourceNodeReference == 100U &&
            assembly.objects[0].runtimeTranslation.x == 10.0F &&
            assembly.objects[1].runtimeTranslation.x == 0.0F,
        "room object list did not reproduce native prepend order");
    require(
        assembly.roomObjectRanges[0] ==
            LegacyDynamicBspRoomObjectRange{
                .firstObjectIndex = 0U,
                .objectCount = 2U,
            } &&
            assembly.roomObjectRanges[1] ==
                LegacyDynamicBspRoomObjectRange{
                    .firstObjectIndex = 2U,
                    .objectCount = 0U,
                },
        "room object ranges are not a canonical flat partition");

    const auto& firstMesh = assembly.meshes[0];
    require(
        firstMesh.localArena.trees.size() == 2U &&
            firstMesh.localArena.treeReferences ==
                std::vector<std::size_t>{1U, 0U},
        "serialized tree list did not reproduce native prepend order");
    require(
        firstMesh.localArena.polygons[0].point0[0] == -0.75F &&
            firstMesh.localArena.polygons[1].point0[0] == -1.0F,
        "node polygon list did not reproduce native prepend order");
    require(
        firstMesh.localArena.polygons[0]
                .materialCollisionMode2152 ==
            8U &&
            firstMesh.polygonMaterialReferences[0] == 50U,
        "triangle material was not bound into the serialized BSP");
    require(
        std::abs(firstMesh.localBoundingRadius -
                 std::sqrt(2.0F)) < 1.0e-5F,
        "runtime local bounding radius is wrong");

    const auto staticArena =
        emptyStaticArena(catalog.rooms.size());
    const auto hit = traceMissionWorldRuntimeCombinedLine(
        staticArena,
        {},
        0U,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F},
        assembly.meshes,
        assembly.objects);
    require(
        hit.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            hit.hit.has_value() &&
            hit.hit->kind ==
                MissionWorldRuntimeCombinedLineHitKind::
                    dynamicObject &&
            hit.hit->dynamicObjectIndex == 1U &&
            hit.hit->dynamicMeshIndex == 0U &&
            hit.hit->sourceTriangleIndex == 0U &&
            hit.hit->sourceMaterialReference == 50U &&
            hit.hit->materialCollisionMode2152 == 8U,
        "assembled BSP was not directly consumable by combined tracing");
}

void testRoomFallbackPortalAndReflectedBasis() {
    auto ccf = baseSource();
    ccf.meshes = {mesh(7U, 50U), mesh(8U, 50U)};
    ccf.placedNodes = {
        object(
            100U,
            7U,
            999U,
            {0.0F, 0.0F, 3.0F},
            {tree(100U)}),
        object(
            200U,
            8U,
            10U,
            {20.0F, 0.0F, 0.0F},
            {tree(200U)},
            0U,
            20U),
    };
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    const auto catalog = catalogFor(sources);
    const BasisTransform reflected{
        .sourceToRuntime = Mat3{{
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{0.0F, 0.0F, -1.0F},
        }},
        .runtimeUnitsPerSourceUnit = 2.0F,
    };
    const auto assembly = buildMissionPlacedDynamicBspAssembly(
        sources, catalog, reflected);

    require(
        assembly.complete() &&
            assembly.objects.size() == 2U,
        "fallback/portal/reflected assembly failed");
    // Both objects live in root; the later portal object is list-head.
    require(
        assembly.objects[0].portalType == 0 &&
            assembly.objects[0].portalWorldRoomIndex == 1U &&
            assembly.objects[0].portalObjectVisible &&
            assembly.objectProvenance[1].sourceNodeReference == 100U &&
            assembly.objectProvenance[1].worldRoomIndex == 0U,
        "room fallback or portal metadata was translated incorrectly");
    const auto& reflectedPolygon =
        assembly.meshes[0].localArena.polygons[0];
    require(
        reflectedPolygon.point0 ==
            CcfVector3{-2.0F, -2.0F, 0.0F} &&
            reflectedPolygon.edge01 ==
                CcfVector3{2.0F, 4.0F, 0.0F} &&
            reflectedPolygon.edge12 ==
                CcfVector3{2.0F, -4.0F, 0.0F} &&
            reflectedPolygon.faceNormal ==
                CcfVector3{0.0F, 0.0F, -1.0F} &&
            assembly.objects[1].runtimeTranslation ==
                Vec3{0.0F, 0.0F, -6.0F},
        "reflected runtime-local polygon conversion is wrong");

    const auto staticArena =
        emptyStaticArena(catalog.rooms.size());
    const std::span<const LegacyDynamicBspLineObject> fallbackOnly{
        assembly.objects.data() + 1U, 1U};
    const auto hit = traceMissionWorldRuntimeCombinedLine(
        staticArena,
        reflected,
        0U,
        {0.0F, 0.0F, -4.0F},
        {0.0F, 0.0F, -8.0F},
        assembly.meshes,
        fallbackOnly);
    require(
        hit.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            hit.hit.has_value() &&
            hit.hit->runtimePoint.z == -6.0F,
        "reflected serialized BSP did not preserve collision geometry");

    const auto& rootRange = assembly.roomObjectRanges[0];
    const std::span<const LegacyDynamicBspLineObject> rootObjects{
        assembly.objects.data() + rootRange.firstObjectIndex,
        rootRange.objectCount,
    };
    const auto portalHit = traceMissionWorldRuntimeCombinedLine(
        staticArena,
        reflected,
        0U,
        {40.0F, 0.0F, 2.0F},
        {40.0F, 0.0F, -2.0F},
        assembly.meshes,
        rootObjects);
    require(
        portalHit.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            portalHit.hit.has_value() &&
            portalHit.hit->dynamicObjectIndex == 0U &&
            portalHit.hit->portalType == 0 &&
            portalHit.hit->portalWorldRoomIndex == 1U,
        "assembled type-zero portal was not visible to the direct query");

    const auto continued = traceMissionWorldRuntimeCombinedPortalLine(
        staticArena,
        reflected,
        0U,
        {40.0F, 0.0F, 2.0F},
        {40.0F, 0.0F, -2.0F},
        assembly.meshes,
        assembly.objects,
        assembly.roomObjectRanges);
    require(
        continued.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::noHit &&
            !continued.hit.has_value() &&
            continued.portalTransitionCount == 1U,
        "assembled room ranges did not drive portal continuation");
}

void testEmptyAssemblyAndRetainedByteLimit() {
    auto empty = baseSource();
    empty.meshes.push_back(mesh(7U, 50U));
    empty.placedNodes.push_back(
        object(100U, 7U, 10U, {}, {}));
    const std::vector<MissionCcfRoomLoadSource> emptySources{
        {.ccf = &empty},
    };
    const auto emptyCatalog = catalogFor(emptySources);
    const auto emptyAssembly =
        buildMissionPlacedDynamicBspAssembly(
            emptySources, emptyCatalog);
    require(
        emptyAssembly.complete() &&
            emptyAssembly.meshes.empty() &&
            emptyAssembly.objects.empty() &&
            emptyAssembly.retainedPayloadBytes ==
                emptyCatalog.rooms.size() *
                    sizeof(LegacyDynamicBspRoomObjectRange),
        "empty dynamic scene was not a valid retained snapshot");

    auto ccf = baseSource();
    ccf.meshes.push_back(mesh(7U, 50U));
    ccf.placedNodes.push_back(
        object(100U, 7U, 10U, {}, {tree(100U)}));
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    const auto catalog = catalogFor(sources);
    const auto exact =
        buildMissionPlacedDynamicBspAssembly(sources, catalog);
    require(exact.complete(), "retained-byte fixture failed");

    MissionPlacedDynamicBspLimits limits;
    limits.maximumRetainedBytes = exact.retainedPayloadBytes;
    require(
        buildMissionPlacedDynamicBspAssembly(
            sources, catalog, {}, limits).complete(),
        "exact retained-byte budget was rejected");
    --limits.maximumRetainedBytes;
    const auto rejected =
        buildMissionPlacedDynamicBspAssembly(
            sources, catalog, {}, limits);
    require(
        !rejected.complete() &&
            rejected.meshes.empty() &&
            rejected.objects.empty() &&
            rejected.roomObjectRanges.empty() &&
            rejected.retainedPayloadBytes == 0U &&
            rejected.issues.size() == 1U &&
            rejected.issues[0].kind ==
                MissionPlacedDynamicBspIssueKind::
                    retainedByteLimitExceeded,
        "one-under retained-byte budget was not atomic");
}

void testFailClosedValidation() {
    auto ccf = baseSource();
    ccf.meshes.push_back(mesh(7U, 50U));
    ccf.placedNodes.push_back(
        object(100U, 7U, 10U, {}, {tree(100U)}));
    const std::vector<MissionCcfRoomLoadSource> sources{
        {.ccf = &ccf},
    };
    const auto catalog = catalogFor(sources);

    auto ownerMismatch = ccf;
    auto& ownerObject = std::get<CcfPlacedObjectMetadata>(
        ownerMismatch.placedNodes[0].data);
    ownerObject.dynamicBspTrees[0]
        .polygons[0].placedObjectReference = 999U;
    const std::vector<MissionCcfRoomLoadSource> mismatchSources{
        {.ccf = &ownerMismatch},
    };
    const auto mismatch = buildMissionPlacedDynamicBspAssembly(
        mismatchSources, catalogFor(mismatchSources));
    require(
        !mismatch.complete() &&
            mismatch.meshes.empty() &&
            mismatch.roomObjectRanges.empty() &&
            mismatch.issues[0].kind ==
                MissionPlacedDynamicBspIssueKind::
                    placedObjectReferenceMismatch,
        "polygon owner mismatch did not fail closed");

    auto scaled = ccf;
    scaled.placedNodes[0].transform =
        placedTransform({}, 2.0F);
    const std::vector<MissionCcfRoomLoadSource> scaledSources{
        {.ccf = &scaled},
    };
    const auto invalidScale =
        buildMissionPlacedDynamicBspAssembly(
            scaledSources, catalogFor(scaledSources));
    require(
        !invalidScale.complete() &&
            invalidScale.issues[0].kind ==
                MissionPlacedDynamicBspIssueKind::
                    invalidTransform,
        "unsupported placed scale was accepted");

    auto impreciseRotation = ccf;
    auto& orientation =
        std::get<std::array<CcfVector3, 3>>(
            impreciseRotation.placedNodes[0]
                .transform.orientation);
    orientation[0][0] = 1.000025F;
    const std::vector<MissionCcfRoomLoadSource>
        impreciseRotationSources{
            {.ccf = &impreciseRotation},
        };
    const auto consumerIncompatible =
        buildMissionPlacedDynamicBspAssembly(
            impreciseRotationSources,
            catalogFor(impreciseRotationSources));
    require(
        !consumerIncompatible.complete() &&
            consumerIncompatible.issues[0].kind ==
                MissionPlacedDynamicBspIssueKind::
                    invalidTransform,
        "assembly accepted a transform rejected by the line consumer");

    auto badCatalog = catalog;
    badCatalog.rooms[0].contributors.clear();
    const auto unauthenticated =
        buildMissionPlacedDynamicBspAssembly(
            sources, badCatalog);
    require(
        !unauthenticated.complete() &&
            unauthenticated.issues[0].kind ==
                MissionPlacedDynamicBspIssueKind::
                    catalogMismatch,
        "tampered room catalog was accepted");

    MissionPlacedDynamicBspLimits limits;
    limits.maximumDepth = 0U;
    const auto invalidDepth =
        buildMissionPlacedDynamicBspAssembly(
            sources, catalog, {}, limits);
    require(
        !invalidDepth.complete() &&
            invalidDepth.issues[0].kind ==
                MissionPlacedDynamicBspIssueKind::
                    limitExceeded,
        "zero tree-depth limit was accepted");

    limits = {};
    limits.maximumScannedMaterials = 0U;
    const auto materialBomb =
        buildMissionPlacedDynamicBspAssembly(
            sources, catalog, {}, limits);
    require(
        !materialBomb.complete() &&
            materialBomb.issues[0].kind ==
                MissionPlacedDynamicBspIssueKind::
                    limitExceeded,
        "material scan limit was not enforced before indexing");

    auto tampered = buildMissionPlacedDynamicBspAssembly(
        sources, catalog);
    require(tampered.complete(), "complete() tamper fixture failed");
    ++tampered.retainedPayloadBytes;
    require(
        !tampered.complete(),
        "complete() accepted a forged retained-byte ledger");
}

} // namespace

int main() {
    try {
        testFirstUseCacheMaterialBindingAndNativeOrder();
        testRoomFallbackPortalAndReflectedBasis();
        testEmptyAssemblyAndRetainedByteLimit();
        testFailClosedValidation();
        std::cout
            << "Mission placed dynamic BSP assembly tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
