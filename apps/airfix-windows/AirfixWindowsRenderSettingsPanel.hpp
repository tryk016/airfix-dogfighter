#pragma once

#include "airfix/input/InputFrame.hpp"
#include "airfix/settings/ControllerInputBindingPickerModel.hpp"
#include "airfix/settings/ControllerInputProfileMenuModel.hpp"
#include "airfix/settings/RenderPresentationSettingsMenuModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::windows {

inline constexpr std::int32_t airfixWindowsUiNavigationActuation =
    input::uiNavigationActuationQ15;
inline constexpr std::int32_t airfixWindowsUiNavigationRelease =
    input::uiNavigationReleaseQ15;

enum class AirfixWindowsRenderSettingsScreen : std::uint8_t {
  pause,
  displaySettings,
  controllerCalibration,
  controllerAxisCalibration,
  controllerButtonBindings,
  controllerBindingConflict,
};

enum class AirfixWindowsRenderSettingsItem : std::uint8_t {
  displaySettings,
  controllerCalibration,
  resume,
  renderScale,
  interfaceScale,
  presentation,
  verticalFovAdjustment,
  visualProfile,
  rendererStatistics,
  apply,
  cancel,
  leftStickX,
  leftStickY,
  rightStickX,
  rightStickY,
  buttonBindings,
  innerDeadzone,
  outerSaturation,
  sensitivity,
  responseCurve,
  inversion,
  resetAxis,
  resetAllCalibration,
  bindingAction,
  bindingAssignment,
  moveBinding,
  resetAllAssignments,
  swapAssignments,
  saveControllerProfile,
  back,
  count,
};

enum class AirfixWindowsRenderSettingsStatus : std::uint8_t {
  ready,
  noChanges,
  applying,
  applied,
  applyFailed,
  persistenceUnavailable,
  invalidSettings,
  controllerProfileReady,
  controllerProfileNoChanges,
  controllerProfileSaving,
  controllerProfileSaved,
  controllerProfileSavedRestartRequired,
  controllerProfileSaveFailed,
  controllerProfilePersistenceUnavailable,
  invalidControllerProfile,
  controllerBindingConflict,
  controllerBindingProtectedConflict,
  controllerBindingActionUnavailable,
};

enum class AirfixWindowsRenderSettingsSessionOverride : std::uint8_t {
  renderScale = 1U << 0U,
  presentation = 1U << 1U,
  visualProfile = 1U << 2U,
  rendererStatistics = 1U << 3U,
  verticalFovAdjustment = 1U << 4U,
};

using AirfixWindowsRenderSettingsSessionOverrideMask = std::uint8_t;

inline constexpr AirfixWindowsRenderSettingsSessionOverrideMask
    airfixWindowsRenderSettingsAllSessionOverrides = static_cast<
        AirfixWindowsRenderSettingsSessionOverrideMask>(
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::renderScale) |
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::presentation) |
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::visualProfile) |
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::rendererStatistics) |
        static_cast<std::uint8_t>(
            AirfixWindowsRenderSettingsSessionOverride::
                verticalFovAdjustment));

[[nodiscard]] constexpr AirfixWindowsRenderSettingsSessionOverrideMask
airfixWindowsRenderSettingsSessionOverrideMask(
    const render::RenderPresentationSettingsOverride &overrides) noexcept {
  AirfixWindowsRenderSettingsSessionOverrideMask mask{};
  if (overrides.renderScalePercent.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::renderScale);
  }
  if (overrides.scenePresentation.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::presentation);
  }
  if (overrides.visualProfile.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::visualProfile);
  }
  if (overrides.diagnosticsOverlayEnabled.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::rendererStatistics);
  }
  if (overrides.verticalFovAdjustmentDegrees.has_value()) {
    mask |= static_cast<std::uint8_t>(
        AirfixWindowsRenderSettingsSessionOverride::
            verticalFovAdjustment);
  }
  return mask;
}

