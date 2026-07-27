#include "airfix/render/LegacyQuaternionRotation.hpp"

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

static_assert(noexcept(legacyQuaternionRotation(
    std::declval<const LegacyQuaternion&>())));
static_assert(std::is_copy_constructible_v<LegacyQuaternion>);
static_assert(std::is_copy_constructible_v<
              LegacyQuaternionRotationResult>);

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] Mat3 requireRotation(
    const LegacyQuaternion quaternion,
    const char* const message) {
    const auto result = legacyQuaternionRotation(quaternion);
    require(
        result.complete() && result.rotation.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.rotation;
}

void requireIssue(
    const LegacyQuaternion quaternion,
    const LegacyQuaternionRotationIssueKind expected,
    const char* const message) {
    const auto result = legacyQuaternionRotation(quaternion);
    require(
        !result.complete() && !result.rotation.has_value() &&
            result.issue.has_value() &&
            result.issue->kind == expected,
        message);
}

void testIdentityAndZeroQuaternion() {
    const Mat3 identity{};
    require(
        requireRotation({}, "identity quaternion failed") ==
            identity,
        "identity quaternion matrix changed");
    require(
        requireRotation(
            LegacyQuaternion{0.0F, 0.0F, 0.0F, 0.0F},
            "zero quaternion failed") == identity,
        "zero quaternion was normalized or rejected");
}

void testGoldenAsymmetricQuaternion() {
    const auto rotation = requireRotation(
        LegacyQuaternion{0.5F, -0.25F, 0.75F, -1.25F},
        "asymmetric quaternion failed");
    require(
        rotation == Mat3{{
            Vec3{-3.25F, -1.625F, -0.125F},
            Vec3{0.875F, -2.25F, -2.125F},
            Vec3{1.375F, -1.625F, -0.25F},
        }},
        "asymmetric quaternion field order changed");
}

void testNonUnitQuaternionIsNotNormalized() {
    const auto rotation = requireRotation(
        LegacyQuaternion{2.0F, 1.0F, 0.0F, 0.0F},
        "non-unit quaternion failed");
    require(
        rotation == Mat3{{
            Vec3{1.0F, 0.0F, 0.0F},
            Vec3{0.0F, -1.0F, 4.0F},
            Vec3{0.0F, -4.0F, -1.0F},
        }},
        "non-unit quaternion was normalized");
}

void testWidenedIntermediateRounding() {
    const auto rotation = requireRotation(
        LegacyQuaternion{
            std::bit_cast<float>(std::uint32_t{0x3F08596CU}),
            std::bit_cast<float>(std::uint32_t{0x3F0885DBU}),
            std::bit_cast<float>(std::uint32_t{0xBF96017EU}),
            std::bit_cast<float>(std::uint32_t{0xBFF33AC5U}),
        },
        "rounding discriminator quaternion failed");
    require(
        std::bit_cast<std::uint32_t>(rotation.columns[0].x) ==
            0xC10F7F30U,
        "matrix regressed to premature binary32 rounding");
}

void testRuntimeColumnConvention() {
    const float halfRoot = std::sqrt(0.5F);
    const auto rotation = requireRotation(
        LegacyQuaternion{halfRoot, 0.0F, halfRoot, 0.0F},
        "quarter-turn quaternion failed");
    const Vec3 result =
        applyRuntimeColumn(rotation, Vec3{0.0F, 0.0F, -1.0F});
    require(
        std::abs(result.x + 1.0F) <= 2.0e-6F &&
            std::abs(result.y) <= 2.0e-6F &&
            std::abs(result.z) <= 2.0e-6F,
        "quaternion matrix was transposed");
}

void testInvalidInputAndOverflowFailAtomically() {
    requireIssue(
        LegacyQuaternion{
            1.0F,
            std::numeric_limits<float>::quiet_NaN(),
            0.0F,
            0.0F,
        },
        LegacyQuaternionRotationIssueKind::nonFiniteInput,
        "non-finite quaternion was accepted");

    const float maximum = std::numeric_limits<float>::max();
    requireIssue(
        LegacyQuaternion{maximum, maximum, maximum, maximum},
        LegacyQuaternionRotationIssueKind::nonFiniteOutput,
        "matrix output overflow was accepted");
}

void testInputIsUnchangedAndConversionDoesNotAllocate() {
    const LegacyQuaternion input{0.5F, -0.25F, 0.75F, -1.25F};
    const auto original = input;

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    float checksum = 0.0F;
    bool allComplete = true;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const auto result = legacyQuaternionRotation(input);
        if (!result.rotation.has_value()) {
            allComplete = false;
            break;
        }
        checksum += result.rotation->columns[index % 3U].x;
    }
    trackAllocations.store(false, std::memory_order_release);

    require(
        allComplete && std::isfinite(checksum),
        "repeated quaternion conversion failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "quaternion conversion allocated");
    require(input == original, "quaternion conversion mutated input");
}

} // namespace

int main() {
    try {
        testIdentityAndZeroQuaternion();
        testGoldenAsymmetricQuaternion();
        testNonUnitQuaternionIsNotNormalized();
        testWidenedIntermediateRounding();
        testRuntimeColumnConvention();
        testInvalidInputAndOverflowFailAtomically();
        testInputIsUnchangedAndConversionDoesNotAllocate();
        std::cout << "LegacyQuaternionRotation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "LegacyQuaternionRotation tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
