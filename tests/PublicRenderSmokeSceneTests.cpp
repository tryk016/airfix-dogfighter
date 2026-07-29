#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/PublicRenderSmokeScene.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testPublicSceneContract() {
  const auto scene = airfix::render::makePublicRenderSmokeScene();
  require(scene.model.meshes.size() == 2U, "smoke scene mesh count changed");
  require(scene.model.instances.size() == 3U,
          "smoke scene instance count changed");
  require(scene.model.instances[0].meshSlot == 1U &&
              scene.model.instances[1].meshSlot == 0U &&
              scene.model.instances[2].meshSlot == 1U,
          "smoke scene no longer exercises non-monotonic mesh reuse");
  require(scene.textureRgba8.size() ==
              airfix::render::PublicRenderSmokeScene::textureWidth *
                  airfix::render::PublicRenderSmokeScene::textureHeight * 4U,
          "smoke texture dimensions and byte count disagree");
  require(scene.fallbackRgba8 ==
              std::array<std::uint8_t, 4U>{255U, 255U, 255U, 255U},
          "smoke fallback texture changed");

  const auto submission =
      airfix::render::buildDrawSubmissionPlan(scene.model, 1U);
  require(submission.plan.has_value(),
          "smoke scene did not produce a draw plan");
  require(submission.issues.empty(), "smoke scene produced validation issues");
  require(submission.plan->meshUploads.size() == 2U,
          "smoke scene upload count changed");
  require(submission.plan->commands.size() == 4U,
          "smoke scene draw-command count changed");
  require(submission.plan->commands[0].meshSlot == 1U &&
              submission.plan->commands[1].meshSlot == 0U &&
              submission.plan->commands[2].meshSlot == 0U &&
              submission.plan->commands[3].meshSlot == 1U,
          "smoke scene command order changed");
}

} // namespace

int main() {
  try {
    testPublicSceneContract();
    std::cout << "all public render smoke scene tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "public render smoke scene test failure: " << error.what()
              << '\n';
    return 1;
  }
}