struct AirfixWindowsUiPixelExtent final {
  std::uint32_t width{1920U};
  std::uint32_t height{1080U};
  float dpiScale{1.0F};

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsUiPixelExtent &,
             const AirfixWindowsUiPixelExtent &) noexcept = default;
};

struct AirfixWindowsUiPixelRect final {
  float x{};
  float y{};
  float width{};
  float height{};

  [[nodiscard]] constexpr bool contains(const float pointX,
                                        const float pointY) const noexcept {
    return pointX >= x && pointY >= y && pointX < x + width &&
           pointY < y + height;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsUiPixelRect &,
             const AirfixWindowsUiPixelRect &) noexcept = default;
};

struct AirfixWindowsRenderSettingsViewItem final {
  AirfixWindowsRenderSettingsItem item{
      AirfixWindowsRenderSettingsItem::displaySettings};
  AirfixWindowsUiPixelRect bounds;
  AirfixWindowsUiPixelRect previousBounds;
  AirfixWindowsUiPixelRect nextBounds;
  bool selected{};
  bool enabled{true};

  [[nodiscard]] friend constexpr bool
  operator==(const AirfixWindowsRenderSettingsViewItem &,
             const AirfixWindowsRenderSettingsViewItem &) noexcept = default;
};

inline constexpr std::size_t airfixWindowsRenderSettingsMaximumViewItems = 8U;
inline constexpr std::uint8_t airfixWindowsControllerBindingNoControlIndex =
    static_cast<std::uint8_t>(input::controllerAssignableControlCount);

struct AirfixWindowsControllerProfilePanelState final {
  input::ControllerInputProfileRecord active;
  input::ControllerInputProfileRecord persisted;
  settings::ControllerInputProfileMenuCapabilities capabilities;
};

struct AirfixWindowsControllerAxisInputSnapshot final {
  std::array<input::Q15, input::controllerProfileAxisCount> rawAxes{};
  bool connected{};

  [[nodiscard]] friend constexpr bool operator==(
      const AirfixWindowsControllerAxisInputSnapshot &,
      const AirfixWindowsControllerAxisInputSnapshot &) noexcept = default;
};

// A storage-neutral, bounded view description. It intentionally contains no
// paths, persistence records, checksums, texture identities, or arbitrary
// caller strings.
struct AirfixWindowsRenderSettingsViewSnapshot final {
  AirfixWindowsRenderSettingsScreen screen{
      AirfixWindowsRenderSettingsScreen::pause};
  AirfixWindowsRenderSettingsItem selectedItem{
      AirfixWindowsRenderSettingsItem::displaySettings};
  AirfixWindowsRenderSettingsStatus status{
      AirfixWindowsRenderSettingsStatus::ready};
  render::RenderPresentationSettings appliedSettings;
  render::RenderPresentationSettings draftSettings;
  AirfixWindowsUiPixelExtent output;
  float layoutScale{1.0F};
  AirfixWindowsUiPixelRect panelBounds;
  AirfixWindowsUiPixelRect titleBounds;
  AirfixWindowsUiPixelRect statusBounds;
  std::array<AirfixWindowsRenderSettingsViewItem,
             airfixWindowsRenderSettingsMaximumViewItems>
      items{};
  std::uint8_t itemCount{};
  AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask{};
  std::array<input::ControllerAxisCalibrationRecord,
             input::controllerProfileAxisCount>
      controllerDraftAxes{};
  input::ControllerAxisElement selectedControllerAxis{
      input::ControllerAxisElement::leftStickX};
  input::ControllerDigitalGameplayAction selectedControllerBindingAction{
      input::ControllerDigitalGameplayAction::primaryFire};
  input::ControllerDigitalGameplayBindingStatus selectedControllerBindingStatus{
      input::ControllerDigitalGameplayBindingStatus::editable};
  std::uint8_t selectedControllerBindingControlIndex{
      airfixWindowsControllerBindingNoControlIndex};
  settings::ControllerInputBindingPickerPhase controllerBindingPickerPhase{
      settings::ControllerInputBindingPickerPhase::closed};
  std::optional<input::ControllerDigitalGameplayAction>
      conflictingControllerBindingAction;
  input::Q15 controllerPreviewRaw{};
  input::Q15 controllerPreviewEffective{};
  bool dirty{};
  bool applying{};
  bool persistenceAvailable{true};
  bool controllerProfileAvailable{};
  bool controllerProfileDirty{};
  bool controllerProfileSaving{};
  bool controllerProfilePersistenceAvailable{};
  bool controllerProfileRepairRequired{};
  bool controllerProfileRestartRequired{};
  bool controllerConnected{};

