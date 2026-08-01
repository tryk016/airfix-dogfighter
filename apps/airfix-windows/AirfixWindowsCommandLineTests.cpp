#include "AirfixWindowsCommandLine.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::render::ScenePresentationMode;
using airfix::render::VisualProfile;
using airfix::windows::parseAirfixWindowsCommandLine;
using namespace std::string_view_literals;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <std::size_t Size>
[[nodiscard]] auto parse(const std::array<std::string_view, Size> &arguments) {
  return parseAirfixWindowsCommandLine(arguments);
}

template <std::size_t Size>
void requireRejected(const std::array<std::string_view, Size> &arguments,
                     const std::string_view message) {
  bool rejected = false;
  try {
    (void)parse(arguments);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, message);
}

void testEmptyAndSmokeModes() {
  const std::array<std::string_view, 0U> empty{};
  const auto defaultOptions = parse(empty);
  require(!defaultOptions.smokeTest && !defaultOptions.contentRoot &&
              !defaultOptions.useInstalledContent &&
              !defaultOptions.manageInstalledContent &&
              !defaultOptions.importAfPackSource && !defaultOptions.mission &&
              !defaultOptions.captureSize &&
              !defaultOptions.renderOverrides.renderScalePercent &&
              !defaultOptions.renderOverrides.scenePresentation &&
              !defaultOptions.renderOverrides.visualProfile &&
              !defaultOptions.renderOverrides.diagnosticsOverlayEnabled &&
              !defaultOptions.renderOverrides.verticalFovAdjustmentDegrees &&
              !defaultOptions.captureDiagnosticFrameOutput &&
              !defaultOptions.captureOverviewFrameOutput &&
              !defaultOptions.captureCrosshairValidationFrameOutput &&
              !defaultOptions.captureHealthGaugeValidationFrameOutput &&
              !defaultOptions.captureSettingsPanelOutput &&
              !defaultOptions.captureControllerCalibrationPanelOutput &&
              !defaultOptions.captureControllerBindingsPanelOutput,
          "empty command line must retain a sparse data-less shell");

  const std::array smoke{"--smoke-test"sv};
  const auto smokeOptions = parse(smoke);
  require(smokeOptions.smokeTest && !smokeOptions.contentRoot &&
              !smokeOptions.useInstalledContent &&
              !smokeOptions.importAfPackSource && !smokeOptions.mission &&
              !smokeOptions.renderOverrides.renderScalePercent &&
              !smokeOptions.renderOverrides.scenePresentation &&
              !smokeOptions.renderOverrides.visualProfile &&
              !smokeOptions.renderOverrides.diagnosticsOverlayEnabled &&
              !smokeOptions.renderOverrides.verticalFovAdjustmentDegrees,
          "smoke mode without render flags must remain sparse");
}

void testPresentationOptions() {
  const std::array smoke{
      "--smoke-test"sv,
      "--render-scale"sv,
      "50"sv,
      "--original-4x3"sv,
      "--render-diagnostics"sv,
      "--visual-profile"sv,
      "enhanced"sv,
      "--vertical-fov-adjustment"sv,
      "25"sv,
  };
  const auto smokeOptions = parse(smoke);
  require(smokeOptions.smokeTest &&
              smokeOptions.renderOverrides.renderScalePercent == 50.0F &&
              smokeOptions.renderOverrides.scenePresentation ==
                  ScenePresentationMode::originalFourByThree &&
              smokeOptions.renderOverrides.visualProfile ==
                  VisualProfile::enhanced &&
              smokeOptions.renderOverrides.diagnosticsOverlayEnabled == true &&
              smokeOptions.renderOverrides.verticalFovAdjustmentDegrees ==
                  25.0F &&
              !smokeOptions.contentRoot,
          "data-less smoke presentation settings were not retained");

  const std::array interactive{
      "--render-scale"sv,
      "200"sv,
      "--widescreen-hor-plus"sv,
      "--visual-profile"sv,
      "classic"sv,
      "--no-render-diagnostics"sv,
      "--vertical-fov-adjustment"sv,
      "0"sv,
  };
  const auto interactiveOptions = parse(interactive);
  require(!interactiveOptions.smokeTest &&
              interactiveOptions.renderOverrides.renderScalePercent == 200.0F &&
              interactiveOptions.renderOverrides.scenePresentation ==
                  ScenePresentationMode::widescreenHorPlus &&
              interactiveOptions.renderOverrides.visualProfile ==
                  VisualProfile::classic &&
              interactiveOptions.renderOverrides.diagnosticsOverlayEnabled ==
                  false &&
              interactiveOptions.renderOverrides.verticalFovAdjustmentDegrees ==
                  0.0F &&
              !interactiveOptions.contentRoot,
          "interactive sparse presentation overrides were not retained");
}

