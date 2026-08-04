#include "AirfixWindowsTexturePack.hpp"

namespace airfix::windows {

AirfixWindowsTexturePackOpenResult openAirfixWindowsTexturePack(
    const std::filesystem::path &configuredRoot,
    const std::string_view manifestRelativePath) noexcept {
  return texture::openTexturePackSession(configuredRoot,
                                         manifestRelativePath);
}

} // namespace airfix::windows
