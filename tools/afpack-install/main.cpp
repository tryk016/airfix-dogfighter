#include "airfix/package/AfPackInstaller.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct Options final {
  std::filesystem::path source;
  std::filesystem::path contentRoot;
  std::string transactionId;
};

[[nodiscard]] Options parseOptions(const int argumentCount,
                                   const char *const arguments[]) {
  std::optional<std::filesystem::path> source;
  std::optional<std::filesystem::path> contentRoot;
  std::optional<std::string> transactionId;

  for (int index = 1; index < argumentCount; ++index) {
    const std::string_view argument = arguments[index];
    if (argument == "--help") {
      std::cout << "usage: afpack-install --source <file.afpack> "
                   "--content-root <private-directory> --transaction <uuid>\n";
      std::exit(0);
    }
    if (index + 1 >= argumentCount) {
      throw std::invalid_argument("missing value after option");
    }
    const std::string_view value = arguments[++index];
    if (value.empty()) {
      throw std::invalid_argument("empty option value");
    }
    if (argument == "--source" && !source.has_value()) {
      source = std::filesystem::path(value);
    } else if (argument == "--content-root" && !contentRoot.has_value()) {
      contentRoot = std::filesystem::path(value);
    } else if (argument == "--transaction" && !transactionId.has_value()) {
      transactionId = std::string(value);
    } else {
      throw std::invalid_argument("unknown or duplicate option");
    }
  }

  if (!source.has_value() || !contentRoot.has_value() ||
      !transactionId.has_value()) {
    throw std::invalid_argument(
        "--source, --content-root, and --transaction are required");
  }
  return {
      .source = std::move(*source),
      .contentRoot = std::move(*contentRoot),
      .transactionId = std::move(*transactionId),
  };
}

} // namespace

int main(const int argumentCount, const char *const arguments[]) {
  try {
    const Options options = parseOptions(argumentCount, arguments);
    const auto result = airfix::afpack::installPack(
        options.source, options.contentRoot, options.transactionId);
    std::cout << "AFPACK installed: generation=" << result.active.generation
              << " bytes=" << result.size
              << " reused=" << (result.reusedExisting ? "yes" : "no") << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "afpack-install: " << error.what() << '\n';
    return 1;
  }
}
