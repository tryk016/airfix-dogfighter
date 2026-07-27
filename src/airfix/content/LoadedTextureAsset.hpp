#pragma once

#include "airfix/render/TextureRuntimeData.hpp"

#include <cstddef>
#include <vector>

namespace airfix::content {

struct LoadedTextureAsset {
    render::TextureAssetId assetId;
    std::size_t sourceFileIndex{};
    render::GtiUploadPlan upload;
    std::vector<assets::RgbaImage> uploadLevels;
};

} // namespace airfix::content
