#include "airfix/render/SceneOverviewCamera.hpp"

#include "airfix/render/LegacyCameraTransform.hpp"
#include "airfix/render/LegacyGameplayCameraCollision.hpp"
#include "airfix/render/LegacyGameplayCameraPoseSnapshot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

constexpr double radiansPerHalfDegree = 0.00872664625997164788461845384244;

[[nodiscard]] bool finite(const Vec3 &value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3 &value) noexcept {
  return finite(value.columns[0]) && finite(value.columns[1]) &&
         finite(value.columns[2]);
}

[[nodiscard]] bool validBounds(const Bounds3 &bounds) noexcept {
  return finite(bounds.minimum) && finite(bounds.maximum) &&
         bounds.minimum.x <= bounds.maximum.x &&
         bounds.minimum.y <= bounds.maximum.y &&
         bounds.minimum.z <= bounds.maximum.z;
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

[[nodiscard]] SceneOverviewCameraBuildResult
failure(const SceneOverviewCameraIssueKind issue) noexcept {
  return {
      .snapshot = std::nullopt,
      .issue = issue,
  };
}

[[nodiscard]] bool
validConfig(const SceneOverviewCameraConfig &config) noexcept {
  return config.logicalCanvasWidth != 0U && config.logicalCanvasHeight != 0U &&
         std::isfinite(config.horizontalFovDegrees) &&
         config.horizontalFovDegrees >= 1.0F &&
         config.horizontalFovDegrees <= 175.0F &&
         std::isfinite(config.viewportMarginFraction) &&
         config.viewportMarginFraction >= 0.0F &&
         config.viewportMarginFraction < 0.5F &&
         finite(config.forwardDirection) &&
         std::isfinite(config.minimumDepthPadding) &&
         config.minimumDepthPadding > 0.0F &&
         std::isfinite(config.relativeDepthPadding) &&
         config.relativeDepthPadding >= 0.0F;
}

} // namespace

