#pragma once

#include "airfix/texture/TexturePackSession.hpp"

#include <filesystem>
#include <string_view>

namespace airfix::windows {

using AirfixWindowsTexturePackStatus = texture::TexturePackSessionStatus;
using AirfixWindowsTexturePackSession = texture::TexturePackSession;
using AirfixWindowsTexturePackOpenResult = texture::TexturePackSessionOpenResult;

// Opens one session-only, read-only private package capability. The configured
// root spelling and manifest-relative path are never retained or returned.
// Failure status is deliberately fixed and safe for product diagnostics.
[[nodiscard]] AirfixWindowsTexturePackOpenResult
openAirfixWindowsTexturePack(const std::filesystem::path &configuredRoot,
                             std::string_view manifestRelativePath) noexcept;

[[nodiscard]] constexpr std::string_view texturePackStatusCategory(
    const AirfixWindowsTexturePackStatus status) noexcept {
  return texture::texturePackSessionStatusCategory(status);
}

} // namespace airfix::windows
