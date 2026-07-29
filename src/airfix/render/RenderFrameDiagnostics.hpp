#pragma once

#include "airfix/render/NativeRenderLayout.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace airfix::render {

enum class GpuMemoryMeasurement : std::uint8_t {
    unavailable,
    estimated,
    backendReported,
};

// One backend observation. Timing is presentation-only information and must
// never be consumed by deterministic simulation or physics.
struct RenderFrameDiagnosticSample final {
    OutputPixelExtent outputExtent;
    RenderTargetPixelExtent renderTargetExtent;
    float renderScalePercent{100.0F};
    double frameIntervalMilliseconds{};
    double cpuFrameMilliseconds{};
    std::optional<double> gpuFrameMilliseconds;
    std::uint64_t drawCallCount{};
    std::uint64_t sceneDrawCallCount{};
    std::uint64_t triangleCount{};
    std::uint64_t sceneTriangleCount{};
    std::uint32_t activeLightCount{};
    std::optional<std::uint64_t> gpuMemoryBytes;
    GpuMemoryMeasurement gpuMemoryMeasurement{
        GpuMemoryMeasurement::unavailable};
};

struct RenderFrameDiagnostics final {
    OutputPixelExtent outputExtent;
    RenderTargetPixelExtent renderTargetExtent;
    float renderScalePercent{100.0F};
    double framesPerSecond{};
    double cpuFrameMilliseconds{};
    std::optional<double> gpuFrameMilliseconds;
    std::uint64_t drawCallCount{};
    std::uint64_t sceneDrawCallCount{};
    std::uint64_t triangleCount{};
    std::uint64_t sceneTriangleCount{};
    std::uint32_t activeLightCount{};
    std::optional<std::uint64_t> gpuMemoryBytes;
    GpuMemoryMeasurement gpuMemoryMeasurement{
        GpuMemoryMeasurement::unavailable};
};

// Maintains a bounded exponential moving average without owning a clock.
// Backends supply monotonic intervals so tests can exercise the exact policy.
class RenderFrameDiagnosticsAccumulator final {
public:
    [[nodiscard]] bool record(
        const RenderFrameDiagnosticSample& sample) noexcept;

    [[nodiscard]] const std::optional<RenderFrameDiagnostics>&
    latest() const noexcept;

    void reset() noexcept;

private:
    std::optional<RenderFrameDiagnostics> latest_;
};

struct RenderDiagnosticsImage final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t bytesPerRow{};
    std::uint32_t pixelScale{};
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] bool complete() const noexcept;
};

// Text is intentionally compact, uppercase ASCII, and locale-independent so
// the same counters can be compared in D3D11 and Metal captures.
[[nodiscard]] std::string formatRenderFrameDiagnostics(
    const RenderFrameDiagnostics& diagnostics);

// Produces a small premultiplied-looking RGBA8 developer panel. It is composed
// after scene scaling, in output pixels, so its sharpness and hit-free layout
// never inherit the 3D render scale.
[[nodiscard]] RenderDiagnosticsImage rasterizeRenderFrameDiagnostics(
    const RenderFrameDiagnostics& diagnostics);

} // namespace airfix::render
