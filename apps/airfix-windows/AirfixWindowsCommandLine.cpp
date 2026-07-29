#include "AirfixWindowsCommandLine.hpp"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace airfix::windows {
namespace {

constexpr std::size_t maximumPrivatePathBytes = 4096U;

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

} // namespace

AirfixWindowsCommandLineOptions parseAirfixWindowsCommandLine(
    const std::span<const std::string_view> arguments) {
  AirfixWindowsCommandLineOptions options;
  if (arguments.empty()) {
    return options;
  }
  if (arguments.size() == 1U && arguments.front() == "--smoke-test") {
    options.smokeTest = true;
    return options;
  }

  std::optional<std::string> setup;
  std::optional<std::string> level;
  std::optional<std::string> playerObject;
  std::optional<std::uint32_t> startIndex;
  std::optional<std::filesystem::path> captureFrameOutput;

  for (std::size_t index = 0U; index < arguments.size(); ++index) {
    const auto option = arguments[index];
    if (option == "--content-root" || option == "--validate-content-root") {
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
    } else {
      invalidCommandLine();
    }
  }

  const bool hasMissionPair = setup.has_value() && level.has_value();
  const bool hasAnyMissionOption = setup.has_value() || level.has_value() ||
                                   playerObject.has_value() ||
                                   startIndex.has_value();
  if (!options.contentRoot.has_value() ||
      (hasAnyMissionOption && !hasMissionPair) ||
      (captureFrameOutput.has_value() &&
       (options.validateContentOnly || !hasMissionPair))) {
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
  return options;
}

} // namespace airfix::windows
