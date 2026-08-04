#include "airfix/texture/TexturePackInstallation.hpp"

#include "airfix/io/DurableFile.hpp"
#include "airfix/texture/TexturePackDiscovery.hpp"
#include "airfix/texture/TexturePackLocatorStore.hpp"

#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace airfix::texture {
namespace {

[[nodiscard]] bool ensurePrivateDirectory(
    const std::filesystem::path& path) noexcept {
    std::error_code error;
    auto status = std::filesystem::symlink_status(path, error);
    if (!error && std::filesystem::exists(status)) {
        return std::filesystem::is_directory(status) &&
               !std::filesystem::is_symlink(status);
    }
    if (error != std::errc::no_such_file_or_directory && error) {
        return false;
    }
    error.clear();
    return std::filesystem::create_directory(path, error) && !error;
}

void removeOwnedDirectory(const std::filesystem::path& packages,
                          const std::filesystem::path& candidate) noexcept {
    if (packages.empty() || candidate.empty() ||
        candidate.parent_path() != packages ||
        !candidate.is_absolute()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(candidate, ignored);
}

[[nodiscard]] TexturePackInstallStatus statusForDiscovery(
    const TexturePackDiscoveryStatus status) noexcept {
    switch (status) {
    case TexturePackDiscoveryStatus::ready:
        return TexturePackInstallStatus::ready;
    case TexturePackDiscoveryStatus::invalidConfiguration:
    case TexturePackDiscoveryStatus::rootUnavailable:
        return TexturePackInstallStatus::sourceUnavailable;
    case TexturePackDiscoveryStatus::unsafeEntry:
        return TexturePackInstallStatus::packageInvalid;
    case TexturePackDiscoveryStatus::scanLimitExceeded:
        return TexturePackInstallStatus::scanLimitExceeded;
    case TexturePackDiscoveryStatus::manifestAbsent:
        return TexturePackInstallStatus::manifestAbsent;
    case TexturePackDiscoveryStatus::manifestAmbiguous:
        return TexturePackInstallStatus::manifestAmbiguous;
    case TexturePackDiscoveryStatus::allocationFailure:
        return TexturePackInstallStatus::allocationFailure;
    case TexturePackDiscoveryStatus::internalFailure:
        return TexturePackInstallStatus::internalFailure;
    }
    return TexturePackInstallStatus::internalFailure;
}

} // namespace

InstalledTexturePackInspection inspectInstalledTexturePack(
    const std::filesystem::path& storageRoot) noexcept {
    InstalledTexturePackInspection result;
    if (storageRoot.empty() || !storageRoot.is_absolute()) {
        result.status = InstalledTexturePackStatus::invalidConfiguration;
        return result;
    }
    try {
        const auto loaded = loadTexturePackLocator(storageRoot);
        if (!loaded.ready()) {
            result.status = loaded.persistenceBlocked
                                ? InstalledTexturePackStatus::persistenceBlocked
                                : InstalledTexturePackStatus::notConfigured;
            return result;
        }
        const auto packages = storageRoot / "packages";
        const auto packageRoot =
            packages / loaded.locator->packageDirectoryName;
        auto opened = openTexturePackSession(
            packageRoot, loaded.locator->manifestRelativePath);
        if (!opened.success()) {
            result.status =
                opened.status == TexturePackSessionStatus::allocationFailure
                    ? InstalledTexturePackStatus::allocationFailure
                    : InstalledTexturePackStatus::packageUnavailable;
            return result;
        }
        result.status = InstalledTexturePackStatus::ready;
        result.locator = loaded.locator;
        result.session = std::move(opened.session);
        return result;
    } catch (const std::bad_alloc&) {
        result.status = InstalledTexturePackStatus::allocationFailure;
        return result;
    } catch (...) {
        result.status = InstalledTexturePackStatus::internalFailure;
        return result;
    }
}

TexturePackInstallResult installImportedTexturePack(
    const std::filesystem::path& storageRoot,
    const std::filesystem::path& importedDirectoryCopy,
    const std::string_view packageDirectoryName) noexcept {
    TexturePackInstallResult result;
    if (storageRoot.empty() || !storageRoot.is_absolute() ||
        importedDirectoryCopy.empty() ||
        !importedDirectoryCopy.is_absolute() ||
        !validTexturePackDirectoryName(packageDirectoryName)) {
        result.status = TexturePackInstallStatus::invalidConfiguration;
        return result;
    }

    const auto packages = storageRoot / "packages";
    const auto finalPath = packages / std::string(packageDirectoryName);
    const auto stagingPath = packages /
                             (std::string(packageDirectoryName) + ".partial");
    bool movedToStaging = false;
    bool movedToFinal = false;
    bool locatorCommitted = false;
    try {
        if (!ensurePrivateDirectory(storageRoot) ||
            !ensurePrivateDirectory(packages)) {
            result.status = TexturePackInstallStatus::destinationUnavailable;
            return result;
        }
        std::error_code error;
        const auto sourceStatus = std::filesystem::symlink_status(
            importedDirectoryCopy, error);
        if (error || !std::filesystem::is_directory(sourceStatus) ||
            std::filesystem::is_symlink(sourceStatus) ||
            std::filesystem::exists(stagingPath, error) || error ||
            std::filesystem::exists(finalPath, error) || error) {
            result.status = TexturePackInstallStatus::sourceUnavailable;
            return result;
        }

        std::filesystem::rename(importedDirectoryCopy, stagingPath, error);
        if (error) {
            result.status = TexturePackInstallStatus::sourceUnavailable;
            return result;
        }
        movedToStaging = true;
        io::syncDirectory(packages);

        auto discovered = discoverTexturePackSession(stagingPath);
        if (!discovered.success()) {
            result.status = statusForDiscovery(discovered.status);
            removeOwnedDirectory(packages, stagingPath);
            return result;
        }
        const auto manifestRelativePath = discovered.manifestRelativePath;
        discovered.session.reset();

        std::filesystem::rename(stagingPath, finalPath, error);
        if (error) {
            result.status = TexturePackInstallStatus::destinationUnavailable;
            removeOwnedDirectory(packages, stagingPath);
            return result;
        }
        movedToStaging = false;
        movedToFinal = true;
        io::syncDirectory(packages);

        auto opened = openTexturePackSession(finalPath, manifestRelativePath);
        if (!opened.success()) {
            result.status =
                opened.status == TexturePackSessionStatus::allocationFailure
                    ? TexturePackInstallStatus::allocationFailure
                    : TexturePackInstallStatus::packageInvalid;
            removeOwnedDirectory(packages, finalPath);
            return result;
        }

        const TexturePackLocatorRecord locator{
            .packageDirectoryName = std::string(packageDirectoryName),
            .manifestRelativePath = manifestRelativePath,
        };
        try {
            static_cast<void>(saveTexturePackLocator(storageRoot, locator));
        } catch (const TexturePackLocatorStoreError& error) {
            if (error.kind() ==
                TexturePackLocatorStoreErrorKind::commitUnknown) {
                const auto inspected = inspectInstalledTexturePack(storageRoot);
                if (inspected.success() && inspected.locator == locator) {
                    locatorCommitted = true;
                    result.status = TexturePackInstallStatus::ready;
                    result.locator = locator;
                    result.session = std::move(opened.session);
                    return result;
                }
                result.status = TexturePackInstallStatus::commitUnknown;
                return result;
            }
            result.status =
                error.kind() ==
                        TexturePackLocatorStoreErrorKind::persistenceBlocked
                    ? TexturePackInstallStatus::persistenceBlocked
                    : TexturePackInstallStatus::destinationUnavailable;
            opened.session.reset();
            removeOwnedDirectory(packages, finalPath);
            return result;
        }

        locatorCommitted = true;
        result.status = TexturePackInstallStatus::ready;
        result.locator = locator;
        result.session = std::move(opened.session);
        return result;
    } catch (const std::bad_alloc&) {
        result.status = TexturePackInstallStatus::allocationFailure;
    } catch (...) {
        result.status = TexturePackInstallStatus::internalFailure;
    }
    if (movedToStaging) {
        removeOwnedDirectory(packages, stagingPath);
    }
    if (movedToFinal && !locatorCommitted) {
        removeOwnedDirectory(packages, finalPath);
    }
    return result;
}

} // namespace airfix::texture
