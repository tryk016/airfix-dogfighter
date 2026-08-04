#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"

#include <cstdint>
#include <optional>

namespace airfix::settings {

struct RenderPresentationSettingsMenuCapabilities final {
  bool persistenceAvailable{true};
  // This exposes only a coarse product capability. The model never receives
  // a private root, manifest, checksum, or asset identity.
  bool enhancedTexturesAvailable{};

  [[nodiscard]] friend constexpr bool operator==(
      const RenderPresentationSettingsMenuCapabilities &,
      const RenderPresentationSettingsMenuCapabilities &) noexcept = default;
};

enum class RenderPresentationSettingsMenuPhase : std::uint8_t {
  idle,
  applying,
};

enum class RenderPresentationSettingsMenuEditStatus : std::uint8_t {
  accepted,
  applyInProgress,
  enhancedTexturesUnavailable,
  invalidSettings,
};

struct RenderPresentationSettingsMenuEditResult final {
  RenderPresentationSettingsMenuEditStatus status{
      RenderPresentationSettingsMenuEditStatus::accepted};
  std::optional<render::RenderPresentationSettingsIssue> issue;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == RenderPresentationSettingsMenuEditStatus::accepted &&
           !issue.has_value();
  }
};

struct RenderPresentationSettingsMenuApplyTicket final {
  std::uint64_t serial{};
  render::RenderPresentationSettings candidate;
  render::RenderPresentationSettingsDelta delta;

  [[nodiscard]] friend constexpr bool operator==(
      const RenderPresentationSettingsMenuApplyTicket &,
      const RenderPresentationSettingsMenuApplyTicket &) noexcept = default;
};

// Allocation-free owner-thread state for a presentation-settings menu.
//
// The model deliberately knows nothing about UIKit, persistence formats,
// renderer resources, or private texture content. VisualProfile::enhanced is
// the existing presentation-policy preview selector; it is not an HD-texture
// availability signal. TextureMode editing is gated only by the coarse
// enhancedTexturesAvailable capability; products remain responsible for the
// atomic mission reload represented by the resulting delta.
class RenderPresentationSettingsMenuModel final {
public:
  // Invalid applied settings fail closed and do not create a model.
  // initialSerial exists so serial exhaustion can be tested without wrap.
  [[nodiscard]] static std::optional<RenderPresentationSettingsMenuModel>
  create(const render::RenderPresentationSettings &applied,
         RenderPresentationSettingsMenuCapabilities capabilities = {},
         std::uint64_t initialSerial = 0U) noexcept;

  RenderPresentationSettingsMenuModel(
      const RenderPresentationSettingsMenuModel &) = default;
  RenderPresentationSettingsMenuModel(
      RenderPresentationSettingsMenuModel &&) noexcept = default;
  RenderPresentationSettingsMenuModel &
  operator=(const RenderPresentationSettingsMenuModel &) = default;
  RenderPresentationSettingsMenuModel &
  operator=(RenderPresentationSettingsMenuModel &&) noexcept = default;
  ~RenderPresentationSettingsMenuModel() = default;

  [[nodiscard]] const render::RenderPresentationSettings &
  appliedSettings() const noexcept {
    return applied_;
  }

  [[nodiscard]] const render::RenderPresentationSettings &
  draftSettings() const noexcept {
    return draft_;
  }

  [[nodiscard]] const render::RenderPresentationSettingsDelta &
  delta() const noexcept {
    return delta_;
  }

  [[nodiscard]] RenderPresentationSettingsMenuPhase phase() const noexcept {
    return current_.has_value() ? RenderPresentationSettingsMenuPhase::applying
                                : RenderPresentationSettingsMenuPhase::idle;
  }

  [[nodiscard]] bool dirty() const noexcept { return delta_.anyChanged(); }

  [[nodiscard]] bool canApply() const noexcept {
    return phase() == RenderPresentationSettingsMenuPhase::idle &&
           capabilities_.persistenceAvailable && dirty() && !exhausted_;
  }

  [[nodiscard]] bool canCancel() const noexcept {
    return phase() == RenderPresentationSettingsMenuPhase::idle;
  }

  [[nodiscard]] bool persistenceAvailable() const noexcept {
    return capabilities_.persistenceAvailable;
  }

  [[nodiscard]] bool enhancedTexturesAvailable() const noexcept {
    return capabilities_.enhancedTexturesAvailable;
  }

  [[nodiscard]] bool exhausted() const noexcept { return exhausted_; }

  void setPersistenceAvailable(bool available) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  setRenderScalePercent(float value) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  setScenePresentation(render::ScenePresentationMode value) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  setVisualProfile(render::VisualProfile value) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  setTextureMode(texture::TextureMode value) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  setDiagnosticsOverlayEnabled(bool value) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  setVerticalFovAdjustmentDegrees(float value) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  setUiScalePercent(float value) noexcept;

  // Reverts the draft exactly to applied while idle. An in-flight Apply is
  // immutable and cannot be cancelled by this presentation model.
  [[nodiscard]] bool cancelDraft() noexcept;

  // Captures the exact candidate and delta. No ticket is issued for a clean
  // draft, unavailable persistence, concurrent Apply, or exhausted serial.
  [[nodiscard]] std::optional<RenderPresentationSettingsMenuApplyTicket>
  beginApply() noexcept;

  // Only the complete exact ticket can finish the active Apply. Success
  // promotes its candidate to applied; failure preserves the dirty draft.
  // Stale, forged, or duplicate callbacks are ignored.
  [[nodiscard]] bool finishApplySuccess(
      const RenderPresentationSettingsMenuApplyTicket &ticket) noexcept;

  [[nodiscard]] bool finishApplyFailure(
      const RenderPresentationSettingsMenuApplyTicket &ticket) noexcept;

private:
  RenderPresentationSettingsMenuModel(
      const render::RenderPresentationSettings &applied,
      RenderPresentationSettingsMenuCapabilities capabilities,
      std::uint64_t initialSerial) noexcept;

  [[nodiscard]] RenderPresentationSettingsMenuEditResult
  edit(const render::RenderPresentationSettingsOverride &overrides) noexcept;

  render::RenderPresentationSettings applied_;
  render::RenderPresentationSettings draft_;
  render::RenderPresentationSettingsDelta delta_;
  RenderPresentationSettingsMenuCapabilities capabilities_;
  std::optional<RenderPresentationSettingsMenuApplyTicket> current_;
  std::uint64_t serial_{};
  bool exhausted_{};
};

} // namespace airfix::settings
