#include "AirfixWindowsUiSemantics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace airfix::windows {
namespace {

[[nodiscard]] AirfixWindowsUiSemanticText
text(const std::wstring_view source) noexcept {
  AirfixWindowsUiSemanticText result;
  if (source.size() >= result.codeUnits.size()) {
    return result;
  }
  std::copy(source.begin(), source.end(), result.codeUnits.begin());
  result.length = static_cast<std::uint8_t>(source.size());
  return result;
}

[[nodiscard]] AirfixWindowsUiSemanticText
prefixedText(const std::wstring_view prefix,
             const AirfixWindowsUiSemanticText &suffix) noexcept {
  AirfixWindowsUiSemanticText result;
  const auto size = prefix.size() + suffix.view().size();
  if (size >= result.codeUnits.size()) {
    return result;
  }
  auto output =
      std::copy(prefix.begin(), prefix.end(), result.codeUnits.begin());
  std::copy(suffix.view().begin(), suffix.view().end(), output);
  result.length = static_cast<std::uint8_t>(size);
  return result;
}

template <typename... Args>
[[nodiscard]] AirfixWindowsUiSemanticText formattedText(const wchar_t *format,
                                                        Args... args) noexcept {
  AirfixWindowsUiSemanticText result;
  const int written = swprintf_s(result.codeUnits.data(),
                                 result.codeUnits.size(), format, args...);
  if (written < 0 ||
      static_cast<std::size_t>(written) >= result.codeUnits.size()) {
    return {};
  }
  result.length = static_cast<std::uint8_t>(written);
  return result;
}

[[nodiscard]] AirfixWindowsUiSemanticText controllerActionLabel(
    const input::ControllerDigitalGameplayAction action) noexcept {
  switch (action) {
  case input::ControllerDigitalGameplayAction::primaryFire:
    return text(L"Primary fire");
  case input::ControllerDigitalGameplayAction::secondaryFire:
    return text(L"Secondary fire");
  case input::ControllerDigitalGameplayAction::weaponNext:
    return text(L"Next weapon");
  case input::ControllerDigitalGameplayAction::rearView:
    return text(L"Rear view");
  case input::ControllerDigitalGameplayAction::cameraCycle:
    return text(L"Cycle camera");
  case input::ControllerDigitalGameplayAction::cameraRecenter:
    return text(L"Recenter camera");
  case input::ControllerDigitalGameplayAction::missionStatus:
    return text(L"Mission status");
  case input::ControllerDigitalGameplayAction::count:
    break;
  }
  return text(L"Unavailable");
}

[[nodiscard]] AirfixWindowsUiSemanticText
controllerControlLabel(const std::uint8_t index) noexcept {
  constexpr std::array<std::wstring_view, 14U> labels{
      L"Right trigger",    L"Left trigger",        L"Right shoulder",
      L"Left shoulder",    L"Primary face button", L"Secondary face button",
      L"Left face button", L"Top face button",     L"Right stick click",
      L"D-pad up",         L"D-pad down",          L"D-pad left",
      L"D-pad right",      L"Menu button",
  };
  return text(index < labels.size() ? labels[index] : L"Unavailable");
}

[[nodiscard]] bool
validScreen(const AirfixWindowsRenderSettingsScreen screen) noexcept {
  switch (screen) {
  case AirfixWindowsRenderSettingsScreen::pause:
  case AirfixWindowsRenderSettingsScreen::displaySettings:
  case AirfixWindowsRenderSettingsScreen::controllerCalibration:
  case AirfixWindowsRenderSettingsScreen::controllerAxisCalibration:
  case AirfixWindowsRenderSettingsScreen::controllerButtonBindings:
  case AirfixWindowsRenderSettingsScreen::controllerBindingConflict:
    return true;
  }
  return false;
}

[[nodiscard]] bool finiteRect(const AirfixWindowsUiPixelRect rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height) &&
         rect.width >= 0.0F && rect.height >= 0.0F;
}

