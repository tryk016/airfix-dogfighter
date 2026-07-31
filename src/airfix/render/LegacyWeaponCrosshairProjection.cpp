#include "airfix/render/LegacyWeaponCrosshairProjection.hpp"

#include <cmath>

namespace airfix::render {
namespace {

[[nodiscard]] LegacyWeaponCrosshairProjectionResult
failure(const LegacyWeaponCrosshairSizeState &state,
        const LegacyWeaponCrosshairProjectionIssueKind kind,
        const std::optional<NativeGameplayScreenProjectionIssue>
            screenProjectionIssue = std::nullopt) noexcept {
  return {
      .state = state,
      .plan = std::nullopt,
      .issue =
          LegacyWeaponCrosshairProjectionIssue{
              .kind = kind,
              .screenProjectionIssue = screenProjectionIssue,
          },
  };
}

[[nodiscard]] bool
finite(const LegacyWeaponCrosshairSizeState &state) noexcept {
  return std::isfinite(state.currentLogicalWidth) &&
         std::isfinite(state.currentLogicalHeight) &&
         std::isfinite(state.targetLogicalWidth) &&
         std::isfinite(state.targetLogicalHeight);
}

} // namespace

LegacyWeaponCrosshairProjectionResult projectLegacyWeaponCrosshairToOutput(
    const LegacyGameplayCameraClipPacket &camera,
    const NativeRenderLayout &layout,
    const LegacyWeaponCrosshairSizeState &state,
    const LegacyWeaponCrosshairProjectionInput &input) noexcept {
  if (input.originalTextureWidth == 0U || input.originalTextureHeight == 0U) {
    return failure(
        state,
        LegacyWeaponCrosshairProjectionIssueKind::textureExtentNotPositive);
  }
  if (!finite(state) || !std::isfinite(input.targetWorldPosition.x) ||
      !std::isfinite(input.targetWorldPosition.y) ||
      !std::isfinite(input.targetWorldPosition.z) ||
      !std::isfinite(input.collisionFraction) ||
      !std::isfinite(input.localAimOffsetZ) ||
      !std::isfinite(input.uiScalePercent)) {
    return failure(state,
                   LegacyWeaponCrosshairProjectionIssueKind::nonFiniteInput);
  }
  if (input.collisionFraction < 0.0F || input.collisionFraction > 1.0F) {
    return failure(
        state,
        LegacyWeaponCrosshairProjectionIssueKind::collisionFractionOutOfRange);
  }
  if (input.uiScalePercent < native_render_policy::minimumUiScalePercent ||
      input.uiScalePercent > native_render_policy::maximumUiScalePercent) {
    return failure(state,
                   LegacyWeaponCrosshairProjectionIssueKind::uiScaleOutOfRange);
  }

  const float distanceScale =
      2.0F - input.collisionFraction * input.localAimOffsetZ;
  const float targetLogicalWidth =
      static_cast<float>(input.originalTextureWidth) * distanceScale;
  const float targetLogicalHeight =
      static_cast<float>(input.originalTextureHeight) * distanceScale;
  if (!std::isfinite(distanceScale) || !std::isfinite(targetLogicalWidth) ||
      !std::isfinite(targetLogicalHeight)) {
    return failure(state,
                   LegacyWeaponCrosshairProjectionIssueKind::nonFiniteOutput);
  }
  if (!(targetLogicalWidth > 0.0F) || !(targetLogicalHeight > 0.0F)) {
    return failure(
        state,
        LegacyWeaponCrosshairProjectionIssueKind::nonPositiveLogicalSize);
  }

  LegacyWeaponCrosshairSizeState next = state;
  next.targetLogicalWidth = targetLogicalWidth;
  next.targetLogicalHeight = targetLogicalHeight;
  if (next.resetCurrentSize) {
    next.currentLogicalWidth = targetLogicalWidth;
    next.currentLogicalHeight = targetLogicalHeight;
    next.resetCurrentSize = false;
  }
  if (!(next.currentLogicalWidth > 0.0F) ||
      !(next.currentLogicalHeight > 0.0F)) {
    return failure(
        state,
        LegacyWeaponCrosshairProjectionIssueKind::nonPositiveLogicalSize);
  }

  const auto projected = projectGameplayWorldPointToOutput(
      camera, layout, input.targetWorldPosition);
  if (!projected.complete()) {
    return failure(
        state, LegacyWeaponCrosshairProjectionIssueKind::screenProjectionFailed,
        projected.issue);
  }

  const float outputScale = layout.uiScale() * input.uiScalePercent / 100.0F;
  const float outputWidth = next.currentLogicalWidth * outputScale;
  const float outputHeight = next.currentLogicalHeight * outputScale;
  const OutputPixelRect outputRect{
      .x = projected.point->output.point.x - outputWidth * 0.5F,
      .y = projected.point->output.point.y - outputHeight * 0.5F,
      .width = outputWidth,
      .height = outputHeight,
  };
  if (!std::isfinite(outputScale) || !std::isfinite(outputRect.x) ||
      !std::isfinite(outputRect.y) || !std::isfinite(outputRect.width) ||
      !std::isfinite(outputRect.height) || !(outputRect.width > 0.0F) ||
      !(outputRect.height > 0.0F)) {
    return failure(state,
                   LegacyWeaponCrosshairProjectionIssueKind::nonFiniteOutput);
  }

  return {
      .state = next,
      .plan =
          LegacyWeaponCrosshairSpritePlan{
              .projectedTarget = *projected.point,
              .outputRect = outputRect,
              .logicalDistanceScale = distanceScale,
              .insideSceneViewport =
                  projected.point->output.insideSceneViewport,
              .withinRecoveredDepthRange =
                  projected.point->withinRecoveredDepthRange,
          },
      .issue = std::nullopt,
  };
}

} // namespace airfix::render
