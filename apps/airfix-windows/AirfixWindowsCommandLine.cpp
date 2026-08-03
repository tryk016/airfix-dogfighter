#include "AirfixWindowsCommandLine.hpp"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace airfix::windows {
namespace {

constexpr std::size_t maximumPrivatePathBytes = 4096U;
constexpr std::uint32_t maximumCaptureDimension = 16384U;
constexpr std::uint32_t minimumRenderScalePercent = 50U;
constexpr std::uint32_t maximumRenderScalePercent = 200U;
constexpr std::uint32_t maximumVerticalFovAdjustmentDegrees = 25U;

[[noreturn]] void invalidCommandLine() {
  throw std::runtime_error(std::string(airfixWindowsUsage()));
}

[[nodiscard]] std::string_view
requireValue(const std::span<const std::string_view> arguments,
             std::size_t &index) {
  if (index + 1U >= arguments.size()) {
    invalidCommandLine();
  }
  ++index;
  const auto value = arguments[index];
  if (value.empty() || value.size() > maximumPrivatePathBytes) {
    invalidCommandLine();
  }
  return value;
}

[[nodiscard]] std::uint32_t parseStartIndex(const std::string_view value) {
  std::uint64_t parsed = 0U;
  const auto *const first = value.data();
  const auto *const last = first + value.size();
  const auto result = std::from_chars(first, last, parsed, 10);
  if (result.ec != std::errc{} || result.ptr != last ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    invalidCommandLine();
  }
  return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] std::uint32_t
parseRenderScalePercent(const std::string_view value) {
  std::uint32_t parsed = 0U;
  const auto *const first = value.data();
  const auto *const last = first + value.size();
  const auto result = std::from_chars(first, last, parsed, 10);
  if (result.ec != std::errc{} || result.ptr != last ||
      parsed < minimumRenderScalePercent ||
      parsed > maximumRenderScalePercent) {
    invalidCommandLine();
  }
  return parsed;
}

[[nodiscard]] std::uint32_t
parseVerticalFovAdjustmentDegrees(const std::string_view value) {
  std::uint32_t parsed = 0U;
  const auto *const first = value.data();
  const auto *const last = first + value.size();
  const auto result = std::from_chars(first, last, parsed, 10);
  if (result.ec != std::errc{} || result.ptr != last ||
      parsed > maximumVerticalFovAdjustmentDegrees) {
    invalidCommandLine();
  }
  return parsed;
}

[[nodiscard]] AirfixWindowsCaptureSize
parseCaptureSize(const std::string_view value) {
  const auto separator = value.find_first_of("xX");
  if (separator == std::string_view::npos || separator == 0U ||
      separator + 1U >= value.size() ||
      value.find_first_of("xX", separator + 1U) != std::string_view::npos) {
    invalidCommandLine();
  }

  const auto parseDimension = [](const std::string_view text) {
    std::uint32_t parsed = 0U;
    const auto *const first = text.data();
    const auto *const last = first + text.size();
    const auto result = std::from_chars(first, last, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != last || parsed == 0U ||
        parsed > maximumCaptureDimension) {
      invalidCommandLine();
    }
    return parsed;
  };
  return {
      .width = parseDimension(value.substr(0U, separator)),
      .height = parseDimension(value.substr(separator + 1U)),
  };
}

} // namespace

