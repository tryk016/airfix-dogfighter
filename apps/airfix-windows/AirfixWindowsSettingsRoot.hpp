#pragma once

#include <filesystem>
#include <string_view>

namespace airfix::windows {

// Converts an SDL UTF-8 preference root to the private settings directory.
// This function is storage-neutral: it validates only the lexical path and
// never creates, resolves, or inspects filesystem entries.
[[nodiscard]] std::filesystem::path
airfixWindowsSettingsDirectoryFromUtf8PreferenceRoot(
    std::string_view utf8PreferenceRoot);

// Resolves the private per-user preference root through SDL and returns its
// "settings" child. Failures throw std::runtime_error without disclosing the
// host path.
[[nodiscard]] std::filesystem::path resolveAirfixWindowsSettingsDirectory();

} // namespace airfix::windows
