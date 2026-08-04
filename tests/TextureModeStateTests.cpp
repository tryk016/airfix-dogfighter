#include "airfix/texture/TextureModeState.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using airfix::texture::ActiveMissionTextureState;
using airfix::texture::TextureMode;
using airfix::texture::TextureModeStateIssue;
using airfix::texture::TexturePackageAvailability;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testClassicIsAlwaysEffective() {
  for (const auto availability : {
           TexturePackageAvailability::notConfigured,
           TexturePackageAvailability::validating,
           TexturePackageAvailability::ready,
           TexturePackageAvailability::unavailable,
       }) {
    const auto result = airfix::texture::resolveTextureModeState(
        TextureMode::classic, availability);
    require(result.complete(), "Classic texture state was rejected");
    require(result.state->effectiveMode == TextureMode::classic &&
                !result.state->fallbackToClassic() &&
                !result.state->missionReloadRequired,
            "Classic texture state depended on the private package");
  }
}

void testEnhancedRequiresReadyPackage() {
  for (const auto test : std::array{
           std::pair{TexturePackageAvailability::notConfigured, true},
           std::pair{TexturePackageAvailability::validating, true},
           std::pair{TexturePackageAvailability::ready, false},
           std::pair{TexturePackageAvailability::unavailable, true},
       }) {
    const auto result = airfix::texture::resolveTextureModeState(
        TextureMode::enhanced, test.first);
    require(result.complete(), "Enhanced texture state was rejected");
    require(result.state->fallbackToClassic() == test.second,
            "Enhanced fallback decision changed");
    require(result.state->effectiveMode ==
                (test.second ? TextureMode::classic : TextureMode::enhanced),
            "Enhanced effective mode changed");
  }
}

void testActiveMissionRequiresTransactionalReload() {
  const ActiveMissionTextureState activeClassic{
      .requestedMode = TextureMode::classic,
      .effectiveMode = TextureMode::classic,
  };
  auto result = airfix::texture::resolveTextureModeState(
      TextureMode::classic, TexturePackageAvailability::ready, activeClassic);
  require(result.complete() && !result.state->missionReloadRequired,
          "unchanged Classic mission required reload");

  result = airfix::texture::resolveTextureModeState(
      TextureMode::enhanced, TexturePackageAvailability::ready, activeClassic);
  require(result.complete() && result.state->missionReloadRequired &&
              result.state->effectiveMode == TextureMode::enhanced,
          "Classic-to-Enhanced transition skipped reload");

  const ActiveMissionTextureState activeEnhanced{
      .requestedMode = TextureMode::enhanced,
      .effectiveMode = TextureMode::enhanced,
  };
  result = airfix::texture::resolveTextureModeState(
      TextureMode::enhanced, TexturePackageAvailability::unavailable,
      activeEnhanced);
  require(result.complete() && result.state->missionReloadRequired &&
              result.state->fallbackToClassic(),
          "package loss mutated an active Enhanced mission in place");

  const ActiveMissionTextureState activeFallback{
      .requestedMode = TextureMode::enhanced,
      .effectiveMode = TextureMode::classic,
  };
  result = airfix::texture::resolveTextureModeState(
      TextureMode::enhanced, TexturePackageAvailability::unavailable,
      activeFallback);
  require(result.complete() && !result.state->missionReloadRequired,
          "unchanged fallback mission required reload");
}

void testForgedInputsFailClosed() {
  auto result = airfix::texture::resolveTextureModeState(
      static_cast<TextureMode>(0xFFU),
      TexturePackageAvailability::notConfigured);
  require(!result.complete() &&
              result.issue == TextureModeStateIssue::unsupportedRequestedMode,
          "forged requested mode was accepted");

  result = airfix::texture::resolveTextureModeState(
      TextureMode::classic, static_cast<TexturePackageAvailability>(0xFFU));
  require(!result.complete() &&
              result.issue ==
                  TextureModeStateIssue::unsupportedPackageAvailability,
          "forged package availability was accepted");

  result = airfix::texture::resolveTextureModeState(
      TextureMode::classic, TexturePackageAvailability::ready,
      ActiveMissionTextureState{
          .requestedMode = TextureMode::classic,
          .effectiveMode = TextureMode::enhanced,
      });
  require(!result.complete() &&
              result.issue ==
                  TextureModeStateIssue::inconsistentActiveMissionState,
          "impossible active Classic mission was accepted");
}

} // namespace

int main() {
  try {
    testClassicIsAlwaysEffective();
    testEnhancedRequiresReadyPackage();
    testActiveMissionRequiresTransactionalReload();
    testForgedInputsFailClosed();
    std::cout << "Texture mode state tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Texture mode state tests failed: " << error.what() << '\n';
    return 1;
  }
}
