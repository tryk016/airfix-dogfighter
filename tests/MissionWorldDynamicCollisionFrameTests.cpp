#include "airfix/render/MissionWorldDynamicCollisionFrame.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
    if (trackAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
}

} // namespace

void* operator new(const std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0U ? 1U : size);
        memory != nullptr) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

using namespace airfix::render;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ConvertedMeshGeometry triangleGeometry(
    const std::uint32_t reference,
    const std::uint32_t materialReference) {
    ConvertedMeshGeometry geometry;
    geometry.reference = reference;
    geometry.vertices = {
        {{-2.0F, -2.0F, 0.0F}},
        {{2.0F, -2.0F, 0.0F}},
        {{0.0F, 2.0F, 0.0F}},
    };
    geometry.triangles = {
        {
            .vertexIndices = {0U, 1U, 2U},
            .materialReference = materialReference,
        },
    };
    return geometry;
}

[[nodiscard]] MissionPlacedDynamicBspAssembly placedFixture() {
    MissionPlacedDynamicBspAssembly placed;
    placed.meshes.push_back(
        buildLegacyDynamicBsp(triangleGeometry(10U, 100U)));
    require(placed.meshes[0].complete(), "placed mesh fixture failed");
    placed.meshProvenance.push_back({
        .sourceIndex = 0U,
        .physicalMeshIndex = 1U,
        .firstPlacedNodeIndex = 2U,
        .sourceMeshReference = 10U,
    });
    placed.objects.push_back({
        .meshIndex = 0U,
        .actorObjectId = 0U,
        .active = true,
        .objectLocalToRuntime = {},
        .runtimeTranslation = {0.0F, 0.0F, 2.0F},
        .portalType = -1,
        .portalWorldRoomIndex = std::nullopt,
        .portalObjectVisible = false,
    });
    placed.objectProvenance.push_back({
        .sourceIndex = 0U,
        .placedNodeIndex = 2U,
        .physicalMeshIndex = 1U,
        .worldRoomIndex = 1U,
        .sourceNodeReference = 20U,
    });
    placed.roomObjectRanges = {
        {
            .firstObjectIndex = 0U,
            .objectCount = 0U,
        },
        {
            .firstObjectIndex = 0U,
            .objectCount = 1U,
        },
    };
    placed.retainedPayloadBytes =
        sizeof(LegacyDynamicBspMesh) +
        sizeof(MissionPlacedDynamicBspMeshProvenance) +
        sizeof(LegacyDynamicBspLineObject) +
        sizeof(MissionPlacedDynamicBspObjectProvenance) +
        2U * sizeof(LegacyDynamicBspRoomObjectRange) +
        placed.meshes[0].retainedPayloadBytes;
    require(placed.complete(), "placed assembly fixture failed");
    return placed;
}

[[nodiscard]] PlayerActorCollisionAssembly playerFixture() {
    PlayerActorCollisionAssembly player;
    player.meshes.push_back(
        buildLegacyDynamicBsp(triangleGeometry(30U, 300U)));
    require(player.meshes[0].complete(), "player mesh fixture failed");
    const PlayerActorVisualProvenance actor{
        .legacySkinSlot = 0U,
        .blueprintIndex = 3U,
        .blueprintReference = 40U,
        .physicalMeshIndex = 4U,
    };
    player.meshProvenance.push_back({
        .actor = actor,
        .collisionMeshIndex = 0U,
        .sourceMeshReference = 30U,
    });
    player.instances.push_back({
        .collisionMeshIndex = 0U,
        .actor = actor,
        .actorLocal =
            {
                .linear = {},
                .translation = {},
                .rawScalar = 1.0F,
            },
    });
    player.retainedPayloadBytes =
        sizeof(LegacyDynamicBspMesh) +
        sizeof(PlayerActorCollisionMeshProvenance) +
        sizeof(PlayerActorCollisionInstance) +
        player.meshes[0].retainedPayloadBytes;
    require(player.complete(), "player assembly fixture failed");
    return player;
}

[[nodiscard]] airfix::assets::MissionWorldSpatialArena emptyArena() {
    airfix::assets::MissionWorldSpatialArena arena;
    arena.rooms.resize(2U);
    return arena;
}

