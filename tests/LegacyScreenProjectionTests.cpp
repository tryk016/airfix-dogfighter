#include "airfix/render/LegacyScreenProjection.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

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

static_assert(noexcept(buildLegacyScreenProjection()));
static_assert(noexcept(
    std::declval<const LegacyScreenProjection&>().project(Vec3{})));
static_assert(
    !std::is_copy_assignable_v<LegacyScreenProjection> &&
    !std::is_move_assignable_v<LegacyScreenProjection>);

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 1.0e-5F) noexcept {
    return std::fabs(actual - expected) <= tolerance;
}

const LegacyScreenProjection& requireProjection(
    const LegacyScreenProjectionBuildResult& result,
    const char* const message) {
    require(
        result.complete() && result.projection.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.projection;
}

LegacyScreenProjectedPoint requirePoint(
    const LegacyScreenProjectionProjectResult& result,
    const char* const message) {
    require(
        result.complete() && result.projected.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.projected;
}

void requireBuildIssue(
    const LegacyScreenProjectionConfig& config,
    const LegacyScreenProjectionBuildIssueKind expected,
    const char* const message) {
    const auto result = buildLegacyScreenProjection(config);
    require(
        !result.complete() && !result.projection.has_value() &&
            result.issue.has_value() &&
            result.issue->kind == expected,
        message);
}

void testDefaults() {
    const auto built = buildLegacyScreenProjection();
    const auto& projection =
        requireProjection(built, "default projection was rejected");
    require(
        projection.nearDistance() == 1.0F &&
            projection.farDistance() == 100.0F &&
            projection.horizontalFovDegrees() == 90.0F &&
            projection.windowWidth() == 1.0F &&
            projection.centre() ==
                LegacyScreenPoint{0.5F, 0.5F} &&
            close(projection.focalLength(), 0.5F) &&
            close(projection.projectScale(), 0.5F),
        "default validated constants changed");

    const auto point = requirePoint(
        projection.project({2.0F, 1.0F, 2.0F}),
        "default point was rejected");
    require(
        close(point.point.x, 1.0F) &&
            close(point.point.y, 0.25F) &&
            point.cameraSpaceZ == 2.0F &&
            point.legacyRhw == 0.5F &&
            !point.usedNearFallback,
        "default projection formula changed");
}

void testEngineClippingWithSyntheticViewport() {
    const auto built = buildLegacyScreenProjection({
        .nearDistance = 0.25F,
        .farDistance = 200.0F,
        .horizontalFovDegrees = 90.0F,
        .windowWidth = 1920.0F,
        .centre = {960.0F, 540.0F},
    });
    const auto& projection =
        requireProjection(
            built, "engine clipping projection was rejected");
    require(
        close(projection.focalLength(), 960.0F) &&
            close(projection.projectScale(), 3840.0F),
        "engine clipping with synthetic viewport changed");

    const auto point = requirePoint(
        projection.project({1.0F, 2.0F, 10.0F}),
        "synthetic viewport point was rejected");
    require(
        close(point.point.x, 1056.0F) &&
            close(point.point.y, 348.0F) &&
            !point.usedNearFallback,
        "engine clipping values did not use the recovered formula");
}

void testFovClamps() {
    auto config = LegacyScreenProjectionConfig{};
    config.horizontalFovDegrees = -500.0F;
    const auto minimumBuilt = buildLegacyScreenProjection(config);
    const auto& minimum = requireProjection(
        minimumBuilt, "minimum-clamped FOV was rejected");
    require(
        minimum.horizontalFovDegrees() == 1.0F,
        "FOV was not clamped to one degree");

    config.horizontalFovDegrees = 500.0F;
    const auto maximumBuilt = buildLegacyScreenProjection(config);
    const auto& maximum = requireProjection(
        maximumBuilt, "maximum-clamped FOV was rejected");
    require(
        maximum.horizontalFovDegrees() == 175.0F &&
            minimum.focalLength() > maximum.focalLength(),
        "FOV was not clamped to 175 degrees");
}

void testShiftedCentreWidthAndYSign() {
    const auto built = buildLegacyScreenProjection({
        .nearDistance = 1.0F,
        .farDistance = 10.0F,
        .horizontalFovDegrees = 90.0F,
        .windowWidth = 800.0F,
        .centre = {123.0F, 456.0F},
    });
    const auto& projection =
        requireProjection(built, "shifted projection was rejected");
    const auto positiveY = requirePoint(
        projection.project({1.0F, 1.0F, 2.0F}),
        "positive-Y point was rejected");
    const auto negativeY = requirePoint(
        projection.project({1.0F, -1.0F, 2.0F}),
        "negative-Y point was rejected");
    require(
        close(positiveY.point.x, 323.0F) &&
            close(positiveY.point.y, 256.0F) &&
            close(negativeY.point.x, 323.0F) &&
            close(negativeY.point.y, 656.0F),
        "width, shifted centre, or screen-Y inversion changed");
}

void testNearFallbackBoundary() {
    const auto built = buildLegacyScreenProjection({
        .nearDistance = 1.0F,
        .farDistance = 10.0F,
        .horizontalFovDegrees = 90.0F,
        .windowWidth = 2.0F,
        .centre = {0.0F, 0.0F},
    });
    const auto& projection =
        requireProjection(built, "near-boundary projection was rejected");

    const auto below = requirePoint(
        projection.project({1.0F, 0.0F, 0.5F}),
        "below-near point was rejected");
    const auto equal = requirePoint(
        projection.project({1.0F, 0.0F, 1.0F}),
        "equal-near point was rejected");
    const auto above = requirePoint(
        projection.project({
            1.0F,
            0.0F,
            std::nextafter(
                1.0F, std::numeric_limits<float>::infinity()),
        }),
        "above-near point was rejected");
    require(
        below.usedNearFallback && equal.usedNearFallback &&
            !above.usedNearFallback &&
            close(below.point.x, 1.0F) &&
            close(equal.point.x, 1.0F) &&
            above.point.x < 1.0F &&
            below.cameraSpaceZ == 0.5F &&
            equal.cameraSpaceZ == 1.0F &&
            above.cameraSpaceZ ==
                std::nextafter(
                    1.0F, std::numeric_limits<float>::infinity()) &&
            below.legacyRhw == 1.0F &&
            equal.legacyRhw == 1.0F &&
            above.legacyRhw == 1.0F / above.cameraSpaceZ,
        "near fallback did not preserve the exact z comparison");
}

void testFarBoundaryAndBeyondStillProject() {
    const auto built = buildLegacyScreenProjection({
        .nearDistance = 1.0F,
        .farDistance = 2.0F,
        .horizontalFovDegrees = 90.0F,
        .windowWidth = 2.0F,
        .centre = {0.0F, 0.0F},
    });
    const auto& projection =
        requireProjection(built, "short-far projection was rejected");
    const auto atFar = requirePoint(
        projection.project({4.0F, 0.0F, 2.0F}),
        "point exactly at far was rejected");
    const auto beyondFar = requirePoint(
        projection.project({8.0F, 0.0F, 4.0F}),
        "point beyond far was clipped");
    require(
        close(atFar.point.x, 2.0F) &&
            atFar.cameraSpaceZ == projection.farDistance() &&
            atFar.legacyRhw ==
                projection.nearDistance() /
                    projection.farDistance() &&
            !atFar.usedNearFallback &&
            close(beyondFar.point.x, 2.0F) &&
            beyondFar.cameraSpaceZ == 4.0F &&
            beyondFar.legacyRhw == 0.25F &&
            !beyondFar.usedNearFallback,
        "far distance incorrectly participated in projection");
}

void testLegacyRhwDecreasesWithIncreasingZ() {
    const auto built = buildLegacyScreenProjection({
        .nearDistance = 0.5F,
        .farDistance = 2.0F,
        .horizontalFovDegrees = 90.0F,
        .windowWidth = 2.0F,
        .centre = {0.0F, 0.0F},
    });
    const auto& projection =
        requireProjection(built, "rhw projection was rejected");
    const auto nearer = requirePoint(
        projection.project({0.0F, 0.0F, 1.0F}),
        "nearer rhw point was rejected");
    const auto farther = requirePoint(
        projection.project({0.0F, 0.0F, 2.0F}),
        "farther rhw point was rejected");
    const auto beyondFar = requirePoint(
        projection.project({0.0F, 0.0F, 4.0F}),
        "beyond-far rhw point was rejected");
    require(
        nearer.legacyRhw > farther.legacyRhw &&
            farther.legacyRhw > beyondFar.legacyRhw &&
            nearer.cameraSpaceZ == 1.0F &&
            farther.cameraSpaceZ == 2.0F &&
            beyondFar.cameraSpaceZ == 4.0F,
        "legacy rhw was not monotonic for increasing positive z");
}

void testInvalidConfigurations() {
    auto config = LegacyScreenProjectionConfig{};
    config.nearDistance = 0.0F;
    requireBuildIssue(
        config,
        LegacyScreenProjectionBuildIssueKind::
            nearDistanceNotPositive,
        "zero near distance was not rejected");

    config = {};
    config.farDistance = config.nearDistance;
    requireBuildIssue(
        config,
        LegacyScreenProjectionBuildIssueKind::
            farDistanceNotGreaterThanNear,
        "equal near/far distances were not rejected");

    config = {};
    config.windowWidth = -1.0F;
    requireBuildIssue(
        config,
        LegacyScreenProjectionBuildIssueKind::
            windowWidthNotPositive,
        "negative window width was not rejected");

    config = {};
    config.centre.x = std::numeric_limits<float>::quiet_NaN();
    requireBuildIssue(
        config,
        LegacyScreenProjectionBuildIssueKind::nonFiniteConfig,
        "NaN centre was not rejected");

    config = {};
    config.horizontalFovDegrees =
        std::numeric_limits<float>::infinity();
    requireBuildIssue(
        config,
        LegacyScreenProjectionBuildIssueKind::nonFiniteConfig,
        "infinite FOV was not rejected");

    config = {};
    config.nearDistance = std::numeric_limits<float>::denorm_min();
    requireBuildIssue(
        config,
        LegacyScreenProjectionBuildIssueKind::
            nonFiniteDerivedValue,
        "overflowing derived project scale was not rejected");
}

void testInvalidInputsAndOutputOverflow() {
    const auto built = buildLegacyScreenProjection();
    const auto& projection =
        requireProjection(built, "default projection was rejected");
    const auto nanResult = projection.project({
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 1.0F});
    const auto infinityResult = projection.project({
        0.0F, 0.0F, std::numeric_limits<float>::infinity()});
    require(
        !nanResult.complete() && nanResult.issue.has_value() &&
            !nanResult.projected.has_value() &&
            nanResult.issue->kind ==
                LegacyScreenProjectionProjectIssueKind::
                    nonFiniteInput &&
            !infinityResult.complete() &&
            !infinityResult.projected.has_value() &&
            infinityResult.issue.has_value() &&
            infinityResult.issue->kind ==
                LegacyScreenProjectionProjectIssueKind::
                    nonFiniteInput,
        "non-finite camera-space input was not rejected");

    const auto largeBuilt = buildLegacyScreenProjection({
        .nearDistance = 1.0F,
        .farDistance = 2.0F,
        .horizontalFovDegrees = 175.0F,
        .windowWidth = std::numeric_limits<float>::max(),
        .centre = {0.0F, 0.0F},
    });
    const auto& large = requireProjection(
        largeBuilt, "finite large projection was rejected");
    const auto overflow = large.project({
        std::numeric_limits<float>::max(), 0.0F, 1.0F});
    require(
        !overflow.complete() && !overflow.projected.has_value() &&
            overflow.issue.has_value() &&
            overflow.issue->kind ==
                LegacyScreenProjectionProjectIssueKind::
                    nonFiniteOutput,
        "non-finite projected output was not rejected atomically");
}

void testProjectDoesNotAllocate() {
    const auto built = buildLegacyScreenProjection();
    const auto& projection =
        requireProjection(built, "default projection was rejected");

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    bool allComplete = true;
    float checksum = 0.0F;
    for (std::size_t index = 1U; index <= 1024U; ++index) {
        const auto result = projection.project({
            static_cast<float>(index),
            -static_cast<float>(index),
            static_cast<float>(index + 1U),
        });
        if (!result.complete()) {
            allComplete = false;
            break;
        }
        checksum += result.projected->point.x;
    }
    trackAllocations.store(false, std::memory_order_release);
    const auto observed =
        allocationCount.load(std::memory_order_relaxed);
    require(
        allComplete && std::isfinite(checksum),
        "repeated projection failed");
    require(observed == 0U, "project performed a heap allocation");
}

} // namespace

int main() {
    try {
        testDefaults();
        testEngineClippingWithSyntheticViewport();
        testFovClamps();
        testShiftedCentreWidthAndYSign();
        testNearFallbackBoundary();
        testFarBoundaryAndBeyondStillProject();
        testLegacyRhwDecreasesWithIncreasingZ();
        testInvalidConfigurations();
        testInvalidInputsAndOutputOverflow();
        testProjectDoesNotAllocate();
        std::cout << "LegacyScreenProjection tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "LegacyScreenProjection tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