void testAuthenticatedMissionRequest() {
  const std::array arguments{
      "--content-root"sv,  "private-pack"sv,
      "--setup"sv,         "Game/Setup/mission.afs"sv,
      "--level"sv,         "Game/Levels/mission.level"sv,
      "--player-object"sv, "Game/Objects/player.object"sv,
      "--start-index"sv,   "4294967295"sv,
  };
  const auto options = parse(arguments);
  require(options.contentRoot == std::filesystem::path("private-pack") &&
              !options.validateContentOnly && options.mission.has_value(),
          "mission request must retain the explicit authenticated root");
  require(options.mission->setupLogicalPath == "Game/Setup/mission.afs" &&
              options.mission->levelLogicalPath ==
                  "Game/Levels/mission.level" &&
              options.mission->playerObjectLogicalPath ==
                  "Game/Objects/player.object" &&
              options.mission->requestedStartIndex ==
                  std::numeric_limits<std::uint32_t>::max(),
          "mission request fields changed during parsing");
}

void testInstalledContentAndImportRequests() {
  const std::array installed{
      "--installed-content"sv,        "--setup"sv,
      "Game/Setup/mission.afs"sv,     "--level"sv,
      "Game/Levels/mission.level"sv,  "--player-object"sv,
      "Game/Objects/player.object"sv,
  };
  const auto installedOptions = parse(installed);
  require(installedOptions.useInstalledContent &&
              !installedOptions.validateContentOnly &&
              !installedOptions.contentRoot && installedOptions.mission &&
              !installedOptions.importAfPackSource,
          "installed-content mission request was not retained");

  const std::array validation{"--validate-installed-content"sv};
  const auto validationOptions = parse(validation);
  require(validationOptions.useInstalledContent &&
              validationOptions.validateContentOnly &&
              !validationOptions.contentRoot && !validationOptions.mission,
          "installed-content validation request was not retained");

  const std::array import{"--import-afpack"sv, "owner-private.afpack"sv};
  const auto importOptions = parse(import);
  require(importOptions.importAfPackSource ==
                  std::filesystem::path("owner-private.afpack") &&
              !importOptions.contentRoot &&
              !importOptions.useInstalledContent && !importOptions.mission &&
              !importOptions.validateContentOnly,
          "private AFPACK import request was not retained");

  const std::array manager{"--manage-installed-content"sv};
  const auto managerOptions = parse(manager);
  require(managerOptions.manageInstalledContent &&
              !managerOptions.importAfPackSource &&
              !managerOptions.useInstalledContent &&
              !managerOptions.contentRoot && !managerOptions.mission &&
              !managerOptions.smokeTest,
          "private AFPACK manager request was not retained");
}

