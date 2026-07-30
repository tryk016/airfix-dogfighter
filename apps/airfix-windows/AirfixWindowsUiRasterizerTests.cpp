#include "AirfixWindowsUiRasterizer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::render::RenderPresentationSettings;
using airfix::windows::AirfixWindowsRenderSettingsPanel;
using airfix::windows::AirfixWindowsUiPixelExtent;
using airfix::windows::AirfixWindowsUiRaster;
using airfix::windows::AirfixWindowsUiRasterizeIssue;
using airfix::windows::AirfixWindowsUiRasterizer;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
panel(const AirfixWindowsUiPixelExtent output) {
  auto result = AirfixWindowsRenderSettingsPanel::create(
      RenderPresentationSettings{}, true, output);
  require(result.has_value(), "valid raster panel fixture was rejected");
  return *result;
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
displaySettingsPanel(const AirfixWindowsUiPixelExtent output) {
  auto result = panel(output);
  const auto pause = result.snapshot();
  const auto &display = pause.items[0];
  const auto intent = result.consumePointer({
      .xPixels = display.bounds.x + display.bounds.width * 0.5F,
      .yPixels = display.bounds.y + display.bounds.height * 0.5F,
      .wheelY = 0,
      .primaryPressed = true,
  });
  require(intent.empty(), "opening Display settings emitted an intent");
  return result;
}

[[nodiscard]] bool containsBytes(const AirfixWindowsUiRaster &raster,
                                 const std::string_view needle) {
  if (needle.empty() || needle.size() > raster.premultipliedBgra8.size()) {
    return false;
  }
  return std::search(raster.premultipliedBgra8.begin(),
                     raster.premultipliedBgra8.end(), needle.begin(),
                     needle.end()) != raster.premultipliedBgra8.end();
}

void requireValidPremultipliedRaster(const AirfixWindowsUiRaster &raster,
                                     const std::uint32_t expectedWidth,
                                     const std::uint32_t expectedHeight) {
  require(raster.complete(), "raster did not report a complete BGRA8 image");
  require(raster.width == expectedWidth && raster.height == expectedHeight &&
              raster.rowPitchBytes == expectedWidth * 4U,
          "raster dimensions or row pitch did not match the physical output");

  bool foundVisiblePixel = false;
  bool foundColorVariation = false;
  const auto &bytes = raster.premultipliedBgra8;
  const std::array<std::uint8_t, 4U> first{bytes[0], bytes[1], bytes[2],
                                           bytes[3]};
  for (std::size_t index = 0U; index + 3U < bytes.size(); index += 4U) {
    const auto blue = bytes[index];
    const auto green = bytes[index + 1U];
    const auto red = bytes[index + 2U];
    const auto alpha = bytes[index + 3U];
    require(blue <= alpha && green <= alpha && red <= alpha,
            "raster contained a non-premultiplied BGRA pixel");
    foundVisiblePixel = foundVisiblePixel || alpha != 0U;
    foundColorVariation = foundColorVariation || blue != first[0] ||
                          green != first[1] || red != first[2] ||
                          alpha != first[3];
  }
  require(foundVisiblePixel, "raster was fully transparent");
  require(foundColorVariation, "raster contained no visible panel content");

  // The public raster contract cannot accept arbitrary caller strings. These
  // checks guard against accidentally embedding common path/checksum markers
  // into a future intermediate surface.
  require(!containsBytes(raster, "C:\\Users\\Private\\settings"),
          "raster leaked a private filesystem path");
  require(!containsBytes(raster, "sha256:private-content-checksum"),
          "raster leaked a private checksum");
}

void rasterizesFullHdAndUltrawideAtDpiScale() {
  AirfixWindowsUiRasterizer rasterizer;

  auto fullHdPanel = panel({.width = 1920U, .height = 1080U, .dpiScale = 1.0F});
  const auto fullHd = rasterizer.rasterize(fullHdPanel.snapshot());
  require(fullHd.complete(), "1920x1080 rasterization failed");
  requireValidPremultipliedRaster(*fullHd.raster, 1920U, 1080U);

  auto ultrawidePanel =
      panel({.width = 3440U, .height = 1440U, .dpiScale = 1.5F});
  const auto ultrawide = rasterizer.rasterize(ultrawidePanel.snapshot());
  require(ultrawide.complete(), "ultrawide DPI rasterization failed");
  requireValidPremultipliedRaster(*ultrawide.raster, 3440U, 1440U);
}

void rasterizesAdaptiveSettingsAtSupportedSmallOutputs() {
  AirfixWindowsUiRasterizer rasterizer;
  constexpr std::array outputs{
      AirfixWindowsUiPixelExtent{
          .width = 960U, .height = 540U, .dpiScale = 1.0F},
      AirfixWindowsUiPixelExtent{
          .width = 1280U, .height = 720U, .dpiScale = 1.5F},
      AirfixWindowsUiPixelExtent{
          .width = 640U, .height = 360U, .dpiScale = 1.0F},
  };
  for (const auto output : outputs) {
    auto subject = displaySettingsPanel(output);
    const auto raster = rasterizer.rasterize(subject.snapshot());
    require(raster.complete(),
            "adaptive Display settings rasterization failed");
    requireValidPremultipliedRaster(*raster.raster, output.width,
                                    output.height);
  }
}

void invalidDimensionsFailClosed() {
  auto subject = panel({.width = 1920U, .height = 1080U, .dpiScale = 1.0F});
  auto snapshot = subject.snapshot();
  snapshot.output.width = 0U;
  const auto result = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!result.complete() && result.issue.has_value() &&
              *result.issue == AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "zero-width raster snapshot did not fail closed");

  snapshot = subject.snapshot();
  snapshot.panelBounds.y = -1.0F;
  const auto escaped = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!escaped.complete() && escaped.issue.has_value() &&
              *escaped.issue == AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "out-of-output UI geometry did not fail closed");
}

} // namespace

int main() {
  try {
    rasterizesFullHdAndUltrawideAtDpiScale();
    rasterizesAdaptiveSettingsAtSupportedSmallOutputs();
    invalidDimensionsFailClosed();
  } catch (const std::exception &error) {
    std::cerr << "AirfixWindowsUiRasterizerTests failed: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
