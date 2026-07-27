#include "airfix/render/LegacyCameraTransform.hpp"
#include "airfix/render/LegacyGameplayCameraCollision.hpp"

#include <atomic>
#include <bit>
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

static_assert(noexcept(legacyGameplayCameraCollisionSphereRadius(
    std::declval<float>())));
static_assert(noexcept(
    legacyGameplayCameraReduceCollisionAxisFactors(
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>(),
        std::declval<const Vec3&>())));
static_assert(noexcept(
    legacyAircraftRecoverGameplayCameraAxisFactors(
        std::declval<const Vec3&>(),
        std::declval<float>(),
        std::declval<float>(),
        std::declval<bool>())));
static_assert(noexcept(legacyGameplayCameraLineHitPoint(
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>(),
    std::declval<float>())));
static_assert(noexcept(legacyGameplayCameraLookAt(
    std::declval<const Vec3&>(),
    std::declval<const Vec3&>())));
static_assert(std::is_copy_constructible_v<
              LegacyGameplayCameraLookAt>);

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 1.0e-6F) noexcept {
    return std::abs(actual - expected) <= tolerance;
}

void requireVec(
    const Vec3 actual,
    const Vec3 expected,
    const char* const message,
    const float tolerance = 1.0e-6F) {
    require(
        close(actual.x, expected.x, tolerance) &&
            close(actual.y, expected.y, tolerance) &&
            close(actual.z, expected.z, tolerance),
        message);
}

void testRecoveredConstantsAndSphereRadius() {
    require(
        legacyGameplayCameraCollisionSphereRadius(1.0F)
                .has_value() &&
            std::bit_cast<std::uint32_t>(
                *legacyGameplayCameraCollisionSphereRadius(1.0F)) ==
                0x3F8CCCCDU,
        "collision-radius multiplier bits changed");
    require(
        std::bit_cast<std::uint32_t>(
            legacyGameplayCameraSpherePortalTraceArgument) ==
            0x3E4CCCCDU,
        "sphere portal trace argument bits changed");
    require(
        std::bit_cast<std::uint32_t>(
            legacyGameplayCameraLinePortalTraceArgument) ==
            0x3DCCCCCDU,
        "line portal trace argument bits changed");

    const auto radius =
        legacyGameplayCameraCollisionSphereRadius(0.25F);
    require(
        radius.has_value() && close(*radius, 0.275F),
        "default near plane produced the wrong sphere radius");

    require(
        !legacyGameplayCameraCollisionSphereRadius(0.0F)
             .has_value() &&
            !legacyGameplayCameraCollisionSphereRadius(-1.0F)
                 .has_value() &&
            !legacyGameplayCameraCollisionSphereRadius(
                 std::numeric_limits<float>::infinity())
                 .has_value() &&
            !legacyGameplayCameraCollisionSphereRadius(
                 std::numeric_limits<float>::max())
                 .has_value(),
        "invalid or overflowing near plane was accepted");
}

void testCollisionFactorReduction() {
    const auto reduced =
        legacyGameplayCameraReduceCollisionAxisFactors(
            Vec3{1.5F, 0.5F, -0.25F},
            Vec3{10.0F, 20.0F, 30.0F},
            Vec3{10.25F, 19.9F, 31.0F});
    require(reduced.has_value(), "valid factor reduction failed");
    requireVec(
        *reduced,
        Vec3{1.0F, 0.3F, 0.0F},
        "factor correction or lower clamp changed");

    const auto noUpperClamp =
        legacyGameplayCameraReduceCollisionAxisFactors(
            Vec3{2.0F, 1.0F, 1.0F},
            Vec3{},
            Vec3{0.1F, 0.0F, 0.0F});
    require(
        noUpperClamp.has_value() &&
            close(noUpperClamp->x, 1.8F),
        "collision reduction incorrectly added an upper clamp");

    require(
        !legacyGameplayCameraReduceCollisionAxisFactors(
             Vec3{std::numeric_limits<float>::quiet_NaN(),
                  1.0F,
                  1.0F},
             Vec3{},
             Vec3{})
             .has_value() &&
            !legacyGameplayCameraReduceCollisionAxisFactors(
                 Vec3{1.0F, 1.0F, 1.0F},
                 Vec3{-std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 Vec3{std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F})
                 .has_value() &&
            !legacyGameplayCameraReduceCollisionAxisFactors(
                 Vec3{-std::numeric_limits<float>::max(),
                      1.0F,
                      1.0F},
                 Vec3{},
                 Vec3{
                     std::numeric_limits<float>::max() / 2.0F,
                     0.0F,
                     0.0F})
                 .has_value(),
        "invalid, overflowing delta, or overflowing reduction was accepted");
}

void testAircraftFactorRecovery() {
    const auto recovered =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.5F, 0.99F, 1.5F},
            0.1F,
            1.0F,
            false);
    require(recovered.has_value(), "valid factor recovery failed");
    requireVec(
        *recovered,
        Vec3{0.525F, 1.0F, 1.0F},
        "recovery increment or clamp changed");
    const auto rateProbe =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{}, 1.0F, 1.0F, false);
    require(
        rateProbe.has_value() &&
            std::bit_cast<std::uint32_t>(rateProbe->x) ==
                std::bit_cast<std::uint32_t>(0.25F),
        "aircraft recovery rate changed");

    const auto clearedByScalar =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.5F, 0.5F, 0.5F},
            0.1F,
            0.0F,
            false);
    const auto clearedByFlag =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.5F, 0.5F, 0.5F},
            0.1F,
            1.0F,
            true);
    require(
        clearedByScalar.has_value() &&
            clearedByFlag.has_value(),
        "valid gated recovery failed");
    requireVec(
        *clearedByScalar,
        Vec3{},
        "non-positive vehicle field did not clear factors");
    requireVec(
        *clearedByFlag,
        Vec3{},
        "vehicle flag did not clear factors");

    const auto negativeRefresh =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            Vec3{0.01F, 0.5F, 2.0F},
            -1.0F,
            1.0F,
            false);
    require(
        negativeRefresh.has_value(),
        "finite raw refresh argument was guessed invalid");
    requireVec(
        *negativeRefresh,
        Vec3{0.0F, 0.25F, 1.0F},
        "raw refresh algebra or clamps changed");

    require(
        !legacyAircraftRecoverGameplayCameraAxisFactors(
             Vec3{1.0F, 1.0F, 1.0F},
             std::numeric_limits<float>::quiet_NaN(),
             1.0F,
             false)
             .has_value() &&
            !legacyAircraftRecoverGameplayCameraAxisFactors(
                 Vec3{-std::numeric_limits<float>::max(),
                      1.0F,
                      1.0F},
                 -std::numeric_limits<float>::max(),
                 1.0F,
                 false)
                 .has_value(),
        "non-finite or overflowing recovery was accepted");
}