void testValidationAndRejections() {
  const std::array validation{"--validate-content-root"sv, "private-pack"sv};
  const auto options = parse(validation);
  require(options.validateContentOnly && options.contentRoot &&
              !options.mission,
          "content-only validation mode was not retained");

  requireRejected(
      std::array{"--setup"sv, "mission.afs"sv, "--level"sv, "mission.level"sv},
      "mission paths without a content root must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv},
                  "an incomplete mission pair must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv,
                             "--start-index"sv, "1"sv},
                  "a start index without a mission pair must fail closed");
  requireRejected(
      std::array{"--content-root"sv, "a"sv, "--content-root"sv, "b"sv},
      "duplicate roots must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv,
                             "--start-index"sv, "4294967296"sv},
                  "an overflowing start index must fail closed");
  requireRejected(
      std::array{"--smoke-test"sv, "--content-root"sv, "private-pack"sv},
      "the public smoke mode must remain exclusive");
  requireRejected(
      std::array{"--smoke-test"sv, "--installed-content"sv},
      "the public smoke mode and installed content must remain exclusive");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--installed-content"sv},
      "explicit and installed content roots must remain exclusive");
  requireRejected(
      std::array{"--installed-content"sv, "--validate-installed-content"sv},
      "installed content modes must not be duplicated");
  requireRejected(std::array{"--import-afpack"sv, "owner-private.AFPACK"sv},
                  "non-canonical AFPACK extension must fail closed");
  requireRejected(std::array{"--import-afpack"sv, "owner-private.afpack"sv,
                             "--render-scale"sv, "100"sv},
                  "AFPACK import and render overrides must remain exclusive");
  requireRejected(
      std::array{"--import-afpack"sv, "owner-private.afpack"sv,
                 "--installed-content"sv},
      "AFPACK import and content consumption must remain exclusive");
  requireRejected(std::array{"--import-afpack"sv, "owner-private.afpack"sv,
                             "--import-afpack"sv, "other.afpack"sv},
                  "duplicate AFPACK imports must fail closed");
  requireRejected(std::array{"--manage-installed-content"sv,
                             "--manage-installed-content"sv},
                  "duplicate AFPACK managers must fail closed");
  requireRejected(std::array{"--manage-installed-content"sv,
                             "--import-afpack"sv, "owner-private.afpack"sv},
                  "AFPACK manager and direct import must remain exclusive");
  requireRejected(
      std::array{"--manage-installed-content"sv, "--installed-content"sv},
      "AFPACK manager and content consumption must remain exclusive");
  requireRejected(
      std::array{"--manage-installed-content"sv, "--render-scale"sv, "100"sv},
      "AFPACK manager and renderer overrides must remain exclusive");
  requireRejected(std::array{"--smoke-test"sv, "--render-scale"sv, "49"sv},
                  "render scale below 50 percent must fail closed");
  requireRejected(std::array{"--smoke-test"sv, "--render-scale"sv, "201"sv},
                  "render scale above 200 percent must fail closed");
  requireRejected(std::array{"--smoke-test"sv, "--render-scale"sv, "-50"sv},
                  "signed render scale must fail closed");
  requireRejected(std::array{"--smoke-test"sv, "--render-scale"sv, "50"sv,
                             "--render-scale"sv, "100"sv},
                  "duplicate render scale must fail closed");
  requireRejected(std::array{"--vertical-fov-adjustment"sv, "26"sv},
                  "vertical-FOV adjustment above 25 degrees must fail closed");
  requireRejected(std::array{"--vertical-fov-adjustment"sv, "-1"sv},
                  "signed vertical-FOV adjustment must fail closed");
  requireRejected(
      std::array{"--vertical-fov-adjustment"sv, "12.5"sv},
      "fractional command-line vertical-FOV adjustment must fail closed");
  requireRejected(
      std::array{
          "--vertical-fov-adjustment"sv,
          "5"sv,
          "--vertical-fov-adjustment"sv,
          "10"sv,
      },
      "duplicate vertical-FOV adjustment must fail closed");
  requireRejected(std::array{"--vertical-fov-adjustment"sv},
                  "missing vertical-FOV adjustment must fail closed");
  requireRejected(
      std::array{"--smoke-test"sv, "--original-4x3"sv, "--original-4x3"sv},
      "duplicate Original 4:3 mode must fail closed");
  requireRejected(
      std::array{"--render-diagnostics"sv, "--render-diagnostics"sv},
      "duplicate diagnostics mode must fail closed");
  requireRejected(std::array{"--original-4x3"sv, "--widescreen-hor-plus"sv},
                  "conflicting presentation modes must fail closed");
  requireRejected(
      std::array{"--widescreen-hor-plus"sv, "--widescreen-hor-plus"sv},
      "duplicate widescreen mode must fail closed");
  requireRejected(
      std::array{"--render-diagnostics"sv, "--no-render-diagnostics"sv},
      "conflicting diagnostics modes must fail closed");
  requireRejected(
      std::array{"--no-render-diagnostics"sv, "--no-render-diagnostics"sv},
      "duplicate disabled diagnostics mode must fail closed");
  requireRejected(std::array{"--visual-profile"sv, "classic"sv,
                             "--visual-profile"sv, "enhanced"sv},
                  "duplicate visual profile must fail closed");
  requireRejected(std::array{"--visual-profile"sv, "future"sv},
                  "unknown visual profile must fail closed");
  requireRejected(std::array{"--visual-profile"sv},
                  "missing visual profile must fail closed");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--unknown-option"sv},
      "unknown options must fail closed");
  requireRejected(std::array{"--content-root"sv, ""sv},
                  "empty option values must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv,
                             "--start-index"sv, "-1"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv},
                  "signed start indices must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--setup"sv, "other.afs"sv,
                             "--level"sv, "mission.level"sv},
                  "duplicate mission options must fail closed");
  const std::string overlongPath(4097U, 'x');
  requireRejected(
      std::array<std::string_view, 2U>{"--content-root"sv, overlongPath},
      "overlong private paths must fail closed");
  requireRejected(std::array{"--validate-content-root"sv, "private-pack"sv,
                             "--setup"sv, "mission.afs"sv, "--level"sv,
                             "mission.level"sv, "--capture-frame"sv,
                             "frame.bmp"sv},
                  "validation and capture modes must remain exclusive");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv,
                             "--capture-frame"sv, "frame.png"sv},
                  "a misleading capture extension must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv,
                             "--capture-frame"sv, "frame.bmp"sv},
                  "capture without a complete mission must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv,
                             "--capture-overview-frame"sv, "frame.bmp"sv},
                  "overview capture without a mission must fail closed");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv,
                 "--capture-crosshair-validation-frame"sv, "frame.bmp"sv},
      "crosshair validation capture without a mission must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv,
                             "--capture-size"sv, "3840x2160"sv},
                  "capture size without frame capture must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv,
                             "--capture-diagnostic-frame"sv, "public.bmp"sv},
                  "public diagnostic capture must reject private content");
  requireRejected(
      std::array{"--smoke-test"sv, "--capture-diagnostic-frame"sv,
                 "public.bmp"sv},
      "public diagnostic capture and smoke mode must remain exclusive");
  requireRejected(
      std::array{"--smoke-test"sv, "--capture-settings-panel"sv,
                 "settings.bmp"sv},
      "public settings capture and smoke mode must remain exclusive");
  requireRejected(
      std::array{"--smoke-test"sv, "--capture-controller-calibration-panel"sv,
                 "controller.bmp"sv},
      "public controller calibration capture and smoke mode must remain "
      "exclusive");
  requireRejected(
      std::array{"--smoke-test"sv, "--capture-controller-bindings-panel"sv,
                 "bindings.bmp"sv},
      "public controller bindings capture and smoke mode must remain "
      "exclusive");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv,
                             "--capture-settings-panel"sv, "settings.bmp"sv},
                  "public settings capture must reject private content");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv,
                 "--capture-controller-calibration-panel"sv,
                 "controller.bmp"sv},
      "public controller calibration capture must reject private content");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv,
                 "--capture-controller-bindings-panel"sv, "bindings.bmp"sv},
      "public controller bindings capture must reject private content");
  requireRejected(std::array{"--capture-diagnostic-frame"sv,
                             "diagnostics.bmp"sv, "--capture-settings-panel"sv,
                             "settings.bmp"sv},
                  "public capture modes must remain mutually exclusive");
  requireRejected(std::array{"--capture-settings-panel"sv, "settings.bmp"sv,
                             "--capture-controller-calibration-panel"sv,
                             "controller.bmp"sv},
                  "public panel captures must remain mutually exclusive");
  requireRejected(
      std::array{"--capture-controller-calibration-panel"sv, "controller.bmp"sv,
                 "--capture-controller-bindings-panel"sv, "bindings.bmp"sv},
      "public controller panel captures must remain mutually exclusive");
  requireRejected(std::array{"--capture-settings-panel"sv, "settings.png"sv},
                  "settings-panel capture must require BMP output");
  requireRejected(std::array{"--capture-controller-calibration-panel"sv,
                             "controller.png"sv},
                  "controller calibration capture must require BMP output");
  requireRejected(
      std::array{"--capture-controller-bindings-panel"sv, "bindings.png"sv},
      "controller bindings capture must require BMP output");
  requireRejected(
      std::array{"--capture-controller-bindings-panel"sv, "one.bmp"sv,
                 "--capture-controller-bindings-panel"sv, "two.bmp"sv},
      "duplicate controller bindings capture must fail closed");
  requireRejected(std::array{"--capture-diagnostic-frame"sv, "public.bmp"sv,
                             "--no-render-diagnostics"sv},
                  "diagnostic capture and disabled diagnostics must conflict");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv,
                             "--capture-overview-frame"sv, "overview.bmp"sv,
                             "--no-render-diagnostics"sv},
                  "overview capture and disabled diagnostics must conflict");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                 "mission.afs"sv, "--level"sv, "mission.level"sv,
                 "--capture-crosshair-validation-frame"sv, "crosshair.bmp"sv,
                 "--no-render-diagnostics"sv},
      "crosshair validation and disabled diagnostics must conflict");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv,
                             "--capture-frame"sv, "frame.bmp"sv,
                             "--capture-overview-frame"sv, "overview.bmp"sv},
                  "private capture modes must remain mutually exclusive");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                 "mission.afs"sv, "--level"sv, "mission.level"sv,
                 "--capture-overview-frame"sv, "overview.bmp"sv,
                 "--capture-crosshair-validation-frame"sv, "crosshair.bmp"sv},
      "overview and crosshair validation captures must remain exclusive");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv,
                             "--capture-frame"sv, "frame.bmp"sv,
                             "--capture-size"sv, "0x2160"sv},
                  "zero capture dimension must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv,
                             "--capture-frame"sv, "frame.bmp"sv,
                             "--capture-size"sv, "16385x2160"sv},
                  "capture dimension above D3D11 limits must fail closed");
  requireRejected(std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                             "mission.afs"sv, "--level"sv, "mission.level"sv,
                             "--capture-frame"sv, "frame.bmp"sv,
                             "--capture-size"sv, "1920x1080x2"sv},
                  "malformed capture size must fail closed");
}

