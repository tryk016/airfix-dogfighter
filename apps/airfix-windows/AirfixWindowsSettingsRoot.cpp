#include "AirfixWindowsSettingsRoot.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace airfix::windows {
namespace {

constexpr char organizationName[] = "tryk016";
constexpr char applicationName[] = "Airfix Dogfighter";
constexpr std::string_view settingsDirectoryName = "settings";

struct SdlMemoryDeleter final {
  void operator()(char *const memory) const noexcept { SDL_free(memory); }
};

using SdlString = std::unique_ptr<char, SdlMemoryDeleter>;

[[noreturn]] void invalidPreferenceRoot() {
  throw std::runtime_error("SDL returned an invalid preference root");
}

[[nodiscard]] std::u8string asUtf8String(const std::string_view bytes) {
  std::u8string result;
  result.reserve(bytes.size());
  for (const unsigned char byte : bytes) {
    result.push_back(static_cast<char8_t>(byte));
  }
  return result;
}

} // namespace

std::filesystem::path airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
    const std::string_view utf8PreferenceRoot) {
  if (utf8PreferenceRoot.empty() ||
      utf8PreferenceRoot.find('\0') != std::string_view::npos) {
    invalidPreferenceRoot();
  }

  std::filesystem::path preferenceRoot;
  try {
    // A char8_t path source has UTF-8 semantics in C++20, including on
    // Windows where std::filesystem::path stores a native UTF-16 path.
    preferenceRoot = std::filesystem::path(asUtf8String(utf8PreferenceRoot));
  } catch (const std::filesystem::filesystem_error &) {
    invalidPreferenceRoot();
  }

  if (preferenceRoot.empty() || !preferenceRoot.is_absolute() ||
      !preferenceRoot.has_root_path()) {
    invalidPreferenceRoot();
  }

  return preferenceRoot.lexically_normal() /
         std::filesystem::path(settingsDirectoryName);
}

std::filesystem::path resolveAirfixWindowsSettingsDirectory() {
  SdlString preferenceRoot(SDL_GetPrefPath(organizationName, applicationName));
  if (!preferenceRoot) {
    throw std::runtime_error(
        "SDL could not resolve the private settings directory");
  }
  return airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
      preferenceRoot.get());
}

} // namespace airfix::windows