  [[nodiscard]] friend constexpr bool operator==(
      const AirfixWindowsRenderSettingsViewSnapshot &,
      const AirfixWindowsRenderSettingsViewSnapshot &) noexcept = default;
};

struct AirfixWindowsPointerInput final {
  float xPixels{};
  float yPixels{};
  std::int32_t wheelY{};
  bool primaryPressed{};
};

struct AirfixWindowsRenderSettingsIntent final {
  std::optional<settings::RenderPresentationSettingsMenuApplyTicket>
      applyTicket;
  std::optional<settings::ControllerInputProfileMenuSaveTicket>
      controllerProfileSaveTicket;
  bool resumeRequested{};

  [[nodiscard]] constexpr bool empty() const noexcept {
    return !applyTicket.has_value() &&
           !controllerProfileSaveTicket.has_value() && !resumeRequested;
  }
};

// Owner-thread Windows presentation-panel state. The class performs no
// renderer, filesystem, registry, or persistence work. Apply returns the
// shared model's immutable ticket to the product coordinator.
class AirfixWindowsRenderSettingsPanel final {
public:
  [[nodiscard]] static std::optional<AirfixWindowsRenderSettingsPanel> create(
      const render::RenderPresentationSettings &applied,
      bool persistenceAvailable = true, AirfixWindowsUiPixelExtent output = {},
      AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask = 0U,
      bool resumeAvailable = true,
      std::optional<AirfixWindowsControllerProfilePanelState>
          controllerProfile = std::nullopt) noexcept;

  AirfixWindowsRenderSettingsPanel(const AirfixWindowsRenderSettingsPanel &) =
      default;
  AirfixWindowsRenderSettingsPanel(
      AirfixWindowsRenderSettingsPanel &&) noexcept = default;
  AirfixWindowsRenderSettingsPanel &
  operator=(const AirfixWindowsRenderSettingsPanel &) = default;
  AirfixWindowsRenderSettingsPanel &
  operator=(AirfixWindowsRenderSettingsPanel &&) noexcept = default;
  ~AirfixWindowsRenderSettingsPanel() = default;

  [[nodiscard]] AirfixWindowsRenderSettingsIntent
  consumeInputFrame(const input::InputFrame &frame) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsIntent
  consumePointer(const AirfixWindowsPointerInput &pointer) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsViewSnapshot
  snapshot() const noexcept;

  void setOutput(AirfixWindowsUiPixelExtent output) noexcept;
  void setPersistenceAvailable(bool available) noexcept;
  void setSessionOverrideMask(
      AirfixWindowsRenderSettingsSessionOverrideMask mask) noexcept;
  void setResumeAvailable(bool available) noexcept;
  void setControllerProfilePersistenceAvailable(bool available) noexcept;
  void setControllerAxisInput(
      const AirfixWindowsControllerAxisInputSnapshot &input) noexcept;

  [[nodiscard]] bool
  finishApplySuccess(const settings::RenderPresentationSettingsMenuApplyTicket
                         &ticket) noexcept;
  [[nodiscard]] bool
  finishApplyFailure(const settings::RenderPresentationSettingsMenuApplyTicket
                         &ticket) noexcept;
  [[nodiscard]] bool finishControllerProfileSaveSuccess(
      const settings::ControllerInputProfileMenuSaveTicket &ticket) noexcept;
  [[nodiscard]] bool finishControllerProfileSaveFailure(
      const settings::ControllerInputProfileMenuSaveTicket &ticket) noexcept;

