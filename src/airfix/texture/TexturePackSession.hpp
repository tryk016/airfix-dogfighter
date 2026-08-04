#pragma once

#include "airfix/texture/PrivateTextureFileStore.hpp"
#include "airfix/texture/TextureHdManifestIndex.hpp"
#include "airfix/texture/TextureReplacementResolver.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

namespace airfix::texture {

enum class TexturePackSessionStatus : std::uint8_t {
    ready,
    invalidConfiguration,
    rootUnavailable,
    manifestUnavailable,
    manifestInvalid,
    allocationFailure,
    internalFailure,
};

struct TexturePackSessionOpenResult;

// One read-only capability over an owner-configured private texture package.
// The configured root spelling and manifest-relative path are deliberately not
// retained. The immutable index and root-confined file store outlive the
// non-owning resolver.
class TexturePackSession final {
  public:
    TexturePackSession(const TexturePackSession&) = delete;
    TexturePackSession& operator=(const TexturePackSession&) = delete;
    TexturePackSession(TexturePackSession&&) = delete;
    TexturePackSession& operator=(TexturePackSession&&) = delete;

    [[nodiscard]] const TextureReplacementResolver& resolver() const noexcept {
        return *resolver_;
    }

    [[nodiscard]] const PrivateTextureFileStore& files() const noexcept {
        return *files_;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return resolver_->generation();
    }

  private:
    friend struct TexturePackSessionOpenResult;
    friend TexturePackSessionOpenResult openTexturePackSession(
        const std::filesystem::path&, std::string_view) noexcept;

    TexturePackSession(std::unique_ptr<PrivateTextureFileStore> files,
                       std::unique_ptr<TextureHdManifestIndex> index,
                       std::unique_ptr<TextureReplacementResolver> resolver)
        noexcept
        : files_(std::move(files)), index_(std::move(index)),
          resolver_(std::move(resolver)) {}

    std::unique_ptr<PrivateTextureFileStore> files_;
    std::unique_ptr<TextureHdManifestIndex> index_;
    std::unique_ptr<TextureReplacementResolver> resolver_;
};

struct TexturePackSessionOpenResult final {
    TexturePackSessionStatus status{TexturePackSessionStatus::internalFailure};
    std::unique_ptr<TexturePackSession> session;

    [[nodiscard]] bool success() const noexcept {
        return status == TexturePackSessionStatus::ready && session != nullptr;
    }
};

// Opens one process-local generation after pinning the root without following
// symlinks/reparse points and parsing an accepted-only reviewed manifest. All
// failure categories are fixed and safe for product diagnostics.
[[nodiscard]] TexturePackSessionOpenResult openTexturePackSession(
    const std::filesystem::path& configuredRoot,
    std::string_view manifestRelativePath) noexcept;

[[nodiscard]] constexpr std::string_view texturePackSessionStatusCategory(
    const TexturePackSessionStatus status) noexcept {
    switch (status) {
    case TexturePackSessionStatus::ready:
        return "ready";
    case TexturePackSessionStatus::invalidConfiguration:
        return "invalid-configuration";
    case TexturePackSessionStatus::rootUnavailable:
        return "root-unavailable";
    case TexturePackSessionStatus::manifestUnavailable:
        return "manifest-unavailable";
    case TexturePackSessionStatus::manifestInvalid:
        return "manifest-invalid";
    case TexturePackSessionStatus::allocationFailure:
        return "allocation-failure";
    case TexturePackSessionStatus::internalFailure:
        return "internal-failure";
    }
    return "internal-failure";
}

} // namespace airfix::texture
