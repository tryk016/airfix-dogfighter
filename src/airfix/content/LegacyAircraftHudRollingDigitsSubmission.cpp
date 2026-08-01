#include "airfix/content/LegacyAircraftHudRollingDigitsSubmission.hpp"

#include <cmath>
#include <utility>

namespace airfix::content {
namespace {

using namespace render::recovered_legacy_aircraft_hud_rolling_digits;

[[nodiscard]] bool finite(const render::OutputPixelRect &rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height) &&
         rect.width > 0.0F && rect.height > 0.0F;
}

[[nodiscard]] bool
validUv(const LegacyAircraftHudRollingDigitsUvRect &uv) noexcept {
  return std::isfinite(uv.minimumU) && std::isfinite(uv.minimumV) &&
         std::isfinite(uv.maximumU) && std::isfinite(uv.maximumV) &&
         uv.minimumU >= 0.0F && uv.minimumV >= 0.0F && uv.maximumU <= 1.0F &&
         uv.maximumV <= 1.0F && uv.minimumU < uv.maximumU &&
         uv.minimumV < uv.maximumV;
}

[[nodiscard]] std::optional<render::OutputPixelRect>
mapCommand(const render::LegacyAircraftHudRollingDigitCommand &command,
           const render::NativeRenderLayout &layout,
           const float widgetScale) noexcept {
  const auto &source = command.destinationRect;
  if (!std::isfinite(source.x) || !std::isfinite(source.y) ||
      !std::isfinite(source.width) || !std::isfinite(source.height) ||
      source.width <= 0.0F || source.height <= 0.0F ||
      command.slotIndex >= digitCount) {
    return std::nullopt;
  }
  const float anchorX =
      source.x - static_cast<float>(command.slotIndex) * destinationPitchX;
  const float anchorY = source.y - destinationTopInset;
  const render::UiLogicalPoint topLeft{
      .x = anchorX + (source.x - anchorX) * widgetScale,
      .y = anchorY + (source.y - anchorY) * widgetScale,
  };
  const render::UiLogicalPoint bottomRight{
      .x = anchorX + (source.x + source.width - anchorX) * widgetScale,
      .y = anchorY + (source.y + source.height - anchorY) * widgetScale,
  };
  const auto mappedTopLeft = layout.outputPointFromUi(topLeft);
  const auto mappedBottomRight = layout.outputPointFromUi(bottomRight);
  const auto outputExtent = layout.outputExtent();
  if (!mappedTopLeft.has_value() || !mappedBottomRight.has_value() ||
      !std::isfinite(mappedTopLeft->x) || !std::isfinite(mappedTopLeft->y) ||
      !std::isfinite(mappedBottomRight->x) ||
      !std::isfinite(mappedBottomRight->y) || mappedTopLeft->x < 0.0F ||
      mappedTopLeft->y < 0.0F || mappedBottomRight->x < mappedTopLeft->x ||
      mappedBottomRight->y < mappedTopLeft->y ||
      mappedBottomRight->x > static_cast<float>(outputExtent.width) ||
      mappedBottomRight->y > static_cast<float>(outputExtent.height)) {
    return std::nullopt;
  }
  return render::OutputPixelRect{
      .x = mappedTopLeft->x,
      .y = mappedTopLeft->y,
      .width = mappedBottomRight->x - mappedTopLeft->x,
      .height = mappedBottomRight->y - mappedTopLeft->y,
  };
}

[[nodiscard]] std::optional<LegacyAircraftHudRollingDigitsUvRect>
mapUv(const render::LegacyAircraftHudRollingDigitSourceRect &source) noexcept {
  const float width =
      static_cast<float>(legacyAircraftHudRollingDigitsTextureWidth);
  const float height =
      static_cast<float>(legacyAircraftHudRollingDigitsTextureHeight);
  const LegacyAircraftHudRollingDigitsUvRect uv{
      .minimumU = source.left / width,
      .minimumV = source.top / height,
      .maximumU = source.right / width,
      .maximumV = source.bottom / height,
  };
  return validUv(uv) ? std::optional<LegacyAircraftHudRollingDigitsUvRect>{uv}
                     : std::nullopt;
}

[[nodiscard]] LegacyAircraftHudRollingDigitsSubmissionResult
failure(const LegacyAircraftHudRollingDigitsSubmissionStatus status) noexcept {
  return {.status = status, .submission = std::nullopt};
}

} // namespace

const LegacyAircraftHudRollingDigitsSubmissionCommand *
LegacyAircraftHudRollingDigitsSubmission::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