[[nodiscard]] bool
rectInsideOutput(const AirfixWindowsUiPixelRect rect,
                 const AirfixWindowsUiPixelExtent output) noexcept {
  constexpr float tolerance = 0.25F;
  return finiteRect(rect) && rect.x >= -tolerance && rect.y >= -tolerance &&
         rect.x + rect.width <= static_cast<float>(output.width) + tolerance &&
         rect.y + rect.height <= static_cast<float>(output.height) + tolerance;
}

[[nodiscard]] constexpr bool
emptyRect(const AirfixWindowsUiPixelRect rect) noexcept {
  return rect == AirfixWindowsUiPixelRect{};
}

[[nodiscard]] constexpr bool
positiveRect(const AirfixWindowsUiPixelRect rect) noexcept {
  return rect.width > 0.0F && rect.height > 0.0F;
}

[[nodiscard]] constexpr bool
controllerScreen(const AirfixWindowsRenderSettingsScreen screen) noexcept {
  return screen == AirfixWindowsRenderSettingsScreen::controllerCalibration ||
         screen ==
             AirfixWindowsRenderSettingsScreen::controllerAxisCalibration ||
         screen ==
             AirfixWindowsRenderSettingsScreen::controllerButtonBindings ||
         screen == AirfixWindowsRenderSettingsScreen::controllerBindingConflict;
}

