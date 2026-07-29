#include "airfix/render/NativeRenderLayout.hpp"

#include <array>
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

static_assert(noexcept(buildNativeRenderLayout({})));
static_assert(
    !std::is_copy_assignable_v<NativeRenderLayout> &&
    !std::is_move_assignable_v<NativeRenderLayout>);
static_assert(
    native_render_policy::minimumRenderScalePercent == 50.0F &&
    native_render_policy::maximumRenderScalePercent == 200.0F);

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 1.0e-3F) noexcept {
    return std::fabs(actual - expected) <= tolerance;
}

const NativeRenderLayout& requireLayout(
    const NativeRenderLayoutBuildResult& result,
    const char* const message) {
    require(
        result.complete() && result.layout.has_value() &&
            !result.issue.has_value(),
        message);
    return *result.layout;
}

void requireIssue(
    const NativeRenderLayoutConfig& config,
    const NativeRenderLayoutBuildIssueKind expected,
    const char* const message) {
    const auto result = buildNativeRenderLayout(config);
    require(
        !result.complete() && !result.layout.has_value() &&
            result.issue.has_value() &&
            result.issue->kind == expected,
        message);
}

void testFourKAtOneHundredPercentIsNative() {
    const auto built = buildNativeRenderLayout({
        .outputExtent = {3840U, 2160U},
    });
    const auto& layout = requireLayout(
        built,
        "4K native layout was rejected");

    require(
        layout.outputExtent() == OutputPixelExtent{3840U, 2160U} &&
            layout.renderTargetExtent() ==
                RenderTargetPixelExtent{3840U, 2160U} &&
            layout.renderScalePercent() == 100.0F &&
            layout.sceneViewportInRenderTarget() ==
                RenderTargetPixelRect{
                    0.0F, 0.0F, 3840.0F, 2160.0F},
        "4K at 100% did not preserve the exact native raster extent");
    require(
        close(layout.cameraLogicalExtent().width, 2560.0F / 3.0F) &&
            layout.cameraLogicalExtent().height == 480.0F &&
            close(layout.cameraLogicalCentre().x, 1280.0F / 3.0F) &&
            layout.cameraLogicalCentre().y == 240.0F &&
            layout.mapReferenceCameraPoint({300.0F, 210.0F}) ==
                CameraLogicalPoint{
                    300.0F + 320.0F / 3.0F, 210.0F},
        "4K physical pixels leaked into the logical camera canvas");
}

void testRenderScaleIsIndependentOfOutputAndUi() {
    const auto halfBuilt = buildNativeRenderLayout({
        .outputExtent = {3840U, 2160U},
        .renderScalePercent = 50.0F,
    });
    const auto doubleBuilt = buildNativeRenderLayout({
        .outputExtent = {3840U, 2160U},
        .renderScalePercent = 200.0F,
    });
    const auto& half = requireLayout(
        halfBuilt,
        "50% render scale was rejected");
    const auto& doubled = requireLayout(
        doubleBuilt,
        "200% render scale was rejected");

    require(
        half.renderTargetExtent() ==
                RenderTargetPixelExtent{1920U, 1080U} &&
            doubled.renderTargetExtent() ==
                RenderTargetPixelExtent{7680U, 4320U} &&
            half.outputExtent() == doubled.outputExtent() &&
            half.uiViewportInOutput() == doubled.uiViewportInOutput() &&
            half.cameraLogicalExtent() == doubled.cameraLogicalExtent(),
        "render scale changed output, UI, or camera logical coordinates");
}

void testRequiredAspectRatiosUseHorPlus() {
    struct Case final {
        OutputPixelExtent output;
        float aspect;
    };
    constexpr std::array<Case, 6U> cases{{
        {{1600U, 1200U}, 4.0F / 3.0F},
        {{1920U, 1200U}, 16.0F / 10.0F},
        {{1920U, 1080U}, 16.0F / 9.0F},
        {{2535U, 1170U}, 19.5F / 9.0F},
        {{2520U, 1080U}, 21.0F / 9.0F},
        {{3840U, 1080U}, 32.0F / 9.0F},
    }};

    float previousHorizontalFov = 0.0F;
    for (const auto& test : cases) {
        const auto built = buildNativeRenderLayout({
            .outputExtent = test.output,
        });
        const auto& layout = requireLayout(
            built,
            "required Hor+ aspect ratio was rejected");
        const auto viewport = layout.sceneViewportInRenderTarget();
        require(
            viewport ==
                RenderTargetPixelRect{
                    0.0F,
                    0.0F,
                    static_cast<float>(test.output.width),
                    static_cast<float>(test.output.height),
                },
            "Hor+ did not use the complete render target");
        require(
            close(
                layout.cameraLogicalExtent().width,
                recovered_legacy_canvas::height * test.aspect) &&
                layout.cameraLogicalExtent().height ==
                    recovered_legacy_canvas::height &&
                close(layout.verticalFovDegrees(), 73.7398F, 1.0e-2F) &&
                layout.horizontalFovDegrees() >= previousHorizontalFov,
            "Hor+ changed vertical FOV or failed to expose more horizontally");
        previousHorizontalFov = layout.horizontalFovDegrees();
    }
}

void testOriginalFourByThreeRemainsAComparisonMode() {
    const auto built = buildNativeRenderLayout({
        .outputExtent = {1920U, 1080U},
        .scenePresentation =
            ScenePresentationMode::originalFourByThree,
    });
    const auto& layout = requireLayout(
        built,
        "Original 4:3 comparison layout was rejected");

    require(
        layout.sceneViewportInRenderTarget() ==
                RenderTargetPixelRect{
                    240.0F, 0.0F, 1440.0F, 1080.0F} &&
            layout.cameraLogicalExtent() ==
                CameraLogicalExtent{640.0F, 480.0F} &&
            layout.cameraLogicalCentre() ==
                CameraLogicalPoint{320.0F, 240.0F} &&
            close(layout.horizontalFovDegrees(), 90.0F) &&
            close(layout.verticalFovDegrees(), 73.7398F, 1.0e-2F),
        "Original 4:3 did not preserve the reference projection");
}

