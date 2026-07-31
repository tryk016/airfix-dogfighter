#include "airfix/render/SceneTextureSampling.hpp"

namespace airfix::render {

bool validateSceneTextureSamplingPolicy(
    const SceneTextureSamplingPolicy &policy) noexcept {
  switch (policy.mode) {
  case SceneTextureSamplingMode::nearestMipPoint:
  case SceneTextureSamplingMode::linearMipPoint:
  case SceneTextureSamplingMode::linearMipLinear:
    return policy.maximumAnisotropy == 1U;
  case SceneTextureSamplingMode::anisotropicMipLinear:
    return policy.maximumAnisotropy >= minimumAnisotropicSampleCount &&
           policy.maximumAnisotropy <= maximumAnisotropicSampleCount;
  }
  return false;
}

std::optional<SceneTextureSamplingPolicy>
sceneTextureSamplingPolicyForProfile(const VisualProfile profile) noexcept {
  switch (profile) {
  case VisualProfile::classic:
    return SceneTextureSamplingPolicy{
        .mode = SceneTextureSamplingMode::nearestMipPoint,
        .maximumAnisotropy = 1U,
    };
  case VisualProfile::enhanced:
    return SceneTextureSamplingPolicy{
        .mode = SceneTextureSamplingMode::anisotropicMipLinear,
        .maximumAnisotropy = enhancedTextureAnisotropy,
    };
  }
  return std::nullopt;
}

} // namespace airfix::render