void testPrivateCaptureRequest() {
  const std::array arguments{
      "--content-root"sv,  "private-pack"sv,      "--setup"sv,
      "mission.afs"sv,     "--level"sv,           "mission.level"sv,
      "--capture-frame"sv, "private-frame.bmp"sv, "--capture-size"sv,
      "3840X2160"sv,       "--render-scale"sv,    "200"sv,
      "--original-4x3"sv,
  };
  const auto options = parse(arguments);
  require(options.mission.has_value() &&
              options.captureFrameOutput ==
                  std::filesystem::path("private-frame.bmp") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{3840U, 2160U} &&
              options.renderOverrides.renderScalePercent == 200.0F &&
              options.renderOverrides.scenePresentation ==
                  ScenePresentationMode::originalFourByThree &&
              !options.validateContentOnly,
          "private capture request was not retained");
}

void testPrivateOverviewCaptureRequest() {
  const std::array arguments{
      "--content-root"sv,  "private-pack"sv,   "--setup"sv,
      "mission.afs"sv,     "--level"sv,        "mission.level"sv,
      "--player-object"sv, "player.object"sv,  "--capture-overview-frame"sv,
      "overview.BMP"sv,    "--capture-size"sv, "1920x1080"sv,
  };
  const auto options = parse(arguments);
  require(options.mission.has_value() &&
              options.captureOverviewFrameOutput ==
                  std::filesystem::path("overview.BMP") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{1920U, 1080U} &&
              options.renderOverrides.diagnosticsOverlayEnabled == true &&
              !options.captureFrameOutput && !options.validateContentOnly,
          "private overview capture request was not retained");
}

