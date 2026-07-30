#pragma once

#include "AirfixWindowsRenderSettingsPanel.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::windows {

enum class AirfixWindowsUiRasterizeIssue : std::uint8_t {
  invalidSnapshot,
  platformInitializationFailed,
  targetCreationFailed,
  drawingFailed,
  pixelCopyFailed,
  allocationFailed,
};

struct AirfixWindowsUiRaster final {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t rowPitchBytes{};
  // WIC GUID_WICPixelFormat32bppPBGRA: blue, green, red, premultiplied alpha.
  std::vector<std::uint8_t> premultipliedBgra8;

  [[nodiscard]] bool complete() const noexcept;
};

struct AirfixWindowsUiRasterizeResult final {
  std::optional<AirfixWindowsUiRaster> raster;
  std::optional<AirfixWindowsUiRasterizeIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return raster.has_value() && !issue.has_value();
  }
};

// Produces a full-output, premultiplied BGRA8 pause/settings overlay using
// Windows' built-in Direct2D, DirectWrite, and WIC implementations. No
// external assets, fonts, paths, or caller-provided strings are consumed.
class AirfixWindowsUiRasterizer final {
public:
  [[nodiscard]] AirfixWindowsUiRasterizeResult rasterize(
      const AirfixWindowsRenderSettingsViewSnapshot &snapshot) const noexcept;
};

} // namespace airfix::windows