void testCombinedPublicationAndSegmentedTrace() {
    const auto placed = placedFixture();
    const auto player = playerFixture();
    std::array<LegacyDynamicBspLineObject, 2U> objects{};
    std::array<LegacyDynamicBspRoomObjectRange, 2U> ranges{};
    const ConvertedNodeTransform playerWorld{
        .linear = {},
        .translation = {0.0F, 0.0F, 2.0F},
        .rawScalar = 1.0F,
    };

    const auto result = publishMissionWorldDynamicCollisionFrame(
        placed,
        &player,
        playerWorld,
        123U,
        true,
        1U,
        objects,
        ranges);
    require(result.published(), "combined frame did not publish");
    require(
        result.frame.meshes.primary.data() == placed.meshes.data() &&
            result.frame.meshes.secondary.data() ==
                player.meshes.data() &&
            result.frame.meshes.size() == 2U &&
            result.frame.meshes.tryGet(0U) == &placed.meshes[0] &&
            result.frame.meshes.tryGet(1U) == &player.meshes[0] &&
            result.frame.meshes.tryGet(2U) == nullptr,
        "segmented mesh view copied or reordered its owners");
    require(
        ranges[0] == LegacyDynamicBspRoomObjectRange{0U, 0U} &&
            ranges[1] ==
                LegacyDynamicBspRoomObjectRange{0U, 2U} &&
            objects[0].meshIndex == 1U &&
            objects[0].actorObjectId == 123U &&
            objects[0].active &&
            objects[0].runtimeTranslation ==
                Vec3{0.0F, 0.0F, 2.0F} &&
            objects[1] == placed.objects[0],
        "player prepend order or flat mesh offset changed");

    const auto arena = emptyArena();
    const auto activeHit = traceMissionWorldRuntimeCombinedPortalLine(
        arena,
        {},
        1U,
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 4.0F},
        result.frame.meshes,
        result.frame.objects,
        result.frame.roomObjectRanges);
    require(
        activeHit.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            activeHit.hit.has_value() &&
            activeHit.hit->legacyFraction == 0.5F &&
            activeHit.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{0U} &&
            activeHit.hit->dynamicMeshIndex ==
                std::optional<std::size_t>{1U} &&
            activeHit.hit->actorObjectId == 123U &&
            activeHit.hit->sourceMaterialReference ==
                std::optional<std::uint32_t>{300U},
        "secondary actor mesh did not win the native-order tie");

    const auto inactive = publishMissionWorldDynamicCollisionFrame(
        placed,
        &player,
        playerWorld,
        123U,
        false,
        1U,
        objects,
        ranges);
    require(
        inactive.published() && !objects[0].active &&
            ranges[1].objectCount == 2U,
        "inactive player changed stable frame sizing");
    const auto inactiveHit = traceMissionWorldRuntimeCombinedPortalLine(
        arena,
        {},
        1U,
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 4.0F},
        inactive.frame.meshes,
        inactive.frame.objects,
        inactive.frame.roomObjectRanges);
    require(
        inactiveHit.status ==
                MissionWorldRuntimeCombinedLineTraceStatus::hit &&
            inactiveHit.hit.has_value() &&
            inactiveHit.hit->dynamicObjectIndex ==
                std::optional<std::size_t>{1U} &&
            inactiveHit.hit->dynamicMeshIndex ==
                std::optional<std::size_t>{0U} &&
            inactiveHit.hit->actorObjectId == 0U &&
            inactiveHit.hit->sourceMaterialReference ==
                std::optional<std::uint32_t>{100U},
        "inactive actor did not fall through to placed collision");
}