[[nodiscard]] bool validSnapshot(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  if (snapshot.accessibilityGeneration == 0U || !validScreen(snapshot.screen) ||
      snapshot.output.width == 0U || snapshot.output.height == 0U ||
      !std::isfinite(snapshot.output.dpiScale) ||
      snapshot.output.dpiScale <= 0.0F ||
      !std::isfinite(snapshot.layoutScale) || snapshot.layoutScale <= 0.0F ||
      snapshot.selectedItem >= AirfixWindowsRenderSettingsItem::count ||
      snapshot.status > AirfixWindowsRenderSettingsStatus::
                            controllerBindingActionUnavailable ||
      render::validateRenderPresentationSettings(snapshot.appliedSettings)
          .has_value() ||
      render::validateRenderPresentationSettings(snapshot.draftSettings)
          .has_value() ||
      (snapshot.sessionOverrideMask &
       static_cast<AirfixWindowsRenderSettingsSessionOverrideMask>(
           ~airfixWindowsRenderSettingsAllSessionOverrides)) != 0U ||
      static_cast<std::size_t>(snapshot.itemCount) > snapshot.items.size() ||
      static_cast<std::size_t>(snapshot.logicalItemCount) >
          snapshot.logicalItems.size() ||
      snapshot.itemCount == 0U || snapshot.logicalItemCount == 0U ||
      snapshot.itemCount > snapshot.logicalItemCount ||
      !rectInsideOutput(snapshot.panelBounds, snapshot.output) ||
      !rectInsideOutput(snapshot.titleBounds, snapshot.output) ||
      !rectInsideOutput(snapshot.statusBounds, snapshot.output) ||
      !positiveRect(snapshot.panelBounds) ||
      !positiveRect(snapshot.titleBounds) ||
      !positiveRect(snapshot.statusBounds) ||
      airfixWindowsUiTitle(snapshot).empty() ||
      airfixWindowsUiStatus(snapshot).empty()) {
    return false;
  }

  const auto expectedItems = airfixWindowsRenderSettingsItemsForScreen(
      snapshot.screen, snapshot.controllerProfileAvailable);
  if (expectedItems.empty() ||
      expectedItems.size() != snapshot.logicalItemCount ||
      (controllerScreen(snapshot.screen) &&
       !snapshot.controllerProfileAvailable)) {
    return false;
  }

  const auto axisIndex =
      static_cast<std::size_t>(snapshot.selectedControllerAxis);
  const auto bindingActionIndex =
      static_cast<std::size_t>(snapshot.selectedControllerBindingAction);
  if (axisIndex >= snapshot.controllerDraftAxes.size() ||
      bindingActionIndex >= input::controllerDigitalGameplayActionCount ||
      snapshot.selectedControllerBindingStatus >
          input::ControllerDigitalGameplayBindingStatus::unsupportedLayout ||
      snapshot.selectedControllerBindingControlIndex >
          airfixWindowsControllerBindingNoControlIndex ||
      snapshot.controllerBindingPickerPhase >
          settings::ControllerInputBindingPickerPhase::confirmingSwap ||
      snapshot.controllerPreviewRaw < -input::q15One ||
      snapshot.controllerPreviewEffective < -input::q15One ||
      (snapshot.controllerProfileSaving &&
       !snapshot.controllerProfileAvailable)) {
    return false;
  }
  if (snapshot.conflictingControllerBindingAction.has_value() &&
      static_cast<std::size_t>(*snapshot.conflictingControllerBindingAction) >=
          input::controllerDigitalGameplayActionCount) {
    return false;
  }
  for (const auto &axis : snapshot.controllerDraftAxes) {
    if (axis.innerDeadzoneQ15 >= axis.outerSaturationQ15 ||
        axis.outerSaturationQ15 > static_cast<std::uint16_t>(input::q15One) ||
        axis.sensitivityPermille <
            input::controllerAxisMinimumSensitivityPermille ||
        axis.sensitivityPermille >
            input::controllerAxisMaximumSensitivityPermille ||
        axis.responseCurve >= input::ControllerResponseCurve::count ||
        axis.inverted > 1U) {
      return false;
    }
  }

  if (snapshot.screen ==
      AirfixWindowsRenderSettingsScreen::controllerButtonBindings) {
    const bool choosing =
        snapshot.controllerBindingPickerPhase ==
        settings::ControllerInputBindingPickerPhase::choosingControl;
    const bool closed = snapshot.controllerBindingPickerPhase ==
                        settings::ControllerInputBindingPickerPhase::closed;
    if ((choosing &&
         (snapshot.selectedControllerBindingStatus !=
              input::ControllerDigitalGameplayBindingStatus::editable ||
          snapshot.selectedControllerBindingControlIndex >=
              input::controllerAssignableControlCount)) ||
        (closed && snapshot.selectedControllerBindingControlIndex !=
                       airfixWindowsControllerBindingNoControlIndex) ||
        (!choosing && !closed) ||
        snapshot.conflictingControllerBindingAction.has_value()) {
      return false;
    }
  }
  if (snapshot.screen ==
          AirfixWindowsRenderSettingsScreen::controllerBindingConflict &&
      (snapshot.controllerBindingPickerPhase !=
           settings::ControllerInputBindingPickerPhase::confirmingSwap ||
       snapshot.selectedControllerBindingControlIndex >=
           input::controllerAssignableControlCount ||
       !snapshot.conflictingControllerBindingAction.has_value())) {
    return false;
  }

  std::array<bool,
             static_cast<std::size_t>(AirfixWindowsRenderSettingsItem::count)>
      seen{};
  std::uint8_t visibleCount{};
  std::uint8_t selectedCount{};
  for (std::uint8_t index = 0U; index < snapshot.logicalItemCount; ++index) {
    const auto &item = snapshot.logicalItems[index];
    const auto ordinal = static_cast<std::size_t>(item.item);
    if (item.item >= AirfixWindowsRenderSettingsItem::count || seen[ordinal] ||
        item.item != expectedItems[index] || item.visible == item.offscreen ||
        !finiteRect(item.bounds) || !finiteRect(item.previousBounds) ||
        !finiteRect(item.nextBounds) ||
        airfixWindowsUiItemLabel(item.item).empty()) {
      return false;
    }
    seen[ordinal] = true;
    if (item.visible) {
      if (visibleCount >= snapshot.itemCount ||
          item != snapshot.items[visibleCount] ||
          !rectInsideOutput(item.bounds, snapshot.output) ||
          !rectInsideOutput(item.previousBounds, snapshot.output) ||
          !rectInsideOutput(item.nextBounds, snapshot.output) ||
          !positiveRect(item.bounds) ||
          (airfixWindowsRenderSettingsItemIsAdjustable(item.item) &&
           (item.previousBounds.width <= 0.0F ||
            item.previousBounds.height <= 0.0F ||
            item.nextBounds.width <= 0.0F || item.nextBounds.height <= 0.0F))) {
        return false;
      }
      ++visibleCount;
    } else if (!emptyRect(item.bounds) || !emptyRect(item.previousBounds) ||
               !emptyRect(item.nextBounds)) {
      return false;
    }
    selectedCount += item.selected ? 1U : 0U;
    if (item.selected && item.item != snapshot.selectedItem) {
      return false;
    }
  }
  if (visibleCount != snapshot.itemCount || selectedCount != 1U) {
    return false;
  }
  return true;
}

