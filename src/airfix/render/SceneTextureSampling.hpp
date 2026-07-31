#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

// Backend-neutral scene-texture sampling. UI and final scene presentation use
// separate sampler contracts and must never inherit this policy.
enum class SceneTextureSamplingMode : std::uint8_t {
  nearestMipPoint,
  linearMipPoint,
  linearMipLinear,
  anisotropicMipLinear,
};

struct SceneTextureSamplingPolicy final {
  SceneTextureSamplingMode mode{SceneTextureSamplingMode::nearestMipPoint};
  std::uint32_t maximumAnisotropy{1U};

  [[nodiscard]] friend constexpr bool
  operator==(const SceneTextureSamplingPolicy &,
             const SceneTextureSamplingPolicy &) noexcept = default;
};

inline constexpr std::uint32_t minimumAnisotropicSampleCount = 2U;
inline constexpr std::uint32_t maximumAnisotropicSampleCount = 16U;
inline constexpr std::uint32_t enhancedTextureAnisotropy = 8U;

[[nodiscard]] bool validateSceneTextureSamplingPolicy(
    const SceneTextureSamplingPolicy &policy) noexcept;

// Classic intentionally preserves the renderer's established point-sampled
// output while retaining authored GTI mip chains. Enhanced is the first real
// visual-profile feature: trilinear anisotropic scene sampling. Forged profile
// values fail closed.
[[nodiscard]] std::optional<SceneTextureSamplingPolicy>
sceneTextureSamplingPolicyForProfile(VisualProfile profile) noexcept;

} // namespace airfix::render