void testPrivateCrosshairValidationCaptureRequest() {
  const std::array arguments{
      "--content-root"sv,
      "private-pack"sv,
      "--setup"sv,
      "mission.afs"sv,
      "--level"sv,
      "mission.level"sv,
      "--capture-crosshair-validation-frame"sv,
      "crosshair.bmp"sv,
      "--capture-size"sv,
      "1920x1080"sv,
  };
  const auto options = parse(arguments);
  require(options.mission.has_value() &&
              options.captureCrosshairValidationFrameOutput ==
                  std::filesystem::path("crosshair.bmp") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{1920U, 1080U} &&
              options.renderOverrides.diagnosticsOverlayEnabled == true &&
              !options.captureFrameOutput &&
              !options.captureOverviewFrameOutput &&
              !options.validateContentOnly,
          "private crosshair validation capture request was not retained");
}

void testPrivateHealthGaugeValidationCaptureRequest() {
  const std::array arguments{
      "--content-root"sv,
      "private-pack"sv,
      "--setup"sv,
      "mission.afs"sv,
      "--level"sv,
      "mission.level"sv,
      "--capture-health-gauge-validation-frame"sv,
      "health-gauge.BMP"sv,
      "--capture-size"sv,
      "1920x1080"sv,
  };
  const auto options = parse(arguments);
  require(options.mission.has_value() &&
              options.captureHealthGaugeValidationFrameOutput ==
                  std::filesystem::path("health-gauge.BMP") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{1920U, 1080U} &&
              options.renderOverrides.diagnosticsOverlayEnabled == true &&
              !options.captureFrameOutput &&
              !options.captureOverviewFrameOutput &&
              !options.captureCrosshairValidationFrameOutput &&
              !options.validateContentOnly,
          "private health-gauge validation capture request was not retained");
}