[[nodiscard]] constexpr std::uint16_t
rowRuntimeId(const AirfixWindowsRenderSettingsScreen screen,
             const AirfixWindowsRenderSettingsItem item) noexcept {
  return static_cast<std::uint16_t>(100U +
                                    static_cast<std::uint16_t>(screen) * 100U +
                                    static_cast<std::uint16_t>(item) * 3U);
}

} // namespace

AirfixWindowsUiSemanticText
airfixWindowsUiItemLabel(const AirfixWindowsRenderSettingsItem item) noexcept {
  switch (item) {
  case AirfixWindowsRenderSettingsItem::displaySettings:
    return text(L"Display settings");
  case AirfixWindowsRenderSettingsItem::controllerCalibration:
    return text(L"Controller settings");
  case AirfixWindowsRenderSettingsItem::resume:
    return text(L"Resume");
  case AirfixWindowsRenderSettingsItem::renderScale:
    return text(L"Render scale");
  case AirfixWindowsRenderSettingsItem::interfaceScale:
    return text(L"Interface scale");
  case AirfixWindowsRenderSettingsItem::presentation:
    return text(L"Presentation");
  case AirfixWindowsRenderSettingsItem::verticalFovAdjustment:
    return text(L"Vertical FOV increase");
  case AirfixWindowsRenderSettingsItem::visualProfile:
    return text(L"Visual profile");
  case AirfixWindowsRenderSettingsItem::rendererStatistics:
    return text(L"Renderer statistics");
  case AirfixWindowsRenderSettingsItem::apply:
    return text(L"Apply");
  case AirfixWindowsRenderSettingsItem::cancel:
    return text(L"Cancel");
  case AirfixWindowsRenderSettingsItem::leftStickX:
    return text(L"Left stick X");
  case AirfixWindowsRenderSettingsItem::leftStickY:
    return text(L"Left stick Y");
  case AirfixWindowsRenderSettingsItem::rightStickX:
    return text(L"Right stick X");
  case AirfixWindowsRenderSettingsItem::rightStickY:
    return text(L"Right stick Y");
  case AirfixWindowsRenderSettingsItem::buttonBindings:
    return text(L"Button bindings");
  case AirfixWindowsRenderSettingsItem::innerDeadzone:
    return text(L"Inner deadzone");
  case AirfixWindowsRenderSettingsItem::outerSaturation:
    return text(L"Outer saturation");
  case AirfixWindowsRenderSettingsItem::sensitivity:
    return text(L"Sensitivity");
  case AirfixWindowsRenderSettingsItem::responseCurve:
    return text(L"Response curve");
  case AirfixWindowsRenderSettingsItem::inversion:
    return text(L"Invert axis");
  case AirfixWindowsRenderSettingsItem::resetAxis:
    return text(L"Reset selected axis");
  case AirfixWindowsRenderSettingsItem::resetAllCalibration:
    return text(L"Reset all calibration");
  case AirfixWindowsRenderSettingsItem::bindingAction:
    return text(L"Action");
  case AirfixWindowsRenderSettingsItem::bindingAssignment:
    return text(L"Assignment");
  case AirfixWindowsRenderSettingsItem::moveBinding:
    return text(L"Move");
  case AirfixWindowsRenderSettingsItem::resetAllAssignments:
    return text(L"Reset all assignments");
  case AirfixWindowsRenderSettingsItem::swapAssignments:
    return text(L"Swap assignments");
  case AirfixWindowsRenderSettingsItem::saveControllerProfile:
    return text(L"Save for next launch");
  case AirfixWindowsRenderSettingsItem::back:
    return text(L"Back");
  case AirfixWindowsRenderSettingsItem::count:
    break;
  }
  return {};
}