void testLineHitPoint() {
    const Vec3 anchor{10.0F, 20.0F, 30.0F};
    const Vec3 camera{14.0F, 12.0F, 34.0F};

    const auto start =
        legacyGameplayCameraLineHitPoint(anchor, camera, 0.0F);
    const auto middle =
        legacyGameplayCameraLineHitPoint(anchor, camera, 0.25F);
    const auto end =
        legacyGameplayCameraLineHitPoint(anchor, camera, 1.0F);
    require(
        start.has_value() && middle.has_value() &&
            end.has_value(),
        "valid line fractions failed");
    requireVec(*start, anchor, "zero fraction changed anchor");
    requireVec(
        *middle,
        Vec3{11.0F, 18.0F, 31.0F},
        "line interpolation order changed");
    requireVec(*end, camera, "unit fraction changed camera");

    require(
        !legacyGameplayCameraLineHitPoint(
             anchor, camera, -0.01F)
             .has_value() &&
            !legacyGameplayCameraLineHitPoint(
                 anchor, camera, 1.01F)
                 .has_value() &&
            !legacyGameplayCameraLineHitPoint(
                 anchor,
                 camera,
                 std::numeric_limits<float>::quiet_NaN())
                 .has_value() &&
            !legacyGameplayCameraLineHitPoint(
                 Vec3{-std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 Vec3{std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 0.5F)
                 .has_value(),
        "invalid fraction or overflowing segment was accepted");
}

void requireLooksAtPositiveZ(
    const Vec3 camera,
    const Vec3 anchor,
    const char* const message) {
    const auto lookAt =
        legacyGameplayCameraLookAt(anchor, camera);
    require(lookAt.has_value(), message);

    const auto transform = buildLegacyCameraTransform(
        LegacyCameraTransformConfig{
            .linear = lookAt->cameraWorldLinear,
            .translation = camera,
        });
    require(
        transform.complete() && transform.transform.has_value(),
        "look-at matrix was not a valid camera transform");
    const auto cameraSpace =
        transform.transform->transform(anchor);
    require(
        cameraSpace.complete() &&
            cameraSpace.cameraSpacePosition.has_value(),
        "look-at target did not transform");

    const float expectedDistance = std::sqrt(
        lookAt->direction.x * lookAt->direction.x +
        lookAt->direction.y * lookAt->direction.y +
        lookAt->direction.z * lookAt->direction.z);
    requireVec(
        *cameraSpace.cameraSpacePosition,
        Vec3{0.0F, 0.0F, expectedDistance},
        message,
        2.0e-5F);
}

void testLookAtAxesAndCameraConvention() {
    const auto forward = legacyGameplayCameraLookAt(
        Vec3{0.0F, 0.0F, 5.0F}, Vec3{});
    require(forward.has_value(), "forward look-at failed");
    requireVec(
        forward->axisRotationRadians,
        Vec3{},
        "forward look-at was not zero rotation");
    require(
        forward->cameraWorldLinear == Mat3{},
        "forward look-at was not identity");

    constexpr float halfPi = 1.57079632679489661923F;
    const auto right = legacyGameplayCameraLookAt(
        Vec3{5.0F, 0.0F, 0.0F}, Vec3{});
    const auto up = legacyGameplayCameraLookAt(
        Vec3{0.0F, 5.0F, 0.0F}, Vec3{});
    require(
        right.has_value() && up.has_value(),
        "axis-aligned look-at failed");
    require(
        close(right->axisRotationRadians.x, 0.0F) &&
            close(right->axisRotationRadians.y, halfPi),
        "right look-at yaw sign changed");
    require(
        close(up->axisRotationRadians.x, halfPi) &&
            close(up->axisRotationRadians.y, 0.0F),
        "up look-at pitch sign changed");

    requireLooksAtPositiveZ(
        Vec3{},
        Vec3{5.0F, 0.0F, 0.0F},
        "right target did not map to positive camera Z");
    requireLooksAtPositiveZ(
        Vec3{},
        Vec3{0.0F, 5.0F, 0.0F},
        "up target did not map to positive camera Z");
    requireLooksAtPositiveZ(
        Vec3{10.0F, -4.0F, 7.0F},
        Vec3{-2.0F, 5.0F, 19.0F},
        "diagonal target did not map to positive camera Z");

    const auto diagonal = legacyGameplayCameraLookAt(
        Vec3{1.0F, 1.0F, 1.0F}, Vec3{});
    require(
        diagonal.has_value(),
        "unit diagonal look-at failed");
    constexpr float inverseSqrt2 = 0.70710678118654752440F;
    constexpr float inverseSqrt3 = 0.57735026918962576451F;
    constexpr float inverseSqrt6 = 0.40824829046386301637F;
    requireVec(
        diagonal->cameraWorldLinear.columns[0],
        Vec3{inverseSqrt2, 0.0F, -inverseSqrt2},
        "diagonal camera right column changed",
        2.0e-6F);
    requireVec(
        diagonal->cameraWorldLinear.columns[1],
        Vec3{-inverseSqrt6, 2.0F * inverseSqrt6, -inverseSqrt6},
        "diagonal camera up column changed",
        2.0e-6F);
    requireVec(
        diagonal->cameraWorldLinear.columns[2],
        Vec3{inverseSqrt3, inverseSqrt3, inverseSqrt3},
        "diagonal camera forward column changed",
        2.0e-6F);

    require(
        !legacyGameplayCameraLookAt(Vec3{}, Vec3{})
             .has_value() &&
            !legacyGameplayCameraLookAt(
                 Vec3{std::numeric_limits<float>::infinity(),
                      0.0F,
                      0.0F},
                 Vec3{})
                 .has_value() &&
            !legacyGameplayCameraLookAt(
                 Vec3{std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F},
                 Vec3{-std::numeric_limits<float>::max(),
                      0.0F,
                      0.0F})
                 .has_value(),
        "degenerate, non-finite, or overflowing look-at was accepted");
}

void testPrimitivesDoNotAllocateOrMutateInputs() {
    const Vec3 factors{0.2F, 0.4F, 0.6F};
    const Vec3 original{1.0F, 2.0F, 3.0F};
    const Vec3 resolved{1.1F, 1.9F, 3.05F};
    const Vec3 anchor{4.0F, 5.0F, 6.0F};
    const Vec3 camera{-1.0F, 2.0F, -3.0F};

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    float checksum = 0.0F;
    bool complete = true;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const auto radius =
            legacyGameplayCameraCollisionSphereRadius(0.25F);
        const auto reduced =
            legacyGameplayCameraReduceCollisionAxisFactors(
                factors, original, resolved);
        const auto recovered =
            legacyAircraftRecoverGameplayCameraAxisFactors(
                factors, 0.1F, 1.0F, false);
        const auto hit = legacyGameplayCameraLineHitPoint(
            anchor, camera, 0.5F);
        const auto lookAt =
            legacyGameplayCameraLookAt(anchor, camera);
        if (!radius.has_value() || !reduced.has_value() ||
            !recovered.has_value() || !hit.has_value() ||
            !lookAt.has_value()) {
            complete = false;
            break;
        }
        checksum += *radius + reduced->x + recovered->y +
            hit->z + lookAt->cameraWorldLinear.columns[0].x;
    }
    trackAllocations.store(false, std::memory_order_release);

    require(
        complete && std::isfinite(checksum),
        "repeated primitive evaluation failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "camera collision primitives allocated");
    require(
        factors == Vec3{0.2F, 0.4F, 0.6F} &&
            original == Vec3{1.0F, 2.0F, 3.0F} &&
            resolved == Vec3{1.1F, 1.9F, 3.05F} &&
            anchor == Vec3{4.0F, 5.0F, 6.0F} &&
            camera == Vec3{-1.0F, 2.0F, -3.0F},
        "camera primitive mutated an input");
}

} // namespace

int main() {
    try {
        testRecoveredConstantsAndSphereRadius();
        testCollisionFactorReduction();
        testAircraftFactorRecovery();
        testLineHitPoint();
        testLookAtAxesAndCameraConvention();
        testPrimitivesDoNotAllocateOrMutateInputs();
        std::cout
            << "LegacyGameplayCameraCollision tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr
            << "LegacyGameplayCameraCollision tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
