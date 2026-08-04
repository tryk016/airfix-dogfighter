#pragma once

#include "airfix/texture/TexturePackSession.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace airfix::texture {

struct TexturePackDiscoveryLimits final {
    std::size_t maximumEntries{65'536U};
    std::size_t maximumManifestCandidates{256U};
    std::uint64_t maximumAggregateFileBytes{16ULL * 1024U * 1024U * 1024U};
};

enum class TexturePackDiscoveryStatus : std::uint8_t {
    ready,
    invalidConfiguration,
    rootUnavailable,
    unsafeEntry,
    scanLimitExceeded,
    manifestAbsent,
    manifestAmbiguous,
    allocationFailure,
    internalFailure,
};

struct TexturePackDiscoveryResult final {
    TexturePackDiscoveryStatus status{TexturePackDiscoveryStatus::internalFailure};
    std::unique_ptr<TexturePackSession> session;
    // Private, root-relative configuration. Native adapters may persist this
    // inside AFTL but must never display or log it.
    std::string manifestRelativePath;

    [[nodiscard]] bool success() const noexcept {
        return status == TexturePackDiscoveryStatus::ready &&
               session != nullptr && !manifestRelativePath.empty();
    }
};

// Scans one already owner-imported, application-private snapshot. It never
// follows links, rejects non-directory/non-regular entries and multiple hard
// links, applies aggregate limits, and accepts exactly one reviewed manifest
// that can construct a complete TexturePackSession. Candidate names are not
// returned on failure.
[[nodiscard]] TexturePackDiscoveryResult discoverTexturePackSession(
    const std::filesystem::path& importedRoot,
    const TexturePackDiscoveryLimits& limits = {}) noexcept;

} // namespace airfix::texture
