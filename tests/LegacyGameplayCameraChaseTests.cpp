#include "airfix/render/LegacyGameplayCameraChase.hpp"

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

static_assert(noexcept(legacyGameplayCameraChaseStep(
    std::declval<const LegacyGameplayCameraChaseInput&>())));
static_assert(std::is_copy_constructible_v<
              LegacyGameplayCameraChaseInput>);
static_assert(std::is_copy_constructible_v<
              LegacyGameplayCameraChaseResult>);

constexpr float closeTargetBias =
    std::bit_cast<float>(std::uint32_t{0x3A83126FU});
constexpr float closeTargetDamping =
    std::bit_cast<float>(std::uint32_t{0x3F7D70A4U});

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

[[nodiscard]] LegacyGameplayCameraChaseStep requireStep(
    const LegacyGameplayCameraChaseInput& input,
    const char* const message) {
    const auto result = legacyGameplayCameraChaseStep(input);
    require(
        result.complete() && result.step.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.step;
}

[[nodiscard]] LegacyGameplayCameraChaseInput camera0Input() {
    const auto preset = legacyGameplayCameraPreset(0U, false);
    require(preset.has_value(), "camera0 preset missing");
    return LegacyGameplayCameraChaseInput{.preset = *preset};
}

void requireIssue(
    const LegacyGameplayCameraChaseInput& input,
    const LegacyGameplayCameraChaseIssueKind expected,
    const char* const message) {
    const auto result = legacyGameplayCameraChaseStep(input);
    require(
        !result.complete() && !result.step.has_value() &&
            result.issue.has_value() &&
            result.issue->kind == expected,
        message);
}

void testIdentityAtOrigin() {
    const auto input = camera0Input();
    const auto step = requireStep(input, "identity step failed");

    requireVec(
        step.worldOffset,
        Vec3{0.0F, 0.1F, -0.75F},
        "identity world offset changed");
    requireVec(
        step.target,
        Vec3{0.0F, 0.1F, -0.75F},
        "identity target changed");
    requireVec(
        step.unadjustedError,
        Vec3{0.0F, 0.1F, -0.75F},
        "identity error changed");
    require(
        !step.closeTargetErrorScale.has_value(),
        "equal-length error incorrectly took close branch");
    requireVec(
        step.candidateCameraPosition,
        Vec3{0.0F, 0.07F, -0.525F},
        "identity candidate changed");
}

void testExactTargetAndStrictThreshold() {
    auto atTarget = camera0Input();
    atTarget.currentCameraPosition =
        Vec3{0.0F, 0.1F, -0.75F};
    const auto zero =
        requireStep(atTarget, "zero-error step failed");
    require(
        zero.closeTargetErrorScale.has_value() &&
            close(
                *zero.closeTargetErrorScale,
                closeTargetBias * closeTargetDamping),
        "zero error did not take the close branch");
    requireVec(
        zero.candidateCameraPosition,
        atTarget.currentCameraPosition,
        "zero error moved the camera");

    LegacyGameplayCameraChaseInput equal{
        .preset = LegacyGameplayCameraPreset{
            .speed = 1.0F,
            .offset = {1.0F, 0.0F, 0.0F},
        },
    };
    const auto boundary =
        requireStep(equal, "threshold-equality step failed");
    require(
        !boundary.closeTargetErrorScale.has_value(),
        "strict threshold accepted equality");

    auto justBelow = equal;
    justBelow.currentCameraPosition.x =
        1.0F - std::nextafter(1.0F, 0.0F);
    const auto below =
        requireStep(justBelow, "below-threshold step failed");
    require(
        below.closeTargetErrorScale.has_value(),
        "value below threshold skipped close branch");
}

void testRuntimeColumnRotationAndWorldUpY() {
    const Mat3 quarterTurnY{{
        Vec3{0.0F, 0.0F, -1.0F},
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{1.0F, 0.0F, 0.0F},
    }};
    LegacyGameplayCameraChaseInput turnY{
        .vehicleRotation = quarterTurnY,
        .preset = LegacyGameplayCameraPreset{
            .speed = 0.0F,
            .offset = {0.0F, 0.0F, -1.0F},
        },
    };
    const auto yStep =
        requireStep(turnY, "Y-rotation step failed");
    requireVec(
        yStep.worldOffset,
        Vec3{-1.0F, 0.0F, 0.0F},
        "runtime-column rotation was transposed");

    const Mat3 quarterTurnZ{{
        Vec3{0.0F, 1.0F, 0.0F},
        Vec3{-1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
    LegacyGameplayCameraChaseInput turnZ{
        .vehicleRotation = quarterTurnZ,
        .preset = LegacyGameplayCameraPreset{
            .speed = 0.0F,
            .offset = {1.0F, 2.0F, 0.0F},
        },
    };
    const auto zStep =
        requireStep(turnZ, "Z-rotation step failed");
    requireVec(
        zStep.worldOffset,
        Vec3{0.0F, 3.0F, 0.0F},
        "offset Y was rotated instead of remaining world-up");
}

void testFactorsAndTranslation() {
    LegacyGameplayCameraChaseInput input{
        .vehiclePosition = {12.0F, 23.0F, 34.0F},
        .currentCameraPosition = {10.0F, 20.0F, 30.0F},
        .preset = LegacyGameplayCameraPreset{
            .speed = 0.5F,
            .offset = {0.0F, 0.0F, 0.0F},
        },
        .axisFactors = {0.0F, 0.5F, 1.0F},
    };
    const auto step =
        requireStep(input, "factor/translation step failed");
    requireVec(
        step.target,
        Vec3{12.0F, 23.0F, 34.0F},
        "absolute target changed");
    requireVec(
        step.candidateCameraPosition,
        Vec3{10.0F, 20.75F, 32.0F},
        "factor-speed-error order changed");
}

void testFiniteNonRotationIsNotSilentlyRepaired() {
    LegacyGameplayCameraChaseInput input{
        .vehicleRotation = Mat3{{
            Vec3{2.0F, 0.0F, 0.0F},
            Vec3{0.5F, 1.0F, 0.0F},
            Vec3{0.0F, 0.0F, -1.0F},
        }},
        .preset = LegacyGameplayCameraPreset{
            .speed = 0.0F,
            .offset = {1.0F, 2.0F, 3.0F},
        },
    };
    const auto step =
        requireStep(input, "finite raw matrix was rejected");
    requireVec(
        step.worldOffset,
        Vec3{2.0F, 2.0F, -3.0F},
        "finite raw matrix was normalized or transposed");
}

void testInvalidInputAndOverflowFailAtomically() {
    auto nonFinite = camera0Input();
    nonFinite.axisFactors.y =
        std::numeric_limits<float>::quiet_NaN();
    requireIssue(
        nonFinite,
        LegacyGameplayCameraChaseIssueKind::nonFiniteInput,
        "non-finite input was accepted");

    auto overflow = camera0Input();
    overflow.vehiclePosition.x =
        std::numeric_limits<float>::max();
    overflow.preset.offset.x =
        std::numeric_limits<float>::max();
    requireIssue(
        overflow,
        LegacyGameplayCameraChaseIssueKind::
            nonFiniteDerivedValue,
        "derived overflow was accepted");
}

void testInputIsUnchangedAndStepDoesNotAllocate() {
    auto input = camera0Input();
    input.vehiclePosition = {5.0F, 6.0F, 7.0F};
    input.currentCameraPosition = {1.0F, 2.0F, 3.0F};
    input.axisFactors = {0.25F, 0.5F, 0.75F};
    const auto original = input;

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    float checksum = 0.0F;
    bool allComplete = true;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const auto result = legacyGameplayCameraChaseStep(input);
        if (!result.step.has_value()) {
            allComplete = false;
            break;
        }
        checksum += result.step->candidateCameraPosition.x;
    }
    trackAllocations.store(false, std::memory_order_release);

    require(
        allComplete && std::isfinite(checksum),
        "repeated chase step failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "chase step allocated");
    require(input == original, "chase step mutated its input");
}

} // namespace

int main() {
    try {
        testIdentityAtOrigin();
        testExactTargetAndStrictThreshold();
        testRuntimeColumnRotationAndWorldUpY();
        testFactorsAndTranslation();
        testFiniteNonRotationIsNotSilentlyRepaired();
        testInvalidInputAndOverflowFailAtomically();
        testInputIsUnchangedAndStepDoesNotAllocate();
        std::cout << "LegacyGameplayCameraChase tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "LegacyGameplayCameraChase tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
