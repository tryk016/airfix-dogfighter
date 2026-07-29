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
  std::optional<std::filesystem::path> captureDiagnosticFrameOutput;
  std::optional<AirfixWindowsCaptureSize> captureSize;
  std::optional<std::uint32_t> renderScalePercent;
  bool originalFourByThreeSeen = false;
  bool renderDiagnosticsSeen = false;

  for (std::size_t index = 0U; index < arguments.size(); ++index) {
    const auto option = arguments[index];
    if (option == "--smoke-test") {
      if (options.smokeTest) {
        invalidCommandLine();
      }
      options.smokeTest = true;
    } else if (option == "--render-scale") {
      if (renderScalePercent.has_value()) {
        invalidCommandLine();
      }
      renderScalePercent =
          parseRenderScalePercent(requireValue(arguments, index));
    } else if (option == "--original-4x3") {
      if (originalFourByThreeSeen) {
        invalidCommandLine();
      }
      originalFourByThreeSeen = true;
      options.originalFourByThreePresentation = true;
    } else if (option == "--render-diagnostics") {
      if (renderDiagnosticsSeen) {
        invalidCommandLine();
      }
      renderDiagnosticsSeen = true;
      options.renderDiagnostics = true;
    } else if (option == "--content-root" ||
               option == "--validate-content-root") {
      if (options.contentRoot.has_value()) {
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
    } else if (option == "--capture-diagnostic-frame") {
      if (captureDiagnosticFrameOutput.has_value()) {
        invalidCommandLine();
      }
      captureDiagnosticFrameOutput =
          std::filesystem::path(requireValue(arguments, index));
      const auto extension =
          captureDiagnosticFrameOutput->extension().string();
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
      hasAnyMissionOption || captureFrameOutput.has_value();
  const bool hasAnyCapture =
      captureFrameOutput.has_value() ||
      captureDiagnosticFrameOutput.has_value();
  if ((options.smokeTest &&
       (options.contentRoot.has_value() || hasContentSpecificOption ||
        captureDiagnosticFrameOutput.has_value() ||
        captureSize.has_value())) ||
      (!options.contentRoot.has_value() && hasContentSpecificOption) ||
      (options.contentRoot.has_value() &&
       captureDiagnosticFrameOutput.has_value()) ||
      (hasAnyMissionOption && !hasMissionPair) ||
      (captureFrameOutput.has_value() &&
       (options.validateContentOnly || !hasMissionPair)) ||
      (captureDiagnosticFrameOutput.has_value() &&
       (hasAnyMissionOption || options.validateContentOnly)) ||
      (captureSize.has_value() && !hasAnyCapture) ||
      (captureFrameOutput.has_value() &&
       captureDiagnosticFrameOutput.has_value())) {
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
  options.captureDiagnosticFrameOutput =
      std::move(captureDiagnosticFrameOutput);
  options.captureSize = captureSize;
  options.renderScalePercent =
      renderScalePercent.value_or(options.renderScalePercent);
  if (options.captureDiagnosticFrameOutput.has_value()) {
    options.renderDiagnostics = true;
  }
  return options;
}

} // namespace airfix::windows