void testUiUsesOutputSafeAreaInsteadOfRenderTarget() {
    const auto built = buildNativeRenderLayout({
        .outputExtent = {2400U, 1200U},
        .renderScalePercent = 50.0F,
        .uiDesignExtent = {1920.0F, 1080.0F},
        .outputSafeArea =
            {
                .left = 120.0F,
                .top = 60.0F,
                .right = 120.0F,
                .bottom = 60.0F,
            },
    });
    const auto& layout = requireLayout(
        built,
        "safe-area UI layout was rejected");

    require(
        layout.renderTargetExtent() ==
                RenderTargetPixelExtent{1200U, 600U} &&
            layout.uiSafeRectInOutput() ==
                OutputPixelRect{
                    120.0F, 60.0F, 2160.0F, 1080.0F} &&
            layout.uiViewportInOutput() ==
                OutputPixelRect{
                    240.0F, 60.0F, 1920.0F, 1080.0F} &&
            layout.uiDesignExtent() ==
                UiLogicalExtent{1920.0F, 1080.0F} &&
            layout.uiScale() == 1.0F,
        "UI was scaled in render-target pixels or ignored the safe area");

    require(
        layout.cameraPointFromOutput({1200.0F, 600.0F}) ==
                std::optional<CameraLogicalPoint>{
                    layout.cameraLogicalCentre()} &&
            layout.uiPointFromOutput({240.0F, 60.0F}) ==
                std::optional<UiLogicalPoint>{
                    UiLogicalPoint{0.0F, 0.0F}} &&
            layout.uiPointFromOutput({1200.0F, 600.0F}) ==
                std::optional<UiLogicalPoint>{
                    UiLogicalPoint{960.0F, 540.0F}} &&
            layout.outputPointFromUi({960.0F, 540.0F}) ==
                std::optional<OutputPixelPoint>{
                    OutputPixelPoint{1200.0F, 600.0F}} &&
            !layout.uiPointFromOutput({100.0F, 600.0F})
                 .has_value(),
        "output, camera, and UI coordinate mapping was not reversible");
}

void testInvalidInputsFailAtomically() {
    requireIssue(
        {},
        NativeRenderLayoutBuildIssueKind::outputExtentNotPositive,
        "zero output extent was not rejected");

    auto config = NativeRenderLayoutConfig{
        .outputExtent = {1920U, 1080U},
    };
    config.renderScalePercent =
        std::numeric_limits<float>::quiet_NaN();
    requireIssue(
        config,
        NativeRenderLayoutBuildIssueKind::nonFiniteConfig,
        "NaN render scale was not rejected");

    config = {.outputExtent = {1920U, 1080U}};
    config.renderScalePercent = 49.0F;
    requireIssue(
        config,
        NativeRenderLayoutBuildIssueKind::renderScaleOutOfRange,
        "render scale below 50% was not rejected");

    config = {.outputExtent = {1920U, 1080U}};
    config.referenceCameraCanvas.height = 0.0F;
    requireIssue(
        config,
        NativeRenderLayoutBuildIssueKind::logicalExtentNotPositive,
        "zero logical camera height was not rejected");

    config = {.outputExtent = {1920U, 1080U}};
    config.referenceHorizontalFovDegrees = 180.0F;
    requireIssue(
        config,
        NativeRenderLayoutBuildIssueKind::referenceFovOutOfRange,
        "unsafe reference FOV was not rejected");

    config = {.outputExtent = {1920U, 1080U}};
    config.outputSafeArea = {
        .left = 960.0F,
        .right = 960.0F,
    };
    requireIssue(
        config,
        NativeRenderLayoutBuildIssueKind::invalidSafeArea,
        "empty safe area was not rejected");

    config = {
        .outputExtent =
            {
                std::numeric_limits<std::uint32_t>::max(),
                1080U,
            },
        .renderScalePercent = 200.0F,
    };
    requireIssue(
        config,
        NativeRenderLayoutBuildIssueKind::renderTargetExtentOverflow,
        "overflowing render target was not rejected");
}

void testBuildDoesNotAllocate() {
    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    bool complete = true;
    float checksum = 0.0F;
    for (std::size_t index = 0U; index < 1024U; ++index) {
        const auto result = buildNativeRenderLayout({
            .outputExtent = {5120U, 1440U},
            .renderScalePercent = 100.0F,
            .outputSafeArea =
                {
                    .left = static_cast<float>(index % 10U),
                    .right = static_cast<float>(index % 10U),
                },
        });
        if (!result.complete()) {
            complete = false;
            break;
        }
        checksum += result.layout->cameraLogicalExtent().width;
    }
    trackAllocations.store(false, std::memory_order_release);

    require(
        complete && std::isfinite(checksum),
        "repeated native render layout build failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "native render layout build allocated");
}

} // namespace

int main() {
    try {
        testFourKAtOneHundredPercentIsNative();
        testRenderScaleIsIndependentOfOutputAndUi();
        testRequiredAspectRatiosUseHorPlus();
        testOriginalFourByThreeRemainsAComparisonMode();
        testUiUsesOutputSafeAreaInsteadOfRenderTarget();
        testInvalidInputsFailAtomically();
        testBuildDoesNotAllocate();
        std::cout << "NativeRenderLayout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "NativeRenderLayout tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
