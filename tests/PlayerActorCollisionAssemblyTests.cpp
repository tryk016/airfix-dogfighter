#include "airfix/render/PlayerActorCollisionAssembly.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

std::atomic<std::size_t> allocationCount{0U};
std::atomic<bool> countAllocations{false};

[[nodiscard]] void* allocate(const std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const memory = std::malloc(size == 0U ? 1U : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

} // namespace

void* operator new(const std::size_t size) {
    return allocate(size);
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* const memory) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory) noexcept {
    std::free(memory);
}

void operator delete(
    void* const memory,
    const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](
    void* const memory,
    const std::size_t) noexcept {
    std::free(memory);
}

namespace {

using namespace airfix::assets;
using namespace airfix::render;

static_assert(noexcept(publishPlayerActorCollisionFrame(
    std::declval<const PlayerActorCollisionAssembly&>(),
    std::declval<const ConvertedNodeTransform&>(),
    std::declval<std::uint32_t>(),
    std::declval<bool>(),
    std::declval<std::size_t>(),
    std::declval<std::span<LegacyDynamicBspLineObject>>(),
    std::declval<
        std::span<LegacyDynamicBspRoomObjectRange>>())));

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float left,
    const float right,
    const float tolerance = 1.0e-5F) noexcept {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] CcfMeshMetadata triangleMesh(
    const std::uint32_t meshReference,
    const std::uint32_t materialReference) {
    CcfMeshMetadata mesh;
    mesh.reference = meshReference;
    mesh.orientation = {
        CcfVector3{1.0F, 0.0F, 0.0F},
        CcfVector3{0.0F, 1.0F, 0.0F},
        CcfVector3{0.0F, 0.0F, 1.0F},
    };
    mesh.vertices = {
        CcfMeshVertexMetadata{
            .position = {-2.0F, -2.0F, 0.0F},
        },
        CcfMeshVertexMetadata{
            .position = {2.0F, -2.0F, 0.0F},
        },
        CcfMeshVertexMetadata{
            .position = {0.0F, 2.0F, 0.0F},
        },
    };
    mesh.triangles = {
        CcfMeshTriangleMetadata{
            .vertexIndices = {0U, 1U, 2U},
            .materialReference = materialReference,
        },
    };
    return mesh;
}

[[nodiscard]] CcfBlueprintMetadata blueprint(
    const std::uint32_t reference,
    const std::size_t meshIndex) {
    CcfBlueprintMetadata value;
    value.kind = CcfBlueprintKind::mesh;
    value.reference = reference;
    value.meshIndex = meshIndex;
    return value;
}

[[nodiscard]] PlayerActorVisualProvenance provenance(
    const std::size_t blueprintIndex,
    const std::uint32_t blueprintReference,
    const std::size_t physicalMeshIndex) {
    return {
        .legacySkinSlot = 0U,
        .blueprintIndex = blueprintIndex,
        .blueprintReference = blueprintReference,
        .physicalMeshIndex = physicalMeshIndex,
    };
}

