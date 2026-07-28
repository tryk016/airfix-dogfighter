#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
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
    !std::is_copy_constructible_v<LegacyGameplayCameraMissionRuntime>);
static_assert(
    !std::is_move_constructible_v<LegacyGameplayCameraMissionRuntime>);
static_assert(noexcept(
    std::declval<LegacyGameplayCameraMissionRuntime&>().tryAdvance(
        std::declval<
            const LegacyGameplayCameraStepCoordinatorInput&>())));
static_assert(noexcept(
    std::declval<LegacyGameplayCameraMissionRuntime&>().tryAcquire()));

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] MissionWorldSpatialArena arena(
    const std::size_t polygonCount = 0U) {
    MissionWorldSpatialArena result;
    result.rooms.resize(1U);
    result.polygons.resize(polygonCount);
    return result;
}

[[nodiscard]] LegacyGameplayCameraStepCoordinatorInitializeInput
initializeInput() {
    return {
        .vehicleWorldPosition = {0.0F, 0.0F, 0.0F},
        .vehicleWorldRotation = {},
        .worldRoomIndex = 0U,
        .cameraCyclePressCount = 0U,
    };
}

[[nodiscard]] LegacyGameplayCameraStepCoordinatorInput input(
    const std::uint64_t simulationStep,
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

[[nodiscard]] LegacyGameplayCameraMissionRuntimeBuildResult
buildRuntime() {
    auto missionArena = arena();
    return LegacyGameplayCameraMissionRuntime::create(
        std::move(missionArena), {}, initializeInput());
}

void requireBuildIssue(
    const LegacyGameplayCameraMissionRuntimeBuildResult& result,
    const LegacyGameplayCameraMissionRuntimeBuildIssueKind kind,
    const char* const message) {
    require(
        !result.complete() && result.runtime == nullptr &&
            result.additionalRetainedBytes == 0U &&
            result.issue.has_value() && result.issue->kind == kind,
        message);
}

void testCreationOwnsBootstrapArenaAndExactWorkspaces() {
    auto missionArena = arena(3U);
    const auto originalPolygonCount = missionArena.polygons.size();
    auto built = LegacyGameplayCameraMissionRuntime::create(
        std::move(missionArena), {}, initializeInput());
    require(built.complete(), "mission runtime creation failed");
    require(
        built.runtime->candidateCapacity() == originalPolygonCount &&
            built.runtime->constraintPlaneCapacity() ==
                originalPolygonCount &&
            built.runtime->additionalRetainedBytes() ==
                built.additionalRetainedBytes &&
            built.additionalRetainedBytes ==
                LegacyGameplayCameraPacketExchange::retainedBytes() +
                    originalPolygonCount *
                        sizeof(MissionWorldRuntimeSphereCandidate) +
                    originalPolygonCount * sizeof(Vec3),
        "mission runtime workspace admission diverged");

    auto initial = built.runtime->tryAcquire();
    require(
        initial.has_value() && initial->valid() &&
            initial->simulationStep() == 0U &&
            initial->cameraPublicationGeneration() == 1U &&
            built.runtime->currentSnapshot().has_value() &&
            built.runtime->currentSnapshot()->simulationStep == 0U &&
            built.runtime->currentMode() ==
                LegacyGameplayCameraMode::camera0,
        "mission runtime bootstrap was incoherent");
}

void testInvalidOwnershipInputsFailBeforePublication() {
    MissionWorldSpatialArena invalidArena;
    auto invalid = LegacyGameplayCameraMissionRuntime::create(
        std::move(invalidArena), {}, initializeInput());
    requireBuildIssue(
        invalid,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::incompleteArena,
        "incomplete arena was accepted");

    auto basisArena = arena();
    BasisTransform singularBasis;
    singularBasis.sourceToRuntime.columns[2] = {0.0F, 0.0F, 0.0F};
    auto invalidBasis = LegacyGameplayCameraMissionRuntime::create(
        std::move(basisArena), singularBasis, initializeInput());
    requireBuildIssue(
        invalidBasis,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::invalidBasis,
        "invalid basis was accepted");

    auto roomArena = arena();
    auto wrongRoom = initializeInput();
    wrongRoom.worldRoomIndex = 1U;
    auto invalidRoom = LegacyGameplayCameraMissionRuntime::create(
        std::move(roomArena), {}, wrongRoom);
    requireBuildIssue(
        invalidRoom,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            initialWorldRoomOutOfRange,
        "out-of-range initial room was accepted");
}

void testWorkspaceAndRetainedLimitsFailClosed() {
    LegacyGameplayCameraMissionRuntimeLimits candidateLimits;
    candidateLimits.maximumCandidateRecords = 1U;
    auto candidateArena = arena(2U);
    auto candidates = LegacyGameplayCameraMissionRuntime::create(
        std::move(candidateArena),
        {},
        initializeInput(),
        candidateLimits);
    requireBuildIssue(
        candidates,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            candidateRecordLimitExceeded,
        "candidate limit was ignored");

    LegacyGameplayCameraMissionRuntimeLimits constraintLimits;
    constraintLimits.maximumConstraintPlanes = 1U;
    auto constraintArena = arena(2U);
    auto constraints = LegacyGameplayCameraMissionRuntime::create(
        std::move(constraintArena),
        {},
        initializeInput(),
        constraintLimits);
    requireBuildIssue(
        constraints,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            constraintPlaneLimitExceeded,
        "constraint limit was ignored");

    LegacyGameplayCameraMissionRuntimeLimits byteLimits;
    byteLimits.maximumAdditionalRetainedBytes =
        LegacyGameplayCameraPacketExchange::retainedBytes() - 1U;
    auto byteArena = arena();
    auto bytes = LegacyGameplayCameraMissionRuntime::create(
        std::move(byteArena), {}, initializeInput(), byteLimits);
    requireBuildIssue(
        bytes,
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::
            retainedByteLimitExceeded,
        "retained-byte limit was ignored");
}

void testAdvancePublishesCompletePacketsAndInputMode() {
    auto built = buildRuntime();
    require(built.complete(), "advance fixture creation failed");

    const auto camera1 = built.runtime->tryAdvance(input(1U, 1U));
    require(
        camera1.advanced() && camera1.published() &&
            camera1.coordinator.complete() &&
            camera1.coordinator.committedMode ==
                LegacyGameplayCameraMode::camera1 &&
            camera1.transportResult ==
                LegacyGameplayCameraPacketPublishResult::published,
        "complete camera step was not published");

    auto lease = built.runtime->tryAcquire();
    require(
        lease.has_value() && lease->simulationStep() == 1U &&
            lease->cameraPublicationGeneration() == 2U &&
            lease->packet() != nullptr &&
            built.runtime->currentSnapshot() ==
                lease->packet()->pose().frame() &&
            built.runtime->currentMode() ==
                LegacyGameplayCameraMode::camera1,
        "published runtime packet was torn");
}

void testBusyTransportDropsOnlyVisualPacket() {
    auto built = buildRuntime();
    require(built.complete(), "busy fixture creation failed");

    auto initialLease = built.runtime->tryAcquire();
    require(initialLease.has_value(), "initial retained lease missing");
    const auto first = built.runtime->tryAdvance(input(1U));
    require(first.published(), "first alternate-slot publication failed");
    auto frontLease = built.runtime->tryAcquire();
    require(frontLease.has_value(), "front retained lease missing");

    const auto busy = built.runtime->tryAdvance(input(2U));
    require(
        busy.advanced() && !busy.published() &&
            busy.status ==
                LegacyGameplayCameraMissionRuntimeAdvanceStatus::
                    advancedPacketBusy &&
            busy.transportResult ==
                LegacyGameplayCameraPacketPublishResult::busy &&
            built.runtime->currentSnapshot()->simulationStep == 2U,
        "busy transport rolled back or rejected coordinator state");

    initialLease.reset();
    const auto later = built.runtime->tryAdvance(input(3U));
    require(
        later.published(),
        "later generation after a busy packet did not publish");
    auto latest = built.runtime->tryAcquire();
    require(
        latest.has_value() && latest->simulationStep() == 3U &&
            latest->cameraPublicationGeneration() == 4U &&
            frontLease->simulationStep() == 1U,
        "generation skip corrupted a retained packet");
}

void testCoordinatorFailurePreservesPublishedPacket() {
    auto built = buildRuntime();
    require(built.complete(), "failure fixture creation failed");
    auto invalid = input(1U);
    invalid.refreshDeltaSeconds =
        std::numeric_limits<float>::quiet_NaN();
    const auto rejected = built.runtime->tryAdvance(invalid);
    require(
        !rejected.advanced() &&
            rejected.status ==
                LegacyGameplayCameraMissionRuntimeAdvanceStatus::
                    coordinatorRejected &&
            rejected.coordinator.status ==
                LegacyGameplayCameraStepCoordinatorStatus::
                    axisFactorRecoveryFailed &&
            !rejected.transportResult.has_value() &&
            built.runtime->currentSnapshot()->simulationStep == 0U,
        "coordinator failure changed mission runtime state");

    auto latest = built.runtime->tryAcquire();
    require(
        latest.has_value() && latest->simulationStep() == 0U &&
            latest->cameraPublicationGeneration() == 1U,
        "coordinator failure changed the published packet");
}

void testAdvanceResultKeepsCoordinatorAndTransportOutcomesSeparate() {
    LegacyGameplayCameraMissionRuntimeAdvanceResult result;
    result.status =
        LegacyGameplayCameraMissionRuntimeAdvanceStatus::transportRejected;
    result.transportResult =
        LegacyGameplayCameraPacketPublishResult::
            exchangeGenerationExhausted;
    require(
        result.advanced() && !result.published(),
        "transport rejection hid an already committed coordinator state");
}

void testWeakEndpointAndLeaseRemainLifetimeSafe() {
    auto built = buildRuntime();
    require(built.complete(), "lifetime fixture creation failed");
    std::shared_ptr<LegacyGameplayCameraMissionRuntime> runtime(
        std::move(built.runtime));
    std::weak_ptr<LegacyGameplayCameraMissionRuntime> endpoint(runtime);
    auto lease = runtime->tryAcquire();
    require(lease.has_value(), "lifetime lease missing");

    runtime.reset();
    require(
        endpoint.expired() && lease->valid() &&
            lease->simulationStep() == 0U &&
            lease->packet() != nullptr,
        "weak endpoint or retained render lease outlived ownership unsafely");
}

void testSteadyStateDoesNotAllocate() {
    auto built = buildRuntime();
    require(built.complete(), "allocation fixture creation failed");

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    bool complete = true;
    for (std::uint64_t step = 1U; step <= 4'096U; ++step) {
        const auto advanced = built.runtime->tryAdvance(input(step));
        auto lease = built.runtime->tryAcquire();
        complete = complete && advanced.published() &&
            lease.has_value() && lease->simulationStep() == step &&
            lease->cameraPublicationGeneration() == step + 1U;
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(complete, "allocation probe runtime step failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "mission camera runtime allocated in the steady-state path");
}

} // namespace

int main() {
    try {
        testCreationOwnsBootstrapArenaAndExactWorkspaces();
        testInvalidOwnershipInputsFailBeforePublication();
        testWorkspaceAndRetainedLimitsFailClosed();
        testAdvancePublishesCompletePacketsAndInputMode();
        testBusyTransportDropsOnlyVisualPacket();
        testCoordinatorFailurePreservesPublishedPacket();
        testAdvanceResultKeepsCoordinatorAndTransportOutcomesSeparate();
        testWeakEndpointAndLeaseRemainLifetimeSafe();
        testSteadyStateDoesNotAllocate();
        std::cout << "LegacyGameplayCameraMissionRuntime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        countAllocations.store(false, std::memory_order_relaxed);
        std::cerr << "LegacyGameplayCameraMissionRuntime tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
