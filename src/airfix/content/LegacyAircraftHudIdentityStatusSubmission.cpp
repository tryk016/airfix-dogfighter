#include "airfix/content/LegacyAircraftHudIdentityStatusSubmission.hpp"

#include <cmath>
#include <utility>

namespace airfix::content {
namespace {

using namespace render::recovered_legacy_aircraft_hud_identity_status;
using render::recovered_legacy_canvas::height;
using render::recovered_legacy_canvas::width;

[[nodiscard]] LegacyAircraftHudIdentityStatusSubmissionResult
failure(const LegacyAircraftHudIdentityStatusSubmissionStatus status) noexcept {
  return {.status = status, .submission = std::nullopt};
}

[[nodiscard]] constexpr std::optional<std::size_t>
commandRank(const render::LegacyAircraftHudIdentityStatusCommandKind kind,
            const std::uint8_t digitSlotIndex) noexcept {
  using Kind = render::LegacyAircraftHudIdentityStatusCommandKind;
  if (kind == Kind::aircraftIcon) {
    return 0U;
  }
  if (kind == Kind::healthDigit) {
    return digitSlotIndex <
                   render::recovered_legacy_aircraft_hud_rolling_digits::
                       digitCount
               ? std::optional<std::size_t>{1U + digitSlotIndex}
               : std::nullopt;
  }
  if (kind == Kind::teamBadge) {
    return 5U;
  }
  if (kind == Kind::technologyDigit) {
    return digitSlotIndex <
                   render::recovered_legacy_aircraft_hud_rolling_digits::
                       digitCount
               ? std::optional<std::size_t>{6U + digitSlotIndex}
               : std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] bool
finiteRect(const render::OutputPixelRect rectangle) noexcept {
  return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
         std::isfinite(rectangle.width) && std::isfinite(rectangle.height) &&
         rectangle.width > 0.0F && rectangle.height > 0.0F;
}

[[nodiscard]] bool
validUv(const LegacyAircraftHudIdentityStatusUvRect uv) noexcept {
  return std::isfinite(uv.minimumU) && std::isfinite(uv.minimumV) &&
         std::isfinite(uv.maximumU) && std::isfinite(uv.maximumV) &&
         uv.minimumU >= 0.0F && uv.minimumV >= 0.0F && uv.maximumU <= 1.0F &&
         uv.maximumV <= 1.0F && uv.minimumU < uv.maximumU &&
         uv.minimumV < uv.maximumV;
}

[[nodiscard]] std::optional<render::OutputPixelPoint>
mapPoint(const render::LegacyCanvasPoint point,
         const render::NativeRenderLayout &layout,
         const float widgetScale) noexcept {
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return std::nullopt;
  }
  const render::UiLogicalPoint scaled{
      .x = width * 0.5F + (point.x - width * 0.5F) * widgetScale,
      .y = height - (height - point.y) * widgetScale,
  };
  const auto mapped = layout.outputPointFromUi(scaled);
  const auto extent = layout.outputExtent();
  if (!mapped.has_value() || !std::isfinite(mapped->x) ||
      !std::isfinite(mapped->y) || mapped->x < 0.0F || mapped->y < 0.0F ||
      mapped->x > static_cast<float>(extent.width) ||
      mapped->y > static_cast<float>(extent.height)) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] std::optional<render::OutputPixelRect>
mapRectangle(const render::LegacyCanvasRect source,
             const render::NativeRenderLayout &layout,
             const float widgetScale) noexcept {
  if (!std::isfinite(source.x) || !std::isfinite(source.y) ||
      !std::isfinite(source.width) || !std::isfinite(source.height) ||
      source.width <= 0.0F || source.height <= 0.0F) {
    return std::nullopt;
  }
  const auto topLeft = mapPoint({source.x, source.y}, layout, widgetScale);
  const auto bottomRight = mapPoint(
      {source.x + source.width, source.y + source.height}, layout, widgetScale);
  if (!topLeft.has_value() || !bottomRight.has_value()) {
    return std::nullopt;
  }
  const render::OutputPixelRect result{
      .x = topLeft->x,
      .y = topLeft->y,
      .width = bottomRight->x - topLeft->x,
      .height = bottomRight->y - topLeft->y,
  };
  return finiteRect(result) ? std::optional<render::OutputPixelRect>{result}
                            : std::nullopt;
}

[[nodiscard]] std::optional<LegacyAircraftHudIdentityStatusUvRect>
uvFrom(const render::LegacyCanvasRect source, const float textureWidth,
       const float textureHeight) noexcept {
  if (!std::isfinite(source.x) || !std::isfinite(source.y) ||
      !std::isfinite(source.width) || !std::isfinite(source.height) ||
      source.x < 0.0F || source.y < 0.0F || source.width <= 0.0F ||
      source.height <= 0.0F || source.x + source.width > textureWidth ||
      source.y + source.height > textureHeight) {
    return std::nullopt;
  }
  const LegacyAircraftHudIdentityStatusUvRect result{
      .minimumU = source.x / textureWidth,
      .minimumV = source.y / textureHeight,
      .maximumU = (source.x + source.width) / textureWidth,
      .maximumV = (source.y + source.height) / textureHeight,
  };
  return validUv(result)
             ? std::optional<LegacyAircraftHudIdentityStatusUvRect>{result}
             : std::nullopt;
}

[[nodiscard]] const LoadedLegacyAircraftHudIdentityStatusTexture *
teamTexture(const LoadedLegacyAircraftHudIdentityStatusTextureSet &textures,
            const render::LegacyAircraftHudTeamBadge badge) noexcept {
  return textures.teamBadge(
      badge == render::LegacyAircraftHudTeamBadge::technologyStar
          ? LegacyAircraftHudIdentityStatusTextureKind::technologyStar
          : LegacyAircraftHudIdentityStatusTextureKind::technologyCross);
}

} // namespace

const LegacyAircraftHudIdentityStatusSubmissionCommand *
LegacyAircraftHudIdentityStatusSubmission::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

bool LegacyAircraftHudIdentityStatusSubmission::belongsTo(
    const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures)
    const noexcept {
  if (!identityTextures.valid() || !digitTextures.valid() ||
      identityTextures.transactionIdentity !=
          digitTextures.transactionIdentity ||
      identityTextures.revision != digitTextures.revision ||
      transactionIdentity != identityTextures.transactionIdentity ||
      revision != identityTextures.revision || !std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent ||
      commandCount > orderedCommands.size()) {
    return false;
  }
  const auto *digitTexture =
      digitTextures.texture(LegacyAircraftHudRollingDigitsTextureRole::digits);
  if (digitTexture == nullptr) {
    return false;
  }

  std::optional<std::size_t> previousRank;
  using Kind = render::LegacyAircraftHudIdentityStatusCommandKind;
  for (std::size_t index = 0U; index < commandCount; ++index) {
    const auto &entry = orderedCommands[index];
    const auto rank = commandRank(entry.kind, entry.digitSlotIndex);
    if (!rank.has_value() ||
        (previousRank.has_value() && *rank <= *previousRank) ||
        !finiteRect(entry.outputRect) || !validUv(entry.uv) ||
        entry.colourArgb != whiteArgb ||
        entry.blendMode != LegacyAircraftHudIdentityStatusBlendMode::
                               sourceAlphaOneMinusSourceAlpha ||
        entry.depthMode !=
            LegacyAircraftHudIdentityStatusDepthMode::alwaysWrite ||
        entry.samplingMode !=
            LegacyAircraftHudIdentityStatusSamplingMode::linearClamp) {
      return false;
    }

    if (entry.kind == Kind::aircraftIcon) {
      const auto *texture = identityTextures.icon(entry.sourceTextureIndex);
      if (entry.textureNamespace !=
              LegacyAircraftHudIdentityStatusTextureNamespace::identityStatus ||
          texture == nullptr || entry.textureId != texture->textureId ||
          entry.uv !=
              LegacyAircraftHudIdentityStatusUvRect{0.0F, 0.0F, 1.0F, 1.0F}) {
        return false;
      }
    } else if (entry.kind == Kind::teamBadge) {
      const auto *texture = teamTexture(identityTextures, entry.teamBadge);
      if (entry.textureNamespace !=
              LegacyAircraftHudIdentityStatusTextureNamespace::identityStatus ||
          texture == nullptr || entry.textureId != texture->textureId ||
          entry.sourceTextureIndex != 0U ||
          entry.uv !=
              LegacyAircraftHudIdentityStatusUvRect{0.0F, 0.0F, 1.0F, 1.0F}) {
        return false;
      }
    } else if (entry.kind == Kind::healthDigit ||
               entry.kind == Kind::technologyDigit) {
      if (entry.textureNamespace !=
              LegacyAircraftHudIdentityStatusTextureNamespace::rollingDigits ||
          entry.textureId != digitTexture->textureId ||
          entry.sourceTextureIndex != 0U) {
        return false;
      }
    } else {
      return false;
    }
    previousRank = rank;
  }
  return true;
}

LegacyAircraftHudIdentityStatusSubmissionResult
buildLegacyAircraftHudIdentityStatusSubmission(
    const render::LegacyAircraftHudIdentityStatusPlan &plan,
    const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
    const render::NativeRenderLayout &layout,
    const float uiScalePercent) noexcept {
  if (!plan.ready()) {
    return failure(
        LegacyAircraftHudIdentityStatusSubmissionStatus::planNotReady);
  }
  if (!identityTextures.valid()) {
    return failure(LegacyAircraftHudIdentityStatusSubmissionStatus::
                       invalidIdentityTextureSet);
  }
  if (!digitTextures.valid()) {
    return failure(LegacyAircraftHudIdentityStatusSubmissionStatus::
                       invalidDigitTextureSet);
  }
  if (identityTextures.transactionIdentity !=
          digitTextures.transactionIdentity ||
      identityTextures.revision != digitTextures.revision) {
    return failure(
        LegacyAircraftHudIdentityStatusSubmissionStatus::textureOwnersMismatch);
  }
  if (plan.sourceScreenWidth != static_cast<std::uint32_t>(width) ||
      plan.sourceScreenHeight != static_cast<std::uint32_t>(height)) {
    return failure(LegacyAircraftHudIdentityStatusSubmissionStatus::
                       incompatibleLegacyScreenExtent);
  }
  const auto designExtent = layout.uiDesignExtent();
  if (designExtent.width != width || designExtent.height != height) {
    return failure(LegacyAircraftHudIdentityStatusSubmissionStatus::
                       incompatibleUiDesignExtent);
  }
  if (!std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent) {
    return failure(
        LegacyAircraftHudIdentityStatusSubmissionStatus::uiScaleOutOfRange);
  }
  if (plan.commandCount > plan.orderedCommands.size()) {
    return failure(
        LegacyAircraftHudIdentityStatusSubmissionStatus::invalidPlanCommand);
  }
  const auto *digitTexture =
      digitTextures.texture(LegacyAircraftHudRollingDigitsTextureRole::digits);
  if (digitTexture == nullptr) {
    return failure(LegacyAircraftHudIdentityStatusSubmissionStatus::
                       invalidDigitTextureSet);
  }

  LegacyAircraftHudIdentityStatusSubmission submission{
      .revision = identityTextures.revision,
      .transactionIdentity = identityTextures.transactionIdentity,
      .uiScalePercent = uiScalePercent,
  };
  const float widgetScale = uiScalePercent / 100.0F;
  std::optional<std::size_t> previousRank;
  using Kind = render::LegacyAircraftHudIdentityStatusCommandKind;
  for (std::size_t index = 0U; index < plan.commandCount; ++index) {
    const auto &source = plan.orderedCommands[index];
    const auto rank = commandRank(source.kind, source.digitSlotIndex);
    if (!rank.has_value() ||
        (previousRank.has_value() && *rank <= *previousRank)) {
      return failure(
          LegacyAircraftHudIdentityStatusSubmissionStatus::invalidPlanCommand);
    }
    const auto output =
        mapRectangle(source.destinationRect, layout, widgetScale);
    if (!output.has_value()) {
      return failure(
          LegacyAircraftHudIdentityStatusSubmissionStatus::outputMappingFailed);
    }

    LegacyAircraftHudIdentityStatusSubmissionCommand destination{
        .kind = source.kind,
        .teamBadge = source.teamBadge,
        .sourceTextureIndex = source.textureIndex,
        .digitSlotIndex = source.digitSlotIndex,
        .outputRect = *output,
        .colourArgb = source.colourArgb,
    };
    const LoadedLegacyAircraftHudIdentityStatusTexture *identityTexture =
        nullptr;
    float sourceWidth{};
    float sourceHeight{};
    if (source.kind == Kind::aircraftIcon) {
      identityTexture = identityTextures.icon(source.textureIndex);
      sourceWidth =
          static_cast<float>(legacyAircraftHudIdentityStatusTextureWidth);
      sourceHeight =
          static_cast<float>(legacyAircraftHudIdentityStatusTextureHeight);
    } else if (source.kind == Kind::teamBadge) {
      identityTexture = teamTexture(identityTextures, source.teamBadge);
      destination.sourceTextureIndex = 0U;
      sourceWidth =
          static_cast<float>(legacyAircraftHudIdentityStatusTextureWidth);
      sourceHeight =
          static_cast<float>(legacyAircraftHudIdentityStatusTextureHeight);
    } else if (source.kind == Kind::healthDigit ||
               source.kind == Kind::technologyDigit) {
      destination.textureNamespace =
          LegacyAircraftHudIdentityStatusTextureNamespace::rollingDigits;
      destination.textureId = digitTexture->textureId;
      destination.sourceTextureIndex = 0U;
      sourceWidth =
          static_cast<float>(legacyAircraftHudRollingDigitsTextureWidth);
      sourceHeight =
          static_cast<float>(legacyAircraftHudRollingDigitsTextureHeight);
    } else {
      return failure(
          LegacyAircraftHudIdentityStatusSubmissionStatus::invalidPlanCommand);
    }
    if (source.kind == Kind::aircraftIcon || source.kind == Kind::teamBadge) {
      if (identityTexture == nullptr) {
        return failure(LegacyAircraftHudIdentityStatusSubmissionStatus::
                           invalidIdentityTextureSet);
      }
      destination.textureNamespace =
          LegacyAircraftHudIdentityStatusTextureNamespace::identityStatus;
      destination.textureId = identityTexture->textureId;
    }
    const auto uv = uvFrom(source.sourceRect, sourceWidth, sourceHeight);
    if (!uv.has_value() || source.colourArgb != whiteArgb) {
      return failure(
          LegacyAircraftHudIdentityStatusSubmissionStatus::invalidPlanCommand);
    }
    destination.uv = *uv;
    submission.orderedCommands[index] = destination;
    previousRank = rank;
  }
  submission.commandCount = plan.commandCount;
  if (!submission.belongsTo(identityTextures, digitTextures)) {
    return failure(
        LegacyAircraftHudIdentityStatusSubmissionStatus::invalidPlanCommand);
  }
  return {
      .status = LegacyAircraftHudIdentityStatusSubmissionStatus::ready,
      .submission = std::move(submission),
  };
}

} // namespace airfix::content