AirfixWindowsCommandLineOptions parseAirfixWindowsCommandLine(
    const std::span<const std::string_view> arguments) {
  AirfixWindowsCommandLineOptions options;
  if (arguments.empty()) {
    return options;
  }

  std::optional<std::string> setup;
  std::optional<std::string> level;
  std::optional<std::string> playerObject;
  std::optional<std::uint32_t> startIndex;
  std::optional<std::filesystem::path> captureFrameOutput;
  std::optional<std::filesystem::path> captureOverviewFrameOutput;
  std::optional<std::filesystem::path> captureCrosshairValidationFrameOutput;
  std::optional<std::filesystem::path> captureHealthGaugeValidationFrameOutput;
  std::optional<std::filesystem::path> captureHudValidationFrameOutput;
  std::optional<std::filesystem::path> captureDiagnosticFrameOutput;
  std::optional<std::filesystem::path> captureSettingsPanelOutput;
  std::optional<std::filesystem::path> captureControllerCalibrationPanelOutput;
  std::optional<std::filesystem::path> captureControllerBindingsPanelOutput;
  std::optional<std::filesystem::path> importAfPackSource;
  std::optional<AirfixWindowsCaptureSize> captureSize;
  bool scenePresentationSeen = false;
  bool visualProfileSeen = false;
  bool renderDiagnosticsSeen = false;

  for (std::size_t index = 0U; index < arguments.size(); ++index) {
    const auto option = arguments[index];
    if (option == "--smoke-test") {
      if (options.smokeTest) {
        invalidCommandLine();
      }
      options.smokeTest = true;
    } else if (option == "--render-scale") {
      if (options.renderOverrides.renderScalePercent.has_value()) {
        invalidCommandLine();
      }
      options.renderOverrides.renderScalePercent = static_cast<float>(
          parseRenderScalePercent(requireValue(arguments, index)));
    } else if (option == "--original-4x3" ||
               option == "--widescreen-hor-plus") {
      if (scenePresentationSeen) {
        invalidCommandLine();
      }
      scenePresentationSeen = true;
      options.renderOverrides.scenePresentation =
          option == "--original-4x3"
              ? airfix::render::ScenePresentationMode::originalFourByThree
              : airfix::render::ScenePresentationMode::widescreenHorPlus;
    } else if (option == "--vertical-fov-adjustment") {
      if (options.renderOverrides.verticalFovAdjustmentDegrees.has_value()) {
        invalidCommandLine();
      }
      options.renderOverrides.verticalFovAdjustmentDegrees = static_cast<float>(
          parseVerticalFovAdjustmentDegrees(requireValue(arguments, index)));
    } else if (option == "--visual-profile") {
      if (visualProfileSeen) {
        invalidCommandLine();
      }
      visualProfileSeen = true;
      const auto value = requireValue(arguments, index);
      if (value == "classic") {
        options.renderOverrides.visualProfile =
            airfix::render::VisualProfile::classic;
      } else if (value == "enhanced") {
        options.renderOverrides.visualProfile =
            airfix::render::VisualProfile::enhanced;
      } else {
        invalidCommandLine();
      }
    } else if (option == "--render-diagnostics" ||
               option == "--no-render-diagnostics") {
      if (renderDiagnosticsSeen) {
        invalidCommandLine();
      }
      renderDiagnosticsSeen = true;
      options.renderOverrides.diagnosticsOverlayEnabled =
          option == "--render-diagnostics";
    } else if (option == "--import-afpack") {
      if (importAfPackSource.has_value()) {
        invalidCommandLine();
      }
      importAfPackSource =
          std::filesystem::path(requireValue(arguments, index));
      if (importAfPackSource->extension() != ".afpack") {
        invalidCommandLine();
      }
    } else if (option == "--manage-installed-content") {
      if (options.manageInstalledContent) {
        invalidCommandLine();
      }
      options.manageInstalledContent = true;
    } else if (option == "--installed-content" ||
               option == "--validate-installed-content") {
      if (options.useInstalledContent || options.contentRoot.has_value()) {
        invalidCommandLine();
      }
      options.useInstalledContent = true;
      options.validateContentOnly = option == "--validate-installed-content";
    } else if (option == "--content-root" ||
               option == "--validate-content-root") {
      if (options.contentRoot.has_value() || options.useInstalledContent) {
        invalidCommandLine();
      }
      const auto value = requireValue(arguments, index);
      options.contentRoot = std::filesystem::path(value);
      options.validateContentOnly = option == "--validate-content-root";
    } else if (option == "--setup") {
      if (setup.has_value()) {
        invalidCommandLine();
      }
      setup = std::string(requireValue(arguments, index));
    } else if (option == "--level") {
      if (level.has_value()) {
        invalidCommandLine();
      }
      level = std::string(requireValue(arguments, index));
    } else if (option == "--player-object") {
      if (playerObject.has_value()) {
        invalidCommandLine();
      }
      playerObject = std::string(requireValue(arguments, index));
    } else if (option == "--start-index") {
      if (startIndex.has_value()) {
        invalidCommandLine();
      }
      startIndex = parseStartIndex(requireValue(arguments, index));
    } else if (option == "--capture-frame") {
      if (captureFrameOutput.has_value()) {
        invalidCommandLine();
      }
      captureFrameOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension = captureFrameOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-overview-frame") {
      if (captureOverviewFrameOutput.has_value()) {
        invalidCommandLine();
      }
      captureOverviewFrameOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension = captureOverviewFrameOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-crosshair-validation-frame") {
      if (captureCrosshairValidationFrameOutput.has_value()) {
        invalidCommandLine();
      }
      captureCrosshairValidationFrameOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension =
          captureCrosshairValidationFrameOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-health-gauge-validation-frame") {
      if (captureHealthGaugeValidationFrameOutput.has_value()) {
        invalidCommandLine();
      }
      captureHealthGaugeValidationFrameOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension =
          captureHealthGaugeValidationFrameOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-hud-validation-frame") {
      if (captureHudValidationFrameOutput.has_value()) {
        invalidCommandLine();
      }
      captureHudValidationFrameOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension =
          captureHudValidationFrameOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-diagnostic-frame") {
      if (captureDiagnosticFrameOutput.has_value()) {
        invalidCommandLine();
      }
      captureDiagnosticFrameOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension = captureDiagnosticFrameOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-settings-panel") {
      if (captureSettingsPanelOutput.has_value()) {
        invalidCommandLine();
      }
      captureSettingsPanelOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension = captureSettingsPanelOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-controller-calibration-panel") {
      if (captureControllerCalibrationPanelOutput.has_value()) {
        invalidCommandLine();
      }
      captureControllerCalibrationPanelOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension =
          captureControllerCalibrationPanelOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-controller-bindings-panel") {
      if (captureControllerBindingsPanelOutput.has_value()) {
        invalidCommandLine();
      }
      captureControllerBindingsPanelOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension =
          captureControllerBindingsPanelOutput->extension().string();
      if (extension != ".bmp" && extension != ".BMP") {
        invalidCommandLine();
      }
    } else if (option == "--capture-size") {
      if (captureSize.has_value()) {
        invalidCommandLine();
      }
      captureSize = parseCaptureSize(requireValue(arguments, index));
    } else {
      invalidCommandLine();
    }
  }

  const bool hasMissionPair = setup.has_value() && level.has_value();
  const bool hasAnyMissionOption = setup.has_value() || level.has_value() ||
                                   playerObject.has_value() ||
                                   startIndex.has_value();
  const bool hasContentSpecificOption =
      hasAnyMissionOption || captureFrameOutput.has_value() ||
      captureOverviewFrameOutput.has_value() ||
      captureCrosshairValidationFrameOutput.has_value() ||
      captureHealthGaugeValidationFrameOutput.has_value() ||
      captureHudValidationFrameOutput.has_value();
  const bool hasAnyCapture =
      captureFrameOutput.has_value() ||
      captureOverviewFrameOutput.has_value() ||
      captureCrosshairValidationFrameOutput.has_value() ||
      captureHealthGaugeValidationFrameOutput.has_value() ||
      captureHudValidationFrameOutput.has_value() ||
      captureDiagnosticFrameOutput.has_value() ||
      captureSettingsPanelOutput.has_value() ||
      captureControllerCalibrationPanelOutput.has_value() ||
      captureControllerBindingsPanelOutput.has_value();
  const bool hasContentSelection =
      options.contentRoot.has_value() || options.useInstalledContent;
  const bool hasRenderOverride =
      options.renderOverrides.renderScalePercent.has_value() ||
      options.renderOverrides.scenePresentation.has_value() ||
      options.renderOverrides.verticalFovAdjustmentDegrees.has_value() ||
      options.renderOverrides.visualProfile.has_value() ||
      options.renderOverrides.diagnosticsOverlayEnabled.has_value();
  const bool hasImport = importAfPackSource.has_value();
  if ((options.smokeTest &&
       (hasContentSelection || hasContentSpecificOption ||
        captureDiagnosticFrameOutput.has_value() ||
        captureSettingsPanelOutput.has_value() ||
        captureControllerCalibrationPanelOutput.has_value() ||
        captureControllerBindingsPanelOutput.has_value() ||
        captureSize.has_value())) ||
      (!hasContentSelection && hasContentSpecificOption) ||
      (hasContentSelection &&
       (captureDiagnosticFrameOutput.has_value() ||
        captureSettingsPanelOutput.has_value() ||
        captureControllerCalibrationPanelOutput.has_value() ||
        captureControllerBindingsPanelOutput.has_value())) ||
      (hasAnyMissionOption && !hasMissionPair) ||
      (captureFrameOutput.has_value() &&
       (options.validateContentOnly || !hasMissionPair)) ||
      (captureOverviewFrameOutput.has_value() &&
       (options.validateContentOnly || !hasMissionPair)) ||
      (captureCrosshairValidationFrameOutput.has_value() &&
       (options.validateContentOnly || !hasMissionPair)) ||
      (captureHealthGaugeValidationFrameOutput.has_value() &&
       (options.validateContentOnly || !hasMissionPair)) ||
      (captureHudValidationFrameOutput.has_value() &&
       (options.validateContentOnly || !hasMissionPair)) ||
      (captureDiagnosticFrameOutput.has_value() &&
       (hasAnyMissionOption || options.validateContentOnly)) ||
      (captureSettingsPanelOutput.has_value() &&
       (hasAnyMissionOption || options.validateContentOnly)) ||
      (captureControllerCalibrationPanelOutput.has_value() &&
       (hasAnyMissionOption || options.validateContentOnly)) ||
      (captureControllerBindingsPanelOutput.has_value() &&
       (hasAnyMissionOption || options.validateContentOnly)) ||
      (captureSize.has_value() && !hasAnyCapture) ||
      (hasImport &&
       (options.smokeTest || hasContentSelection || hasContentSpecificOption ||
        hasAnyCapture || captureSize.has_value() || hasRenderOverride)) ||
      (options.manageInstalledContent &&
       (options.smokeTest || hasContentSelection || hasContentSpecificOption ||
        hasAnyCapture || captureSize.has_value() || hasRenderOverride ||
        hasImport)) ||
      ((static_cast<unsigned>(captureFrameOutput.has_value()) +
        static_cast<unsigned>(captureOverviewFrameOutput.has_value()) +
        static_cast<unsigned>(
            captureCrosshairValidationFrameOutput.has_value()) +
        static_cast<unsigned>(
            captureHealthGaugeValidationFrameOutput.has_value()) +
        static_cast<unsigned>(captureHudValidationFrameOutput.has_value()) +
        static_cast<unsigned>(captureDiagnosticFrameOutput.has_value()) +
        static_cast<unsigned>(captureSettingsPanelOutput.has_value()) +
        static_cast<unsigned>(
            captureControllerCalibrationPanelOutput.has_value()) +
        static_cast<unsigned>(
            captureControllerBindingsPanelOutput.has_value())) > 1U)) {
    invalidCommandLine();
  }
  if (hasMissionPair) {
    options.mission = AirfixWindowsMissionOptions{
        .setupLogicalPath = std::move(*setup),
        .levelLogicalPath = std::move(*level),
        .playerObjectLogicalPath = std::move(playerObject),
        .requestedStartIndex = startIndex.value_or(0U),
    };
  }
  options.captureFrameOutput = std::move(captureFrameOutput);
  options.captureOverviewFrameOutput = std::move(captureOverviewFrameOutput);
  options.captureCrosshairValidationFrameOutput =
      std::move(captureCrosshairValidationFrameOutput);
  options.captureHealthGaugeValidationFrameOutput =
      std::move(captureHealthGaugeValidationFrameOutput);
  options.captureHudValidationFrameOutput =
      std::move(captureHudValidationFrameOutput);
  options.captureDiagnosticFrameOutput =
      std::move(captureDiagnosticFrameOutput);
  options.captureSettingsPanelOutput = std::move(captureSettingsPanelOutput);
  options.captureControllerCalibrationPanelOutput =
      std::move(captureControllerCalibrationPanelOutput);
  options.captureControllerBindingsPanelOutput =
      std::move(captureControllerBindingsPanelOutput);
  options.importAfPackSource = std::move(importAfPackSource);
  options.captureSize = captureSize;
  if (options.captureDiagnosticFrameOutput.has_value()) {
    if (options.renderOverrides.diagnosticsOverlayEnabled == false) {
      invalidCommandLine();
    }
    options.renderOverrides.diagnosticsOverlayEnabled = true;
  }
  if (options.captureOverviewFrameOutput.has_value()) {
    if (options.renderOverrides.diagnosticsOverlayEnabled == false) {
      invalidCommandLine();
    }
    options.renderOverrides.diagnosticsOverlayEnabled = true;
  }
  if (options.captureCrosshairValidationFrameOutput.has_value()) {
    if (options.renderOverrides.diagnosticsOverlayEnabled == false) {
      invalidCommandLine();
    }
    options.renderOverrides.diagnosticsOverlayEnabled = true;
  }
  if (options.captureHealthGaugeValidationFrameOutput.has_value()) {
    if (options.renderOverrides.diagnosticsOverlayEnabled == false) {
      invalidCommandLine();
    }
    options.renderOverrides.diagnosticsOverlayEnabled = true;
  }
  if (options.captureHudValidationFrameOutput.has_value()) {
    if (options.renderOverrides.diagnosticsOverlayEnabled == false) {
      invalidCommandLine();
    }
    options.renderOverrides.diagnosticsOverlayEnabled = true;
  }
  return options;
}

} // namespace airfix::windows
