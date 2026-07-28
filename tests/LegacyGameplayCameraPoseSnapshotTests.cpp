#include "airfix/render/LegacyGameplayCameraPoseSnapshot.hpp"

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

using namespace airfix::render;

static_assert(noexcept(buildLegacyGameplayCameraPoseSnapshot(
    std::declval<const LegacyGameplayCameraFrameSnapshot&>(),
    std::declval<const Vec3&>())));
static_assert(
    noexcept(std::declval<const LegacyGameplayCameraPoseSnapshot&>().project(
        std::declval<const Vec3&>())));
static_assert(std::is_copy_constructible_v<LegacyGameplayCameraPoseSnapshot>);
static_assert(!std::is_copy_assignable_v<LegacyGameplayCameraPoseSnapshot>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(const float actual,
    const float expected,
    const float tolerance = 2.0e-5F) noexcept {
    return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyGameplayCameraFrameSnapshot frame(
    const Vec3 position = {0.0F, 0.0F, -10.0F}) {
    return {
        .simulationStep = 42U,
        .publicationGeneration = 7U,
        .state =
            {
                .roomState =
                    {
                        .runtimeWorldPosition = position,
                        .worldRoomIndex = 3U,
                    },
                .axisFactors = {0.25F, 0.5F, 0.75F},
            },
    };
}

void testDefaultGameplayPoseBindsOneFrame() {
    auto source = frame();
    const auto built =
        buildLegacyGameplayCameraPoseSnapshot(source, {0.0F, 0.0F, 0.0F});
    require(built.complete() && built.snapshot.has_value(),
        "default gameplay pose did not build");
    const auto& snapshot = *built.snapshot;

    require(
        snapshot.frame() == source &&
            snapshot.vehicleWorldAnchor() == Vec3{0.0F, 0.0F, 0.0F} &&
            snapshot.worldToView().uniformScale() == 1.0F &&
            snapshot.worldToView().inverseScaleSquared() == 1.0F &&
            snapshot.projection().nearDistance() == 0.25F &&
            snapshot.projection().farDistance() == 200.0F &&
            snapshot.projection().horizontalFovDegrees() == 90.0F &&
            snapshot.projection().windowWidth() == 640.0F &&
            snapshot.projection().centre() == LegacyScreenPoint{320.0F, 240.0F},
        "pose did not retain one frame or recovered defaults");
    require(close(snapshot.anchorCameraSpacePosition().x, 0.0F) &&
                close(snapshot.anchorCameraSpacePosition().y, 0.0F) &&
                close(snapshot.anchorCameraSpacePosition().z, 10.0F),
        "look-at did not place the vehicle on positive camera Z");

    source.state.roomState.runtimeWorldPosition.z = 100.0F;
    source.publicationGeneration = 99U;
    require(
        snapshot.frame().publicationGeneration == 7U &&
            snapshot.frame().state.roomState.runtimeWorldPosition.z == -10.0F,
        "pose retained a mutable view of the source frame");
}

void testWorldToScreenComposition() {
    const auto built =
        buildLegacyGameplayCameraPoseSnapshot(frame(), {0.0F, 0.0F, 0.0F});
    const auto anchor = built.snapshot->project({0.0F, 0.0F, 0.0F});
    const auto right = built.snapshot->project({1.0F, 0.0F, 0.0F});
    require(anchor.complete() && right.complete(),
        "valid world points did not project");
    require(
        close(anchor.point->cameraSpacePosition.z, 10.0F) &&
            close(anchor.point->projected.point.x, 320.0F) &&
            close(anchor.point->projected.point.y, 240.0F) &&
            close(anchor.point->projected.cameraSpaceZ, 10.0F) &&
            close(anchor.point->projected.legacyRhw, 0.025F) &&
            !anchor.point->projected.usedNearFallback &&
            right.point->projected.point.x > anchor.point->projected.point.x,
        "world-to-view and screen projection were not composed in order");
}

void testArbitraryLookAtDirection() {
    const Vec3 cameraPosition{1.0F, -1.0F, -1.0F};
    const Vec3 anchor{4.0F, 2.0F, 3.0F};
    const auto built =
        buildLegacyGameplayCameraPoseSnapshot(frame(cameraPosition), anchor);
    require(built.complete(), "arbitrary look-at pose did not build");

    const auto camera = built.snapshot->project(cameraPosition);
    require(camera.complete() &&
                close(camera.point->cameraSpacePosition.x, 0.0F) &&
                close(camera.point->cameraSpacePosition.y, 0.0F) &&
                close(camera.point->cameraSpacePosition.z, 0.0F) &&
                camera.point->projected.usedNearFallback &&
                close(built.snapshot->anchorCameraSpacePosition().x, 0.0F) &&
                close(built.snapshot->anchorCameraSpacePosition().y, 0.0F) &&
                built.snapshot->anchorCameraSpacePosition().z > 0.0F,
        "arbitrary look-at axes did not form a coherent view");
}

void testBuildFailuresAreAtomic() {
    auto noGeneration = frame();
    noGeneration.publicationGeneration = 0U;
    auto invalidState = frame();
    invalidState.state.axisFactors.x = -1.0F;
    auto invalidProjection = LegacyGameplayCameraPoseConfig{};
    invalidProjection.projection.nearDistance = 0.0F;

    const auto generation =
        buildLegacyGameplayCameraPoseSnapshot(noGeneration, {0.0F, 0.0F, 0.0F});
    const auto state =
        buildLegacyGameplayCameraPoseSnapshot(invalidState, {0.0F, 0.0F, 0.0F});
    const auto anchor = buildLegacyGameplayCameraPoseSnapshot(frame(),
        {
            std::numeric_limits<float>::infinity(),
            0.0F,
            0.0F,
        });
    const auto zeroDirection =
        buildLegacyGameplayCameraPoseSnapshot(frame(), {0.0F, 0.0F, -10.0F});
    const auto projection = buildLegacyGameplayCameraPoseSnapshot(
        frame(), {0.0F, 0.0F, 0.0F}, invalidProjection);

    require(
        !generation.snapshot.has_value() &&
            generation.issue->kind ==
                LegacyGameplayCameraPoseBuildIssueKind::invalidFrameSnapshot &&
            !state.snapshot.has_value() &&
            state.issue->kind ==
                LegacyGameplayCameraPoseBuildIssueKind::invalidFrameSnapshot &&
            !anchor.snapshot.has_value() &&
            anchor.issue->kind == LegacyGameplayCameraPoseBuildIssueKind::
                                      invalidVehicleWorldAnchor &&
            !zeroDirection.snapshot.has_value() &&
            zeroDirection.issue->kind ==
                LegacyGameplayCameraPoseBuildIssueKind::lookAtFailed &&
            !projection.snapshot.has_value() &&
            projection.issue->kind ==
                LegacyGameplayCameraPoseBuildIssueKind::projectionBuildFailed &&
            projection.issue->projectionIssue.has_value() &&
            !projection.issue->transformIssue.has_value(),
        "failed pose build exposed a partial snapshot");
}

void testProjectionFailureIsAtomic() {
    const auto built =
        buildLegacyGameplayCameraPoseSnapshot(frame(), {0.0F, 0.0F, 0.0F});
    const auto transformFailure = built.snapshot->project({
        std::numeric_limits<float>::infinity(),
        0.0F,
        0.0F,
    });
    const auto projectionFailure = built.snapshot->project({
        std::numeric_limits<float>::max(),
        0.0F,
        0.0F,
    });
    require(
        !transformFailure.complete() && !transformFailure.point.has_value() &&
            transformFailure.issue->kind ==
                LegacyGameplayCameraWorldProjectionIssueKind::transformFailed &&
            transformFailure.issue->transformIssue.has_value() &&
            !transformFailure.issue->projectionIssue.has_value() &&
            !projectionFailure.complete() &&
            !projectionFailure.point.has_value() &&
            projectionFailure.issue->kind ==
                LegacyGameplayCameraWorldProjectionIssueKind::
                    projectionFailed &&
            !projectionFailure.issue->transformIssue.has_value() &&
            projectionFailure.issue->projectionIssue.has_value(),
        "failed transform or projection exposed partial output");
}

void testHotPathsDoNotAllocate() {
    const auto source = frame();
    bool buildComplete = false;
    bool projectionComplete = false;

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0U; iteration < 4'096U; ++iteration) {
        const auto built =
            buildLegacyGameplayCameraPoseSnapshot(source, {0.0F, 0.0F, 0.0F});
        const auto projected = built.snapshot->project({1.0F, 2.0F, 3.0F});
        buildComplete = built.complete();
        projectionComplete = projected.complete();
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(
        buildComplete && projectionComplete, "pose allocation probe failed");
    require(allocationCount.load(std::memory_order_relaxed) == 0U,
        "pose build or world projection allocated");
}

} // namespace

int main() {
    try {
        testDefaultGameplayPoseBindsOneFrame();
        testWorldToScreenComposition();
        testArbitraryLookAtDirection();
        testBuildFailuresAreAtomic();
        testProjectionFailureIsAtomic();
        testHotPathsDoNotAllocate();
        std::cout << "LegacyGameplayCameraPoseSnapshot tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        countAllocations.store(false, std::memory_order_relaxed);
        std::cerr << "LegacyGameplayCameraPoseSnapshot tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
