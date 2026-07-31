#include "airfix/render/SceneOverviewCamera.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::render::Bounds3;
using airfix::render::buildSceneOverviewCamera;
using airfix::render::DrawMeshInstance;
using airfix::render::DrawMeshPayload;
using airfix::render::DrawModelPayload;
using airfix::render::DrawVertex;
using airfix::render::Mat3;
using airfix::render::SceneOverviewCameraConfig;
using airfix::render::SceneOverviewCameraIssueKind;
using airfix::render::Vec3;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 0.0001F) noexcept {
  return std::abs(actual - expected) <= tolerance;
}

[[nodiscard]] std::array<Vec3, 8U> corners(const Bounds3 &bounds) noexcept {
  return {{
      {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
      {bounds.maximum.x, bounds.minimum.y, bounds.minimum.z},
      {bounds.minimum.x, bounds.maximum.y, bounds.minimum.z},
      {bounds.maximum.x, bounds.maximum.y, bounds.minimum.z},
      {bounds.minimum.x, bounds.minimum.y, bounds.maximum.z},
      {bounds.maximum.x, bounds.minimum.y, bounds.maximum.z},
      {bounds.minimum.x, bounds.maximum.y, bounds.maximum.z},
      {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z},
  }};
}

[[nodiscard]] DrawModelPayload representativeModel() {
  DrawMeshPayload mesh;
  mesh.vertices.push_back(DrawVertex{});
  mesh.localBounds = {
      .minimum = {-2.0F, -1.0F, -3.0F},
      .maximum = {4.0F, 5.0F, 6.0F},
  };

  DrawModelPayload model;
  model.meshes.push_back(mesh);
  model.instances.push_back({
      .meshSlot = 0U,
      .sourceNodeReference = 1U,
      .modelLinear =
          Mat3{
              .columns =
                  {
                      Vec3{0.0F, 0.0F, -1.0F},
                      Vec3{0.0F, 1.0F, 0.0F},
                      Vec3{1.0F, 0.0F, 0.0F},
                  },
          },
      .modelTranslation = {10.0F, 2.0F, -4.0F},
  });
  model.instances.push_back({
      .meshSlot = 0U,
      .sourceNodeReference = 2U,
      .modelLinear = {},
      .modelTranslation = {-20.0F, 0.0F, 12.0F},
  });
  return model;
}

void testFitsTransformedSceneAcrossAspects() {
  const auto model = representativeModel();
  constexpr std::array<std::array<std::uint32_t, 2U>, 4U> extents{{
      {1024U, 768U},
      {1920U, 1080U},
      {3440U, 1440U},
      {5120U, 1440U},
  }};
  for (const auto extent : extents) {
    const SceneOverviewCameraConfig config{
        .logicalCanvasWidth = extent[0],
        .logicalCanvasHeight = extent[1],
    };
    const auto built = buildSceneOverviewCamera(model, config);
    require(built.complete(),
            "valid transformed scene did not produce an overview");
    const auto &overview = *built.snapshot;
    require(close(overview.worldBounds.minimum.x, -22.0F) &&
                close(overview.worldBounds.minimum.y, -1.0F) &&
                close(overview.worldBounds.minimum.z, -8.0F) &&
                close(overview.worldBounds.maximum.x, 16.0F) &&
                close(overview.worldBounds.maximum.y, 7.0F) &&
                close(overview.worldBounds.maximum.z, 18.0F),
            "instance transforms were not included in world bounds");

    const float minimumX = overview.viewportMarginFraction * extent[0];
    const float maximumX = extent[0] - minimumX;
    const float minimumY = overview.viewportMarginFraction * extent[1];
    const float maximumY = extent[1] - minimumY;
    for (const auto &corner : corners(overview.worldBounds)) {
      const auto projected = overview.clipPacket.pose().project(corner);
      require(projected.complete(), "overview rejected a world-bounds corner");
      require(projected.point->projected.point.x >= minimumX - 0.25F &&
                  projected.point->projected.point.x <= maximumX + 0.25F &&
                  projected.point->projected.point.y >= minimumY - 0.25F &&
                  projected.point->projected.point.y <= maximumY + 0.25F,
              "overview did not preserve the requested edge margin");
      const auto clipped = overview.clipPacket.project(corner);
      require(clipped.complete() && clipped.point->withinRecoveredDepthRange,
              "overview clipped a world-bounds corner by depth");
    }
  }
}

void testEmptyAndInvalidModelsFailClosed() {
  const DrawModelPayload empty;
  const auto emptyResult = buildSceneOverviewCamera(empty);
  require(!emptyResult.complete() &&
              emptyResult.issue == SceneOverviewCameraIssueKind::emptyModel,
          "empty model did not fail closed");

  auto invalidSlot = representativeModel();
  invalidSlot.instances[0].meshSlot = 1U;
  const auto slotResult = buildSceneOverviewCamera(invalidSlot);
  require(!slotResult.complete() &&
              slotResult.issue == SceneOverviewCameraIssueKind::invalidMeshSlot,
          "invalid mesh slot did not fail closed");

  auto invalidBounds = representativeModel();
  invalidBounds.meshes[0].localBounds.maximum.x =
      std::numeric_limits<float>::quiet_NaN();
  const auto boundsResult = buildSceneOverviewCamera(invalidBounds);
  require(!boundsResult.complete() &&
              boundsResult.issue ==
                  SceneOverviewCameraIssueKind::invalidLocalBounds,
          "non-finite local bounds did not fail closed");

  auto invalidTransform = representativeModel();
  invalidTransform.instances[0].modelTranslation.z =
      std::numeric_limits<float>::infinity();
  const auto transformResult = buildSceneOverviewCamera(invalidTransform);
  require(!transformResult.complete() &&
              transformResult.issue ==
                  SceneOverviewCameraIssueKind::invalidInstanceTransform,
          "non-finite instance transform did not fail closed");
}

void testInvalidPoliciesFailClosed() {
  const auto model = representativeModel();
  auto invalidMargin = SceneOverviewCameraConfig{};
  invalidMargin.viewportMarginFraction = 0.5F;
  const auto marginResult = buildSceneOverviewCamera(model, invalidMargin);
  require(!marginResult.complete() &&
              marginResult.issue == SceneOverviewCameraIssueKind::invalidConfig,
          "invalid viewport margin did not fail closed");

  auto invalidDirection = SceneOverviewCameraConfig{};
  invalidDirection.forwardDirection = {};
  const auto directionResult =
      buildSceneOverviewCamera(model, invalidDirection);
  require(!directionResult.complete() &&
              directionResult.issue ==
                  SceneOverviewCameraIssueKind::invalidConfig,
          "zero overview direction did not fail closed");

  auto invalidCanvas = SceneOverviewCameraConfig{};
  invalidCanvas.logicalCanvasHeight = 0U;
  const auto canvasResult = buildSceneOverviewCamera(model, invalidCanvas);
  require(!canvasResult.complete() &&
              canvasResult.issue == SceneOverviewCameraIssueKind::invalidConfig,
          "empty overview canvas did not fail closed");
}

} // namespace

int main() {
  try {
    testFitsTransformedSceneAcrossAspects();
    testEmptyAndInvalidModelsFailClosed();
    testInvalidPoliciesFailClosed();
    std::cout << "Scene overview camera tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Scene overview camera tests failed: " << error.what() << '\n';
    return 1;
  }
}
