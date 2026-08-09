#include "airfix/content/MissionLaunchSelectionCodec.hpp"
#include "airfix/io/DurableFile.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options final {
  std::string setup;
  std::string level;
  std::optional<std::string> playerObject;
  std::uint32_t startIndex{};
  std::filesystem::path output;
};

[[nodiscard]] std::uint32_t parseStartIndex(const std::string_view value) {
  std::uint32_t result = 0U;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result, 10);
  if (value.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size()) {
    throw std::invalid_argument("--start-index must be a uint32 value");
  }
  return result;
}

[[nodiscard]] Options parseOptions(const int argumentCount,
                                   const char *const arguments[]) {
  Options options;
  bool hasStartIndex = false;
  for (int index = 1; index < argumentCount; ++index) {
    const std::string_view option = arguments[index];
    if (option == "--help") {
      std::cout << "usage: afmission-create --setup <logical-path> "
                   "--level <logical-path> [--player-object <logical-path>] "
                   "[--start-index <uint32>] --output <file.afmission>\n";
      std::exit(0);
    }
    if (index + 1 >= argumentCount) {
      throw std::invalid_argument("missing value after option");
    }
    const std::string value = arguments[++index];
    if (value.empty()) {
      throw std::invalid_argument("empty option value");
    }
    if (option == "--setup" && options.setup.empty()) {
      options.setup = value;
    } else if (option == "--level" && options.level.empty()) {
      options.level = value;
    } else if (option == "--player-object" &&
               !options.playerObject.has_value()) {
      options.playerObject = value;
    } else if (option == "--start-index" && !hasStartIndex) {
      options.startIndex = parseStartIndex(value);
      hasStartIndex = true;
    } else if (option == "--output" && options.output.empty()) {
      options.output = value;
    } else {
      throw std::invalid_argument("unknown or duplicate option");
    }
  }
  if (options.setup.empty() || options.level.empty() ||
      options.output.empty()) {
    throw std::invalid_argument("--setup, --level, and --output are required");
  }
  if (options.output.extension() != ".afmission") {
    throw std::invalid_argument("--output must use the .afmission extension");
  }
  return options;
}

} // namespace

int main(const int argumentCount, const char *const arguments[]) {
  try {
    const auto options = parseOptions(argumentCount, arguments);
    const auto bytes = airfix::content::encodeMissionLaunchSelection({
        .setupLogicalPath = options.setup,
        .levelLogicalPath = options.level,
        .playerObjectLogicalPath = options.playerObject,
        .requestedStartIndex = options.startIndex,
    });
    airfix::io::writeFileExclusiveDurable(options.output, bytes);
    std::cout << "created private mission selection: bytes=" << bytes.size()
              << " player=" << (options.playerObject.has_value() ? "yes" : "no")
              << " start=" << options.startIndex << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "afmission-create: " << error.what() << '\n';
    return 1;
  }
}