AirfixWindowsUiSemanticText airfixWindowsUiItemValue(
    const AirfixWindowsRenderSettingsItem item,
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  const auto &draft = snapshot.draftSettings;
  switch (item) {
  case AirfixWindowsRenderSettingsItem::renderScale:
    return formattedText(L"%.0f%%",
                         static_cast<double>(draft.renderScalePercent));
  case AirfixWindowsRenderSettingsItem::interfaceScale:
    return formattedText(L"%.0f%%", static_cast<double>(draft.uiScalePercent));
  case AirfixWindowsRenderSettingsItem::presentation:
    return text(draft.scenePresentation ==
                        render::ScenePresentationMode::originalFourByThree
                    ? L"Original 4:3"
                    : L"Hor+");
  case AirfixWindowsRenderSettingsItem::verticalFovAdjustment:
    return formattedText(
        L"+%.0f deg", static_cast<double>(draft.verticalFovAdjustmentDegrees));
  case AirfixWindowsRenderSettingsItem::visualProfile:
    return text(draft.visualProfile == render::VisualProfile::enhanced
                    ? L"Enhanced preview"
                    : L"Classic");
  case AirfixWindowsRenderSettingsItem::rendererStatistics:
    return text(draft.diagnosticsOverlayEnabled ? L"On" : L"Off");
  case AirfixWindowsRenderSettingsItem::bindingAction:
    return controllerActionLabel(snapshot.selectedControllerBindingAction);
  case AirfixWindowsRenderSettingsItem::bindingAssignment:
    return controllerControlLabel(
        snapshot.selectedControllerBindingControlIndex);
  case AirfixWindowsRenderSettingsItem::innerDeadzone:
  case AirfixWindowsRenderSettingsItem::outerSaturation:
  case AirfixWindowsRenderSettingsItem::sensitivity:
  case AirfixWindowsRenderSettingsItem::responseCurve:
  case AirfixWindowsRenderSettingsItem::inversion: {
    const auto axis = static_cast<std::size_t>(snapshot.selectedControllerAxis);
    if (!snapshot.controllerProfileAvailable ||
        axis >= snapshot.controllerDraftAxes.size()) {
      return text(L"Unavailable");
    }
    const auto &calibration = snapshot.controllerDraftAxes[axis];
    if (item == AirfixWindowsRenderSettingsItem::innerDeadzone ||
        item == AirfixWindowsRenderSettingsItem::outerSaturation) {
      const auto value = item == AirfixWindowsRenderSettingsItem::innerDeadzone
                             ? calibration.innerDeadzoneQ15
                             : calibration.outerSaturationQ15;
      return formattedText(L"%.1f%%", static_cast<double>(value) * 100.0 /
                                          static_cast<double>(input::q15One));
    }
    if (item == AirfixWindowsRenderSettingsItem::sensitivity) {
      return formattedText(
          L"%.0f%%",
          static_cast<double>(calibration.sensitivityPermille) / 10.0);
    }
    if (item == AirfixWindowsRenderSettingsItem::responseCurve) {
      switch (calibration.responseCurve) {
      case input::ControllerResponseCurve::linear:
        return text(L"Linear");
      case input::ControllerResponseCurve::squared:
        return text(L"Squared");
      case input::ControllerResponseCurve::cubic:
        return text(L"Cubic");
      case input::ControllerResponseCurve::count:
        return text(L"Invalid");
      }
    }
    return text(calibration.inverted != 0U ? L"On" : L"Off");
  }
  default:
    return {};
  }
}

AirfixWindowsUiSemanticText airfixWindowsUiTitle(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  switch (snapshot.screen) {
  case AirfixWindowsRenderSettingsScreen::pause:
    return text(L"Paused");
  case AirfixWindowsRenderSettingsScreen::displaySettings:
    return text(L"Display settings");
  case AirfixWindowsRenderSettingsScreen::controllerCalibration:
    return text(L"Controller settings");
  case AirfixWindowsRenderSettingsScreen::controllerAxisCalibration:
    switch (snapshot.selectedControllerAxis) {
    case input::ControllerAxisElement::leftStickX:
      return text(L"Left stick X calibration");
    case input::ControllerAxisElement::leftStickY:
      return text(L"Left stick Y calibration");
    case input::ControllerAxisElement::rightStickX:
      return text(L"Right stick X calibration");
    case input::ControllerAxisElement::rightStickY:
      return text(L"Right stick Y calibration");
    case input::ControllerAxisElement::count:
      break;
    }
    break;
  case AirfixWindowsRenderSettingsScreen::controllerButtonBindings:
    return text(L"Button bindings");
  case AirfixWindowsRenderSettingsScreen::controllerBindingConflict:
    return text(L"Assignment conflict");
  }
  return {};
}

