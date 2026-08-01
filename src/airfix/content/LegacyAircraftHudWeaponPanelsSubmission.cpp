#include "airfix/content/LegacyAircraftHudWeaponPanelsSubmission.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace airfix::content {
namespace {

using namespace render::recovered_legacy_aircraft_hud_weapon_panels;
using render::recovered_legacy_canvas::height;
using render::recovered_legacy_canvas::width;

[[nodiscard]] LegacyAircraftHudWeaponPanelsSubmissionResult
failure(const LegacyAircraftHudWeaponPanelsSubmissionStatus status) noexcept {
  return {.status = status, .submission = std::nullopt};
}

[[nodiscard]] constexpr std::optional<std::size_t>
commandRank(const render::LegacyAircraftHudWeaponPanelSlot slot,
            const render::LegacyAircraftHudWeaponPanelCommandKind kind,
            const std::uint8_t digitSlotIndex) noexcept {
  using Kind = render::LegacyAircraftHudWeaponPanelCommandKind;
  using Slot = render::LegacyAircraftHudWeaponPanelSlot;
  if (kind == Kind::background) {
    return slot == Slot::primary     ? std::optional<std::size_t>{0U}
           : slot == Slot::secondary ? std::optional<std::size_t>{1U}
                                     : std::nullopt;
  }
  const std::size_t base = slot == Slot::primary     ? 2U
                           : slot == Slot::secondary ? 8U
                                                     : maximumCommands;
  if (base == maximumCommands) {
    return std::nullopt;
  }
  if (kind == Kind::digit) {
    return digitSlotIndex <
                   render::recovered_legacy_aircraft_hud_rolling_digits::
                       digitCount
               ? std::optional<std::size_t>{base + digitSlotIndex}
               : std::nullopt;
  }
  if (kind == Kind::icon) {
    return base + 4U;
  }
  if (kind == Kind::statusOverlay) {
    return base + 5U;
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
validUv(const LegacyAircraftHudWeaponPanelUvRect uv) noexcept {
  return std::isfinite(uv.minimumU) && std::isfinite(uv.minimumV) &&
         std::isfinite(uv.maximumU) && std::isfinite(uv.maximumV) &&
         uv.minimumU >= 0.0F && uv.minimumV >= 0.0F && uv.maximumU <= 1.0F &&
         uv.maximumV <= 1.0F && uv.minimumU < uv.maximumU &&
         uv.minimumV < uv.maximumV;
}

[[nodiscard]] bool statusColour(const std::uint32_t colour) noexcept {
  return std::find(statusColoursArgb.begin(), statusColoursArgb.end(),
                   colour) != statusColoursArgb.end();
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

[[nodiscard]] std::optional<LegacyAircraftHudWeaponPanelUvRect>
uvFrom(const render::LegacyCanvasRect source, const float textureWidth,
       const float textureHeight) noexcept {
  if (!std::isfinite(source.x) || !std::isfinite(source.y) ||
      !std::isfinite(source.width) || !std::isfinite(source.height) ||
      source.x < 0.0F || source.y < 0.0F || source.width <= 0.0F ||
      source.height <= 0.0F || source.x + source.width > textureWidth ||
      source.y + source.height > textureHeight) {
    return std::nullopt;
  }
  const LegacyAircraftHudWeaponPanelUvRect result{
      .minimumU = source.x / textureWidth,
      .minimumV = source.y / textureHeight,
      .maximumU = (source.x + source.width) / textureWidth,
      .maximumV = (source.y + source.height) / textureHeight,
  };
  return validUv(result)
             ? std::optional<LegacyAircraftHudWeaponPanelUvRect>{result}
             : std::nullopt;
}

[[nodiscard]] const LoadedLegacyAircraftHudWeaponPanelTexture *
background(const LoadedLegacyAircraftHudWeaponPanelTextureSet &textures,
           const render::LegacyAircraftHudWeaponPanelSlot slot) noexcept {
  return textures.background(
      slot == render::LegacyAircraftHudWeaponPanelSlot::primary
          ? LegacyAircraftHudWeaponPanelTextureKind::primaryBackground
          : LegacyAircraftHudWeaponPanelTextureKind::secondaryBackground);
}

} // namespace

const LegacyAircraftHudWeaponPanelSubmissionCommand *
LegacyAircraftHudWeaponPanelsSubmission::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

bool LegacyAircraftHudWeaponPanelsSubmission::belongsTo(
    const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures)
    const noexcept {
  if (!weaponTextures.valid() || !digitTextures.valid() ||
      weaponTextures.transactionIdentity != digitTextures.transactionIdentity ||
      weaponTextures.revision != digitTextures.revision ||
      transactionIdentity != weaponTextures.transactionIdentity ||
      revision != weaponTextures.revision || !std::isfinite(uiScalePercent) ||
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
  for (std::size_t index = 0U; index < commandCount; ++index) {
    const auto &entry = orderedCommands[index];
    const auto rank = commandRank(entry.slot, entry.kind, entry.digitSlotIndex);
    if (!rank.has_value() ||
        (previousRank.has_value() && *rank <= *previousRank) ||
        !finiteRect(entry.outputRect) ||
        entry.depthMode != LegacyAircraftHudWeaponPanelDepthMode::alwaysWrite) {
      return false;
    }

    using Kind = render::LegacyAircraftHudWeaponPanelCommandKind;
    if (entry.kind == Kind::statusOverlay) {
      if (entry.textured() || entry.textureId.value != 0U ||
          entry.sourceTextureIndex != 0U ||
          entry.blendMode != LegacyAircraftHudWeaponPanelBlendMode::
                                 destinationMultiplySourceColour ||
          entry.samplingMode !=
              LegacyAircraftHudWeaponPanelSamplingMode::notApplicable ||
          entry.uv != LegacyAircraftHudWeaponPanelUvRect{} ||
          !statusColour(entry.colourArgb)) {
        return false;
      }
    } else {
      if (!entry.textured() || !validUv(entry.uv) ||
          entry.colourArgb != whiteArgb ||
          entry.blendMode != LegacyAircraftHudWeaponPanelBlendMode::
                                 sourceAlphaOneMinusSourceAlpha ||
          entry.samplingMode !=
              LegacyAircraftHudWeaponPanelSamplingMode::linearClamp) {
        return false;
      }
      if (entry.kind == Kind::digit) {
        if (entry.textureNamespace !=
                LegacyAircraftHudWeaponPanelTextureNamespace::rollingDigits ||
            entry.textureId != digitTexture->textureId ||
            entry.sourceTextureIndex != 0U) {
          return false;
        }
      } else if (entry.kind == Kind::background) {
        const auto *texture = background(weaponTextures, entry.slot);
        if (entry.textureNamespace !=
                LegacyAircraftHudWeaponPanelTextureNamespace::weaponPanels ||
            texture == nullptr || entry.textureId != texture->textureId ||
            entry.sourceTextureIndex != 0U ||
            entry.uv !=
                LegacyAircraftHudWeaponPanelUvRect{0.0F, 0.0F, 1.0F, 1.0F}) {
          return false;
        }
      } else if (entry.kind == Kind::icon) {
        const auto *texture = weaponTextures.icon(entry.sourceTextureIndex);
        if (entry.textureNamespace !=
                LegacyAircraftHudWeaponPanelTextureNamespace::weaponPanels ||
            texture == nullptr || entry.textureId != texture->textureId ||
            entry.uv !=
                LegacyAircraftHudWeaponPanelUvRect{0.0F, 0.0F, 1.0F, 1.0F}) {
          return false;
        }
      } else {
        return false;
      }
    }
    previousRank = rank;
  }
  return true;
}

LegacyAircraftHudWeaponPanelsSubmissionResult
buildLegacyAircraftHudWeaponPanelsSubmission(
    const render::LegacyAircraftHudWeaponPanelsPlan &plan,
    const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
    const render::NativeRenderLayout &layout,
    const float uiScalePercent) noexcept {
  if (!plan.ready()) {
    return failure(LegacyAircraftHudWeaponPanelsSubmissionStatus::planNotReady);
  }
  if (!weaponTextures.valid()) {
    return failure(
        LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidWeaponTextureSet);
  }
  if (!digitTextures.valid()) {
    return failure(
        LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidDigitTextureSet);
  }
  if (weaponTextures.transactionIdentity != digitTextures.transactionIdentity ||
      weaponTextures.revision != digitTextures.revision) {
    return failure(
        LegacyAircraftHudWeaponPanelsSubmissionStatus::textureOwnersMismatch);
  }
  if (plan.sourceScreenWidth != static_cast<std::uint32_t>(width) ||
      plan.sourceScreenHeight != static_cast<std::uint32_t>(height)) {
    return failure(LegacyAircraftHudWeaponPanelsSubmissionStatus::
                       incompatibleLegacyScreenExtent);
  }
  const auto designExtent = layout.uiDesignExtent();
  if (designExtent.width != width || designExtent.height != height) {
    return failure(LegacyAircraftHudWeaponPanelsSubmissionStatus::
                       incompatibleUiDesignExtent);
  }
  if (!std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent) {
    return failure(
        LegacyAircraftHudWeaponPanelsSubmissionStatus::uiScaleOutOfRange);
  }
  if (plan.commandCount > plan.orderedCommands.size()) {
    return failure(
        LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidPlanCommand);
  }
  const auto *digitTexture =
      digitTextures.texture(LegacyAircraftHudRollingDigitsTextureRole::digits);
  if (digitTexture == nullptr) {
    return failure(
        LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidDigitTextureSet);
  }

  LegacyAircraftHudWeaponPanelsSubmission submission{
      .revision = weaponTextures.revision,
      .transactionIdentity = weaponTextures.transactionIdentity,
      .uiScalePercent = uiScalePercent,
  };
  const float widgetScale = uiScalePercent / 100.0F;
  std::optional<std::size_t> previousRank;
  for (std::size_t index = 0U; index < plan.commandCount; ++index) {
    const auto &source = plan.orderedCommands[index];
    const auto rank =
        commandRank(source.slot, source.kind, source.digitSlotIndex);
    if (!rank.has_value() ||
        (previousRank.has_value() && *rank <= *previousRank)) {
      return failure(
          LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidPlanCommand);
    }
    const auto output =
        mapRectangle(source.destinationRect, layout, widgetScale);
    if (!output.has_value()) {
      return failure(
          LegacyAircraftHudWeaponPanelsSubmissionStatus::outputMappingFailed);
    }

    LegacyAircraftHudWeaponPanelSubmissionCommand destination{
        .slot = source.slot,
        .kind = source.kind,
        .sourceTextureIndex = source.textureIndex,
        .digitSlotIndex = source.digitSlotIndex,
        .outputRect = *output,
        .colourArgb = source.colourArgb,
        .depthMode = LegacyAircraftHudWeaponPanelDepthMode::alwaysWrite,
    };
    using Kind = render::LegacyAircraftHudWeaponPanelCommandKind;
    if (source.kind == Kind::statusOverlay) {
      if (!statusColour(source.colourArgb)) {
        return failure(
            LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidPlanCommand);
      }
      destination.sourceTextureIndex = 0U;
      destination.blendMode = LegacyAircraftHudWeaponPanelBlendMode::
          destinationMultiplySourceColour;
      destination.samplingMode =
          LegacyAircraftHudWeaponPanelSamplingMode::notApplicable;
    } else {
      const LoadedLegacyAircraftHudWeaponPanelTexture *weaponTexture = nullptr;
      float sourceWidth{};
      float sourceHeight{};
      if (source.kind == Kind::background) {
        weaponTexture = background(weaponTextures, source.slot);
        sourceWidth =
            static_cast<float>(legacyAircraftHudWeaponPanelBackgroundWidth);
        sourceHeight =
            static_cast<float>(legacyAircraftHudWeaponPanelBackgroundHeight);
        destination.sourceTextureIndex = 0U;
      } else if (source.kind == Kind::icon) {
        weaponTexture = weaponTextures.icon(source.textureIndex);
        sourceWidth = static_cast<float>(legacyAircraftHudWeaponPanelIconWidth);
        sourceHeight =
            static_cast<float>(legacyAircraftHudWeaponPanelIconHeight);
      } else if (source.kind == Kind::digit) {
        destination.textureNamespace =
            LegacyAircraftHudWeaponPanelTextureNamespace::rollingDigits;
        destination.textureId = digitTexture->textureId;
        destination.sourceTextureIndex = 0U;
        sourceWidth =
            static_cast<float>(legacyAircraftHudRollingDigitsTextureWidth);
        sourceHeight =
            static_cast<float>(legacyAircraftHudRollingDigitsTextureHeight);
      } else {
        return failure(
            LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidPlanCommand);
      }
      if (source.kind != Kind::digit) {
        if (weaponTexture == nullptr) {
          return failure(LegacyAircraftHudWeaponPanelsSubmissionStatus::
                             invalidWeaponTextureSet);
        }
        destination.textureNamespace =
            LegacyAircraftHudWeaponPanelTextureNamespace::weaponPanels;
        destination.textureId = weaponTexture->textureId;
      }
      const auto uv = uvFrom(source.sourceRect, sourceWidth, sourceHeight);
      if (!uv.has_value() || source.colourArgb != whiteArgb) {
        return failure(
            LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidPlanCommand);
      }
      destination.uv = *uv;
      destination.blendMode =
          LegacyAircraftHudWeaponPanelBlendMode::sourceAlphaOneMinusSourceAlpha;
      destination.samplingMode =
          LegacyAircraftHudWeaponPanelSamplingMode::linearClamp;
    }
    submission.orderedCommands[index] = destination;
    previousRank = rank;
  }
  submission.commandCount = plan.commandCount;
  if (!submission.belongsTo(weaponTextures, digitTextures)) {
    return failure(
        LegacyAircraftHudWeaponPanelsSubmissionStatus::invalidPlanCommand);
  }
  return {
      .status = LegacyAircraftHudWeaponPanelsSubmissionStatus::ready,
      .submission = std::move(submission),
  };
}

} // namespace airfix::content
