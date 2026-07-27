#include "airfix/render/LegacyCanvasLayout.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

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

static_assert(noexcept(buildLegacyCanvasAspectFitLayout({})));
static_assert(
    !std::is_copy_assignable_v<LegacyCanvasAspectFitLayout> &&
    !std::is_move_assignable_v<LegacyCanvasAspectFitLayout>);

static_assert(recovered_legacy_canvas::width == 640.0F);
static_assert(recovered_legacy_canvas::height == 480.0F);
static_assert(recovered_legacy_canvas::gameplayLeft == 35.0F);
static_assert(recovered_legacy_canvas::gameplayTop == 35.0F);
static_assert(recovered_legacy_canvas::gameplayRight == 555.0F);
static_assert(recovered_legacy_canvas::gameplayBottom == 345.0F);
static_assert(recovered_legacy_canvas::gameplayWidth == 520.0F);
static_assert(recovered_legacy_canvas::gameplayHeight == 310.0F);
static_assert(
    recovered_legacy_canvas::gameplayCentre ==
    LegacyCanvasPoint{295.0F, 190.0F});

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 1.0e-4F) noexcept {
    return std::fabs(actual - expected) <= tolerance;
}

const LegacyCanvasAspectFitLayout& requireLayout(
    const LegacyCanvasAspectFitBuildResult& result,
    const char* const message) {
    require(
        result.complete() && result.layout.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.layout;
}

void requireBuildIssue(
    const LegacyCanvasRect targetRect,
    const LegacyCanvasLayoutBuildIssueKind expected,
    const char* const message) {
    const auto result =
        buildLegacyCanvasAspectFitLayout(targetRect);
    require(
        !result.complete() && !result.layout.has_value() &&
            result.issue.has_value() &&
            result.issue->kind == expected,
        message);
}

void testRecoveredConstantsAndFourByThreeTarget() {
    require(
        recovered_legacy_canvas::gameplayRect ==
            LegacyCanvasRect{35.0F, 35.0F, 520.0F, 310.0F},
        "recovered gameplay rectangle changed");

    const LegacyCanvasRect target{0.0F, 0.0F, 1280.0F, 960.0F};
    const auto built = buildLegacyCanvasAspectFitLayout(target);
    const auto& layout =
        requireLayout(built, "4:3 target was rejected");
    require(
        layout.targetRect() == target &&
            layout.fittedRect() == target &&
            layout.scale() == 2.0F,
        "4:3 target was not filled exactly");
}

void testWidescreenTarget() {
    const auto built = buildLegacyCanvasAspectFitLayout(
        {0.0F, 0.0F, 1920.0F, 1080.0F});
    const auto& layout =
        requireLayout(built, "1920x1080 target was rejected");
    require(
        layout.scale() == 2.25F &&
            layout.fittedRect() ==
                LegacyCanvasRect{
                    240.0F, 0.0F, 1440.0F, 1080.0F},
        "1920x1080 aspect-fit geometry changed");
}

void testPortraitTarget() {
    const auto built = buildLegacyCanvasAspectFitLayout(
        {0.0F, 0.0F, 1080.0F, 1920.0F});
    const auto& layout =
        requireLayout(built, "1080x1920 target was rejected");
    require(
        layout.scale() == 1.6875F &&
            layout.fittedRect() ==
                LegacyCanvasRect{
                    0.0F, 555.0F, 1080.0F, 810.0F},
        "1080x1920 aspect-fit geometry changed");
}

void testNonzeroTargetOrigin() {
    const LegacyCanvasRect target{
        100.0F, 200.0F, 1200.0F, 600.0F};
    const auto built = buildLegacyCanvasAspectFitLayout(target);
    const auto& layout =
        requireLayout(built, "offset target was rejected");
    require(
        close(layout.scale(), 1.25F) &&
            layout.fittedRect() ==
                LegacyCanvasRect{
                    300.0F, 200.0F, 800.0F, 600.0F},
        "nonzero target origin was not preserved while centring");
}

void testInvalidTargets() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    requireBuildIssue(
        {nan, 0.0F, 640.0F, 480.0F},
        LegacyCanvasLayoutBuildIssueKind::nonFiniteTargetRect,
        "NaN target origin was not rejected");
    requireBuildIssue(
        {0.0F, 0.0F, infinity, 480.0F},
        LegacyCanvasLayoutBuildIssueKind::nonFiniteTargetRect,
        "infinite target width was not rejected");
    requireBuildIssue(
        {0.0F, 0.0F, 0.0F, 480.0F},
        LegacyCanvasLayoutBuildIssueKind::targetExtentNotPositive,
        "zero target width was not rejected");
    requireBuildIssue(
        {0.0F, 0.0F, 640.0F, -1.0F},
        LegacyCanvasLayoutBuildIssueKind::targetExtentNotPositive,
        "negative target height was not rejected");
    requireBuildIssue(
        {
            std::numeric_limits<float>::max(),
            0.0F,
            640.0F,
            480.0F,
        },
        LegacyCanvasLayoutBuildIssueKind::nonFiniteDerivedLayout,
        "overflowing target right endpoint was not rejected");
    requireBuildIssue(
        {
            0.0F,
            std::numeric_limits<float>::max(),
            640.0F,
            480.0F,
        },
        LegacyCanvasLayoutBuildIssueKind::nonFiniteDerivedLayout,
        "overflowing target bottom endpoint was not rejected");
    requireBuildIssue(
        {
            0.0F,
            0.0F,
            std::numeric_limits<float>::denorm_min(),
            480.0F,
        },
        LegacyCanvasLayoutBuildIssueKind::nonFiniteDerivedLayout,
        "underflowing derived scale was not rejected");
}

void testBuildIsImmutableAndDoesNotAllocate() {
    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    bool allComplete = true;
    float checksum = 0.0F;
    for (std::size_t index = 1U; index <= 1024U; ++index) {
        const auto result = buildLegacyCanvasAspectFitLayout({
            static_cast<float>(index),
            static_cast<float>(index * 2U),
            1920.0F,
            1080.0F,
        });
        if (!result.complete()) {
            allComplete = false;
            break;
        }
        checksum += result.layout->fittedRect().x;
    }
    trackAllocations.store(false, std::memory_order_release);

    require(
        allComplete && std::isfinite(checksum),
        "repeated aspect-fit build failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "aspect-fit build performed a heap allocation");
}

} // namespace

int main() {
    try {
        testRecoveredConstantsAndFourByThreeTarget();
        testWidescreenTarget();
        testPortraitTarget();
        testNonzeroTargetOrigin();
        testInvalidTargets();
        testBuildIsImmutableAndDoesNotAllocate();
        std::cout << "LegacyCanvasLayout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "LegacyCanvasLayout tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
