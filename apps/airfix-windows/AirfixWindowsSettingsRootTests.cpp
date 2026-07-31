#include "AirfixWindowsSettingsRoot.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::windows::airfixWindowsContentDirectoryFromUtf8PreferenceRoot;
using airfix::windows::airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <typename Callable>
void requireRejected(Callable &&callable, const std::string_view message) {
  try {
    callable();
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

void absoluteRootAppendsSettings() {
  const std::filesystem::path root(
      u8"C:\\Users\\Example\\AppData\\Roaming\\tryk016\\"
      u8"Airfix Dogfighter");
  const auto result = airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
      "C:\\Users\\Example\\AppData\\Roaming\\tryk016\\"
      "Airfix Dogfighter\\");
  require(result == root.lexically_normal() / "settings",
          "absolute preference root did not append settings");
  require(result.is_absolute(), "settings directory must remain absolute");

  const auto content = airfixWindowsContentDirectoryFromUtf8PreferenceRoot(
      "C:\\Users\\Example\\AppData\\Roaming\\tryk016\\"
      "Airfix Dogfighter\\");
  require(content == root.lexically_normal() / "content",
          "absolute preference root did not append content");
  require(content.parent_path() == result.parent_path(),
          "settings and content must remain sibling directories");
}

void utf8RootSurvivesNativeConversion() {
  std::string utf8Root = "C:\\Users\\";
  utf8Root += "\xC5\xBB";
  utf8Root += "aneta\\Airfix";

  const auto result =
      airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(utf8Root);
  require(result.u8string() ==
              std::u8string(u8"C:\\Users\\\u017Baneta\\Airfix\\settings"),
          "UTF-8 preference root was not preserved");
}

void invalidRootsAreRejected() {
  requireRejected(
      [] { (void)airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(""); },
      "empty preference root was accepted");
  requireRejected(
      [] { (void)airfixWindowsContentDirectoryFromUtf8PreferenceRoot(""); },
      "empty preference root was accepted for content");
  requireRejected(
      [] {
        (void)airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
            "relative\\preference");
      },
      "relative preference root was accepted");
  requireRejected(
      [] {
        (void)airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
            "C:drive-relative");
      },
      "drive-relative preference root was accepted");
  requireRejected(
      [] {
        (void)airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
            "\\root-relative");
      },
      "root-relative preference root was accepted");
  requireRejected(
      [] {
        const std::string embeddedNull{"C:\\pref\0ignored",
                                       sizeof("C:\\pref\0ignored") - 1U};
        (void)airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
            embeddedNull);
      },
      "embedded NUL was accepted");
  requireRejected(
      [] {
        const std::string invalidUtf8{"C:\\pref\\\xC3\x28"};
        (void)airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(invalidUtf8);
      },
      "ill-formed UTF-8 was accepted");
}

} // namespace

int main() {
  try {
    absoluteRootAppendsSettings();
    utf8RootSurvivesNativeConversion();
    invalidRootsAreRejected();
  } catch (const std::exception &error) {
    std::cerr << "AirfixWindowsSettingsRootTests failed: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