bool LegacyAircraftHudRollingDigitsSubmission::belongsTo(
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures)
    const noexcept {
  if (!textures.valid() || !transactionIdentity.valid() ||
      transactionIdentity != textures.transactionIdentity ||
      revision != textures.revision || !std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent ||
      commandCount > orderedCommands.size()) {
    return false;
  }
  const auto *texture =
      textures.texture(LegacyAircraftHudRollingDigitsTextureRole::digits);
  if (texture == nullptr || texture->textureId.value != 0U) {
    return false;
  }
  std::optional<std::uint8_t> previousSlot;
  for (std::size_t index = 0U; index < commandCount; ++index) {
    const auto &entry = orderedCommands[index];
    if (entry.slotIndex >= digitCount ||
        (previousSlot.has_value() && entry.slotIndex <= *previousSlot) ||
        entry.textureRole !=
            LegacyAircraftHudRollingDigitsTextureRole::digits ||
        entry.textureId != texture->textureId || !finite(entry.outputRect) ||
        !validUv(entry.uv) ||
        entry.blendMode != LegacyAircraftHudRollingDigitsBlendMode::
                               sourceAlphaOneMinusSourceAlpha ||
        entry.depthMode !=
            LegacyAircraftHudRollingDigitsDepthMode::alwaysWrite ||
        entry.samplingMode !=
            LegacyAircraftHudRollingDigitsSamplingMode::linearClamp) {
      return false;
    }
    previousSlot = entry.slotIndex;
  }
  return true;
}

LegacyAircraftHudRollingDigitsSubmissionResult
buildLegacyAircraftHudRollingDigitsSubmission(
    const render::LegacyAircraftHudRollingDigitsPlan &plan,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures,
    const render::NativeRenderLayout &layout,
    const float uiScalePercent) noexcept {
  if (!plan.ready()) {
    return failure(
        LegacyAircraftHudRollingDigitsSubmissionStatus::planNotReady);
  }
  if (!textures.valid()) {
    return failure(
        LegacyAircraftHudRollingDigitsSubmissionStatus::invalidTextureSet);
  }
  const auto designExtent = layout.uiDesignExtent();
  if (designExtent.width != render::recovered_legacy_canvas::width ||
      designExtent.height != render::recovered_legacy_canvas::height) {
    return failure(LegacyAircraftHudRollingDigitsSubmissionStatus::
                       incompatibleUiDesignExtent);
  }
  if (!std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent) {
    return failure(
        LegacyAircraftHudRollingDigitsSubmissionStatus::uiScaleOutOfRange);
  }
  if (plan.commandCount > plan.orderedCommands.size()) {
    return failure(
        LegacyAircraftHudRollingDigitsSubmissionStatus::invalidPlanCommand);
  }
  const auto *texture =
      textures.texture(LegacyAircraftHudRollingDigitsTextureRole::digits);
  if (texture == nullptr || texture->textureId.value != 0U) {
    return failure(
        LegacyAircraftHudRollingDigitsSubmissionStatus::invalidTextureSet);
  }

  LegacyAircraftHudRollingDigitsSubmission submission{
      .revision = textures.revision,
      .transactionIdentity = textures.transactionIdentity,
      .uiScalePercent = uiScalePercent,
  };
  const float widgetScale = uiScalePercent / 100.0F;
  std::optional<std::uint8_t> previousSlot;
  for (std::size_t index = 0U; index < plan.commandCount; ++index) {
    const auto &source = plan.orderedCommands[index];
    if (source.slotIndex >= digitCount ||
        (previousSlot.has_value() && source.slotIndex <= *previousSlot)) {
      return failure(
          LegacyAircraftHudRollingDigitsSubmissionStatus::invalidPlanCommand);
    }
    const auto outputRect = mapCommand(source, layout, widgetScale);
    const auto uv = mapUv(source.sourceRect);
    if (!outputRect.has_value()) {
      return failure(
          LegacyAircraftHudRollingDigitsSubmissionStatus::outputMappingFailed);
    }
    if (!uv.has_value()) {
      return failure(
          LegacyAircraftHudRollingDigitsSubmissionStatus::invalidPlanCommand);
    }
    submission.orderedCommands[index] = {
        .slotIndex = source.slotIndex,
        .textureRole = LegacyAircraftHudRollingDigitsTextureRole::digits,
        .textureId = texture->textureId,
        .outputRect = *outputRect,
        .uv = *uv,
        .tintArgb = source.colourArgb,
        .blendMode = LegacyAircraftHudRollingDigitsBlendMode::
            sourceAlphaOneMinusSourceAlpha,
        .depthMode = LegacyAircraftHudRollingDigitsDepthMode::alwaysWrite,
        .samplingMode = LegacyAircraftHudRollingDigitsSamplingMode::linearClamp,
    };
    previousSlot = source.slotIndex;
  }
  submission.commandCount = plan.commandCount;
  if (!submission.belongsTo(textures)) {
    return failure(
        LegacyAircraftHudRollingDigitsSubmissionStatus::invalidPlanCommand);
  }
  return {
      .status = LegacyAircraftHudRollingDigitsSubmissionStatus::ready,
      .submission = std::move(submission),
  };
}

} // namespace airfix::content
