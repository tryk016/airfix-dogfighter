#include "airfix/render/LegacyGameplayCameraStepCoordinator.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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

void operator delete(void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

namespace {

using namespace airfix::assets;
using namespace airfix::render;

static_assert(
    noexcept(std::declval<LegacyGameplayCameraStepCoordinator&>().tryInitialize(
        std::declval<
            const LegacyGameplayCameraStepCoordinatorInitializeInput&>())));
static_assert(
    noexcept(std::declval<LegacyGameplayCameraStepCoordinator&>().tryAdvance(
        std::declval<const MissionWorldSpatialArena&>(),
        std::declval<const BasisTransform&>(),
        std::declval<const LegacyGameplayCameraStepCoordinatorInput&>(),
        std::declval<std::span<MissionWorldRuntimeSphereCandidate>>(),
        std::declval<std::span<Vec3>>())));
static_assert(
    !std::is_copy_constructible_v<LegacyGameplayCameraStepCoordinator>);
static_assert(
    !std::is_move_constructible_v<LegacyGameplayCameraStepCoordinator>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] MissionWorldSpatialArena emptyArena() {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(1U);
    return arena;
}

[[nodiscard]] LegacyGameplayCameraStepCoordinatorInput
input(const std::uint64_t simulationStep,
      const std::uint64_t cameraCyclePressCount = 0U) {
    return {
        .vehicleChaseWorldPosition = {0.0F, 0.0F, 0.0F},
        .vehicleWorldAnchor = {0.0F, 0.0F, 0.0F},
        .vehicleWorldRotation = {},
        .refreshDeltaSeconds =
            legacyAircraftNominalRefreshDeltaSeconds,
        .vehicleHealth = 1.0F,
        .vehicleInactive = false,
        .cameraCyclePressCount = cameraCyclePressCount,
        .rearViewHeld = false,
        .simulationStep = simulationStep,
    };
}

LegacyGameplayCameraStepCoordinatorInitializeResult
initialize(LegacyGameplayCameraStepCoordinator& coordinator,
           const std::uint64_t cameraCyclePressCount = 0U) {
    const auto initialized = coordinator.tryInitialize({
        .vehicleWorldPosition = {0.0F, 0.0F, 0.0F},
        .vehicleWorldRotation = {},
        .worldRoomIndex = 0U,
        .cameraCyclePressCount = cameraCyclePressCount,
    });
    require(initialized.complete(), "coordinator initialization failed");
    return initialized;
}

void testInitializationOwnsBootstrapAndCounterProvenance() {
    LegacyGameplayCameraStepCoordinator coordinator;
    require(!coordinator.initialized() &&
                !coordinator.currentSnapshot().has_value() &&
                !coordinator.currentMode().has_value() &&
                !coordinator.lastCameraCyclePressCount().has_value(),
            "fresh coordinator exposed state");

    const auto initialized = initialize(coordinator, 7U);
    const auto& frame = initialized.packet->pose().frame();
    require(
        coordinator.initialized() && coordinator.currentSnapshot() == frame &&
            coordinator.currentMode() == LegacyGameplayCameraMode::camera0 &&
            coordinator.lastCameraCyclePressCount() == 7U &&
            frame.simulationStep == 0U && frame.publicationGeneration == 1U &&
            frame.state.roomState.runtimeWorldPosition ==
                Vec3{0.0F, 0.1F, -0.75F},
        "initial camera0 packet and coordinator state diverged");

    const auto duplicate = coordinator.tryInitialize({});
    require(duplicate.status ==
                    LegacyGameplayCameraStepCoordinatorInitializeStatus::
                        alreadyInitialized &&
                !duplicate.packet.has_value() &&
                coordinator.currentSnapshot() == frame,
            "duplicate initialization changed the coordinator");
}

void testStationaryStepComposesAndCommitsEveryStage() {
    auto arena = emptyArena();
    std::array<MissionWorldRuntimeSphereCandidate, 1U> candidates{};
    std::array<Vec3, 1U> constraints{};
    LegacyGameplayCameraStepCoordinator coordinator;
    initialize(coordinator);

    const auto advanced =
        coordinator.tryAdvance(arena, {}, input(1U), candidates, constraints);
    require(advanced.complete(), "stationary camera step failed");
    const auto snapshot = coordinator.currentSnapshot();
    require(snapshot.has_value() && snapshot->simulationStep == 1U &&
                snapshot->publicationGeneration == 2U &&
                snapshot->state.roomState.runtimeWorldPosition ==
                    Vec3{0.0F, 0.1F, -0.75F} &&
                snapshot->state.axisFactors == Vec3{1.0F, 1.0F, 1.0F} &&
                advanced.packet->pose().frame() == *snapshot &&
                advanced.committedMode == LegacyGameplayCameraMode::camera0,
            "complete step did not bind state, generation, and clip packet");
}

void testChasePositionAndWorldAnchorRemainDistinct() {
    auto arena = emptyArena();
    std::array<MissionWorldRuntimeSphereCandidate, 1U> candidates{};
    std::array<Vec3, 1U> constraints{};
    LegacyGameplayCameraStepCoordinator coordinator;
    initialize(coordinator);

    auto separated = input(1U);
    separated.vehicleChaseWorldPosition = {0.0F, 0.0F, 1.0F};
    separated.vehicleWorldAnchor = {0.0F, 0.0F, 0.25F};
    separated.refreshDeltaSeconds = 1.0F;
    const auto advanced =
        coordinator.tryAdvance(arena, {}, separated, candidates, constraints);
    require(advanced.complete() &&
                advanced.packet->pose().vehicleWorldAnchor() ==
                    separated.vehicleWorldAnchor &&
                coordinator.currentSnapshot()
                        ->state.roomState.runtimeWorldPosition !=
                    Vec3{0.0F, 0.1F, -0.75F},
            "coordinator collapsed the chase position and world anchor");
}

void testAircraftFactorRecoveryPrecedesChaseAndCollision() {
    auto arena = emptyArena();
    std::array<MissionWorldRuntimeSphereCandidate, 1U> candidates{};
    std::array<Vec3, 1U> constraints{};
    LegacyGameplayCameraStepCoordinator coordinator;
    initialize(coordinator);

    auto gated = input(1U);
    gated.vehicleChaseWorldPosition = {0.0F, 0.0F, 1.0F};
    gated.vehicleWorldAnchor = {0.0F, 0.0F, 1.0F};
    gated.vehicleHealth = 0.0F;
    const auto cleared =
        coordinator.tryAdvance(arena, {}, gated, candidates, constraints);
    require(cleared.complete() &&
                coordinator.currentSnapshot()->state.axisFactors == Vec3{} &&
                coordinator.currentSnapshot()
                        ->state.roomState.runtimeWorldPosition ==
                    Vec3{0.0F, 0.1F, -0.75F},
            "factor gate did not stop chase before collision");

    auto recovering = input(2U);
    recovering.vehicleChaseWorldPosition = {0.0F, 0.0F, 1.0F};
    recovering.vehicleWorldAnchor = {0.0F, 0.0F, 1.0F};
    recovering.refreshDeltaSeconds = 1.0F;
    const auto recovered =
        coordinator.tryAdvance(arena, {}, recovering, candidates, constraints);
    require(recovered.complete() &&
                coordinator.currentSnapshot()->state.axisFactors ==
                    Vec3{0.25F, 0.25F, 0.25F} &&
                coordinator.currentSnapshot()
                        ->state.roomState.runtimeWorldPosition !=
                    Vec3{0.0F, 0.1F, -0.75F},
            "aircraft factor recovery did not precede the next chase");
}

void testCycleAndRearSelectionRemainTransactional() {
    auto arena = emptyArena();
    std::array<MissionWorldRuntimeSphereCandidate, 1U> candidates{};
    std::array<Vec3, 1U> constraints{};
    LegacyGameplayCameraStepCoordinator coordinator;
    initialize(coordinator, 10U);

    auto cycle = input(1U, 11U);
    const auto camera1 =
        coordinator.tryAdvance(arena, {}, cycle, candidates, constraints);
    require(camera1.complete() &&
                coordinator.currentMode() ==
                    LegacyGameplayCameraMode::camera1 &&
                coordinator.lastCameraCyclePressCount() == 11U,
            "one camera press did not commit camera1");

    auto rear = input(2U, 11U);
    rear.rearViewHeld = true;
    const auto rearResult =
        coordinator.tryAdvance(arena, {}, rear, candidates, constraints);
    require(rearResult.complete() &&
                rearResult.committedMode == LegacyGameplayCameraMode::camera1 &&
                coordinator.currentMode() == LegacyGameplayCameraMode::camera1,
            "rear view changed the persistent camera mode");

    auto wrapped = input(3U, 16U);
    const auto wrappedResult =
        coordinator.tryAdvance(arena, {}, wrapped, candidates, constraints);
    require(wrappedResult.complete() &&
                wrappedResult.committedMode ==
                    LegacyGameplayCameraMode::camera0 &&
                coordinator.lastCameraCyclePressCount() == 16U,
            "batched camera presses did not apply modulo-three cycling");
}

void testFailuresLeaveModeCounterAndStateUnchanged() {
    auto arena = emptyArena();
    std::array<MissionWorldRuntimeSphereCandidate, 1U> candidates{};
    std::array<Vec3, 1U> constraints{};
    LegacyGameplayCameraStepCoordinator coordinator;
    const auto uninitialized =
        coordinator.tryAdvance(arena, {}, input(1U), candidates, constraints);
    require(uninitialized.status ==
                    LegacyGameplayCameraStepCoordinatorStatus::notInitialized &&
                !uninitialized.packet.has_value(),
            "uninitialized coordinator exposed a step");

    initialize(coordinator, 4U);
    const auto before = coordinator.currentSnapshot();

    const auto regressed = coordinator.tryAdvance(
        arena, {}, input(1U, 3U), candidates, constraints);
    auto invalidRecovery = input(1U, 5U);
    invalidRecovery.refreshDeltaSeconds =
        std::numeric_limits<float>::quiet_NaN();
    const auto recovery = coordinator.tryAdvance(
        arena, {}, invalidRecovery, candidates, constraints);
    auto invalidArena = emptyArena();
    invalidArena.issues.push_back({});
    const auto collision = coordinator.tryAdvance(
        invalidArena, {}, input(1U, 5U), candidates, constraints);

    require(regressed.status == LegacyGameplayCameraStepCoordinatorStatus::
                                    cameraCycleCounterRegressed &&
                recovery.status == LegacyGameplayCameraStepCoordinatorStatus::
                                       axisFactorRecoveryFailed &&
                collision.status == LegacyGameplayCameraStepCoordinatorStatus::
                                        staticCollisionFailed &&
                collision.staticCollisionStatus ==
                    LegacyGameplayCameraStaticCollisionStatus::invalidArena &&
                !regressed.packet.has_value() && !recovery.packet.has_value() &&
                !collision.packet.has_value() &&
                coordinator.currentSnapshot() == before &&
                coordinator.currentMode() ==
                    LegacyGameplayCameraMode::camera0 &&
                coordinator.lastCameraCyclePressCount() == 4U,
            "failed precommit stage changed authoritative state");
}

void testPoseAndCommitFailuresRemainAtomic() {
    auto arena = emptyArena();
    std::array<MissionWorldRuntimeSphereCandidate, 1U> candidates{};
    std::array<Vec3, 1U> constraints{};
    LegacyGameplayCameraStepCoordinator coordinator;
    initialize(coordinator);
    const auto initial = coordinator.currentSnapshot();

    auto degeneratePose = input(1U, 1U);
    degeneratePose.vehicleWorldAnchor =
        initial->state.roomState.runtimeWorldPosition;
    degeneratePose.vehicleHealth = 0.0F;
    const auto pose = coordinator.tryAdvance(
        arena, {}, degeneratePose, candidates, constraints);
    require(
        pose.status == LegacyGameplayCameraStepCoordinatorStatus::poseFailed &&
            pose.poseIssue.has_value() && !pose.packet.has_value() &&
            coordinator.currentSnapshot() == initial &&
            coordinator.currentMode() == LegacyGameplayCameraMode::camera0 &&
            coordinator.lastCameraCyclePressCount() == 0U,
        "pose failure committed state or camera mode");

    const auto first = coordinator.tryAdvance(
        arena, {}, input(1U, 1U), candidates, constraints);
    require(first.complete(), "commit failure fixture did not advance");
    const auto committed = coordinator.currentSnapshot();
    const auto stale = coordinator.tryAdvance(
        arena, {}, input(1U, 2U), candidates, constraints);
    require(
        stale.status ==
                LegacyGameplayCameraStepCoordinatorStatus::stateCommitFailed &&
            stale.stateCommitResult == LegacyGameplayCameraStateCommitResult::
                                           simulationStepNotIncreasing &&
            !stale.packet.has_value() &&
            coordinator.currentSnapshot() == committed &&
            coordinator.currentMode() == LegacyGameplayCameraMode::camera1 &&
            coordinator.lastCameraCyclePressCount() == 1U,
        "stale commit changed state, packet, mode, or counter");
}

void testAdvanceDoesNotAllocate() {
    auto arena = emptyArena();
    std::array<MissionWorldRuntimeSphereCandidate, 1U> candidates{};
    std::array<Vec3, 1U> constraints{};
    LegacyGameplayCameraStepCoordinator coordinator;
    initialize(coordinator);

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    bool complete = true;
    for (std::uint64_t step = 1U; step <= 4'096U; ++step) {
        const auto advanced = coordinator.tryAdvance(
            arena, {}, input(step), candidates, constraints);
        complete = complete && advanced.complete();
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(complete, "allocation probe step failed");
    require(allocationCount.load(std::memory_order_relaxed) == 0U,
            "camera step coordinator allocated in the hot path");
}

} // namespace

int main() {
    try {
        testInitializationOwnsBootstrapAndCounterProvenance();
        testStationaryStepComposesAndCommitsEveryStage();
        testChasePositionAndWorldAnchorRemainDistinct();
        testAircraftFactorRecoveryPrecedesChaseAndCollision();
        testCycleAndRearSelectionRemainTransactional();
        testFailuresLeaveModeCounterAndStateUnchanged();
        testPoseAndCommitFailuresRemainAtomic();
        testAdvanceDoesNotAllocate();
        std::cout << "LegacyGameplayCameraStepCoordinator tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        countAllocations.store(false, std::memory_order_relaxed);
        std::cerr << "LegacyGameplayCameraStepCoordinator tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
