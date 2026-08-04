#include "airfix/settings/RenderPresentationSettingsMenuModel.hpp"

#include <limits>

namespace airfix::settings {
namespace {

[[nodiscard]] constexpr RenderPresentationSettingsMenuEditResult
editResult(const RenderPresentationSettingsMenuEditStatus status,
           const std::optional<render::RenderPresentationSettingsIssue> &issue =
               std::nullopt) noexcept {
  return {
      .status = status,
      .issue = issue,
  };
}

} // namespace

std::optional<RenderPresentationSettingsMenuModel>
RenderPresentationSettingsMenuModel::create(
    const render::RenderPresentationSettings &applied,
    const RenderPresentationSettingsMenuCapabilities capabilities,
    const std::uint64_t initialSerial) noexcept {
  if (render::validateRenderPresentationSettings(applied).has_value()) {
    return std::nullopt;
  }
  return RenderPresentationSettingsMenuModel{
      applied,
      capabilities,
      initialSerial,
  };
}

RenderPresentationSettingsMenuModel::RenderPresentationSettingsMenuModel(
    const render::RenderPresentationSettings &applied,
    const RenderPresentationSettingsMenuCapabilities capabilities,
    const std::uint64_t initialSerial) noexcept
    : applied_(applied), draft_(applied), capabilities_(capabilities),
      serial_(initialSerial),
      exhausted_(initialSerial == std::numeric_limits<std::uint64_t>::max()) {}

void RenderPresentationSettingsMenuModel::setPersistenceAvailable(
    const bool available) noexcept {
  capabilities_.persistenceAvailable = available;
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::setRenderScalePercent(
    const float value) noexcept {
  render::RenderPresentationSettingsOverride overrides;
  overrides.renderScalePercent = value;
  return edit(overrides);
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::setScenePresentation(
    const render::ScenePresentationMode value) noexcept {
  render::RenderPresentationSettingsOverride overrides;
  overrides.scenePresentation = value;
  return edit(overrides);
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::setVisualProfile(
    const render::VisualProfile value) noexcept {
  render::RenderPresentationSettingsOverride overrides;
  overrides.visualProfile = value;
  return edit(overrides);
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::setTextureMode(
    const texture::TextureMode value) noexcept {
  if (phase() == RenderPresentationSettingsMenuPhase::applying) {
    return editResult(
        RenderPresentationSettingsMenuEditStatus::applyInProgress);
  }
  if (value == texture::TextureMode::enhanced &&
      value != draft_.textureMode &&
      !capabilities_.enhancedTexturesAvailable) {
    return editResult(
        RenderPresentationSettingsMenuEditStatus::enhancedTexturesUnavailable);
  }
  render::RenderPresentationSettingsOverride overrides;
  overrides.textureMode = value;
  return edit(overrides);
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::setDiagnosticsOverlayEnabled(
    const bool value) noexcept {
  render::RenderPresentationSettingsOverride overrides;
  overrides.diagnosticsOverlayEnabled = value;
  return edit(overrides);
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::setVerticalFovAdjustmentDegrees(
    const float value) noexcept {
  render::RenderPresentationSettingsOverride overrides;
  overrides.verticalFovAdjustmentDegrees = value;
  return edit(overrides);
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::setUiScalePercent(
    const float value) noexcept {
  render::RenderPresentationSettingsOverride overrides;
  overrides.uiScalePercent = value;
  return edit(overrides);
}

bool RenderPresentationSettingsMenuModel::cancelDraft() noexcept {
  if (!canCancel()) {
    return false;
  }
  draft_ = applied_;
  delta_ = {};
  return true;
}

std::optional<RenderPresentationSettingsMenuApplyTicket>
RenderPresentationSettingsMenuModel::beginApply() noexcept {
  if (!canApply()) {
    return std::nullopt;
  }

  ++serial_;
  if (serial_ == std::numeric_limits<std::uint64_t>::max()) {
    exhausted_ = true;
  }
  current_ = RenderPresentationSettingsMenuApplyTicket{
      .serial = serial_,
      .candidate = draft_,
      .delta = delta_,
  };
  return current_;
}

bool RenderPresentationSettingsMenuModel::finishApplySuccess(
    const RenderPresentationSettingsMenuApplyTicket &ticket) noexcept {
  if (!current_.has_value() || *current_ != ticket) {
    return false;
  }
  applied_ = ticket.candidate;
  draft_ = ticket.candidate;
  delta_ = {};
  current_.reset();
  return true;
}

bool RenderPresentationSettingsMenuModel::finishApplyFailure(
    const RenderPresentationSettingsMenuApplyTicket &ticket) noexcept {
  if (!current_.has_value() || *current_ != ticket) {
    return false;
  }
  current_.reset();
  return true;
}

RenderPresentationSettingsMenuEditResult
RenderPresentationSettingsMenuModel::edit(
    const render::RenderPresentationSettingsOverride &overrides) noexcept {
  if (phase() == RenderPresentationSettingsMenuPhase::applying) {
    return editResult(
        RenderPresentationSettingsMenuEditStatus::applyInProgress);
  }

  const auto resolved =
      render::resolveRenderPresentationSettings(draft_, overrides);
  if (!resolved.accepted()) {
    return editResult(RenderPresentationSettingsMenuEditStatus::invalidSettings,
                      resolved.issue);
  }

  const auto candidateDelta =
      render::diffRenderPresentationSettings(applied_, resolved.settings);
  if (!candidateDelta.complete()) {
    return editResult(RenderPresentationSettingsMenuEditStatus::invalidSettings,
                      candidateDelta.issue);
  }

  draft_ = resolved.settings;
  delta_ = *candidateDelta.delta;
  return editResult(RenderPresentationSettingsMenuEditStatus::accepted);
}

} // namespace airfix::settings
