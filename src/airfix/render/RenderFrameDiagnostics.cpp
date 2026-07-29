#include "airfix/render/RenderFrameDiagnostics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

namespace airfix::render {
namespace {

constexpr double smoothingWeight = 0.15;
constexpr std::uint32_t glyphWidth = 5U;
constexpr std::uint32_t glyphHeight = 7U;
constexpr std::uint32_t glyphAdvance = 6U;
constexpr std::uint32_t lineAdvance = 8U;
constexpr std::uint32_t panelPadding = 4U;

[[nodiscard]] bool finiteNonNegative(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool valid(
    const RenderFrameDiagnosticSample& sample) noexcept {
    if (sample.outputExtent.width == 0U ||
        sample.outputExtent.height == 0U ||
        sample.renderTargetExtent.width == 0U ||
        sample.renderTargetExtent.height == 0U ||
        !std::isfinite(sample.renderScalePercent) ||
        sample.renderScalePercent <
            native_render_policy::minimumRenderScalePercent ||
        sample.renderScalePercent >
            native_render_policy::maximumRenderScalePercent ||
        !std::isfinite(sample.frameIntervalMilliseconds) ||
        sample.frameIntervalMilliseconds <= 0.0 ||
        !finiteNonNegative(sample.cpuFrameMilliseconds) ||
        sample.sceneDrawCallCount > sample.drawCallCount ||
        sample.sceneTriangleCount > sample.triangleCount) {
        return false;
    }
    if (sample.gpuFrameMilliseconds.has_value() &&
        !finiteNonNegative(*sample.gpuFrameMilliseconds)) {
        return false;
    }
    const bool memoryAvailable = sample.gpuMemoryBytes.has_value();
    if (memoryAvailable !=
        (sample.gpuMemoryMeasurement !=
         GpuMemoryMeasurement::unavailable)) {
        return false;
    }
    return true;
}

[[nodiscard]] double smooth(
    const double previous,
    const double current) noexcept {
    return previous +
        (current - previous) * smoothingWeight;
}

[[nodiscard]] std::array<std::uint8_t, glyphHeight> glyph(
    const char character) noexcept {
    using Rows = std::array<std::uint8_t, glyphHeight>;
    switch (character) {
    case 'A': return Rows{14, 17, 17, 31, 17, 17, 17};
    case 'B': return Rows{30, 17, 17, 30, 17, 17, 30};
    case 'C': return Rows{14, 17, 16, 16, 16, 17, 14};
    case 'D': return Rows{30, 17, 17, 17, 17, 17, 30};
    case 'E': return Rows{31, 16, 16, 30, 16, 16, 31};
    case 'F': return Rows{31, 16, 16, 30, 16, 16, 16};
    case 'G': return Rows{14, 17, 16, 23, 17, 17, 15};
    case 'H': return Rows{17, 17, 17, 31, 17, 17, 17};
    case 'I': return Rows{31, 4, 4, 4, 4, 4, 31};
    case 'J': return Rows{7, 2, 2, 2, 18, 18, 12};
    case 'K': return Rows{17, 18, 20, 24, 20, 18, 17};
    case 'L': return Rows{16, 16, 16, 16, 16, 16, 31};
    case 'M': return Rows{17, 27, 21, 21, 17, 17, 17};
    case 'N': return Rows{17, 25, 21, 19, 17, 17, 17};
    case 'O': return Rows{14, 17, 17, 17, 17, 17, 14};
    case 'P': return Rows{30, 17, 17, 30, 16, 16, 16};
    case 'Q': return Rows{14, 17, 17, 17, 21, 18, 13};
    case 'R': return Rows{30, 17, 17, 30, 20, 18, 17};
    case 'S': return Rows{15, 16, 16, 14, 1, 1, 30};
    case 'T': return Rows{31, 4, 4, 4, 4, 4, 4};
    case 'U': return Rows{17, 17, 17, 17, 17, 17, 14};
    case 'V': return Rows{17, 17, 17, 17, 17, 10, 4};
    case 'W': return Rows{17, 17, 17, 21, 21, 21, 10};
    case 'X': return Rows{17, 17, 10, 4, 10, 17, 17};
    case 'Y': return Rows{17, 17, 10, 4, 4, 4, 4};
    case 'Z': return Rows{31, 1, 2, 4, 8, 16, 31};
    case '0': return Rows{14, 17, 19, 21, 25, 17, 14};
    case '1': return Rows{4, 12, 4, 4, 4, 4, 14};
    case '2': return Rows{14, 17, 1, 2, 4, 8, 31};
    case '3': return Rows{30, 1, 1, 14, 1, 1, 30};
    case '4': return Rows{2, 6, 10, 18, 31, 2, 2};
    case '5': return Rows{31, 16, 16, 30, 1, 1, 30};
    case '6': return Rows{14, 16, 16, 30, 17, 17, 14};
    case '7': return Rows{31, 1, 2, 4, 8, 8, 8};
    case '8': return Rows{14, 17, 17, 14, 17, 17, 14};
    case '9': return Rows{14, 17, 17, 15, 1, 1, 14};
    case '.': return Rows{0, 0, 0, 0, 0, 12, 12};
    case '/': return Rows{1, 2, 2, 4, 8, 8, 16};
    case '%': return Rows{25, 26, 2, 4, 8, 11, 19};
    case '-': return Rows{0, 0, 0, 31, 0, 0, 0};
    case ':': return Rows{0, 12, 12, 0, 12, 12, 0};
    default: return {};
    }
}

void appendLine(
    std::string& result,
    const char* const format,
    const auto... arguments) {
    std::array<char, 160U> buffer{};
    const int length = std::snprintf(
        buffer.data(), buffer.size(), format, arguments...);
    if (length <= 0) {
        return;
    }
    if (!result.empty()) {
        result.push_back('\n');
    }
    const auto bounded = std::min<std::size_t>(
        static_cast<std::size_t>(length), buffer.size() - 1U);
    result.append(buffer.data(), bounded);
}

[[nodiscard]] std::uint32_t diagnosticsPixelScale(
    const OutputPixelExtent extent) noexcept {
    const auto horizontal = std::max(1U, extent.width / 960U);
    const auto vertical = std::max(1U, extent.height / 540U);
    return std::clamp(std::min(horizontal, vertical), 1U, 4U);
}

void fillPixel(
    RenderDiagnosticsImage& image,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::array<std::uint8_t, 4U> color) noexcept {
    if (x >= image.width || y >= image.height) {
        return;
    }
    const auto offset =
        static_cast<std::size_t>(y) * image.bytesPerRow +
        static_cast<std::size_t>(x) * 4U;
    image.rgba8[offset] = color[0];
    image.rgba8[offset + 1U] = color[1];
    image.rgba8[offset + 2U] = color[2];
    image.rgba8[offset + 3U] = color[3];
}

} // namespace

bool RenderFrameDiagnosticsAccumulator::record(
    const RenderFrameDiagnosticSample& sample) noexcept {
    if (!valid(sample)) {
        return false;
    }

    const double currentFps =
        1000.0 / sample.frameIntervalMilliseconds;
    if (!std::isfinite(currentFps)) {
        return false;
    }
    const auto previous = latest_;
    RenderFrameDiagnostics next{
        .outputExtent = sample.outputExtent,
        .renderTargetExtent = sample.renderTargetExtent,
        .renderScalePercent = sample.renderScalePercent,
        .framesPerSecond = currentFps,
        .cpuFrameMilliseconds = sample.cpuFrameMilliseconds,
        .gpuFrameMilliseconds = sample.gpuFrameMilliseconds,
        .drawCallCount = sample.drawCallCount,
        .sceneDrawCallCount = sample.sceneDrawCallCount,
        .triangleCount = sample.triangleCount,
        .sceneTriangleCount = sample.sceneTriangleCount,
        .activeLightCount = sample.activeLightCount,
        .gpuMemoryBytes = sample.gpuMemoryBytes,
        .gpuMemoryMeasurement = sample.gpuMemoryMeasurement,
    };
    if (previous.has_value()) {
        next.framesPerSecond =
            smooth(previous->framesPerSecond, currentFps);
        next.cpuFrameMilliseconds =
            smooth(previous->cpuFrameMilliseconds,
                   sample.cpuFrameMilliseconds);
        if (sample.gpuFrameMilliseconds.has_value() &&
            previous->gpuFrameMilliseconds.has_value()) {
            next.gpuFrameMilliseconds = smooth(
                *previous->gpuFrameMilliseconds,
                *sample.gpuFrameMilliseconds);
        }
    }
    latest_ = next;
    return true;
}

const std::optional<RenderFrameDiagnostics>&
RenderFrameDiagnosticsAccumulator::latest() const noexcept {
    return latest_;
}

void RenderFrameDiagnosticsAccumulator::reset() noexcept {
    latest_.reset();
}

bool RenderDiagnosticsImage::complete() const noexcept {
    if (width == 0U || height == 0U || pixelScale == 0U ||
        bytesPerRow != width * 4U) {
        return false;
    }
    const auto expected =
        static_cast<std::uint64_t>(bytesPerRow) * height;
    return expected <= std::numeric_limits<std::size_t>::max() &&
        rgba8.size() == static_cast<std::size_t>(expected);
}

std::string formatRenderFrameDiagnostics(
    const RenderFrameDiagnostics& diagnostics) {
    std::string result;
    result.reserve(256U);
    appendLine(
        result,
        "OUT %uX%u  RT %uX%u  SCALE %.0f%%",
        diagnostics.outputExtent.width,
        diagnostics.outputExtent.height,
        diagnostics.renderTargetExtent.width,
        diagnostics.renderTargetExtent.height,
        static_cast<double>(diagnostics.renderScalePercent));
    if (diagnostics.gpuFrameMilliseconds.has_value()) {
        appendLine(
            result,
            "FPS %.1f  CPU %.2fMS  GPU %.2fMS",
            diagnostics.framesPerSecond,
            diagnostics.cpuFrameMilliseconds,
            *diagnostics.gpuFrameMilliseconds);
    } else {
        appendLine(
            result,
            "FPS %.1f  CPU %.2fMS  GPU N/A",
            diagnostics.framesPerSecond,
            diagnostics.cpuFrameMilliseconds);
    }
    appendLine(
        result,
        "DRAW %llu/%llu  TRI %llu/%llu  LIGHT %u",
        static_cast<unsigned long long>(diagnostics.drawCallCount),
        static_cast<unsigned long long>(
            diagnostics.sceneDrawCallCount),
        static_cast<unsigned long long>(diagnostics.triangleCount),
        static_cast<unsigned long long>(
            diagnostics.sceneTriangleCount),
        diagnostics.activeLightCount);
    if (!diagnostics.gpuMemoryBytes.has_value()) {
        appendLine(result, "VRAM N/A");
    } else {
        const double mebibytes =
            static_cast<double>(*diagnostics.gpuMemoryBytes) /
            (1024.0 * 1024.0);
        appendLine(
            result,
            diagnostics.gpuMemoryMeasurement ==
                    GpuMemoryMeasurement::backendReported
                ? "VRAM %.1fMIB REPORTED"
                : "VRAM %.1fMIB EST",
            mebibytes);
    }
    return result;
}

RenderDiagnosticsImage rasterizeRenderFrameDiagnostics(
    const RenderFrameDiagnostics& diagnostics) {
    const std::string text =
        formatRenderFrameDiagnostics(diagnostics);
    std::size_t maximumCharacters = 0U;
    std::size_t lineCount = 0U;
    std::size_t lineStart = 0U;
    for (std::size_t index = 0U; index <= text.size(); ++index) {
        if (index == text.size() || text[index] == '\n') {
            maximumCharacters =
                std::max(maximumCharacters, index - lineStart);
            ++lineCount;
            lineStart = index + 1U;
        }
    }
    if (maximumCharacters == 0U || lineCount == 0U) {
        return {};
    }

    const std::uint32_t scale =
        diagnosticsPixelScale(diagnostics.outputExtent);
    const auto contentWidth =
        maximumCharacters * glyphAdvance - 1U;
    const auto contentHeight =
        lineCount * lineAdvance - 1U;
    const auto width64 =
        (contentWidth + panelPadding * 2U) * scale;
    const auto height64 =
        (contentHeight + panelPadding * 2U) * scale;
    if (width64 > std::numeric_limits<std::uint32_t>::max() ||
        height64 > std::numeric_limits<std::uint32_t>::max() ||
        width64 > diagnostics.outputExtent.width ||
        height64 > diagnostics.outputExtent.height) {
        return {};
    }

    RenderDiagnosticsImage image{
        .width = static_cast<std::uint32_t>(width64),
        .height = static_cast<std::uint32_t>(height64),
        .bytesPerRow = static_cast<std::uint32_t>(width64 * 4U),
        .pixelScale = scale,
        .rgba8 = {},
    };
    const auto byteCount =
        static_cast<std::uint64_t>(image.bytesPerRow) * image.height;
    if (byteCount > std::numeric_limits<std::size_t>::max()) {
        return {};
    }
    try {
        image.rgba8.resize(static_cast<std::size_t>(byteCount));
    } catch (...) {
        return {};
    }

    constexpr std::array<std::uint8_t, 4U> background{
        5U, 10U, 16U, 210U};
    constexpr std::array<std::uint8_t, 4U> border{
        78U, 171U, 255U, 235U};
    constexpr std::array<std::uint8_t, 4U> foreground{
        235U, 246U, 255U, 255U};
    for (std::uint32_t y = 0U; y < image.height; ++y) {
        for (std::uint32_t x = 0U; x < image.width; ++x) {
            const bool edge =
                x < scale || y < scale ||
                x >= image.width - scale ||
                y >= image.height - scale;
            fillPixel(image, x, y, edge ? border : background);
        }
    }

    std::uint32_t cursorX = panelPadding * scale;
    std::uint32_t cursorY = panelPadding * scale;
    for (const char character : text) {
        if (character == '\n') {
            cursorX = panelPadding * scale;
            cursorY += lineAdvance * scale;
            continue;
        }
        const auto rows = glyph(character);
        for (std::uint32_t row = 0U; row < glyphHeight; ++row) {
            for (std::uint32_t column = 0U;
                 column < glyphWidth;
                 ++column) {
                const auto mask = static_cast<std::uint8_t>(
                    1U << (glyphWidth - 1U - column));
                if ((rows[row] & mask) == 0U) {
                    continue;
                }
                for (std::uint32_t sy = 0U; sy < scale; ++sy) {
                    for (std::uint32_t sx = 0U; sx < scale; ++sx) {
                        fillPixel(
                            image,
                            cursorX + column * scale + sx,
                            cursorY + row * scale + sy,
                            foreground);
                    }
                }
            }
        }
        cursorX += glyphAdvance * scale;
    }
    return image;
}

} // namespace airfix::render