void testNoPlayerAndAtomicFailures() {
    auto placed = placedFixture();
    const auto player = playerFixture();
    std::array<LegacyDynamicBspLineObject, 1U> placedOnlyObjects{};
    std::array<LegacyDynamicBspRoomObjectRange, 2U> placedOnlyRanges{};
    const auto noPlayer = publishMissionWorldDynamicCollisionFrame(
        placed,
        nullptr,
        {},
        999U,
        true,
        std::numeric_limits<std::size_t>::max(),
        placedOnlyObjects,
        placedOnlyRanges);
    require(
        noPlayer.published() &&
            noPlayer.frame.meshes.secondary.empty() &&
            placedOnlyObjects[0] == placed.objects[0] &&
            placedOnlyRanges[0] ==
                placed.roomObjectRanges[0] &&
            placedOnlyRanges[1] ==
                placed.roomObjectRanges[1],
        "no-player mission did not publish placed collision alone");

    std::array<LegacyDynamicBspLineObject, 2U> objects{
        LegacyDynamicBspLineObject{.meshIndex = 77U},
        LegacyDynamicBspLineObject{.meshIndex = 88U},
    };
    std::array<LegacyDynamicBspRoomObjectRange, 2U> ranges{
        LegacyDynamicBspRoomObjectRange{9U, 10U},
        LegacyDynamicBspRoomObjectRange{11U, 12U},
    };
    const auto originalObjects = objects;
    const auto originalRanges = ranges;
    const ConvertedNodeTransform invalidWorld{
        .linear = {},
        .translation =
            {
                std::numeric_limits<float>::infinity(),
                0.0F,
                0.0F,
            },
        .rawScalar = 1.0F,
    };
    const auto invalid = publishMissionWorldDynamicCollisionFrame(
        placed,
        &player,
        invalidWorld,
        123U,
        true,
        1U,
        objects,
        ranges);
    require(
        invalid.status ==
                MissionWorldDynamicCollisionPublicationStatus::
                    invalidTransform &&
            objects == originalObjects &&
            ranges == originalRanges,
        "invalid transform partially changed caller output");

    const auto shortOutput = publishMissionWorldDynamicCollisionFrame(
        placed,
        &player,
        {},
        123U,
        true,
        1U,
        std::span{objects}.first(1U),
        ranges);
    require(
        shortOutput.status ==
                MissionWorldDynamicCollisionPublicationStatus::
                    outputSizeMismatch &&
            objects == originalObjects &&
            ranges == originalRanges,
        "size mismatch partially changed caller output");

    const auto shortRanges = publishMissionWorldDynamicCollisionFrame(
        placed,
        &player,
        {},
        123U,
        true,
        1U,
        objects,
        std::span{ranges}.first(1U));
    require(
        shortRanges.status ==
                MissionWorldDynamicCollisionPublicationStatus::
                    outputSizeMismatch &&
            objects == originalObjects &&
            ranges == originalRanges,
        "room-range size mismatch partially changed caller output");

    placed.issues.push_back({});
    const auto invalidPlaced =
        publishMissionWorldDynamicCollisionFrame(
            placed,
            &player,
            {},
            123U,
            true,
            1U,
            objects,
            ranges);
    require(
        invalidPlaced.status ==
                MissionWorldDynamicCollisionPublicationStatus::
                    invalidPlacedAssembly &&
            objects == originalObjects &&
            ranges == originalRanges,
        "invalid placed assembly partially changed caller output");
}

void testPublicationDoesNotAllocate() {
    const auto placed = placedFixture();
    const auto player = playerFixture();
    std::array<LegacyDynamicBspLineObject, 2U> objects{};
    std::array<LegacyDynamicBspRoomObjectRange, 2U> ranges{};
    const ConvertedNodeTransform playerWorld{
        .linear = {},
        .translation = {0.0F, 0.0F, 2.0F},
        .rawScalar = 1.0F,
    };

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    const auto result = publishMissionWorldDynamicCollisionFrame(
        placed,
        &player,
        playerWorld,
        123U,
        true,
        1U,
        objects,
        ranges);
    trackAllocations.store(false, std::memory_order_release);

    require(result.published(), "allocation probe did not publish");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "frame publication allocated memory");
}

} // namespace

int main() {
    try {
        testCombinedPublicationAndSegmentedTrace();
        testNoPlayerAndAtomicFailures();
        testPublicationDoesNotAllocate();
    } catch (const std::exception& error) {
        std::cerr << "Mission world dynamic collision frame tests failed: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "Mission world dynamic collision frame tests passed\n";
    return 0;
}
