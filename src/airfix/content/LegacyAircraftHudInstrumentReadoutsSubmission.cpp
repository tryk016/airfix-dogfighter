#include "airfix/content/LegacyAircraftHudInstrumentReadoutsSubmission.hpp"

#include <cmath>
#include <utility>

namespace airfix::content {
namespace {

[[nodiscard]] LegacyAircraftHudInstrumentReadoutsSubmissionResult failure(
    const LegacyAircraftHudInstrumentReadoutsSubmissionStatus status) noexcept {
  return {
      .status = status,
      .submission = std::nullopt,
      .rejectedSide = std::nullopt,
      .readoutStatus = std::nullopt,
  };
}

[[nodiscard]] LegacyAircraftHudInstrumentReadoutsSubmissionResult
readoutFailure(
    const render::LegacyAircraftHudInstrumentReadoutSide side,
    const LegacyAircraftHudRollingDigitsSubmissionStatus status) noexcept {
  return {
      .status = LegacyAircraftHudInstrumentReadoutsSubmissionStatus::
          readoutSubmissionRejected,
      .submission = std::nullopt,
      .rejectedSide = side,
      .readoutStatus = status,
  };
}

[[nodiscard]] constexpr std::uint8_t sideOrdinal(
    const render::LegacyAircraftHudInstrumentReadoutSide side) noexcept {
  return static_cast<std::uint8_t>(side);
}

} // namespace

const LegacyAircraftHudInstrumentReadoutSubmission *
LegacyAircraftHudInstrumentReadoutsSubmission::readout(
    const std::size_t index) const noexcept {
  if (index >= readoutCount || index >= orderedReadouts.size()) {
    return nullptr;
  }
  return &orderedReadouts[index];
}

bool LegacyAircraftHudInstrumentReadoutsSubmission::belongsTo(
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures)
    const noexcept {
  if (!textures.valid() || !transactionIdentity.valid() ||
      transactionIdentity != textures.transactionIdentity ||
      revision != textures.revision || !std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent ||
      readoutCount > orderedReadouts.size()) {
    return false;
  }

  std::optional<std::uint8_t> previousSide;
  for (std::size_t index = 0U; index < readoutCount; ++index) {
    const auto &entry = orderedReadouts[index];
    const auto ordinal = sideOrdinal(entry.side);
    if (ordinal >
            sideOrdinal(render::LegacyAircraftHudInstrumentReadoutSide::left) ||
        (previousSide.has_value() && ordinal <= *previousSide) ||
        !entry.digits.has_value() || entry.digits->revision != revision ||
        entry.digits->transactionIdentity != transactionIdentity ||
        entry.digits->uiScalePercent != uiScalePercent ||
        !entry.digits->belongsTo(textures)) {
      return false;
    }
    previousSide = ordinal;
  }
  return true;
}

LegacyAircraftHudInstrumentReadoutsSubmissionResult
buildLegacyAircraftHudInstrumentReadoutsSubmission(
    const render::LegacyAircraftHudInstrumentReadoutsPlan &plan,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures,
    const render::NativeRenderLayout &layout,
    const float uiScalePercent) noexcept {
  if (!plan.ready()) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsSubmissionStatus::planNotReady);
  }
  if (!textures.valid()) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsSubmissionStatus::invalidTextureSet);
  }
  const auto designExtent = layout.uiDesignExtent();
  if (designExtent.width != render::recovered_legacy_canvas::width ||
      designExtent.height != render::recovered_legacy_canvas::height ||
      plan.sourceScreenWidth !=
          static_cast<std::uint32_t>(designExtent.width) ||
      plan.sourceScreenHeight !=
          static_cast<std::uint32_t>(designExtent.height)) {
    return failure(LegacyAircraftHudInstrumentReadoutsSubmissionStatus::
                       incompatibleUiDesignExtent);
  }
  if (!std::isfinite(uiScalePercent) ||
      uiScalePercent < render::native_render_policy::minimumUiScalePercent ||
      uiScalePercent > render::native_render_policy::maximumUiScalePercent) {
    return failure(
        LegacyAircraftHudInstrumentReadoutsSubmissionStatus::uiScaleOutOfRange);
  }
  if (plan.readoutCount > plan.orderedReadouts.size()) {
    return failure(LegacyAircraftHudInstrumentReadoutsSubmissionStatus::
                       invalidReadoutPlan);
  }

  LegacyAircraftHudInstrumentReadoutsSubmission submission{
      .revision = textures.revision,
      .transactionIdentity = textures.transactionIdentity,
      .uiScalePercent = uiScalePercent,
  };
  std::optional<std::uint8_t> previousSide;
  for (std::size_t index = 0U; index < plan.readoutCount; ++index) {
    const auto *source = plan.readout(index);
    if (source == nullptr) {
      return failure(LegacyAircraftHudInstrumentReadoutsSubmissionStatus::
                         invalidReadoutPlan);
    }
    const auto ordinal = sideOrdinal(source->side);
    if (ordinal >
            sideOrdinal(render::LegacyAircraftHudInstrumentReadoutSide::left) ||
        (previousSide.has_value() && ordinal <= *previousSide) ||
        !source->digits.ready()) {
      return failure(LegacyAircraftHudInstrumentReadoutsSubmissionStatus::
                         invalidReadoutPlan);
    }
    auto built = buildLegacyAircraftHudRollingDigitsSubmission(
        source->digits, textures, layout, uiScalePercent);
    if (!built.ready()) {
      return readoutFailure(source->side, built.status);
    }
    submission.orderedReadouts[index] = {
        .side = source->side,
        .digits = std::move(*built.submission),
    };
    previousSide = ordinal;
  }
  submission.readoutCount = plan.readoutCount;
  if (!submission.belongsTo(textures)) {
    return failure(LegacyAircraftHudInstrumentReadoutsSubmissionStatus::
                       invalidReadoutPlan);
  }
  return {
      .status = LegacyAircraftHudInstrumentReadoutsSubmissionStatus::ready,
      .submission = std::move(submission),
      .rejectedSide = std::nullopt,
      .readoutStatus = std::nullopt,
  };
}

} // namespace airfix::content