SceneOverviewCameraBuildResult
buildSceneOverviewCamera(const DrawModelPayload &model,
                         const SceneOverviewCameraConfig &config) noexcept {
  if (!validConfig(config)) {
    return failure(SceneOverviewCameraIssueKind::invalidConfig);
  }

  Bounds3 worldBounds{
      .minimum =
          {
              std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max(),
          },
      .maximum =
          {
              std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest(),
          },
  };
  bool foundGeometry = false;
  for (const auto &instance : model.instances) {
    const auto meshSlot = static_cast<std::size_t>(instance.meshSlot);
    if (meshSlot >= model.meshes.size()) {
      return failure(SceneOverviewCameraIssueKind::invalidMeshSlot);
    }
    const auto &mesh = model.meshes[meshSlot];
    if (mesh.vertices.empty()) {
      continue;
    }
    if (!validBounds(mesh.localBounds)) {
      return failure(SceneOverviewCameraIssueKind::invalidLocalBounds);
    }
    if (!finite(instance.modelLinear) || !finite(instance.modelTranslation)) {
      return failure(SceneOverviewCameraIssueKind::invalidInstanceTransform);
    }
    for (const auto &local : corners(mesh.localBounds)) {
      auto world = applyRuntimeColumn(instance.modelLinear, local);
      world.x += instance.modelTranslation.x;
      world.y += instance.modelTranslation.y;
      world.z += instance.modelTranslation.z;
      if (!finite(world)) {
        return failure(SceneOverviewCameraIssueKind::nonFiniteWorldBounds);
      }
      worldBounds.minimum.x = std::min(worldBounds.minimum.x, world.x);
      worldBounds.minimum.y = std::min(worldBounds.minimum.y, world.y);
      worldBounds.minimum.z = std::min(worldBounds.minimum.z, world.z);
      worldBounds.maximum.x = std::max(worldBounds.maximum.x, world.x);
      worldBounds.maximum.y = std::max(worldBounds.maximum.y, world.y);
      worldBounds.maximum.z = std::max(worldBounds.maximum.z, world.z);
      foundGeometry = true;
    }
  }
  if (!foundGeometry) {
    return failure(SceneOverviewCameraIssueKind::emptyModel);
  }
  if (!validBounds(worldBounds)) {
    return failure(SceneOverviewCameraIssueKind::nonFiniteWorldBounds);
  }

  const auto midpoint = [](const float minimum, const float maximum) {
    return static_cast<float>(static_cast<double>(minimum) +
                              (static_cast<double>(maximum) - minimum) * 0.5);
  };
  const Vec3 centre{
      midpoint(worldBounds.minimum.x, worldBounds.maximum.x),
      midpoint(worldBounds.minimum.y, worldBounds.maximum.y),
      midpoint(worldBounds.minimum.z, worldBounds.maximum.z),
  };
  const double extentX =
      static_cast<double>(worldBounds.maximum.x) - worldBounds.minimum.x;
  const double extentY =
      static_cast<double>(worldBounds.maximum.y) - worldBounds.minimum.y;
  const double extentZ =
      static_cast<double>(worldBounds.maximum.z) - worldBounds.minimum.z;
  const double diagonal =
      std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);
  const double forwardLength =
      std::sqrt(static_cast<double>(config.forwardDirection.x) *
                    config.forwardDirection.x +
                static_cast<double>(config.forwardDirection.y) *
                    config.forwardDirection.y +
                static_cast<double>(config.forwardDirection.z) *
                    config.forwardDirection.z);
  if (!finite(centre) || !std::isfinite(diagonal) ||
      !std::isfinite(forwardLength) || !(forwardLength > 0.0)) {
    return failure(SceneOverviewCameraIssueKind::invalidConfig);
  }
  const Vec3 forward{
      static_cast<float>(config.forwardDirection.x / forwardLength),
      static_cast<float>(config.forwardDirection.y / forwardLength),
      static_cast<float>(config.forwardDirection.z / forwardLength),
  };
  const Vec3 orientationCamera{
      centre.x - forward.x,
      centre.y - forward.y,
      centre.z - forward.z,
  };
  const auto lookAt = legacyGameplayCameraLookAt(centre, orientationCamera);
  if (!lookAt.has_value()) {
    return failure(SceneOverviewCameraIssueKind::cameraTransformFailed);
  }
  const auto centredTransform = buildLegacyCameraTransform({
      .linear = lookAt->cameraWorldLinear,
      .translation = centre,
      .uniformScale = 1.0F,
      .inverseScaleSquared = 1.0F,
  });
  if (!centredTransform.complete()) {
    return failure(SceneOverviewCameraIssueKind::cameraTransformFailed);
  }

  const double halfHorizontalTangent = std::tan(
      static_cast<double>(config.horizontalFovDegrees) * radiansPerHalfDegree);
  const double halfVerticalTangent =
      halfHorizontalTangent * config.logicalCanvasHeight /
      static_cast<double>(config.logicalCanvasWidth);
  const double usableHalfExtent = 1.0 - 2.0 * config.viewportMarginFraction;
  if (!std::isfinite(halfHorizontalTangent) ||
      !std::isfinite(halfVerticalTangent) || !(halfHorizontalTangent > 0.0) ||
      !(halfVerticalTangent > 0.0) || !(usableHalfExtent > 0.0)) {
    return failure(SceneOverviewCameraIssueKind::invalidConfig);
  }

  double requiredDistance = 0.0;
  for (const auto &corner : corners(worldBounds)) {
    const auto camera = centredTransform.transform->transform(corner);
    if (!camera.complete()) {
      return failure(SceneOverviewCameraIssueKind::cameraTransformFailed);
    }
    requiredDistance =
        std::max(requiredDistance,
                 std::abs(static_cast<double>(camera.cameraSpacePosition->x)) /
                         (halfHorizontalTangent * usableHalfExtent) -
                     camera.cameraSpacePosition->z);
    requiredDistance =
        std::max(requiredDistance,
                 std::abs(static_cast<double>(camera.cameraSpacePosition->y)) /
                         (halfVerticalTangent * usableHalfExtent) -
                     camera.cameraSpacePosition->z);
    requiredDistance = std::max(
        requiredDistance, -static_cast<double>(camera.cameraSpacePosition->z));
  }
  const double depthPadding =
      std::max(static_cast<double>(config.minimumDepthPadding),
               diagonal * config.relativeDepthPadding);
  const double distance = requiredDistance + depthPadding;
  if (!std::isfinite(distance) || !(distance > 0.0)) {
    return failure(SceneOverviewCameraIssueKind::cameraTransformFailed);
  }
  const Vec3 cameraWorldPosition{
      static_cast<float>(centre.x - forward.x * distance),
      static_cast<float>(centre.y - forward.y * distance),
      static_cast<float>(centre.z - forward.z * distance),
  };
  if (!finite(cameraWorldPosition)) {
    return failure(SceneOverviewCameraIssueKind::cameraTransformFailed);
  }

  const auto finalTransform = buildLegacyCameraTransform({
      .linear = lookAt->cameraWorldLinear,
      .translation = cameraWorldPosition,
      .uniformScale = 1.0F,
      .inverseScaleSquared = 1.0F,
  });
  if (!finalTransform.complete()) {
    return failure(SceneOverviewCameraIssueKind::cameraTransformFailed);
  }
  float minimumDepth = std::numeric_limits<float>::max();
  float maximumDepth = std::numeric_limits<float>::lowest();
  for (const auto &corner : corners(worldBounds)) {
    const auto camera = finalTransform.transform->transform(corner);
    if (!camera.complete() || !(camera.cameraSpacePosition->z > 0.0F)) {
      return failure(SceneOverviewCameraIssueKind::sceneDoesNotFit);
    }
    minimumDepth = std::min(minimumDepth, camera.cameraSpacePosition->z);
    maximumDepth = std::max(maximumDepth, camera.cameraSpacePosition->z);
  }
  const float nearDistance = std::max(0.001F, minimumDepth * 0.25F);
  const float farDistance = static_cast<float>(maximumDepth + depthPadding);
  if (!std::isfinite(nearDistance) || !std::isfinite(farDistance) ||
      !(farDistance > nearDistance)) {
    return failure(SceneOverviewCameraIssueKind::sceneDoesNotFit);
  }

  const LegacyGameplayCameraFrameSnapshot frame{
      .simulationStep = 0U,
      .publicationGeneration = 1U,
      .state =
          {
              .roomState =
                  {
                      .runtimeWorldPosition = cameraWorldPosition,
                      .worldRoomIndex = 0U,
                  },
              .axisFactors = {1.0F, 1.0F, 1.0F},
          },
  };
  const auto pose = buildLegacyGameplayCameraPoseSnapshot(
      frame, centre,
      {
          .projection =
              {
                  .nearDistance = nearDistance,
                  .farDistance = farDistance,
                  .horizontalFovDegrees = config.horizontalFovDegrees,
                  .windowWidth = static_cast<float>(config.logicalCanvasWidth),
                  .centre =
                      {
                          static_cast<float>(config.logicalCanvasWidth) * 0.5F,
                          static_cast<float>(config.logicalCanvasHeight) * 0.5F,
                      },
              },
      });
  if (!pose.complete()) {
    return failure(SceneOverviewCameraIssueKind::cameraPoseFailed);
  }
  const auto packet = buildLegacyGameplayCameraClipPacket(
      *pose.snapshot,
      {
          .logicalCanvasHeight = static_cast<float>(config.logicalCanvasHeight),
      });
  if (!packet.complete()) {
    return failure(SceneOverviewCameraIssueKind::cameraClipPacketFailed);
  }

  const float minimumX = config.viewportMarginFraction *
                         static_cast<float>(config.logicalCanvasWidth);
  const float maximumX =
      static_cast<float>(config.logicalCanvasWidth) - minimumX;
  const float minimumY = config.viewportMarginFraction *
                         static_cast<float>(config.logicalCanvasHeight);
  const float maximumY =
      static_cast<float>(config.logicalCanvasHeight) - minimumY;
  constexpr float roundingTolerancePixels = 0.25F;
  for (const auto &corner : corners(worldBounds)) {
    const auto projected = pose.snapshot->project(corner);
    if (!projected.complete() ||
        projected.point->cameraSpacePosition.z < nearDistance ||
        projected.point->cameraSpacePosition.z > farDistance ||
        projected.point->projected.point.x <
            minimumX - roundingTolerancePixels ||
        projected.point->projected.point.x >
            maximumX + roundingTolerancePixels ||
        projected.point->projected.point.y <
            minimumY - roundingTolerancePixels ||
        projected.point->projected.point.y >
            maximumY + roundingTolerancePixels) {
      return failure(SceneOverviewCameraIssueKind::sceneDoesNotFit);
    }
  }

  return {
      .snapshot =
          SceneOverviewCameraSnapshot{
              .worldBounds = worldBounds,
              .cameraWorldPosition = cameraWorldPosition,
              .clipPacket = *packet.packet,
              .viewportMarginFraction = config.viewportMarginFraction,
          },
      .issue = std::nullopt,
  };
}

} // namespace airfix::render
