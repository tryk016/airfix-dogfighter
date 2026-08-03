#pragma once

#include "airfix/crypto/Sha256.hpp"
#include "airfix/render/TextureRuntimeData.hpp"
#include "airfix/texture/TextureMode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace airfix::content {

struct LoadedTextureAsset {
    render::TextureAssetId assetId;
    std::size_t sourceFileIndex{};
    texture::TextureMode sourceMode{texture::TextureMode::classic};
    // Non-zero only while an owner-local replacement session is configured.
    // This is an opaque cache partition identity, never a host path.
    std::uint64_t replacementGeneration{};
    // Populated only by a configured resolver from the exact immutable GTI
    // bytes. It is retained in memory for cache separation and never logged.
    std::optional<crypto::Sha256Digest> sourceGtiSha256;
    render::GtiUploadPlan upload;
    std::vector<assets::RgbaImage> uploadLevels;
};

} // namespace airfix::content