  [[nodiscard]] AirfixWindowsRenderSettingsScreen screen() const noexcept {
    return screen_;
  }

private:
  AirfixWindowsRenderSettingsPanel(
      settings::RenderPresentationSettingsMenuModel model,
      std::optional<settings::ControllerInputProfileMenuModel>
          controllerProfileModel,
      AirfixWindowsUiPixelExtent output,
      AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask,
      bool resumeAvailable) noexcept;

  void moveSelection(std::int32_t direction) noexcept;
  void adjustSelectedValue(std::int32_t direction) noexcept;
  [[nodiscard]] AirfixWindowsRenderSettingsIntent
  activateSelectedItem() noexcept;
  void closeDisplaySettings() noexcept;
  void closeControllerCalibration() noexcept;
  void openSelectedControllerAxis() noexcept;
  void openControllerButtonBindings() noexcept;
  void closeControllerButtonBindings() noexcept;
  void selectControllerBindingAction(std::int32_t direction) noexcept;
  void refreshControllerBindingPicker() noexcept;
  void applyControllerBindingSelection() noexcept;
  void confirmControllerBindingSwap() noexcept;
  void cancelControllerBindingSwap() noexcept;
  void setControllerBindingPickerStatus(
      const settings::ControllerInputBindingPickerResult &result) noexcept;
  [[nodiscard]] AirfixWindowsRenderSettingsItem &
  selectionForCurrentScreen() noexcept;
  [[nodiscard]] const AirfixWindowsRenderSettingsItem &
  selectionForCurrentScreen() const noexcept;

  settings::RenderPresentationSettingsMenuModel model_;
  std::optional<settings::ControllerInputProfileMenuModel>
      controllerProfileModel_;
  settings::ControllerInputBindingPickerModel controllerBindingPicker_;
  AirfixWindowsUiPixelExtent output_;
  AirfixWindowsRenderSettingsSessionOverrideMask sessionOverrideMask_{};
  std::optional<settings::RenderPresentationSettingsMenuApplyTicket>
      activeTicket_;
  std::optional<settings::ControllerInputProfileMenuSaveTicket>
      activeControllerProfileTicket_;
  AirfixWindowsControllerAxisInputSnapshot controllerAxisInput_;
  AirfixWindowsRenderSettingsScreen screen_{
      AirfixWindowsRenderSettingsScreen::pause};
  AirfixWindowsRenderSettingsItem selectedPauseItem_{
      AirfixWindowsRenderSettingsItem::displaySettings};
  AirfixWindowsRenderSettingsItem selectedSettingsItem_{
      AirfixWindowsRenderSettingsItem::renderScale};
  AirfixWindowsRenderSettingsItem selectedControllerProfileItem_{
      AirfixWindowsRenderSettingsItem::leftStickX};
  AirfixWindowsRenderSettingsItem selectedControllerAxisItem_{
      AirfixWindowsRenderSettingsItem::innerDeadzone};
  AirfixWindowsRenderSettingsItem selectedControllerBindingItem_{
      AirfixWindowsRenderSettingsItem::bindingAction};
  AirfixWindowsRenderSettingsItem selectedControllerBindingConflictItem_{
      AirfixWindowsRenderSettingsItem::cancel};
  input::ControllerAxisElement selectedControllerAxis_{
      input::ControllerAxisElement::leftStickX};
  input::ControllerDigitalGameplayAction selectedControllerBindingAction_{
      input::ControllerDigitalGameplayAction::primaryFire};
  AirfixWindowsRenderSettingsStatus status_{
      AirfixWindowsRenderSettingsStatus::ready};
  bool verticalNavigationLatched_{};
  bool horizontalNavigationLatched_{};
  bool resumeAvailable_{true};
};

} // namespace airfix::windows
