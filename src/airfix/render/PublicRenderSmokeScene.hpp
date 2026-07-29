#pragma once

#include "airfix/render/DrawModel.hpp"

#include <array>
#include <cstdint>

namespace airfix::render {

// One deterministic, proprietary-data-free scene shared by native renderer
// bring-up and CI. It deliberately exercises reusable meshes, non-monotonic
// instance order, textured and explicit texture-free ranges, and fallback
// texture selection.
struct PublicRenderSmokeScene {
  static constexpr std::uint32_t textureWidth = 2U;
  static constexpr std::uint32_t textureHeight = 2U;

  DrawModelPayload model;
  std::array<std::uint8_t, textureWidth * textureHeight * 4U> textureRgba8{};
  std::array<std::uint8_t, 4U> fallbackRgba8{};
};

[[nodiscard]] PublicRenderSmokeScene makePublicRenderSmokeScene();

} // namespace airfix::render
