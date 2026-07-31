#include "airfix/render/NativeGameplayScreenProjection.hpp"

namespace airfix::render {
namespace {

[[nodiscard]] NativeGameplayScreenProjectionResult
failure(const NativeGameplayScreenProjectionIssueKind kind,
        const std::optional<LegacyGameplayCameraWorldProjectionIssue>
            legacyProjectionIssue = std::nullopt) noexcept {
  return {
      .point = std::nullopt,
      .issue =
          NativeGameplayScreenProjectionIssue{
              .kind = kind,
              .legacyProjectionIssue = legacyProjectionIssue,
          },
  };
}

[[nodiscard]] bool compatible(const LegacyGameplayCameraClipPacket &camera,
                              const NativeRenderLayout &layout) noexcept {
  const auto reference = layout.referenceCameraLogicalExtent();
  const auto &projection = camera.pose().projection();
  return camera.logicalCanvasWidth() == reference.width &&
         camera.logicalCanvasHeight() == reference.height &&
         projection.horizontalFovDegrees() ==
             layout.referenceHorizontalFovDegrees();
}

} // namespace

NativeGameplayScreenProjectionResult
projectGameplayWorldPointToOutput(const LegacyGameplayCameraClipPacket &camera,
                                  const NativeRenderLayout &layout,
                                  const Vec3 &worldPosition) noexcept {
  if (!compatible(camera, layout)) {
    return failure(
        NativeGameplayScreenProjectionIssueKind::cameraLayoutMismatch);
  }

  const auto legacy = camera.pose().project(worldPosition);
  if (!legacy.complete()) {
    return failure(
        NativeGameplayScreenProjectionIssueKind::legacyProjectionFailed,
        legacy.issue);
  }

  const auto cameraLogicalPoint = layout.mapReferenceCameraPoint({
      .x = legacy.point->projected.point.x,
      .y = legacy.point->projected.point.y,
  });
  const auto output = layout.outputPointFromCamera(cameraLogicalPoint);
  if (!output.has_value()) {
    return failure(NativeGameplayScreenProjectionIssueKind::nonFiniteOutput);
  }

  const auto &projection = camera.pose().projection();
  const float cameraSpaceZ = legacy.point->projected.cameraSpaceZ;
  return {
      .point =
          NativeGameplayScreenProjectedWorldPoint{
              .legacy = *legacy.point,
              .cameraLogicalPoint = cameraLogicalPoint,
              .output = *output,
              .withinRecoveredDepthRange =
                  cameraSpaceZ >= projection.nearDistance() &&
                  cameraSpaceZ <= projection.farDistance(),
          },
      .issue = std::nullopt,
  };
}

} // namespace airfix::render