AirfixWindowsUiSemanticText airfixWindowsUiStatus(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  switch (snapshot.status) {
  case AirfixWindowsRenderSettingsStatus::ready:
    if (snapshot.sessionOverrideMask != 0U) {
      return text(L"Session overrides are active");
    }
    if (snapshot.screen == AirfixWindowsRenderSettingsScreen::displaySettings) {
      return text(snapshot.dirty
                      ? L"Changes are ready to apply"
                      : L"Presentation only - gameplay is unchanged");
    }
    return text(L"Paused");
  case AirfixWindowsRenderSettingsStatus::noChanges:
    return text(L"No display changes to apply");
  case AirfixWindowsRenderSettingsStatus::applying:
    return text(L"Applying display settings...");
  case AirfixWindowsRenderSettingsStatus::applied:
    return text(L"Display settings applied");
  case AirfixWindowsRenderSettingsStatus::applyFailed:
    return text(L"Display settings were not changed");
  case AirfixWindowsRenderSettingsStatus::persistenceUnavailable:
    return text(L"Display settings cannot be saved");
  case AirfixWindowsRenderSettingsStatus::invalidSettings:
    return text(L"The selected display settings are invalid");
  case AirfixWindowsRenderSettingsStatus::controllerProfileReady:
    if (snapshot.screen ==
        AirfixWindowsRenderSettingsScreen::controllerAxisCalibration) {
      return text(snapshot.controllerConnected
                      ? L"Live preview uses the same transform as gameplay"
                      : L"Connect a controller to preview this axis");
    }
    if (snapshot.controllerProfileRepairRequired &&
        !snapshot.controllerProfileDirty) {
      return text(L"Recovered profile is ready to repair");
    }
    return text(snapshot.controllerProfileDirty
                    ? L"Controller profile changes are ready to save"
                    : L"Choose calibration or button assignments");
  case AirfixWindowsRenderSettingsStatus::controllerProfileNoChanges:
    return text(L"No controller profile changes to save");
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaving:
    return text(L"Saving controller profile...");
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaved:
    return text(L"Controller profile repaired");
  case AirfixWindowsRenderSettingsStatus::controllerProfileSavedRestartRequired:
    return text(L"Saved - controller changes take effect after restart");
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaveFailed:
    return text(L"Controller profile was not saved - retry is available");
  case AirfixWindowsRenderSettingsStatus::
      controllerProfilePersistenceUnavailable:
    return text(L"Controller profile cannot be saved");
  case AirfixWindowsRenderSettingsStatus::invalidControllerProfile:
    return text(L"The selected controller profile is invalid");
  case AirfixWindowsRenderSettingsStatus::controllerBindingConflict:
    return text(L"Assignment is in use - cancel or swap explicitly");
  case AirfixWindowsRenderSettingsStatus::controllerBindingProtectedConflict:
    return text(L"That assignment is protected and cannot be moved");
  case AirfixWindowsRenderSettingsStatus::controllerBindingActionUnavailable:
    return text(L"This custom or unavailable action cannot be edited");
  }
  return {};
}

