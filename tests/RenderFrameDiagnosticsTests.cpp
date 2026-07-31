#include "airfix/render/RenderFrameDiagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

using namespace airfix::render;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] RenderFrameDiagnosticSample sample() {
    return {
        .outputExtent = {1920U, 1080U},
        .renderTargetExtent = {1920U, 1080U},
        .renderScalePercent = 100.0F,
        .frameIntervalMilliseconds = 1000.0 / 60.0,
        .cpuFrameMilliseconds = 4.0,
        .gpuFrameMilliseconds = 3.0,
        .drawCallCount = 122U,
        .sceneDrawCallCount = 120U,
        .triangleCount = 170'041U,
        .sceneTriangleCount = 170'039U,
        .activeLightCount = 2U,
        .gpuMemoryBytes = 96U * 1024U * 1024U,
        .gpuMemoryMeasurement = GpuMemoryMeasurement::estimated,
    };
}

void testValidationAndAtomicPublication() {
    RenderFrameDiagnosticsAccumulator accumulator;
    auto valid = sample();
    require(accumulator.record(valid), "valid sample was rejected");
    const auto baseline = accumulator.latest();
    require(baseline.has_value(), "valid sample was not published");

    auto invalid = valid;
    invalid.renderScalePercent =
        std::numeric_limits<float>::quiet_NaN();
    require(!accumulator.record(invalid), "non-finite scale was accepted");
    require(
        accumulator.latest()->outputExtent == baseline->outputExtent &&
            accumulator.latest()->framesPerSecond ==
                baseline->framesPerSecond,
        "invalid sample changed the published diagnostics");

    invalid = valid;
    invalid.sceneDrawCallCount = invalid.drawCallCount + 1U;
    require(!accumulator.record(invalid), "invalid draw totals were accepted");

    invalid = valid;
    invalid.frameIntervalMilliseconds =
        std::numeric_limits<double>::denorm_min();
    require(!accumulator.record(invalid),
            "an overflowing FPS sample was accepted");

    invalid = valid;
    invalid.gpuMemoryMeasurement =
        GpuMemoryMeasurement::unavailable;
    require(!accumulator.record(invalid),
            "unlabelled memory measurement was accepted");

    invalid = valid;
    invalid.gpuMemoryBytes.reset();
    require(!accumulator.record(invalid),
            "missing estimated memory value was accepted");
}

void testSmoothingAndLatestFrameCounters() {
    RenderFrameDiagnosticsAccumulator accumulator;
    auto first = sample();
    require(accumulator.record(first), "first sample failed");

    auto second = first;
    second.outputExtent = {3840U, 2160U};
    second.renderTargetExtent = {1920U, 1080U};
    second.renderScalePercent = 50.0F;
    second.frameIntervalMilliseconds = 1000.0 / 30.0;
    second.cpuFrameMilliseconds = 8.0;
    second.gpuFrameMilliseconds = 6.0;
    second.drawCallCount = 12U;
    second.sceneDrawCallCount = 10U;
    require(accumulator.record(second), "second sample failed");

    const auto& latest = *accumulator.latest();
    require(latest.outputExtent == second.outputExtent &&
                latest.renderTargetExtent ==
                    second.renderTargetExtent &&
                latest.renderScalePercent == 50.0F &&
                latest.drawCallCount == 12U,
            "latest frame metadata was not published immediately");
    require(
        std::fabs(latest.framesPerSecond - 55.5) < 1.0e-9 &&
            std::fabs(latest.cpuFrameMilliseconds - 4.6) < 1.0e-9 &&
            latest.gpuFrameMilliseconds.has_value() &&
            std::fabs(*latest.gpuFrameMilliseconds - 3.45) < 1.0e-9,
        "timing exponential average changed");

    accumulator.reset();
    require(!accumulator.latest().has_value(), "reset retained diagnostics");
}

void testFormattingAndRasterization() {
    RenderFrameDiagnosticsAccumulator accumulator;
    require(accumulator.record(sample()), "format sample failed");
    const auto& diagnostics = *accumulator.latest();
    const auto text = formatRenderFrameDiagnostics(diagnostics);
    require(text.find("OUT 1920X1080") != std::string::npos &&
                text.find("RT 1920X1080") != std::string::npos &&
                text.find("SCALE 100%") != std::string::npos &&
                text.find("GPU 3.00MS") != std::string::npos &&
                text.find("DRAW 122/120") != std::string::npos &&
                text.find("TRI 170041/170039") != std::string::npos &&
                text.find("VRAM 96.0MIB EST") != std::string::npos,
            "diagnostic text omitted required counters");

    const auto image = rasterizeRenderFrameDiagnostics(diagnostics);
    require(image.complete(), "diagnostic panel was incomplete");
    require(image.pixelScale == 2U,
            "1080p diagnostic pixel scale changed");
    require(image.width < diagnostics.outputExtent.width &&
                image.height < diagnostics.outputExtent.height,
            "diagnostic panel escaped the output");
    const auto opaque = std::count_if(
        image.rgba8.begin() + 3,
        image.rgba8.end(),
        [index = std::size_t{3U}](const std::uint8_t alpha) mutable {
            const bool selected = index % 4U == 3U && alpha != 0U;
            ++index;
            return selected;
        });
    require(opaque != 0, "diagnostic panel contains no visible pixels");

    auto fourK = diagnostics;
    fourK.outputExtent = {3840U, 2160U};
    const auto fourKImage = rasterizeRenderFrameDiagnostics(fourK);
    require(fourKImage.complete() && fourKImage.pixelScale == 4U,
            "4K diagnostic panel did not scale in output pixels");
    const auto compactImage = rasterizeRenderFrameDiagnostics(fourK, 75.0F);
    const auto enlargedImage = rasterizeRenderFrameDiagnostics(fourK, 150.0F);
    require(compactImage.complete() && compactImage.pixelScale == 3U &&
                enlargedImage.complete() && enlargedImage.pixelScale == 6U,
            "diagnostic panel did not honor independent UI scale");
    require(!rasterizeRenderFrameDiagnostics(
                 fourK, std::numeric_limits<float>::quiet_NaN())
                    .complete() &&
                !rasterizeRenderFrameDiagnostics(fourK, 151.0F).complete(),
            "diagnostic panel accepted invalid UI scale");

    auto unavailable = diagnostics;
    unavailable.gpuFrameMilliseconds.reset();
    unavailable.gpuMemoryBytes.reset();
    unavailable.gpuMemoryMeasurement =
        GpuMemoryMeasurement::unavailable;
    const auto unavailableText =
        formatRenderFrameDiagnostics(unavailable);
    require(unavailableText.find("GPU N/A") != std::string::npos &&
                unavailableText.find("VRAM N/A") != std::string::npos,
            "unavailable backend measurements were not labelled");
}

} // namespace

int main() {
    try {
        testValidationAndAtomicPublication();
        testSmoothingAndLatestFrameCounters();
        testFormattingAndRasterization();
        std::cout << "Render frame diagnostics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Render frame diagnostics tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
