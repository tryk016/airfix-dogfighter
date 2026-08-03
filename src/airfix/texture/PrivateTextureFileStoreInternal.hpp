#pragma once

#include "airfix/texture/PrivateTextureFileStore.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace airfix::texture::detail {

struct PrivateTextureRelativePath {
  PrivateTextureFileStatus status{
      PrivateTextureFileStatus::invalidRelativePath};
  std::vector<std::string> components;

  [[nodiscard]] bool valid() const noexcept {
    return status == PrivateTextureFileStatus::ready;
  }
};

[[nodiscard]] bool validPrivateTextureFileStoreLimits(
    const PrivateTextureFileStoreLimits &limits) noexcept;

[[nodiscard]] PrivateTextureRelativePath parsePrivateTextureRelativePath(
    std::string_view path,
    const PrivateTextureFileStoreLimits &limits) noexcept;

} // namespace airfix::texture::detail
