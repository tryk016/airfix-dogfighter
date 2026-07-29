#pragma once

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
  std::optional<std::filesystem::path> contentRoot;
  std::optional<AirfixWindowsMissionOptions> mission;
  std::optional<std::filesystem::path> captureFrameOutput;
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
  return "usage: AirfixDogfighter.exe [--smoke-test | "
         "--content-root <path> [--setup <logical-path> "
         "--level <logical-path> [--player-object <logical-path>] "
         "[--start-index <uint32>] [--capture-frame <private-output.bmp> "
         "[--capture-size <width>x<height>]]] | "
         "--validate-content-root <path> [--setup <logical-path> "
         "--level <logical-path> [--player-object <logical-path>] "
         "[--start-index <uint32>]]]";
}

} // namespace airfix::windows