[[nodiscard]] CcfMetadata ccfFixture() {
    CcfMetadata ccf;
    ccf.materials = {
        CcfMaterialMetadata{
            .reference = 10U,
            .collisionMode2152 = 42U,
        },
        CcfMaterialMetadata{
            .reference = 20U,
            .collisionMode2152 = 15U,
        },
    };
    ccf.meshes = {
        triangleMesh(200U, 10U),
        triangleMesh(201U, 20U),
    };
    ccf.blueprints = {
        blueprint(1'000U, 0U),
        blueprint(1'001U, 1U),
        blueprint(1'002U, 0U),
    };
    return ccf;
}

[[nodiscard]] PlayerActorVisualDrawAssembly visualFixture() {
    PlayerActorVisualDrawAssembly visual;
    visual.model.meshes.resize(2U);
    visual.meshProvenance = {
        provenance(0U, 1'000U, 0U),
        provenance(1U, 1'001U, 1U),
    };
    visual.model.instances = {
        DrawMeshInstance{
            .meshSlot = 0U,
            .sourceNodeReference = 1'000U,
            .modelLinear = {},
            .modelTranslation = {},
        },
        DrawMeshInstance{
            .meshSlot = 1U,
            .sourceNodeReference = 1'001U,
            .modelLinear = {},
            .modelTranslation = {0.0F, 0.0F, 1.0F},
        },
        DrawMeshInstance{
            .meshSlot = 0U,
            .sourceNodeReference = 1'002U,
            .modelLinear = {},
            .modelTranslation = {10.0F, 0.0F, 0.0F},
        },
    };
    visual.instanceProvenance = {
        provenance(0U, 1'000U, 0U),
        provenance(1U, 1'001U, 1U),
        provenance(2U, 1'002U, 0U),
    };
    return visual;
}

[[nodiscard]] PlayerActorCollisionIssueKind firstIssue(
    const PlayerActorCollisionAssembly& assembly) {
    require(!assembly.issues.empty(), "expected typed collision issue");
    return assembly.issues.front().kind;
}

void testBuildPreservesMeshAndInstanceOrder() {
    const auto assembly = buildPlayerActorCollisionAssembly(
        ccfFixture(), visualFixture());
    require(assembly.complete(), "valid actor collider did not build");
    require(
        assembly.meshes.size() == 2U &&
            assembly.meshProvenance.size() == 2U &&
            assembly.instances.size() == 3U,
        "actor collision topology changed");
    require(
        assembly.meshProvenance[0].collisionMeshIndex == 0U &&
            assembly.meshProvenance[0].sourceMeshReference == 200U &&
            assembly.meshProvenance[1].collisionMeshIndex == 1U &&
            assembly.meshProvenance[1].sourceMeshReference == 201U,
        "physical mesh first-use order changed");
    require(
        assembly.instances[0].collisionMeshIndex == 0U &&
            assembly.instances[1].collisionMeshIndex == 1U &&
            assembly.instances[2].collisionMeshIndex == 0U &&
            assembly.instances[2].actor.blueprintReference == 1'002U,
        "actor-local instance order changed");
    require(
        assembly.meshes[0]
                .localArena.polygons[0]
                .materialCollisionMode2152 ==
                std::optional<std::uint32_t>{42U} &&
            assembly.meshes[1]
                .localArena.polygons[0]
                .materialCollisionMode2152 ==
                std::optional<std::uint32_t>{15U},
        "authenticated collision material values were lost");

    std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(assembly.meshes.size()) *
            sizeof(LegacyDynamicBspMesh) +
        static_cast<std::uint64_t>(
            assembly.meshProvenance.size()) *
            sizeof(PlayerActorCollisionMeshProvenance) +
        static_cast<std::uint64_t>(assembly.instances.size()) *
            sizeof(PlayerActorCollisionInstance);
    for (const auto& mesh : assembly.meshes) {
        expectedBytes += mesh.retainedPayloadBytes;
    }
    require(
        assembly.retainedPayloadBytes == expectedBytes,
        "actor collision retained-byte accounting changed");
}

void testFramePublicationFeedsCombinedTrace() {
    const auto assembly = buildPlayerActorCollisionAssembly(
        ccfFixture(), visualFixture());
    require(assembly.complete(), "trace fixture did not build");

    std::vector<LegacyDynamicBspLineObject> objects(
        assembly.instances.size());
    std::array<LegacyDynamicBspRoomObjectRange, 3U> ranges{};
    const ConvertedNodeTransform actorWorld{
        .linear = {},
        .translation = {0.0F, 0.0F, 2.0F},
        .rawScalar = 1.0F,
    };
    require(
        publishPlayerActorCollisionFrame(
            assembly,
            actorWorld,
            123U,
            true,
            1U,
            objects,
            ranges) ==
            PlayerActorCollisionPublicationStatus::published,
        "valid actor collision frame did not publish");
    require(
        ranges[0] == LegacyDynamicBspRoomObjectRange{} &&
            ranges[1] ==
                LegacyDynamicBspRoomObjectRange{0U, 3U} &&
            ranges[2] == LegacyDynamicBspRoomObjectRange{} &&
            objects[0].meshIndex == 0U &&
            objects[1].meshIndex == 1U &&
            objects[2].meshIndex == 0U &&
            objects[0].actorObjectId == 123U &&
            objects[0].active &&
            objects[0].portalType == -1 &&
            !objects[0].portalWorldRoomIndex.has_value() &&
            close(objects[0].runtimeTranslation.z, 2.0F) &&
            close(objects[1].runtimeTranslation.z, 3.0F) &&
            close(objects[2].runtimeTranslation.x, 10.0F),
        "frame publication changed pose, room, or provenance");

    MissionWorldSpatialArena arena;
    arena.rooms.resize(3U);
    const auto hit = traceMissionWorldRuntimeCombinedPortalLine(
        arena,
        {},
        1U,
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 4.0F},
        assembly.meshes,
        objects,
        ranges);
    require(
        hit.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            hit.hit.has_value() &&
            hit.hit->actorObjectId == 123U &&
            hit.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{0U} &&
            hit.hit->dynamicMeshIndex ==
                std::optional<std::size_t>{0U} &&
            hit.hit->materialCollisionMode2152 ==
                std::optional<std::uint32_t>{42U} &&
            close(hit.hit->legacyFraction, 0.5F),
        "published actor collider did not feed combined line tracing");
}

void testBuildFailuresAreTypedAndAtomic() {
    auto ccf = ccfFixture();
    auto visual = visualFixture();
    visual.issues.push_back({
        .kind =
            PlayerActorVisualDrawIssueKind::
                invalidObjectVisualAssembly,
    });
    auto result = buildPlayerActorCollisionAssembly(ccf, visual);
    require(
        firstIssue(result) ==
                PlayerActorCollisionIssueKind::actorVisualFailure &&
            result.meshes.empty() &&
            result.instances.empty(),
        "upstream visual issue was not atomic");

    visual = visualFixture();
    ccf.materials.pop_back();
    result = buildPlayerActorCollisionAssembly(ccf, visual);
    require(
        firstIssue(result) ==
            PlayerActorCollisionIssueKind::missingMaterialBinding,
        "missing collision material was accepted");

    ccf = ccfFixture();
    ccf.materials.push_back(ccf.materials.front());
    result = buildPlayerActorCollisionAssembly(ccf, visual);
    require(
        firstIssue(result) ==
            PlayerActorCollisionIssueKind::duplicateMaterialBinding,
        "duplicate collision material was accepted");

    ccf = ccfFixture();
    visual.meshProvenance[0].physicalMeshIndex = 1U;
    result = buildPlayerActorCollisionAssembly(ccf, visual);
    require(
        firstIssue(result) ==
            PlayerActorCollisionIssueKind::invalidMeshProvenance,
        "mismatched physical mesh provenance was accepted");

    visual = visualFixture();
    visual.model.instances[1].modelLinear.columns[0].x = 2.0F;
    result = buildPlayerActorCollisionAssembly(ccf, visual);
    require(
        firstIssue(result) ==
            PlayerActorCollisionIssueKind::invalidActorLocalTransform,
        "non-unit actor-local transform was accepted");

    visual = visualFixture();
    auto limits = PlayerActorCollisionLimits{};
    limits.maximumRetainedBytes =
        sizeof(LegacyDynamicBspMesh) - 1U;
    result = buildPlayerActorCollisionAssembly(ccf, visual, {}, limits);
    require(
        firstIssue(result) ==
            PlayerActorCollisionIssueKind::retainedByteLimitExceeded,
        "aggregate retained-byte limit was not enforced");
}

void testPublicationFailuresLeaveOutputsUntouched() {
    const auto assembly = buildPlayerActorCollisionAssembly(
        ccfFixture(), visualFixture());
    require(assembly.complete(), "publication fixture did not build");
    std::vector<LegacyDynamicBspLineObject> objects(
        assembly.instances.size());
    for (auto& object : objects) {
        object.meshIndex = 999U;
    }
    std::array<LegacyDynamicBspRoomObjectRange, 2U> ranges{
        LegacyDynamicBspRoomObjectRange{7U, 8U},
        LegacyDynamicBspRoomObjectRange{9U, 10U},
    };
    const auto unchangedObjects = objects;
    const auto unchangedRanges = ranges;

    auto tampered = assembly;
    ++tampered.meshes[0].localArena.retainedPayloadBytes;
    require(
        !tampered.complete() &&
            publishPlayerActorCollisionFrame(
                tampered,
                {},
                1U,
                true,
                0U,
                objects,
                ranges) ==
                PlayerActorCollisionPublicationStatus::
                    invalidAssembly &&
            objects == unchangedObjects &&
            ranges == unchangedRanges,
        "forged nested collider bytes reached frame publication");

    tampered = assembly;
    tampered.instances[0].actor.physicalMeshIndex = 1U;
    require(
        !tampered.complete() &&
            publishPlayerActorCollisionFrame(
                tampered,
                {},
                1U,
                true,
                0U,
                objects,
                ranges) ==
                PlayerActorCollisionPublicationStatus::
                    invalidAssembly &&
            objects == unchangedObjects &&
            ranges == unchangedRanges,
        "forged instance provenance reached frame publication");

    tampered = assembly;
    tampered.instances[0].actorLocal.rawScalar = 2.0F;
    require(
        !tampered.complete() &&
            publishPlayerActorCollisionFrame(
                tampered,
                {},
                1U,
                true,
                0U,
                objects,
                ranges) ==
                PlayerActorCollisionPublicationStatus::
                    invalidAssembly &&
            objects == unchangedObjects &&
            ranges == unchangedRanges,
        "forged actor-local scalar reached frame publication");

    ConvertedNodeTransform invalidWorld;
    invalidWorld.linear.columns[0].x = 2.0F;
    require(
        publishPlayerActorCollisionFrame(
            assembly,
            invalidWorld,
            1U,
            true,
            0U,
            objects,
            ranges) ==
                PlayerActorCollisionPublicationStatus::
                    invalidTransform &&
            objects == unchangedObjects &&
            ranges == unchangedRanges,
        "invalid world transform partially mutated publication");

    require(
        publishPlayerActorCollisionFrame(
            assembly,
            {},
            1U,
            true,
            2U,
            objects,
            ranges) ==
                PlayerActorCollisionPublicationStatus::invalidInput &&
            objects == unchangedObjects &&
            ranges == unchangedRanges,
        "invalid room partially mutated publication");

    require(
        publishPlayerActorCollisionFrame(
            assembly,
            {},
            1U,
            true,
            0U,
            std::span{objects}.first(objects.size() - 1U),
            ranges) ==
                PlayerActorCollisionPublicationStatus::
                    outputSizeMismatch &&
            objects == unchangedObjects &&
            ranges == unchangedRanges,
        "wrong object span partially mutated publication");
}

void testFramePublicationDoesNotAllocate() {
    const auto assembly = buildPlayerActorCollisionAssembly(
        ccfFixture(), visualFixture());
    require(assembly.complete(), "allocation fixture did not build");
    std::vector<LegacyDynamicBspLineObject> objects(
        assembly.instances.size());
    std::array<LegacyDynamicBspRoomObjectRange, 3U> ranges{};

    auto status =
        PlayerActorCollisionPublicationStatus::invalidInput;
    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0U;
         iteration < 4'096U;
         ++iteration) {
        status = publishPlayerActorCollisionFrame(
            assembly,
            ConvertedNodeTransform{
                .linear = {},
                .translation = {
                    static_cast<float>(iteration),
                    0.0F,
                    2.0F,
                },
                .rawScalar = 1.0F,
            },
            123U,
            true,
            1U,
            objects,
            ranges);
    }
    countAllocations.store(false, std::memory_order_relaxed);
    require(
        status == PlayerActorCollisionPublicationStatus::published,
        "allocation probe publication failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "actor collision frame publication allocated");
}

} // namespace

int main() {
    try {
        testBuildPreservesMeshAndInstanceOrder();
        testFramePublicationFeedsCombinedTrace();
        testBuildFailuresAreTypedAndAtomic();
        testPublicationFailuresLeaveOutputsUntouched();
        testFramePublicationDoesNotAllocate();
        std::cout << "Player actor collision assembly tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Player actor collision assembly tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
