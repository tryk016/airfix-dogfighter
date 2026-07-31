#include "airfix/render/SceneTextureSampling.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using namespace airfix::render;

static_assert(noexcept(validateSceneTextureSamplingPolicy({})));
static_assert(
    noexcept(sceneTextureSamplingPolicyForProfile(VisualProfile::classic)));

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void testProfilePolicies() {
  const auto classic =
      sceneTextureSamplingPolicyForProfile(VisualProfile::classic);
  require(classic ==
                  SceneTextureSamplingPolicy{
                      .mode = SceneTextureSamplingMode::nearestMipPoint,
                      .maximumAnisotropy = 1U,
                  } &&
              validateSceneTextureSamplingPolicy(*classic),
          "Classic did not preserve point-sampled scene textures");

  const auto enhanced =
      sceneTextureSamplingPolicyForProfile(VisualProfile::enhanced);
  require(enhanced ==
                  SceneTextureSamplingPolicy{
                      .mode = SceneTextureSamplingMode::anisotropicMipLinear,
                      .maximumAnisotropy = enhancedTextureAnisotropy,
                  } &&
              validateSceneTextureSamplingPolicy(*enhanced),
          "Enhanced did not select bounded anisotropic trilinear sampling");

  require(
      !sceneTextureSamplingPolicyForProfile(static_cast<VisualProfile>(0xFFU))
           .has_value(),
      "forged visual profile produced a scene sampler policy");
}

void testPolicyValidation() {
  for (const auto mode : {
           SceneTextureSamplingMode::nearestMipPoint,
           SceneTextureSamplingMode::linearMipPoint,
           SceneTextureSamplingMode::linearMipLinear,
       }) {
    require(validateSceneTextureSamplingPolicy({
                .mode = mode,
                .maximumAnisotropy = 1U,
            }) &&
                !validateSceneTextureSamplingPolicy({
                    .mode = mode,
                    .maximumAnisotropy = 2U,
                }),
            "non-anisotropic sampling accepted an anisotropy count");
  }

  require(!validateSceneTextureSamplingPolicy({
              .mode = SceneTextureSamplingMode::anisotropicMipLinear,
              .maximumAnisotropy = 1U,
          }) &&
              validateSceneTextureSamplingPolicy({
                  .mode = SceneTextureSamplingMode::anisotropicMipLinear,
                  .maximumAnisotropy = 2U,
              }) &&
              validateSceneTextureSamplingPolicy({
                  .mode = SceneTextureSamplingMode::anisotropicMipLinear,
                  .maximumAnisotropy = 16U,
              }) &&
              !validateSceneTextureSamplingPolicy({
                  .mode = SceneTextureSamplingMode::anisotropicMipLinear,
                  .maximumAnisotropy = 17U,
              }) &&
              !validateSceneTextureSamplingPolicy({
                  .mode = static_cast<SceneTextureSamplingMode>(0xFFU),
                  .maximumAnisotropy = 1U,
              }),
          "anisotropic policy limits did not fail closed");
}

} // namespace

int main() {
  try {
    testProfilePolicies();
    testPolicyValidation();
    std::cout << "Scene texture sampling tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Scene texture sampling tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
