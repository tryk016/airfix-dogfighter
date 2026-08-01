#include "airfix/content/LegacyAircraftHudInstrumentsSubmission.hpp"

#include <cmath>
#include <utility>

namespace airfix::content {
namespace {

using namespace render::recovered_legacy_aircraft_hud_instruments;
using render::recovered_legacy_canvas::height;
using render::recovered_legacy_canvas::width;

[[nodiscard]] constexpr std::optional<std::size_t>
roleIndex(const LegacyAircraftHudInstrumentTextureRole role) noexcept {
  const auto index = static_cast<std::size_t>(role);
  return index < legacyAircraftHudInstrumentTextureCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::size_t> commandRank(
    const render::LegacyAircraftHudInstrumentSide side,
    const render::LegacyAircraftHudInstrumentCommandKind kind) noexcept {
  if (side == render::LegacyAircraftHudInstrumentSide::right) {
    return kind == render::LegacyAircraftHudInstrumentCommandKind::face
               ? std::optional<std::size_t>{0U}
               : std::optional<std::size_t>{1U};
  }
  if (side == render::LegacyAircraftHudInstrumentSide::left) {
    return kind == render::LegacyAircraftHudInstrumentCommandKind::face
               ? std::optional<std::size_t>{2U}
               : std::optional<std::size_t>{3U};
  }
  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<LegacyAircraftHudInstrumentTextureRole>
textureRole(
    const render::LegacyAircraftHudInstrumentSide side,
    const render::LegacyAircraftHudInstrumentCommandKind kind) noexcept {
  if (kind == render::LegacyAircraftHudInstrumentCommandKind::indicator) {
    return LegacyAircraftHudInstrumentTextureRole::indicator;
  }
  if (kind != render::LegacyAircraftHudInstrumentCommandKind::face) {
    return std::nullopt;
  }
  return side == render::LegacyAircraftHudInstrumentSide::right
             ? std::optional<
                   LegacyAircraftHudInstrumentTextureRole>{LegacyAircraftHudInstrumentTextureRole::
                                                               rightFace}
         : side == render::LegacyAircraftHudInstrumentSide::left
             ? std::optional<
                   LegacyAircraftHudInstrumentTextureRole>{LegacyAircraftHudInstrumentTextureRole::
                                                               leftFace}
             : std::nullopt;
}

[[nodiscard]] constexpr LegacyAircraftHudInstrumentUvRect
expectedUv(const render::LegacyAircraftHudInstrumentCommandKind kind) noexcept {
  if (kind == render::LegacyAircraftHudInstrumentCommandKind::face) {
    return {0.0F, 0.0F, 1.0F, 1.0F};
  }
  return {0.0F, 0.0F,
          indicatorSourceWidth /
              static_cast<float>(legacyAircraftHudIndicatorWidth),
          indicatorSourceHeight /
              static_cast<float>(legacyAircraftHudIndicatorHeight)};
}

[[nodiscard]] bool finite(const render::OutputPixelPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool
validUv(const LegacyAircraftHudInstrumentUvRect uv) noexcept {
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
  if (!mapped.has_value() || !finite(*mapped) || mapped->x < 0.0F ||
      mapped->y < 0.0F || mapped->x > static_cast<float>(extent.width) ||
      mapped->y > static_cast<float>(extent.height)) {
    return std::nullopt;
  }
  return mapped;
}

[[nodiscard]] LegacyAircraftHudInstrumentsSubmissionResult
failure(const LegacyAircraftHudInstrumentsSubmissionStatus status) noexcept {
  return {.status = status, .submission = std::nullopt};
}

} // namespace

const LegacyAircraftHudInstrumentSubmissionCommand *
LegacyAircraftHudInstrumentsSubmission::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

bool LegacyAircraftHudInstrumentsSubmission::belongsTo(
    const LoadedLegacyAircraftHudInstrumentTextureSet &textures)
    const noexcept {
  if (!textures.valid() || !transactionIdentity.valid() ||
      transactionIdentity != textures.transactionIdentity ||
      revision != textures.revision || !std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent ||
      commandCount > orderedCommands.size()) {
    return false;
  }

  std::optional<std::size_t> previousRank;
  std::optional<std::uint32_t> leftTint;
  for (std::size_t index = 0U; index < commandCount; ++index) {
    const auto &entry = orderedCommands[index];
    const auto rank = commandRank(entry.side, entry.kind);
    const auto role = textureRole(entry.side, entry.kind);
    if (!rank.has_value() || !role.has_value() ||
        (previousRank.has_value() && *rank <= *previousRank)) {
      return false;
    }
    const auto *texture = textures.texture(*role);
    const auto roleOffset = roleIndex(*role);
    if (texture == nullptr || !roleOffset.has_value() ||
        entry.textureRole != *role || entry.textureId != texture->textureId ||
        texture->textureId.value != *roleOffset ||
        entry.uv != expectedUv(entry.kind) || !validUv(entry.uv) ||
        entry.blendMode != LegacyAircraftHudInstrumentBlendMode::
                               sourceAlphaOneMinusSourceAlpha ||
        entry.depthMode != LegacyAircraftHudInstrumentDepthMode::alwaysWrite ||
        entry.samplingMode !=
            LegacyAircraftHudInstrumentSamplingMode::linearClamp) {
      return false;
    }
    if (entry.side == render::LegacyAircraftHudInstrumentSide::right) {
      if (entry.tintArgb != rightInstrumentTintArgb) {
        return false;
      }
    } else if (!leftTint.has_value()) {
      leftTint = entry.tintArgb;
    } else if (entry.tintArgb != *leftTint) {
      return false;
    }
    for (const auto point : entry.outputQuad) {
      if (!finite(point)) {
        return false;
      }
    }
    previousRank = rank;
  }
  return true;
}

LegacyAircraftHudInstrumentsSubmissionResult
buildLegacyAircraftHudInstrumentsSubmission(
    const render::LegacyAircraftHudInstrumentsPlan &plan,
    const LoadedLegacyAircraftHudInstrumentTextureSet &textures,
    const render::NativeRenderLayout &layout,
    const float uiScalePercent) noexcept {
  if (!plan.ready()) {
    return failure(LegacyAircraftHudInstrumentsSubmissionStatus::planNotReady);
  }
  if (!textures.valid()) {
    return failure(
        LegacyAircraftHudInstrumentsSubmissionStatus::invalidTextureSet);
  }
  if (plan.sourceScreenWidth != static_cast<std::uint32_t>(width) ||
      plan.sourceScreenHeight != static_cast<std::uint32_t>(height)) {
    return failure(LegacyAircraftHudInstrumentsSubmissionStatus::
                       incompatibleLegacyScreenExtent);
  }
  const auto designExtent = layout.uiDesignExtent();
  if (designExtent.width != width || designExtent.height != height) {
    return failure(LegacyAircraftHudInstrumentsSubmissionStatus::
                       incompatibleUiDesignExtent);
  }
  if (!std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent) {
    return failure(
        LegacyAircraftHudInstrumentsSubmissionStatus::uiScaleOutOfRange);
  }
  if (plan.commandCount > plan.orderedCommands.size()) {
    return failure(
        LegacyAircraftHudInstrumentsSubmissionStatus::invalidPlanCommand);
  }

  LegacyAircraftHudInstrumentsSubmission submission{
      .revision = textures.revision,
      .transactionIdentity = textures.transactionIdentity,
      .uiScalePercent = uiScalePercent,
  };
  const float widgetScale = uiScalePercent / 100.0F;
  std::optional<std::size_t> previousRank;
  for (std::size_t index = 0U; index < plan.commandCount; ++index) {
    const auto &source = plan.orderedCommands[index];
    const auto rank = commandRank(source.side, source.kind);
    const auto role = textureRole(source.side, source.kind);
    if (!rank.has_value() || !role.has_value() ||
        (previousRank.has_value() && *rank <= *previousRank)) {
      return failure(
          LegacyAircraftHudInstrumentsSubmissionStatus::invalidPlanCommand);
    }
    const auto *texture = textures.texture(*role);
    const auto roleOffset = roleIndex(*role);
    if (texture == nullptr || !roleOffset.has_value() ||
        texture->textureId.value != *roleOffset) {
      return failure(
          LegacyAircraftHudInstrumentsSubmissionStatus::invalidTextureSet);
    }

    LegacyAircraftHudInstrumentSubmissionCommand destination{
        .side = source.side,
        .kind = source.kind,
        .textureRole = *role,
        .textureId = texture->textureId,
        .uv = expectedUv(source.kind),
        .tintArgb = source.tintArgb,
        .blendMode = LegacyAircraftHudInstrumentBlendMode::
            sourceAlphaOneMinusSourceAlpha,
        .depthMode = LegacyAircraftHudInstrumentDepthMode::alwaysWrite,
        .samplingMode = LegacyAircraftHudInstrumentSamplingMode::linearClamp,
    };
    for (std::size_t pointIndex = 0U;
         pointIndex < source.destinationQuad.size(); ++pointIndex) {
      const auto mapped =
          mapPoint(source.destinationQuad[pointIndex], layout, widgetScale);
      if (!mapped.has_value()) {
        return failure(
            LegacyAircraftHudInstrumentsSubmissionStatus::outputMappingFailed);
      }
      destination.outputQuad[pointIndex] = *mapped;
    }
    submission.orderedCommands[index] = destination;
    previousRank = rank;
  }
  submission.commandCount = plan.commandCount;
  if (!submission.belongsTo(textures)) {
    return failure(
        LegacyAircraftHudInstrumentsSubmissionStatus::invalidPlanCommand);
  }
  return {
      .status = LegacyAircraftHudInstrumentsSubmissionStatus::ready,
      .submission = std::move(submission),
  };
}

} // namespace airfix::content