void testPublicDiagnosticCaptureRequest() {
  const std::array arguments{
      "--capture-diagnostic-frame"sv,
      "diagnostics.bmp"sv,
      "--capture-size"sv,
      "2560x1440"sv,
      "--render-scale"sv,
      "75"sv,
      "--original-4x3"sv,
  };
  const auto options = parse(arguments);
  require(options.captureDiagnosticFrameOutput ==
                  std::filesystem::path("diagnostics.bmp") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{2560U, 1440U} &&
              options.renderOverrides.diagnosticsOverlayEnabled == true &&
              options.renderOverrides.renderScalePercent == 75.0F &&
              options.renderOverrides.scenePresentation ==
                  ScenePresentationMode::originalFourByThree &&
              !options.contentRoot && !options.mission,
          "public diagnostic capture request was not retained");
}

void testPublicSettingsPanelCaptureRequest() {
  const std::array arguments{
      "--capture-settings-panel"sv, "settings.bmp"sv,
      "--capture-size"sv,           "3440x1440"sv,
      "--render-scale"sv,           "125"sv,
      "--visual-profile"sv,         "enhanced"sv,
  };
  const auto options = parse(arguments);
  require(options.captureSettingsPanelOutput ==
                  std::filesystem::path("settings.bmp") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{3440U, 1440U} &&
              options.renderOverrides.renderScalePercent == 125.0F &&
              options.renderOverrides.visualProfile ==
                  VisualProfile::enhanced &&
              !options.captureDiagnosticFrameOutput && !options.contentRoot &&
              !options.mission,
          "public settings-panel capture request was not retained");
}

void testPublicControllerCalibrationPanelCaptureRequest() {
  const std::array arguments{
      "--capture-controller-calibration-panel"sv,
      "controller.bmp"sv,
      "--capture-size"sv,
      "2560x1440"sv,
  };
  const auto options = parse(arguments);
  require(options.captureControllerCalibrationPanelOutput ==
                  std::filesystem::path("controller.bmp") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{2560U, 1440U} &&
              !options.captureDiagnosticFrameOutput &&
              !options.captureSettingsPanelOutput && !options.contentRoot &&
              !options.mission,
          "public controller calibration capture request was not retained");
}

void testPublicControllerBindingsPanelCaptureRequest() {
  const std::array arguments{
      "--capture-controller-bindings-panel"sv,
      "bindings.BMP"sv,
      "--capture-size"sv,
      "640x360"sv,
      "--visual-profile"sv,
      "enhanced"sv,
  };
  const auto options = parse(arguments);
  require(options.captureControllerBindingsPanelOutput ==
                  std::filesystem::path("bindings.BMP") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{640U, 360U} &&
              options.renderOverrides.visualProfile ==
                  VisualProfile::enhanced &&
              !options.captureDiagnosticFrameOutput &&
              !options.captureSettingsPanelOutput &&
              !options.captureControllerCalibrationPanelOutput &&
              !options.contentRoot && !options.mission,
          "public controller bindings capture request was not retained");
}

} // namespace

int main() {
  try {
    testEmptyAndSmokeModes();
    testPresentationOptions();
    testAuthenticatedMissionRequest();
    testInstalledContentAndImportRequests();
    testValidationAndRejections();
    testPrivateCaptureRequest();
    testPrivateOverviewCaptureRequest();
    testPrivateCrosshairValidationCaptureRequest();
    testPrivateHealthGaugeValidationCaptureRequest();
    testPublicDiagnosticCaptureRequest();
    testPublicSettingsPanelCaptureRequest();
    testPublicControllerCalibrationPanelCaptureRequest();
    testPublicControllerBindingsPanelCaptureRequest();
    std::cout << "Airfix Windows command-line tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Airfix Windows command-line tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
