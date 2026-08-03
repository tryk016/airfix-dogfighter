#pragma once

#include "airfix/texture/PrivateTextureFileStore.hpp"
#include "airfix/texture/TextureHdManifestIndex.hpp"
#include "airfix/texture/TextureReplacementResolver.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

namespace airfix::windows {

enum class AirfixWindowsTexturePackStatus : std::uint8_t {
  ready,
  invalidConfiguration,
  rootUnavailable,
  manifestUnavailable,
  manifestInvalid,
  allocationFailure,
  internalFailure,
};

struct AirfixWindowsTexturePackOpenResult;

class AirfixWindowsTexturePackSession final {
public:
  AirfixWindowsTexturePackSession(const AirfixWindowsTexturePackSession &) =
      delete;
  AirfixWindowsTexturePackSession &
  operator=(const AirfixWindowsTexturePackSession &) = delete;
  AirfixWindowsTexturePackSession(AirfixWindowsTexturePackSession &&) = delete;
  AirfixWindowsTexturePackSession &
  operator=(AirfixWindowsTexturePackSession &&) = delete;

  [[nodiscard]] const texture::TextureReplacementResolver &
  resolver() const noexcept {
    return *resolver_;
  }

  [[nodiscard]] const texture::PrivateTextureFileStore &files() const noexcept {
    return *files_;
  }

  [[nodiscard]] std::uint64_t generation() const noexcept {
    return resolver_->generation();
  }

private:
  friend struct AirfixWindowsTexturePackOpenResult;
  friend AirfixWindowsTexturePackOpenResult
  openAirfixWindowsTexturePack(const std::filesystem::path &,
                               std::string_view) noexcept;

  AirfixWindowsTexturePackSession(
      std::unique_ptr<texture::PrivateTextureFileStore> files,
      std::unique_ptr<texture::TextureHdManifestIndex> index,
      std::unique_ptr<texture::TextureReplacementResolver> resolver) noexcept
      : files_(std::move(files)), index_(std::move(index)),
        resolver_(std::move(resolver)) {}

  std::unique_ptr<texture::PrivateTextureFileStore> files_;
  std::unique_ptr<texture::TextureHdManifestIndex> index_;
  std::unique_ptr<texture::TextureReplacementResolver> resolver_;
};

struct AirfixWindowsTexturePackOpenResult final {
  AirfixWindowsTexturePackStatus status{
      AirfixWindowsTexturePackStatus::internalFailure};
  std::unique_ptr<AirfixWindowsTexturePackSession> session;

  [[nodiscard]] bool success() const noexcept {
    return status == AirfixWindowsTexturePackStatus::ready &&
           session != nullptr;
  }
};

// Opens one session-only, read-only private package capability. The configured
// root spelling and manifest-relative path are never retained or returned.
// Failure status is deliberately fixed and safe for product diagnostics.
[[nodiscard]] AirfixWindowsTexturePackOpenResult
openAirfixWindowsTexturePack(const std::filesystem::path &configuredRoot,
                             std::string_view manifestRelativePath) noexcept;

[[nodiscard]] constexpr std::string_view texturePackStatusCategory(
    const AirfixWindowsTexturePackStatus status) noexcept {
  switch (status) {
  case AirfixWindowsTexturePackStatus::ready:
    return "ready";
  case AirfixWindowsTexturePackStatus::invalidConfiguration:
    return "invalid-configuration";
  case AirfixWindowsTexturePackStatus::rootUnavailable:
    return "root-unavailable";
  case AirfixWindowsTexturePackStatus::manifestUnavailable:
    return "manifest-unavailable";
  case AirfixWindowsTexturePackStatus::manifestInvalid:
    return "manifest-invalid";
  case AirfixWindowsTexturePackStatus::allocationFailure:
    return "allocation-failure";
  case AirfixWindowsTexturePackStatus::internalFailure:
    return "internal-failure";
  }
  return "internal-failure";
}

} // namespace airfix::windows
