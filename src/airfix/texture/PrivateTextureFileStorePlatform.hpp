#pragma once

#include "airfix/texture/PrivateTextureFileStore.hpp"

#include <cstdint>
#include <filesystem>

namespace airfix::texture {

// Platform-owner boundary only. Portable resolver/renderer code receives the
// returned capability and never receives this configured host path.
[[nodiscard]] PrivateTextureFileStoreOpenResult
openPrivateTextureFileStoreLocalRoot(
    const std::filesystem::path &configuredRoot, std::uint64_t generation,
    const PrivateTextureFileStoreLimits &limits = {}) noexcept;

} // namespace airfix::texture
