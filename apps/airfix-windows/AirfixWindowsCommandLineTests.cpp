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
              !defaultOptions.mission && !defaultOptions.captureSize &&
              defaultOptions.renderScalePercent == 100U &&
              !defaultOptions.originalFourByThreePresentation,
          "empty command line must retain the data-less interactive shell");

  const std::array smoke{"--smoke-test"sv};
  const auto smokeOptions = parse(smoke);
  require(smokeOptions.smokeTest && !smokeOptions.contentRoot &&
              !smokeOptions.mission &&
              smokeOptions.renderScalePercent == 100U,
          "smoke mode must remain data-less");
}

void testPresentationOptions() {
  const std::array smoke{
      "--smoke-test"sv,
      "--render-scale"sv,
      "50"sv,
      "--original-4x3"sv,
  };
  const auto smokeOptions = parse(smoke);
  require(smokeOptions.smokeTest &&
              smokeOptions.renderScalePercent == 50U &&
              smokeOptions.originalFourByThreePresentation &&
              !smokeOptions.contentRoot,
          "data-less smoke presentation settings were not retained");

  const std::array interactive{
      "--render-scale"sv,
      "200"sv,
  };
  const auto interactiveOptions = parse(interactive);
  require(!interactiveOptions.smokeTest &&
              interactiveOptions.renderScalePercent == 200U &&
              !interactiveOptions.originalFourByThreePresentation &&
              !interactiveOptions.contentRoot,
          "interactive render scale was not retained");
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
      std::array{"--smoke-test"sv, "--render-scale"sv, "49"sv},
      "render scale below 50 percent must fail closed");
  requireRejected(
      std::array{"--smoke-test"sv, "--render-scale"sv, "201"sv},
      "render scale above 200 percent must fail closed");
  requireRejected(
      std::array{"--smoke-test"sv, "--render-scale"sv, "-50"sv},
      "signed render scale must fail closed");
  requireRejected(
      std::array{"--smoke-test"sv, "--render-scale"sv, "50"sv,
                 "--render-scale"sv, "100"sv},
      "duplicate render scale must fail closed");
  requireRejected(
      std::array{"--smoke-test"sv, "--original-4x3"sv,
                 "--original-4x3"sv},
      "duplicate Original 4:3 mode must fail closed");
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
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--capture-size"sv,
                 "3840x2160"sv},
      "capture size without frame capture must fail closed");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                 "mission.afs"sv, "--level"sv, "mission.level"sv,
                 "--capture-frame"sv, "frame.bmp"sv, "--capture-size"sv,
                 "0x2160"sv},
      "zero capture dimension must fail closed");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                 "mission.afs"sv, "--level"sv, "mission.level"sv,
                 "--capture-frame"sv, "frame.bmp"sv, "--capture-size"sv,
                 "16385x2160"sv},
      "capture dimension above D3D11 limits must fail closed");
  requireRejected(
      std::array{"--content-root"sv, "private-pack"sv, "--setup"sv,
                 "mission.afs"sv, "--level"sv, "mission.level"sv,
                 "--capture-frame"sv, "frame.bmp"sv, "--capture-size"sv,
                 "1920x1080x2"sv},
      "malformed capture size must fail closed");
}

void testPrivateCaptureRequest() {
  const std::array arguments{
      "--content-root"sv,  "private-pack"sv,      "--setup"sv,
      "mission.afs"sv,     "--level"sv,           "mission.level"sv,
      "--capture-frame"sv, "private-frame.bmp"sv,
      "--capture-size"sv,  "3840X2160"sv,
      "--render-scale"sv,  "200"sv,
      "--original-4x3"sv,
  };
  const auto options = parse(arguments);
  require(options.mission.has_value() &&
              options.captureFrameOutput ==
                  std::filesystem::path("private-frame.bmp") &&
              options.captureSize ==
                  airfix::windows::AirfixWindowsCaptureSize{
                      3840U, 2160U} &&
              options.renderScalePercent == 200U &&
              options.originalFourByThreePresentation &&
              !options.validateContentOnly,
          "private capture request was not retained");
}

} // namespace

int main() {
  try {
    testEmptyAndSmokeModes();
    testPresentationOptions();
    testAuthenticatedMissionRequest();
    testValidationAndRejections();
    testPrivateCaptureRequest();
    std::cout << "Airfix Windows command-line tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Airfix Windows command-line tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
