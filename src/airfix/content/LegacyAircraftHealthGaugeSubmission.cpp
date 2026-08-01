#include "airfix/content/LegacyAircraftHealthGaugeSubmission.hpp"

#include <cmath>
#include <utility>

namespace airfix::content {
namespace {

using render::recovered_legacy_aircraft_health_gauge::damageMaskArgb;
using render::recovered_legacy_canvas::height;
using render::recovered_legacy_canvas::width;

[[nodiscard]] constexpr std::optional<std::size_t>
roleIndex(const LegacyAircraftHealthGaugeTextureRole role) noexcept {
  const auto index = static_cast<std::size_t>(role);
  return index < legacyAircraftHealthGaugeTextureCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

[[nodiscard]] bool finite(const render::OutputPixelPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] std::optional<render::OutputPixelPoint>
mapPoint(const render::LegacyCanvasPoint point,
         const render::NativeRenderLayout &layout,
         const float widgetScale) noexcept {
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
    return std::nullopt;
  }
  const render::UiLogicalPoint scaled{
      .x = point.x * widgetScale,
      .y = height - (height - point.y) * widgetScale,
  };
  const auto mapped = layout.outputPointFromUi(scaled);
  const auto outputExtent = layout.outputExtent();
  if (!mapped.has_value() || !finite(*mapped) || mapped->x < 0.0F ||
      mapped->y < 0.0F || mapped->x > static_cast<float>(outputExtent.width) ||
      mapped->y > static_cast<float>(outputExtent.height)) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] LegacyAircraftHealthGaugeSubmissionResult
failure(const LegacyAircraftHealthGaugeSubmissionStatus status) noexcept {
  return {.status = status, .submission = std::nullopt};
}

} // namespace

const LegacyAircraftHealthGaugeSubmissionCommand *
LegacyAircraftHealthGaugeSubmission::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

bool LegacyAircraftHealthGaugeSubmission::belongsTo(
    const LoadedLegacyAircraftHealthGaugeTextureSet &textures) const noexcept {
  if (!textures.valid() || !transactionIdentity.valid() ||
      transactionIdentity != textures.transactionIdentity ||
      revision != textures.revision || commandCount == 0U ||
      commandCount > orderedCommands.size()) {
    return false;
  }

  bool backgroundSeen = false;
  bool damageMaskSeen = false;
  bool foregroundSeen = false;
  for (std::size_t index = 0U; index < commandCount; ++index) {
    const auto &entry = orderedCommands[index];
    if (entry.depthMode != LegacyAircraftHealthGaugeDepthMode::alwaysWrite ||
        entry.samplingMode !=
            LegacyAircraftHealthGaugeSamplingMode::linearClamp) {
      return false;
    }
    for (const auto point : entry.outputQuad) {
      if (!finite(point)) {
        return false;
      }
    }

    if (entry.kind ==
        render::LegacyAircraftHealthGaugeCommandKind::damageMaskQuad) {
      if (foregroundSeen || entry.textured() ||
          entry.colourArgb != damageMaskArgb ||
          entry.blendMode != LegacyAircraftHealthGaugeBlendMode::opaque) {
        return false;
      }
      damageMaskSeen = true;
      continue;
    }

    const bool background =
        entry.kind ==
        render::LegacyAircraftHealthGaugeCommandKind::armourMeterTexture;
    const bool foreground =
        entry.kind ==
        render::LegacyAircraftHealthGaugeCommandKind::armourTexture;
    if ((!background && !foreground) || !entry.textured() ||
        entry.colourArgb != 0xFFFFFFFFU ||
        entry.blendMode != LegacyAircraftHealthGaugeBlendMode::
                               sourceAlphaOneMinusSourceAlpha ||
        entry.uv != LegacyAircraftHealthGaugeUvRect{}) {
      return false;
    }
    if (background) {
      if (backgroundSeen || damageMaskSeen || foregroundSeen || index != 0U ||
          entry.textureRole !=
              LegacyAircraftHealthGaugeTextureRole::background) {
        return false;
      }
      backgroundSeen = true;
    } else {
      if (!damageMaskSeen || foregroundSeen || index + 1U != commandCount ||
          entry.textureRole !=
              LegacyAircraftHealthGaugeTextureRole::foreground) {
        return false;
      }
      foregroundSeen = true;
    }

    const auto expectedIndex = roleIndex(entry.textureRole);
    if (!expectedIndex.has_value() || entry.textureId.value != *expectedIndex ||
        textures.textures[*expectedIndex].role != entry.textureRole ||
        textures.textures[*expectedIndex].textureId != entry.textureId) {
      return false;
    }
  }
  return damageMaskSeen;
}

LegacyAircraftHealthGaugeSubmissionResult
buildLegacyAircraftHealthGaugeSubmission(
    const render::LegacyAircraftHealthGaugePlan &plan,
    const LoadedLegacyAircraftHealthGaugeTextureSet &textures,
    const render::NativeRenderLayout &layout,
    const float uiScalePercent) noexcept {
  if (!plan.ready()) {
    return failure(LegacyAircraftHealthGaugeSubmissionStatus::planNotReady);
  }
  if (!textures.valid()) {
    return failure(
        LegacyAircraftHealthGaugeSubmissionStatus::invalidTextureSet);
  }
  if (plan.sourceScreenWidth != static_cast<std::uint32_t>(width) ||
      plan.sourceScreenHeight != static_cast<std::uint32_t>(height)) {
    return failure(LegacyAircraftHealthGaugeSubmissionStatus::
                       incompatibleLegacyScreenExtent);
  }
  const auto designExtent = layout.uiDesignExtent();
  if (designExtent.width != width || designExtent.height != height) {
    return failure(
        LegacyAircraftHealthGaugeSubmissionStatus::incompatibleUiDesignExtent);
  }
  if (!std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent) {
    return failure(
        LegacyAircraftHealthGaugeSubmissionStatus::uiScaleOutOfRange);
  }
  if (plan.commandCount > plan.orderedCommands.size()) {
    return failure(
        LegacyAircraftHealthGaugeSubmissionStatus::invalidPlanCommand);
  }

  const float widgetScale = uiScalePercent / 100.0F;
  LegacyAircraftHealthGaugeSubmission submission{
      .revision = textures.revision,
      .transactionIdentity = textures.transactionIdentity,
      .uiScalePercent = uiScalePercent,
  };

  for (std::size_t index = 0U; index < plan.commandCount; ++index) {
    const auto &source = plan.orderedCommands[index];
    auto &destination = submission.orderedCommands[index];
    destination.kind = source.kind;
    destination.depthMode = LegacyAircraftHealthGaugeDepthMode::alwaysWrite;
    destination.samplingMode =
        LegacyAircraftHealthGaugeSamplingMode::linearClamp;

    if (source.kind ==
        render::LegacyAircraftHealthGaugeCommandKind::damageMaskQuad) {
      if (source.colourArgb != damageMaskArgb) {
        return failure(
            LegacyAircraftHealthGaugeSubmissionStatus::invalidPlanCommand);
      }
      for (std::size_t pointIndex = 0U; pointIndex < source.quad.size();
           ++pointIndex) {
        const auto mapped =
            mapPoint(source.quad[pointIndex], layout, widgetScale);
        if (!mapped.has_value()) {
          return failure(
              LegacyAircraftHealthGaugeSubmissionStatus::outputMappingFailed);
        }
        destination.outputQuad[pointIndex] = *mapped;
      }
      destination.colourArgb = damageMaskArgb;
      destination.blendMode = LegacyAircraftHealthGaugeBlendMode::opaque;
      continue;
    }

    const auto role =
        source.kind ==
                render::LegacyAircraftHealthGaugeCommandKind::armourMeterTexture
            ? LegacyAircraftHealthGaugeTextureRole::background
        : source.kind ==
                render::LegacyAircraftHealthGaugeCommandKind::armourTexture
            ? LegacyAircraftHealthGaugeTextureRole::foreground
            : static_cast<LegacyAircraftHealthGaugeTextureRole>(0xFFU);
    const auto roleOffset = roleIndex(role);
    const auto *texture = textures.texture(role);
    if (!roleOffset.has_value() || texture == nullptr ||
        texture->textureId.value != *roleOffset) {
      return failure(
          LegacyAircraftHealthGaugeSubmissionStatus::invalidPlanCommand);
    }

    constexpr float textureExtent =
        static_cast<float>(legacyAircraftHealthGaugeTextureWidth);
    const std::array<render::LegacyCanvasPoint, 4U> textureQuad{{
        source.textureOrigin,
        {source.textureOrigin.x + textureExtent, source.textureOrigin.y},
        {source.textureOrigin.x + textureExtent,
         source.textureOrigin.y + textureExtent},
        {source.textureOrigin.x, source.textureOrigin.y + textureExtent},
    }};
    for (std::size_t pointIndex = 0U; pointIndex < textureQuad.size();
         ++pointIndex) {
      const auto mapped =
          mapPoint(textureQuad[pointIndex], layout, widgetScale);
      if (!mapped.has_value()) {
        return failure(
            LegacyAircraftHealthGaugeSubmissionStatus::outputMappingFailed);
      }
      destination.outputQuad[pointIndex] = *mapped;
    }
    destination.textureRole = role;
    destination.textureId = texture->textureId;
    destination.uv = {};
    destination.colourArgb = 0xFFFFFFFFU;
    destination.blendMode =
        LegacyAircraftHealthGaugeBlendMode::sourceAlphaOneMinusSourceAlpha;
  }
  submission.commandCount = plan.commandCount;
  if (!submission.belongsTo(textures)) {
    return failure(
        LegacyAircraftHealthGaugeSubmissionStatus::invalidPlanCommand);
  }
  return {
      .status = LegacyAircraftHealthGaugeSubmissionStatus::ready,
      .submission = std::move(submission),
  };
}

} // namespace airfix::content