AirfixWindowsUiSemanticBuildResult buildAirfixWindowsUiSemanticTree(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  if (!validSnapshot(snapshot)) {
    return {.issue = AirfixWindowsUiSemanticIssue::invalidSnapshot};
  }

  AirfixWindowsUiSemanticTree tree{
      .accessibilityGeneration = snapshot.accessibilityGeneration,
      .screen = snapshot.screen,
  };
  auto append = [&](AirfixWindowsUiSemanticNode node) noexcept {
    if (static_cast<std::size_t>(tree.nodeCount) >= tree.nodes.size()) {
      return false;
    }
    tree.nodes[tree.nodeCount++] = node;
    return true;
  };

  if (!append({.runtimeId = 1U,
               .parentIndex = airfixWindowsUiSemanticNoParent,
               .role = AirfixWindowsUiSemanticRole::window,
               .bounds = snapshot.panelBounds,
               .name = text(L"Airfix Dogfighter settings"),
               .enabled = true,
               .visible = true}) ||
      !append({.runtimeId = 2U,
               .parentIndex = 0U,
               .role = AirfixWindowsUiSemanticRole::heading,
               .bounds = snapshot.titleBounds,
               .name = airfixWindowsUiTitle(snapshot),
               .enabled = true,
               .visible = true}) ||
      !append({.runtimeId = 3U,
               .parentIndex = 0U,
               .role = AirfixWindowsUiSemanticRole::status,
               .bounds = snapshot.statusBounds,
               .name = airfixWindowsUiStatus(snapshot),
               .enabled = true,
               .visible = true})) {
    return {.issue = AirfixWindowsUiSemanticIssue::capacityExceeded};
  }

  constexpr auto focus =
      airfixWindowsUiSemanticActionMask(AirfixWindowsUiSemanticAction::focus);
  constexpr auto invoke =
      airfixWindowsUiSemanticActionMask(AirfixWindowsUiSemanticAction::invoke);
  constexpr auto decrement = airfixWindowsUiSemanticActionMask(
      AirfixWindowsUiSemanticAction::decrement);
  constexpr auto increment = airfixWindowsUiSemanticActionMask(
      AirfixWindowsUiSemanticAction::increment);

  for (std::uint8_t index = 0U; index < snapshot.logicalItemCount; ++index) {
    const auto &item = snapshot.logicalItems[index];
    const bool adjustable =
        airfixWindowsRenderSettingsItemIsAdjustable(item.item);
    const auto rowIndex = tree.nodeCount;
    const auto rowId = rowRuntimeId(snapshot.screen, item.item);
    const auto label = airfixWindowsUiItemLabel(item.item);
    const auto value = adjustable
                           ? airfixWindowsUiItemValue(item.item, snapshot)
                           : AirfixWindowsUiSemanticText{};
    if ((adjustable && value.empty()) ||
        !append({.runtimeId = rowId,
                 .parentIndex = 0U,
                 .role = adjustable
                             ? AirfixWindowsUiSemanticRole::adjustableValue
                             : AirfixWindowsUiSemanticRole::action,
                 .item = item.item,
                 .bounds = item.bounds,
                 .name = label,
                 .value = value,
                 .actions = static_cast<std::uint8_t>(
                     focus | (adjustable ? decrement | increment : invoke)),
                 .enabled = item.enabled,
                 .selected = item.selected,
                 .visible = item.visible,
                 .offscreen = item.offscreen,
                 .focusable = true})) {
      return {.issue = adjustable && value.empty()
                           ? AirfixWindowsUiSemanticIssue::invalidSnapshot
                           : AirfixWindowsUiSemanticIssue::capacityExceeded};
    }

    if (!adjustable) {
      continue;
    }
    if (!append({.runtimeId = static_cast<std::uint16_t>(rowId + 1U),
                 .parentIndex = rowIndex,
                 .role = AirfixWindowsUiSemanticRole::decrementButton,
                 .item = item.item,
                 .bounds = item.previousBounds,
                 .name = prefixedText(L"Decrease ", label),
                 .actions = static_cast<std::uint8_t>(focus | decrement),
                 .enabled = item.enabled,
                 .visible = item.visible,
                 .offscreen = item.offscreen,
                 .focusable = true}) ||
        !append({.runtimeId = static_cast<std::uint16_t>(rowId + 2U),
                 .parentIndex = rowIndex,
                 .role = AirfixWindowsUiSemanticRole::incrementButton,
                 .item = item.item,
                 .bounds = item.nextBounds,
                 .name = prefixedText(L"Increase ", label),
                 .actions = static_cast<std::uint8_t>(focus | increment),
                 .enabled = item.enabled,
                 .visible = item.visible,
                 .offscreen = item.offscreen,
                 .focusable = true})) {
      return {.issue = AirfixWindowsUiSemanticIssue::capacityExceeded};
    }
  }

  return {.tree = tree};
}

} // namespace airfix::windows
