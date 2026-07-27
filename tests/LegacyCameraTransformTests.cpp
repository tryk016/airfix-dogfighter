#include "airfix/render/LegacyCameraTransform.hpp"

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

static_assert(noexcept(buildLegacyCameraTransform()));
static_assert(noexcept(
    std::declval<const LegacyCameraTransform&>().transform(Vec3{})));
static_assert(
    !std::is_copy_assignable_v<LegacyCameraTransform> &&
    !std::is_move_assignable_v<LegacyCameraTransform>);
static_assert(std::is_same_v<
    decltype(std::declval<const LegacyCameraTransform&>().linear()),
    Mat3>);

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

void requireVec(
    const Vec3 actual,
    const Vec3 expected,
    const char* const message) {
    require(
        close(actual.x, expected.x) &&
            close(actual.y, expected.y) &&
            close(actual.z, expected.z),
        message);
}

const LegacyCameraTransform& requireTransform(
    const LegacyCameraTransformBuildResult& result,
    const char* const message) {
    require(
        result.complete() && result.transform.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.transform;
}

Vec3 requirePosition(
    const LegacyCameraTransformResult& result,
    const char* const message) {
    require(
        result.complete() && result.cameraSpacePosition.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.cameraSpacePosition;
}

void requireBuildIssue(
    const LegacyCameraTransformConfig& config,
    const LegacyCameraTransformBuildIssueKind expected,
    const char* const message) {
    const auto result = buildLegacyCameraTransform(config);
    require(
        !result.complete() && !result.transform.has_value() &&
            result.issue.has_value() &&
            result.issue->kind == expected,
        message);
}

[[nodiscard]] Mat3 positiveNinetyZ() noexcept {
    return Mat3{{
        Vec3{0.0F, -1.0F, 0.0F},
        Vec3{1.0F, 0.0F, 0.0F},
        Vec3{0.0F, 0.0F, 1.0F},
    }};
}

void testIdentityAndAxes() {
    const auto built = buildLegacyCameraTransform();
    const auto& transform =
        requireTransform(built, "identity transform was rejected");
    require(
        transform.linear() == Mat3{} &&
            transform.translation() == Vec3{} &&
            transform.uniformScale() == 1.0F &&
            transform.inverseScaleSquared() == 1.0F,
        "identity validated configuration changed");
    requireVec(
        requirePosition(
            transform.transform({3.0F, -5.0F, 7.0F}),
            "identity point was rejected"),
        {3.0F, -5.0F, 7.0F},
        "identity transform changed a point");
    requireVec(
        requirePosition(
            transform.transform({1.0F, 0.0F, 0.0F}),
            "X axis was rejected"),
        {1.0F, 0.0F, 0.0F},
        "identity transform changed X");
    requireVec(
        requirePosition(
            transform.transform({0.0F, 1.0F, 0.0F}),
            "Y axis was rejected"),
        {0.0F, 1.0F, 0.0F},
        "identity transform changed Y");
    requireVec(
        requirePosition(
            transform.transform({0.0F, 0.0F, 1.0F}),
            "Z axis was rejected"),
        {0.0F, 0.0F, 1.0F},
        "identity transform changed Z");
}

void testLinearGetterFromTemporaryBuildResult() {
    const Mat3 linear =
        buildLegacyCameraTransform().transform.value().linear();
    require(
        linear == Mat3{},
        "linear getter did not safely copy from a temporary build result");
}

void testAsymmetricRotationDetectsTranspose() {
    const auto built = buildLegacyCameraTransform({
        .linear = positiveNinetyZ(),
    });
    const auto& transform =
        requireTransform(built, "90-degree transform was rejected");
    requireVec(
        requirePosition(
            transform.transform({1.0F, 0.0F, 0.0F}),
            "rotated X was rejected"),
        {0.0F, 1.0F, 0.0F},
        "legacy row path was transposed");
    requireVec(
        requirePosition(
            transform.transform({0.0F, 1.0F, 0.0F}),
            "rotated Y was rejected"),
        {-1.0F, 0.0F, 0.0F},
        "asymmetric rotation did not preserve legacy columns");
}

void testPositiveAndNegativeScale() {
    const auto positiveBuilt = buildLegacyCameraTransform({
        .linear = Mat3{{
            Vec3{2.0F, 0.0F, 0.0F},
            Vec3{0.0F, 2.0F, 0.0F},
            Vec3{0.0F, 0.0F, 2.0F},
        }},
        .uniformScale = 2.0F,
        .inverseScaleSquared = 0.25F,
    });
    const auto& positive = requireTransform(
        positiveBuilt, "positive scale was rejected");
    requireVec(
        requirePosition(
            positive.transform({4.0F, 6.0F, 8.0F}),
            "positive scaled point was rejected"),
        {2.0F, 3.0F, 4.0F},
        "positive scale path changed");

    const auto negativeBuilt = buildLegacyCameraTransform({
        .linear = Mat3{{
            Vec3{-2.0F, 0.0F, 0.0F},
            Vec3{0.0F, -2.0F, 0.0F},
            Vec3{0.0F, 0.0F, -2.0F},
        }},
        .uniformScale = -2.0F,
        .inverseScaleSquared = 0.25F,
    });
    const auto& negative = requireTransform(
        negativeBuilt, "negative uniform scale was rejected");
    requireVec(
        requirePosition(
            negative.transform({4.0F, 6.0F, 8.0F}),
            "negative scaled point was rejected"),
        {-2.0F, -3.0F, -4.0F},
        "negative scale did not retain signed linear columns");
}

void testTranslationThenRotationOrder() {
    const auto built = buildLegacyCameraTransform({
        .linear = positiveNinetyZ(),
        .translation = {10.0F, 20.0F, 30.0F},
    });
    const auto& transform =
        requireTransform(built, "translated rotation was rejected");
    requireVec(
        requirePosition(
            transform.transform({12.0F, 23.0F, 34.0F}),
            "translated/rotated point was rejected"),
        {-3.0F, 2.0F, 4.0F},
        "world position was not translated before three row dots");
}

void testInvalidConfigurations() {
    auto config = LegacyCameraTransformConfig{};
    config.linear.columns[0].x =
        std::numeric_limits<float>::quiet_NaN();
    requireBuildIssue(
        config,
        LegacyCameraTransformBuildIssueKind::nonFiniteConfig,
        "NaN linear value was not rejected");

    config = {};
    config.translation.z = std::numeric_limits<float>::infinity();
    requireBuildIssue(
        config,
        LegacyCameraTransformBuildIssueKind::nonFiniteConfig,
        "infinite translation was not rejected");

    config = {};
    config.uniformScale = 0.0F;
    requireBuildIssue(
        config,
        LegacyCameraTransformBuildIssueKind::zeroUniformScale,
        "zero uniform scale was not rejected");

    config = {};
    config.inverseScaleSquared = 0.0F;
    requireBuildIssue(
        config,
        LegacyCameraTransformBuildIssueKind::
            inverseScaleSquaredNotPositive,
        "zero inverse scale squared was not rejected");

    config = {};
    config.linear.columns[1] = {0.01F, 1.0F, 0.0F};
    requireBuildIssue(
        config,
        LegacyCameraTransformBuildIssueKind::
            nonOrthogonalLinearColumns,
        "sheared columns were not rejected before length validation");

    config = {};
    config.linear.columns[1] = {0.0F, 2.0F, 0.0F};
    requireBuildIssue(
        config,
        LegacyCameraTransformBuildIssueKind::
            linearColumnLengthMismatch,
        "non-uniform column lengths were not rejected");

    config = {};
    config.linear = Mat3{{
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 2.0F, 0.0F},
        Vec3{0.0F, 0.0F, 2.0F},
    }};
    config.uniformScale = 2.0F;
    config.inverseScaleSquared = 0.3F;
    requireBuildIssue(
        config,
        LegacyCameraTransformBuildIssueKind::
            inverseScaleSquaredMismatch,
        "inconsistent inverse scale squared was not rejected");
}

void testTinyScaleUsesNormalizedGram() {
    constexpr float scale = 1.0e-5F;
    constexpr float inverseScaleSquared = 1.0e10F;
    const auto validBuilt = buildLegacyCameraTransform({
        .linear = Mat3{{
            Vec3{scale, 0.0F, 0.0F},
            Vec3{0.0F, scale, 0.0F},
            Vec3{0.0F, 0.0F, scale},
        }},
        .uniformScale = scale,
        .inverseScaleSquared = inverseScaleSquared,
    });
    const auto& valid = requireTransform(
        validBuilt, "valid tiny-scale transform was rejected");
    const auto position = requirePosition(
        valid.transform({1.0F, 2.0F, 3.0F}),
        "valid tiny-scale point was rejected");
    require(
        close(position.x, 100'000.0F, 1.0F) &&
            close(position.y, 200'000.0F, 1.0F) &&
            close(position.z, 300'000.0F, 1.0F),
        "tiny-scale transform produced an unexpected finite result");

    auto invalid = LegacyCameraTransformConfig{
        .linear = Mat3{{
            Vec3{scale, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, scale},
        }},
        .uniformScale = scale,
        .inverseScaleSquared = inverseScaleSquared,
    };
    requireBuildIssue(
        invalid,
        LegacyCameraTransformBuildIssueKind::
            linearColumnLengthMismatch,
        "normalized Gram accepted a zero tiny-scale column");
}

void testScaleRangeRejectsSubnormalInverseScaleSquared() {
    constexpr float unsupportedScale = 1.0e21F;
    requireBuildIssue(
        {
            .linear = Mat3{{
                Vec3{unsupportedScale, 0.0F, 0.0F},
                Vec3{0.0F, unsupportedScale, 0.0F},
                Vec3{0.0F, 0.0F, unsupportedScale},
            }},
            .uniformScale = unsupportedScale,
            .inverseScaleSquared = 1.0e-42F,
        },
        LegacyCameraTransformBuildIssueKind::
            scaleOutsideValidatedRange,
        "scale requiring subnormal inverse scale squared was accepted");

    constexpr float supportedScale = 1.0e18F;
    const auto supported = buildLegacyCameraTransform({
        .linear = Mat3{{
            Vec3{supportedScale, 0.0F, 0.0F},
            Vec3{0.0F, supportedScale, 0.0F},
            Vec3{0.0F, 0.0F, supportedScale},
        }},
        .uniformScale = supportedScale,
        .inverseScaleSquared = 1.0e-36F,
    });
    require(
        supported.complete(),
        "large scale with normal inverse scale squared was rejected");
}

void testInvalidInputsAndOutputOverflow() {
    const auto built = buildLegacyCameraTransform();
    const auto& transform =
        requireTransform(built, "identity transform was rejected");
    const auto nanResult = transform.transform({
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    const auto infinityResult = transform.transform({
        0.0F, std::numeric_limits<float>::infinity(), 0.0F});
    require(
        !nanResult.complete() && !nanResult.cameraSpacePosition &&
            nanResult.issue.has_value() &&
            nanResult.issue->kind ==
                LegacyCameraTransformIssueKind::nonFiniteInput &&
            !infinityResult.complete() &&
            !infinityResult.cameraSpacePosition &&
            infinityResult.issue.has_value() &&
            infinityResult.issue->kind ==
                LegacyCameraTransformIssueKind::nonFiniteInput,
        "non-finite world input was not rejected");

    const auto overflowBuilt = buildLegacyCameraTransform({
        .translation = {
            -std::numeric_limits<float>::max(), 0.0F, 0.0F},
    });
    const auto& overflowTransform = requireTransform(
        overflowBuilt, "finite overflow configuration was rejected");
    const auto overflow = overflowTransform.transform({
        std::numeric_limits<float>::max(), 0.0F, 0.0F});
    require(
        !overflow.complete() && !overflow.cameraSpacePosition &&
            overflow.issue.has_value() &&
            overflow.issue->kind ==
                LegacyCameraTransformIssueKind::nonFiniteOutput,
        "non-finite transform output was not rejected atomically");
}

void testBuildAndTransformDoNotAllocate() {
    const auto built = buildLegacyCameraTransform();
    const auto& transform =
        requireTransform(built, "identity transform was rejected");

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    bool allComplete = true;
    float checksum = 0.0F;
    for (std::size_t index = 0U; index < 1024U; ++index) {
        const auto rebuilt = buildLegacyCameraTransform();
        const auto transformed = transform.transform({
            static_cast<float>(index),
            -static_cast<float>(index),
            static_cast<float>(index + 1U),
        });
        if (!rebuilt.complete() || !transformed.complete()) {
            allComplete = false;
            break;
        }
        checksum += transformed.cameraSpacePosition->z;
    }
    trackAllocations.store(false, std::memory_order_release);
    const auto observed =
        allocationCount.load(std::memory_order_relaxed);
    require(
        allComplete && std::isfinite(checksum),
        "repeated build/transform failed");
    require(
        observed == 0U,
        "builder or transform performed a heap allocation");
}

} // namespace

int main() {
    try {
        testIdentityAndAxes();
        testLinearGetterFromTemporaryBuildResult();
        testAsymmetricRotationDetectsTranspose();
        testPositiveAndNegativeScale();
        testTranslationThenRotationOrder();
        testInvalidConfigurations();
        testTinyScaleUsesNormalizedGram();
        testScaleRangeRejectsSubnormalInverseScaleSquared();
        testInvalidInputsAndOutputOverflow();
        testBuildAndTransformDoNotAllocate();
        std::cout << "LegacyCameraTransform tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "LegacyCameraTransform tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
