#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace airfix::windows {

struct AirfixWindowsCaptureSize final {
  std::uint32_t width{};
  std::uint32_t height{};

  [[nodiscard]] friend bool
  operator==(const AirfixWindowsCaptureSize &,
             const AirfixWindowsCaptureSize &) = default;
};

struct AirfixWindowsMissionOptions final {
  std::string setupLogicalPath;
  std::string levelLogicalPath;
  std::optional<std::string> playerObjectLogicalPath;
  std::uint32_t requestedStartIndex{};

  [[nodiscard]] friend bool
  operator==(const AirfixWindowsMissionOptions &,
             const AirfixWindowsMissionOptions &) = default;
};

struct AirfixWindowsCommandLineOptions final {
  bool smokeTest{};
  bool validateContentOnly{};
  bool useInstalledContent{};
  bool manageInstalledContent{};
  airfix::render::RenderPresentationSettingsOverride renderOverrides;
  std::optional<std::filesystem::path> contentRoot;
  std::optional<std::filesystem::path> importAfPackSource;
  std::optional<AirfixWindowsMissionOptions> mission;
  std::optional<std::filesystem::path> captureFrameOutput;
  std::optional<std::filesystem::path> captureOverviewFrameOutput;
  std::optional<std::filesystem::path> captureCrosshairValidationFrameOutput;
  std::optional<std::filesystem::path> captureDiagnosticFrameOutput;
  std::optional<std::filesystem::path> captureSettingsPanelOutput;
  std::optional<std::filesystem::path> captureControllerCalibrationPanelOutput;
  std::optional<std::filesystem::path> captureControllerBindingsPanelOutput;
  std::optional<AirfixWindowsCaptureSize> captureSize;

  [[nodiscard]] friend bool
  operator==(const AirfixWindowsCommandLineOptions &,
             const AirfixWindowsCommandLineOptions &) = default;
};

// Parses only explicit options. Private logical paths remain caller-owned
// process input and are never inferred from the package or installation.
// Invalid, incomplete, duplicate, or overlong requests throw
// std::runtime_error.
[[nodiscard]] AirfixWindowsCommandLineOptions
parseAirfixWindowsCommandLine(std::span<const std::string_view> arguments);

[[nodiscard]] constexpr std::string_view airfixWindowsUsage() noexcept {
  return "usage: AirfixDogfighter.exe "
         "--import-afpack <private-package.afpack>\n"
         "   or: AirfixDogfighter.exe --manage-installed-content\n"
         "   or: AirfixDogfighter.exe "
         "[--render-scale <50-200>] "
         "[--original-4x3 | --widescreen-hor-plus] "
         "[--vertical-fov-adjustment <0-25>] "
         "[--visual-profile <classic|enhanced>] "
         "[--render-diagnostics | --no-render-diagnostics] "
         "[--smoke-test | "
         "--capture-diagnostic-frame <public-output.bmp> "
         "| --capture-settings-panel <public-output.bmp> "
         "| --capture-controller-calibration-panel <public-output.bmp> "
         "| --capture-controller-bindings-panel <public-output.bmp> "
         "[--capture-size <width>x<height>] | "
         "--content-root <path> [--setup <logical-path> "
         "--level <logical-path> [--player-object <logical-path>] "
         "[--start-index <uint32>] "
         "[--capture-frame <private-output.bmp> | "
         "--capture-overview-frame <private-output.bmp> | "
         "--capture-crosshair-validation-frame <private-output.bmp>] "
         "[--capture-size <width>x<height>]] | "
         "--installed-content [--setup <logical-path> "
         "--level <logical-path> [--player-object <logical-path>] "
         "[--start-index <uint32>] "
         "[--capture-frame <private-output.bmp> | "
         "--capture-overview-frame <private-output.bmp> | "
         "--capture-crosshair-validation-frame <private-output.bmp>] "
         "[--capture-size <width>x<height>]] | "
         "--validate-content-root <path> [--setup <logical-path> "
         "--level <logical-path> [--player-object <logical-path>] "
         "[--start-index <uint32>]] | "
         "--validate-installed-content [--setup <logical-path> "
         "--level <logical-path> [--player-object <logical-path>] "
         "[--start-index <uint32>]]]";
}

} // namespace airfix::windows
