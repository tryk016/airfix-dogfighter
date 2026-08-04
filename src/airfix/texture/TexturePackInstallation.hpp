#pragma once

#include "airfix/texture/TexturePackLocator.hpp"
#include "airfix/texture/TexturePackSession.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace airfix::texture {

enum class InstalledTexturePackStatus : std::uint8_t {
    ready,
    notConfigured,
    invalidConfiguration,
    persistenceBlocked,
    packageUnavailable,
    allocationFailure,
    internalFailure,
};

struct InstalledTexturePackInspection final {
    InstalledTexturePackStatus status{
        InstalledTexturePackStatus::internalFailure};
    std::optional<TexturePackLocatorRecord> locator;
    std::unique_ptr<TexturePackSession> session;

    [[nodiscard]] bool success() const noexcept {
        return status == InstalledTexturePackStatus::ready &&
               locator.has_value() && session != nullptr;
    }
};

enum class TexturePackInstallStatus : std::uint8_t {
    ready,
    invalidConfiguration,
    sourceUnavailable,
    destinationUnavailable,
    packageInvalid,
    manifestAbsent,
    manifestAmbiguous,
    scanLimitExceeded,
    persistenceBlocked,
    commitUnknown,
    allocationFailure,
    internalFailure,
};

struct TexturePackInstallResult final {
    TexturePackInstallStatus status{TexturePackInstallStatus::internalFailure};
    std::optional<TexturePackLocatorRecord> locator;
    std::unique_ptr<TexturePackSession> session;

    [[nodiscard]] bool success() const noexcept {
        return status == TexturePackInstallStatus::ready &&
               locator.has_value() && session != nullptr;
    }
};

// Resolves the durable AFTL locator under one application-private storage
// root and opens its immutable package directory through TexturePackSession.
// No path or manifest-derived value is returned on failure.
[[nodiscard]] InstalledTexturePackInspection inspectInstalledTexturePack(
    const std::filesystem::path& storageRoot) noexcept;

// Consumes one system-created local directory copy. The directory is first
// moved to a private staging name, must contain exactly one reviewed manifest,
// is renamed to the supplied product-generated immutable identity, and only
// then becomes active through the durable AFTL pair. Private source data is
// never modified in place; callers must pass a disposable owner-selected copy.
[[nodiscard]] TexturePackInstallResult installImportedTexturePack(
    const std::filesystem::path& storageRoot,
    const std::filesystem::path& importedDirectoryCopy,
    std::string_view packageDirectoryName) noexcept;

} // namespace airfix::texture
