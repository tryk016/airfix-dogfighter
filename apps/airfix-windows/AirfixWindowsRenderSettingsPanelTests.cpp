#include "AirfixWindowsRenderSettingsPanel.hpp"

#include "airfix/input/ControllerInputBatchBridge.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::DigitalAction;
using airfix::input::InputFrame;
using airfix::render::RenderPresentationSettings;
using airfix::windows::AirfixWindowsControllerAxisInputSnapshot;
using airfix::windows::AirfixWindowsControllerProfilePanelState;
using airfix::windows::AirfixWindowsPointerInput;
using airfix::windows::AirfixWindowsRenderSettingsItem;
using airfix::windows::AirfixWindowsRenderSettingsPanel;
using airfix::windows::AirfixWindowsRenderSettingsScreen;
using airfix::windows::AirfixWindowsRenderSettingsSessionOverride;
using airfix::windows::AirfixWindowsRenderSettingsStatus;
using airfix::windows::AirfixWindowsRenderSettingsViewItem;
using airfix::windows::AirfixWindowsRenderSettingsViewSnapshot;
using airfix::windows::AirfixWindowsUiPixelExtent;
using airfix::texture::TextureMode;
using airfix::texture::TextureModeMissionReloadOutcome;
using airfix::texture::TextureModeMissionReloadStatus;
using airfix::texture::TexturePackageAvailability;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] InputFrame analogFrame(const AnalogAxis axis,
                                     const std::int16_t value) noexcept {
  InputFrame frame;
  frame.analogValues[airfix::input::toIndex(axis)] = value;
  return frame;
}

[[nodiscard]] InputFrame pressedFrame(const DigitalAction action) noexcept {
  InputFrame frame;
  const auto index = airfix::input::toIndex(action);
  frame.pressedBits[index / 64U] |= std::uint64_t{1U} << (index % 64U);
  return frame;
}

void releaseAxes(AirfixWindowsRenderSettingsPanel &panel) {
  static_cast<void>(panel.consumeInputFrame(InputFrame{}));
}

void move(AirfixWindowsRenderSettingsPanel &panel,
          const std::int32_t direction) {
  releaseAxes(panel);
  const auto value = static_cast<std::int16_t>(direction < 0 ? 16384 : -16384);
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, value)));
}

void openSettings(AirfixWindowsRenderSettingsPanel &panel) {
  const auto intent =
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm));
  require(intent.empty(), "opening display settings emitted an intent");
  require(panel.screen() == AirfixWindowsRenderSettingsScreen::displaySettings,
          "Display settings did not open");
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
makePanel(const float scale = 100.0F, const bool persistenceAvailable = true,
          const AirfixWindowsUiPixelExtent output = {},
          const float uiScale = 100.0F,
          const TexturePackageAvailability texturePackageAvailability =
              TexturePackageAvailability::notConfigured) {
  RenderPresentationSettings settings;
  settings.renderScalePercent = scale;
  settings.uiScalePercent = uiScale;
  auto panel = AirfixWindowsRenderSettingsPanel::create(
      settings, persistenceAvailable, output, 0U, true, std::nullopt,
      texturePackageAvailability);
  require(panel.has_value(), "valid panel fixture was rejected");
  return *panel;
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel makeControllerPanelWithProfile(
    const airfix::input::ControllerInputProfileRecord &profile,
    const bool persistenceAvailable = true, const bool repairRequired = false,
    const AirfixWindowsUiPixelExtent output = {}) {
  auto panel = AirfixWindowsRenderSettingsPanel::create(
      RenderPresentationSettings{}, true, output, 0U, true,
      AirfixWindowsControllerProfilePanelState{
          .active = profile,
          .persisted = profile,
          .capabilities =
              {
                  .persistenceAvailable = persistenceAvailable,
                  .repairRequired = repairRequired,
              },
      });
  require(panel.has_value(), "valid controller panel fixture was rejected");
  return *panel;
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
makeControllerPanel(const bool persistenceAvailable = true,
                    const bool repairRequired = false,
                    const AirfixWindowsUiPixelExtent output = {}) {
  return makeControllerPanelWithProfile(
      airfix::input::makeDefaultControllerInputProfileRecord(),
      persistenceAvailable, repairRequired, output);
}

[[nodiscard]] const AirfixWindowsRenderSettingsViewItem &
findItem(const AirfixWindowsRenderSettingsViewSnapshot &snapshot,
         const AirfixWindowsRenderSettingsItem item) {
  for (std::uint8_t index = 0U; index < snapshot.itemCount; ++index) {
    if (snapshot.items[index].item == item) {
      return snapshot.items[index];
    }
  }
  throw std::runtime_error("view item not found");
}

[[nodiscard]] AirfixWindowsPointerInput
click(const AirfixWindowsRenderSettingsViewItem &item) noexcept {
  return {
      .xPixels = item.bounds.x + item.bounds.width * 0.5F,
      .yPixels = item.bounds.y + item.bounds.height * 0.5F,
      .wheelY = 0,
      .primaryPressed = true,
  };
}

[[nodiscard]] AirfixWindowsPointerInput
clickNext(const AirfixWindowsRenderSettingsViewItem &item) noexcept {
  return {
      .xPixels = item.nextBounds.x + item.nextBounds.width * 0.5F,
      .yPixels = item.nextBounds.y + item.nextBounds.height * 0.5F,
      .wheelY = 0,
      .primaryPressed = true,
  };
}

void activateItem(AirfixWindowsRenderSettingsPanel &panel,
                  const AirfixWindowsRenderSettingsItem item) {
  for (std::size_t index = 0U; index < 32U; ++index) {
    move(panel, -1);
  }
  for (std::size_t index = 0U;
       index < 32U && panel.snapshot().selectedItem != item; ++index) {
    move(panel, 1);
  }
  const auto snapshot = panel.snapshot();
  require(snapshot.selectedItem == item,
          "navigation item does not belong to the active screen");
  const auto intent = panel.consumePointer(click(findItem(snapshot, item)));
  require(intent.empty(), "navigation-only item emitted an intent");
}

void openControllerCalibration(AirfixWindowsRenderSettingsPanel &panel) {
  activateItem(panel, AirfixWindowsRenderSettingsItem::controllerCalibration);
  require(panel.screen() ==
              AirfixWindowsRenderSettingsScreen::controllerCalibration,
          "Controller calibration did not open");
}

void openLeftStickXCalibration(AirfixWindowsRenderSettingsPanel &panel) {
  activateItem(panel, AirfixWindowsRenderSettingsItem::leftStickX);
  require(panel.screen() ==
              AirfixWindowsRenderSettingsScreen::controllerAxisCalibration,
          "left-stick X calibration did not open");
}

void openControllerButtonBindings(AirfixWindowsRenderSettingsPanel &panel) {
  activateItem(panel, AirfixWindowsRenderSettingsItem::buttonBindings);
  require(panel.screen() ==
              AirfixWindowsRenderSettingsScreen::controllerButtonBindings,
          "controller button bindings did not open");
}

[[nodiscard]] airfix::input::ControlId
actionControl(const airfix::input::ControllerInputProfileRecord &profile,
              const airfix::input::ControllerDigitalGameplayAction action) {
  const auto lookup =
      airfix::input::controllerDigitalGameplayBinding(profile, action);
  require(lookup.editable(), "test action was not uniquely editable");
  return profile.bindings[lookup.bindingIndex].control;
}

void incrementBindingValue(AirfixWindowsRenderSettingsPanel &panel,
                           const AirfixWindowsRenderSettingsItem item,
                           const std::size_t count) {
  for (std::size_t index = 0U; index < count; ++index) {
    const auto view = panel.snapshot();
    static_cast<void>(panel.consumePointer(clickNext(findItem(view, item))));
  }
}

[[nodiscard]] bool
rectInside(const airfix::windows::AirfixWindowsUiPixelRect rect,
           const AirfixWindowsUiPixelExtent output) noexcept {
  constexpr float tolerance = 0.25F;
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height) &&
         rect.width >= 0.0F && rect.height >= 0.0F && rect.x >= -tolerance &&
         rect.y >= -tolerance &&
         rect.x + rect.width <= static_cast<float>(output.width) + tolerance &&
         rect.y + rect.height <= static_cast<float>(output.height) + tolerance;
}

void requireSnapshotInsideOutput(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) {
  require(snapshot.layoutScale > 0.0F && std::isfinite(snapshot.layoutScale),
          "layout scale was not finite and positive");
  require(rectInside(snapshot.panelBounds, snapshot.output),
          "panel escaped the physical output");
  require(rectInside(snapshot.titleBounds, snapshot.output),
          "title escaped the physical output");
  require(rectInside(snapshot.statusBounds, snapshot.output) &&
              snapshot.statusBounds.height > 0.0F,
          "status escaped the physical output");
  for (std::uint8_t index = 0U; index < snapshot.itemCount; ++index) {
    const auto &item = snapshot.items[index];
    require(rectInside(item.bounds, snapshot.output),
            "item escaped the physical output");
    require(rectInside(item.previousBounds, snapshot.output) &&
                rectInside(item.nextBounds, snapshot.output),
            "item controls escaped the physical output");
  }
}

void invalidAppliedSettingsFailClosed() {
  RenderPresentationSettings invalid;
  invalid.renderScalePercent = 250.0F;
  require(!AirfixWindowsRenderSettingsPanel::create(invalid).has_value(),
          "invalid applied settings created a panel");
}

void navigationUsesHysteresisAndBoundedRows() {
  auto panel = makePanel();
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::displaySettings,
          "pause selection did not start on Display settings");

  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, -16384)));
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::resume,
          "down actuation did not select Resume");
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, 32767)));
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::resume,
          "latched vertical axis accepted an opposite direction");
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, 8193)));
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, 32767)));
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::resume,
          "axis released above the hysteresis threshold");
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, 8192)));
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, 16384)));
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::displaySettings,
          "axis did not release at the hysteresis threshold");
  releaseAxes(panel);
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateY, 32767)));
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::displaySettings,
          "pause selection moved above its first row");

  openSettings(panel);
  for (std::uint8_t index = 0U; index < 10U; ++index) {
    move(panel, 1);
  }
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::cancel,
          "settings selection moved beyond Cancel");
  for (std::uint8_t index = 0U; index < 10U; ++index) {
    move(panel, -1);
  }
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::renderScale,
          "settings selection moved above Render scale");
}

void editsUseStepsShouldersAndBounds() {
  auto panel = makePanel(101.0F, true, {}, 100.0F,
                         TexturePackageAvailability::ready);
  openSettings(panel);

  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateX, 16384)));
  require(panel.snapshot().draftSettings.renderScalePercent == 105.0F,
          "right did not snap scale to the next five-percent step");
  static_cast<void>(
      panel.consumeInputFrame(analogFrame(AnalogAxis::uiNavigateX, 32767)));
  require(panel.snapshot().draftSettings.renderScalePercent == 105.0F,
          "horizontal latch repeated without release");
  releaseAxes(panel);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabPrevious)));
  require(panel.snapshot().draftSettings.renderScalePercent == 100.0F,
          "previous shoulder did not decrement scale");

  for (std::uint8_t index = 0U; index < 40U; ++index) {
    static_cast<void>(
        panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabPrevious)));
  }
  require(panel.snapshot().draftSettings.renderScalePercent == 50.0F,
          "render scale moved below 50 percent");
  for (std::uint8_t index = 0U; index < 40U; ++index) {
    static_cast<void>(
        panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  }
  require(panel.snapshot().draftSettings.renderScalePercent == 200.0F,
          "render scale moved above 200 percent");

  move(panel, 1);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  require(panel.snapshot().draftSettings.uiScalePercent == 105.0F,
          "interface scale did not use a five-percent step");
  for (std::uint8_t index = 0U; index < 20U; ++index) {
    static_cast<void>(
        panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  }
  require(panel.snapshot().draftSettings.uiScalePercent == 150.0F,
          "interface scale moved above its maximum");

  move(panel, 1);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  require(panel.snapshot().draftSettings.scenePresentation ==
              airfix::render::ScenePresentationMode::originalFourByThree,
          "next shoulder did not select Original 4:3");
  move(panel, 1);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  require(
      panel.snapshot().draftSettings.verticalFovAdjustmentDegrees ==
          1.0F,
      "confirm did not increment the vertical-FOV adjustment");
  for (std::uint8_t index = 0U; index < 30U; ++index) {
    static_cast<void>(
        panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  }
  require(
      panel.snapshot().draftSettings.verticalFovAdjustmentDegrees ==
          25.0F,
      "vertical-FOV adjustment moved above its safe maximum");
  move(panel, 1);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  require(panel.snapshot().draftSettings.visualProfile ==
              airfix::render::VisualProfile::enhanced,
          "confirm did not toggle the visual profile preview");
  move(panel, 1);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  require(panel.snapshot().draftSettings.textureMode == TextureMode::enhanced &&
              panel.snapshot().textureModeState.effectiveMode ==
                  TextureMode::enhanced &&
              panel.snapshot().textureModeState.missionReloadRequired,
          "confirm did not request an atomic Enhanced mission reload");
  move(panel, 1);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  require(panel.snapshot().draftSettings.diagnosticsOverlayEnabled,
          "confirm did not toggle renderer statistics");
  require(panel.snapshot().dirty,
          "accepted edits did not mark the draft dirty");
}

void applyCancelAndResumeAreDistinctIntents() {
  auto panel = makePanel();
  openSettings(panel);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  for (std::uint8_t index = 0U; index < 7U; ++index) {
    move(panel, 1);
  }

  const auto apply =
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm));
  require(apply.applyTicket.has_value() && !apply.resumeRequested,
          "Apply did not return exactly one apply ticket");
  require(panel.snapshot().applying &&
              panel.snapshot().status ==
                  AirfixWindowsRenderSettingsStatus::applying,
          "Apply did not expose the applying state");

  const auto cancel =
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiCancel));
  require(cancel.empty() &&
              panel.screen() == AirfixWindowsRenderSettingsScreen::pause,
          "closing an applying settings screen did not return to pause");
  require(panel.snapshot().applying,
          "closing settings cancelled an immutable in-flight Apply");
  require(panel.finishApplySuccess(*apply.applyTicket),
          "the exact Apply ticket did not complete");
  require(!panel.snapshot().dirty && !panel.snapshot().applying &&
              panel.snapshot().status ==
                  AirfixWindowsRenderSettingsStatus::applied,
          "successful Apply did not promote and clear the draft");
  require(!panel.finishApplySuccess(*apply.applyTicket),
          "a duplicate Apply completion was accepted");

  move(panel, 1);
  const auto resume =
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm));
  require(resume.resumeRequested && !resume.applyTicket.has_value(),
          "Resume was not emitted as an explicit standalone intent");

  auto cancelledPanel = makePanel();
  openSettings(cancelledPanel);
  static_cast<void>(
      cancelledPanel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  for (std::uint8_t index = 0U; index < 8U; ++index) {
    move(cancelledPanel, 1);
  }
  const auto close =
      cancelledPanel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm));
  require(close.empty() && cancelledPanel.screen() ==
                               AirfixWindowsRenderSettingsScreen::pause,
          "Cancel emitted Resume or Apply");
  openSettings(cancelledPanel);
  require(cancelledPanel.snapshot().draftSettings.renderScalePercent ==
                  100.0F &&
              !cancelledPanel.snapshot().dirty,
          "Cancel did not restore the applied draft");
}

void textureModeAvailabilityAndReloadOutcomeAreExplicit() {
  auto unavailable = makePanel();
  openSettings(unavailable);
  for (std::uint8_t index = 0U; index < 5U; ++index) {
    move(unavailable, 1);
  }
  static_cast<void>(unavailable.consumeInputFrame(
      pressedFrame(DigitalAction::uiConfirm)));
  require(unavailable.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::textureMode &&
              unavailable.snapshot().draftSettings.textureMode ==
                  TextureMode::classic &&
              unavailable.snapshot().status ==
                  AirfixWindowsRenderSettingsStatus::
                      enhancedTexturesUnavailable &&
              !unavailable.snapshot().dirty,
          "unavailable Enhanced option changed the settings draft");

  auto ready = makePanel(100.0F, true, {}, 100.0F,
                         TexturePackageAvailability::ready);
  openSettings(ready);
  for (std::uint8_t index = 0U; index < 5U; ++index) {
    move(ready, 1);
  }
  static_cast<void>(
      ready.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  require(ready.snapshot().textureModeState.missionReloadRequired,
          "ready Enhanced edit omitted the mission reload contract");

  ready.setTextureModeReloadOutcome(TextureModeMissionReloadOutcome{
      .status = TextureModeMissionReloadStatus::reloadFailed,
      .requestedState = ready.snapshot().textureModeState,
      .activeMission = {},
  });
  require(ready.snapshot().status ==
              AirfixWindowsRenderSettingsStatus::
                  textureReloadFailedRestartRequired &&
              ready.snapshot().textureModeState.missionReloadRequired,
          "failed mission reload did not preserve the active Classic state");
}

void resumeRemainsDisabledWithoutReadyGameplay() {
  RenderPresentationSettings settings;
  const auto created =
      AirfixWindowsRenderSettingsPanel::create(settings, true, {}, 0U, false);
  require(created.has_value(), "valid panel with disabled Resume was rejected");
  auto panel = *created;

  move(panel, 1);
  const auto snapshot = panel.snapshot();
  const auto &resume =
      findItem(snapshot, AirfixWindowsRenderSettingsItem::resume);
  require(snapshot.selectedItem == AirfixWindowsRenderSettingsItem::resume &&
              !resume.enabled,
          "unavailable Resume was not exposed as disabled");

  const auto keyboardIntent =
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm));
  require(keyboardIntent.empty(),
          "keyboard/controller activated unavailable Resume");
  const auto pointerIntent = panel.consumePointer(click(resume));
  require(pointerIntent.empty(), "pointer activated unavailable Resume");

  panel.setResumeAvailable(true);
  require(findItem(panel.snapshot(), AirfixWindowsRenderSettingsItem::resume)
              .enabled,
          "Resume did not become available with ready gameplay");
  require(panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm))
              .resumeRequested,
          "enabled Resume did not emit the explicit intent");
}

void layoutFitsDefaultSmallAndHighDpiOutputs() {
  constexpr std::array outputs{
      AirfixWindowsUiPixelExtent{
          .width = 960U, .height = 540U, .dpiScale = 1.0F},
      AirfixWindowsUiPixelExtent{
          .width = 1280U, .height = 720U, .dpiScale = 1.5F},
      AirfixWindowsUiPixelExtent{
          .width = 640U, .height = 360U, .dpiScale = 1.0F},
  };

  for (const auto output : outputs) {
    auto panel = makePanel(100.0F, true, output);
    requireSnapshotInsideOutput(panel.snapshot());
    openSettings(panel);
    const auto settings = panel.snapshot();
    requireSnapshotInsideOutput(settings);
    for (std::uint8_t index = 1U; index < settings.itemCount; ++index) {
      const auto &previous = settings.items[index - 1U].bounds;
      const auto &current = settings.items[index].bounds;
      require(previous.y + previous.height <= current.y + 0.25F,
              "adaptive settings rows overlap");
    }
  }
}

void interfaceScaleUsesNativeRasterAndScrollableRows() {
  auto panel = makePanel(100.0F, true,
                         {
                             .width = 1920U,
                             .height = 1080U,
                             .dpiScale = 1.0F,
                         });
  openSettings(panel);
  const auto baseline = panel.snapshot();
  require(std::fabs(baseline.layoutScale - 1.0F) < 1.0e-6F &&
              baseline.itemCount == 9U,
          "default interface scale changed the native Windows layout");

  move(panel, 1);
  for (std::size_t index = 0U; index < 10U; ++index) {
    static_cast<void>(
        panel.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  }
  const auto enlarged = panel.snapshot();
  require(
      enlarged.draftSettings.uiScalePercent == 150.0F &&
          std::fabs(enlarged.layoutScale - 1.5F) < 1.0e-6F &&
          enlarged.itemCount < 9U &&
          findItem(enlarged, AirfixWindowsRenderSettingsItem::interfaceScale)
              .selected,
      "enlarged interface did not use native DPI scaling and a visible row "
      "window");
  requireSnapshotInsideOutput(enlarged);

  for (std::size_t index = 0U; index < 16U; ++index) {
    move(panel, 1);
  }
  const auto scrolled = panel.snapshot();
  require(
      scrolled.selectedItem == AirfixWindowsRenderSettingsItem::cancel &&
          findItem(scrolled, AirfixWindowsRenderSettingsItem::cancel).selected,
      "selection did not auto-scroll enlarged interface rows");
  requireSnapshotInsideOutput(scrolled);

  auto shortUltrawide = makePanel(
      100.0F, true,
      {
          .width = 1280U,
          .height = 360U,
          .dpiScale = 1.0F,
      },
      150.0F);
  openSettings(shortUltrawide);
  const auto shortSnapshot = shortUltrawide.snapshot();
  require(shortSnapshot.itemCount > 0U,
          "short ultrawide interface hid every settings row");
  const auto &lastRow =
      shortSnapshot.items[shortSnapshot.itemCount - 1U].bounds;
  require(lastRow.y + lastRow.height <=
              shortSnapshot.statusBounds.y + 0.25F,
          "short ultrawide settings row overlaps the status region");
  requireSnapshotInsideOutput(shortSnapshot);
}

void pointerUsesPhysicalLayoutAndWheel() {
  auto panel =
      makePanel(100.0F, true,
                AirfixWindowsUiPixelExtent{
                    .width = 3440U, .height = 1440U, .dpiScale = 1.5F});
  const auto pause = panel.snapshot();
  const auto &display =
      findItem(pause, AirfixWindowsRenderSettingsItem::displaySettings);
  require(display.bounds.x > 0.0F && display.bounds.x + display.bounds.width <
                                         static_cast<float>(pause.output.width),
          "ultrawide panel was not laid out in physical output pixels");
  require(panel.consumePointer(click(display)).empty() &&
              panel.screen() ==
                  AirfixWindowsRenderSettingsScreen::displaySettings,
          "physical click did not open Display settings");

  auto settings = panel.snapshot();
  const auto &scale =
      findItem(settings, AirfixWindowsRenderSettingsItem::renderScale);
  const AirfixWindowsPointerInput increment{
      .xPixels = scale.nextBounds.x + scale.nextBounds.width * 0.5F,
      .yPixels = scale.nextBounds.y + scale.nextBounds.height * 0.5F,
      .wheelY = 0,
      .primaryPressed = true,
  };
  require(panel.consumePointer(increment).empty() &&
              panel.snapshot().draftSettings.renderScalePercent == 105.0F,
          "scale increment hit target was not active in physical pixels");

  static_cast<void>(panel.consumePointer({.xPixels = -100.0F,
                                          .yPixels = -100.0F,
                                          .wheelY = -20,
                                          .primaryPressed = false}));
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::cancel,
          "wheel navigation did not clamp at the last settings row");
  static_cast<void>(panel.consumePointer({.xPixels = -100.0F,
                                          .yPixels = -100.0F,
                                          .wheelY = 20,
                                          .primaryPressed = false}));
  require(panel.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::renderScale,
          "wheel navigation did not clamp at the first settings row");

  settings = panel.snapshot();
  const auto &presentation =
      findItem(settings, AirfixWindowsRenderSettingsItem::presentation);
  const AirfixWindowsPointerInput chooseOriginal{
      .xPixels =
          presentation.nextBounds.x + presentation.nextBounds.width * 0.5F,
      .yPixels =
          presentation.nextBounds.y + presentation.nextBounds.height * 0.5F,
      .wheelY = 0,
      .primaryPressed = true,
  };
  static_cast<void>(panel.consumePointer(chooseOriginal));
  require(panel.snapshot().draftSettings.scenePresentation ==
              airfix::render::ScenePresentationMode::originalFourByThree,
          "pointer did not select Original 4:3");
}

void controllerCalibrationUsesSharedPreviewAndSavesForNextLaunch() {
  auto panel = makeControllerPanel();
  const auto pause = panel.snapshot();
  require(pause.controllerProfileAvailable && pause.itemCount == 3U,
          "controller-enabled pause view omitted calibration");
  openControllerCalibration(panel);
  openLeftStickXCalibration(panel);

  AirfixWindowsControllerAxisInputSnapshot input{};
  input.connected = true;
  input.rawAxes[static_cast<std::size_t>(
      airfix::input::ControllerAxisElement::leftStickX)] = 4095;
  panel.setControllerAxisInput(input);
  auto snapshot = panel.snapshot();
  require(snapshot.controllerConnected &&
              snapshot.controllerPreviewRaw == 4095 &&
              snapshot.controllerPreviewEffective == 0,
          "panel preview did not preserve the exact transport floor");

  input.rawAxes[0] = 4096;
  panel.setControllerAxisInput(input);
  snapshot = panel.snapshot();
  require(snapshot.controllerPreviewEffective == 4096,
          "panel preview rejected the first value above the transport floor");

  input.rawAxes[0] = 19661;
  panel.setControllerAxisInput(input);
  const auto defaults =
      airfix::input::makeDefaultControllerInputProfileRecord();
  const auto resolvedDefaults =
      airfix::input::resolveControllerInputProfile(defaults);
  require(resolvedDefaults.complete(),
          "default controller profile did not resolve in panel test");
  const auto expectedDefault =
      airfix::input::transformControllerAxisForTransport(
          input.rawAxes[0], airfix::input::ControllerAxisElement::leftStickX,
          *resolvedDefaults.profile);
  require(expectedDefault.has_value() &&
              panel.snapshot().controllerPreviewEffective == *expectedDefault,
          "panel preview diverged from the runtime transform");

  snapshot = panel.snapshot();
  const auto &sensitivity =
      findItem(snapshot, AirfixWindowsRenderSettingsItem::sensitivity);
  require(panel.consumePointer(clickNext(sensitivity)).empty(),
          "sensitivity edit emitted an intent");
  snapshot = panel.snapshot();
  require(snapshot.controllerDraftAxes[0].sensitivityPermille == 1050U &&
              snapshot.controllerProfileDirty,
          "axis edit did not update the controller draft");
  auto edited = defaults;
  edited.axes[0].sensitivityPermille = 1050U;
  const auto resolvedEdited =
      airfix::input::resolveControllerInputProfile(edited);
  const auto expectedEdited =
      resolvedEdited.complete()
          ? airfix::input::transformControllerAxisForTransport(
                input.rawAxes[0],
                airfix::input::ControllerAxisElement::leftStickX,
                *resolvedEdited.profile)
          : std::nullopt;
  require(expectedEdited.has_value() &&
              snapshot.controllerPreviewEffective == *expectedEdited,
          "draft calibration did not drive the exact shared preview");

  activateItem(panel, AirfixWindowsRenderSettingsItem::back);
  const auto save = panel.consumePointer(
      click(findItem(panel.snapshot(),
                     AirfixWindowsRenderSettingsItem::saveControllerProfile)));
  require(save.controllerProfileSaveTicket.has_value() &&
              !save.applyTicket.has_value() && !save.resumeRequested,
          "controller save did not emit exactly one save ticket");
  const auto &ticket = *save.controllerProfileSaveTicket;
  require(ticket.candidate.axes[0].sensitivityPermille == 1050U &&
              ticket.candidate.bindings == defaults.bindings &&
              ticket.candidate.bindingCount == defaults.bindingCount &&
              !ticket.repairsPersistence,
          "controller save ticket changed unrelated profile state");
  require(panel.finishControllerProfileSaveSuccess(ticket),
          "exact controller save ticket did not complete");
  snapshot = panel.snapshot();
  require(panel.screen() == AirfixWindowsRenderSettingsScreen::pause &&
              !snapshot.controllerProfileDirty &&
              snapshot.controllerProfileRestartRequired &&
              snapshot.status == AirfixWindowsRenderSettingsStatus::
                                     controllerProfileSavedRestartRequired,
          "successful controller save did not remain restart-only");
  require(!panel.finishControllerProfileSaveSuccess(ticket),
          "duplicate controller save completion was accepted");
}

void controllerCalibrationCancelFailureAndRetryAreAtomic() {
  auto cancelled = makeControllerPanel();
  openControllerCalibration(cancelled);
  openLeftStickXCalibration(cancelled);
  const auto sensitivity = findItem(
      cancelled.snapshot(), AirfixWindowsRenderSettingsItem::sensitivity);
  static_cast<void>(cancelled.consumePointer(clickNext(sensitivity)));
  static_cast<void>(
      cancelled.consumeInputFrame(pressedFrame(DigitalAction::uiCancel)));
  require(cancelled.screen() ==
              AirfixWindowsRenderSettingsScreen::controllerCalibration,
          "axis cancel did not return to controller overview");
  static_cast<void>(
      cancelled.consumeInputFrame(pressedFrame(DigitalAction::uiCancel)));
  require(cancelled.screen() == AirfixWindowsRenderSettingsScreen::pause &&
              !cancelled.snapshot().controllerProfileDirty,
          "controller cancel did not restore the persisted draft");

  auto retry = makeControllerPanel();
  openControllerCalibration(retry);
  openLeftStickXCalibration(retry);
  static_cast<void>(retry.consumePointer(clickNext(findItem(
      retry.snapshot(), AirfixWindowsRenderSettingsItem::sensitivity))));
  activateItem(retry, AirfixWindowsRenderSettingsItem::back);
  const auto first = retry.consumePointer(
      click(findItem(retry.snapshot(),
                     AirfixWindowsRenderSettingsItem::saveControllerProfile)));
  require(first.controllerProfileSaveTicket.has_value(),
          "retry fixture did not begin its first save");
  require(retry.finishControllerProfileSaveFailure(
              *first.controllerProfileSaveTicket),
          "exact failed controller save ticket was rejected");
  auto snapshot = retry.snapshot();
  require(
      snapshot.controllerProfileDirty &&
          snapshot.status ==
              AirfixWindowsRenderSettingsStatus::controllerProfileSaveFailed &&
          findItem(snapshot,
                   AirfixWindowsRenderSettingsItem::saveControllerProfile)
              .enabled,
      "failed controller save did not preserve a retryable draft");
  const auto second = retry.consumePointer(click(findItem(
      snapshot, AirfixWindowsRenderSettingsItem::saveControllerProfile)));
  require(second.controllerProfileSaveTicket.has_value() &&
              second.controllerProfileSaveTicket->serial !=
                  first.controllerProfileSaveTicket->serial,
          "controller retry did not issue a fresh immutable ticket");
}

void controllerPersistenceCapabilitiesFailClosedAndPermitRepair() {
  auto unavailable = makeControllerPanel(false);
  openControllerCalibration(unavailable);
  openLeftStickXCalibration(unavailable);
  static_cast<void>(unavailable.consumePointer(clickNext(findItem(
      unavailable.snapshot(), AirfixWindowsRenderSettingsItem::sensitivity))));
  activateItem(unavailable, AirfixWindowsRenderSettingsItem::back);
  auto snapshot = unavailable.snapshot();
  const auto &save = findItem(
      snapshot, AirfixWindowsRenderSettingsItem::saveControllerProfile);
  require(!save.enabled &&
              snapshot.status == AirfixWindowsRenderSettingsStatus::
                                     controllerProfilePersistenceUnavailable &&
              unavailable.consumePointer(click(save)).empty(),
          "unavailable controller persistence did not fail closed");

  auto repair = makeControllerPanel(true, true);
  openControllerCalibration(repair);
  snapshot = repair.snapshot();
  const auto &repairSave = findItem(
      snapshot, AirfixWindowsRenderSettingsItem::saveControllerProfile);
  require(!snapshot.controllerProfileDirty &&
              snapshot.controllerProfileRepairRequired && repairSave.enabled,
          "recovered controller profile did not expose explicit repair");
  const auto intent = repair.consumePointer(click(repairSave));
  require(intent.controllerProfileSaveTicket.has_value() &&
              intent.controllerProfileSaveTicket->repairsPersistence,
          "repair-only save did not emit a repair ticket");
  require(repair.finishControllerProfileSaveSuccess(
              *intent.controllerProfileSaveTicket),
          "repair-only save did not complete");
  snapshot = repair.snapshot();
  require(!snapshot.controllerProfileRestartRequired &&
              snapshot.status ==
                  AirfixWindowsRenderSettingsStatus::controllerProfileSaved,
          "repair-only save falsely required a restart");
}

void controllerBindingPickerMovesCancelsAndSwapsExplicitly() {
  auto moved = makeControllerPanel();
  openControllerCalibration(moved);
  openControllerButtonBindings(moved);
  auto snapshot = moved.snapshot();
  require(
      snapshot.itemCount == 5U &&
          snapshot.selectedItem ==
              AirfixWindowsRenderSettingsItem::bindingAction &&
          snapshot.selectedControllerBindingAction ==
              airfix::input::ControllerDigitalGameplayAction::primaryFire &&
          snapshot.selectedControllerBindingStatus ==
              airfix::input::ControllerDigitalGameplayBindingStatus::editable &&
          snapshot.selectedControllerBindingControlIndex == 0U &&
          snapshot.controllerBindingPickerPhase ==
              airfix::settings::ControllerInputBindingPickerPhase::
                  choosingControl,
      "button picker did not expose bounded initial action/assignment "
      "metadata");

  move(moved, 1);
  require(moved.snapshot().selectedItem ==
              AirfixWindowsRenderSettingsItem::bindingAssignment,
          "controller navigation did not reach Assignment");
  static_cast<void>(
      moved.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  require(moved.snapshot().selectedControllerBindingControlIndex == 1U,
          "controller shoulder did not advance Assignment");
  incrementBindingValue(
      moved, AirfixWindowsRenderSettingsItem::bindingAssignment, 10U);
  activateItem(moved, AirfixWindowsRenderSettingsItem::moveBinding);
  snapshot = moved.snapshot();
  require(moved.screen() ==
                  AirfixWindowsRenderSettingsScreen::controllerButtonBindings &&
              snapshot.controllerProfileDirty &&
              snapshot.selectedControllerBindingControlIndex == 11U,
          "unused assignment did not move or reopen the bounded picker");
  activateItem(moved, AirfixWindowsRenderSettingsItem::back);
  const auto save = moved.consumePointer(
      click(findItem(moved.snapshot(),
                     AirfixWindowsRenderSettingsItem::saveControllerProfile)));
  require(
      save.controllerProfileSaveTicket.has_value() &&
          actionControl(
              save.controllerProfileSaveTicket->candidate,
              airfix::input::ControllerDigitalGameplayAction::primaryFire) ==
              airfix::input::controls::controller::dpadLeft,
      "remap was absent from the immutable controller-profile save ticket");
  const auto saving = moved.snapshot();
  static_cast<void>(
      moved.consumeInputFrame(pressedFrame(DigitalAction::uiCancel)));
  static_cast<void>(
      moved.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  static_cast<void>(
      moved.consumeInputFrame(pressedFrame(DigitalAction::uiTabNext)));
  const auto stillSaving = moved.snapshot();
  require(stillSaving.screen ==
                  AirfixWindowsRenderSettingsScreen::controllerCalibration &&
              stillSaving.selectedItem == saving.selectedItem &&
              stillSaving.status ==
                  AirfixWindowsRenderSettingsStatus::controllerProfileSaving &&
              stillSaving.controllerProfileSaving &&
              stillSaving.controllerProfileDirty,
          "controller input escaped or mutated the frozen profile during save");
  require(moved.finishControllerProfileSaveSuccess(
              *save.controllerProfileSaveTicket) &&
              moved.snapshot().controllerProfileRestartRequired,
          "saved remap did not remain next-launch-only");

  auto conflict = makeControllerPanel();
  openControllerCalibration(conflict);
  openControllerButtonBindings(conflict);
  incrementBindingValue(conflict,
                        AirfixWindowsRenderSettingsItem::bindingAssignment, 1U);
  activateItem(conflict, AirfixWindowsRenderSettingsItem::moveBinding);
  snapshot = conflict.snapshot();
  require(
      conflict.screen() ==
              AirfixWindowsRenderSettingsScreen::controllerBindingConflict &&
          snapshot.selectedItem == AirfixWindowsRenderSettingsItem::cancel &&
          snapshot.status ==
              AirfixWindowsRenderSettingsStatus::controllerBindingConflict &&
          snapshot.conflictingControllerBindingAction ==
              airfix::input::ControllerDigitalGameplayAction::secondaryFire &&
          snapshot.controllerBindingPickerPhase ==
              airfix::settings::ControllerInputBindingPickerPhase::
                  confirmingSwap &&
          !snapshot.controllerProfileDirty,
      "cancel-first conflict mutated the draft or did not default to Cancel");
  static_cast<void>(
      conflict.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  require(conflict.screen() ==
                  AirfixWindowsRenderSettingsScreen::controllerButtonBindings &&
              !conflict.snapshot().controllerProfileDirty &&
              conflict.snapshot().controllerBindingPickerPhase ==
                  airfix::settings::ControllerInputBindingPickerPhase::
                      choosingControl,
          "default conflict Cancel did not restore the unchanged picker");

  activateItem(conflict, AirfixWindowsRenderSettingsItem::moveBinding);
  activateItem(conflict, AirfixWindowsRenderSettingsItem::swapAssignments);
  snapshot = conflict.snapshot();
  require(conflict.screen() ==
                  AirfixWindowsRenderSettingsScreen::controllerButtonBindings &&
              snapshot.controllerProfileDirty,
          "explicit Swap assignments did not atomically edit the shared draft");
  activateItem(conflict, AirfixWindowsRenderSettingsItem::back);
  const auto swapped = conflict.consumePointer(
      click(findItem(conflict.snapshot(),
                     AirfixWindowsRenderSettingsItem::saveControllerProfile)));
  require(
      swapped.controllerProfileSaveTicket.has_value() &&
          actionControl(
              swapped.controllerProfileSaveTicket->candidate,
              airfix::input::ControllerDigitalGameplayAction::primaryFire) ==
              airfix::input::controls::controller::leftTrigger &&
          actionControl(
              swapped.controllerProfileSaveTicket->candidate,
              airfix::input::ControllerDigitalGameplayAction::secondaryFire) ==
              airfix::input::controls::controller::rightTrigger,
      "explicit conflict confirmation did not swap both assignments");
}

void controllerBindingPickerProtectsCustomAndReservedAssignments() {
  auto bounded = makeControllerPanel();
  openControllerCalibration(bounded);
  openControllerButtonBindings(bounded);
  static_cast<void>(
      bounded.consumeInputFrame(pressedFrame(DigitalAction::uiTabPrevious)));
  require(bounded.snapshot().selectedControllerBindingAction ==
              airfix::input::ControllerDigitalGameplayAction::primaryFire,
          "Action carousel moved before its first typed action");
  incrementBindingValue(bounded, AirfixWindowsRenderSettingsItem::bindingAction,
                        20U);
  require(bounded.snapshot().selectedControllerBindingAction ==
              airfix::input::ControllerDigitalGameplayAction::missionStatus,
          "Action carousel moved beyond its seventh typed action");
  incrementBindingValue(
      bounded, AirfixWindowsRenderSettingsItem::bindingAssignment, 30U);
  require(bounded.snapshot().selectedControllerBindingControlIndex ==
              airfix::input::controllerAssignableControlCount - 1U,
          "Assignment carousel moved beyond its fourteenth typed control");

  auto protectedPanel = makeControllerPanel();
  openControllerCalibration(protectedPanel);
  openControllerButtonBindings(protectedPanel);
  incrementBindingValue(
      protectedPanel, AirfixWindowsRenderSettingsItem::bindingAssignment, 13U);
  activateItem(protectedPanel, AirfixWindowsRenderSettingsItem::moveBinding);
  auto snapshot = protectedPanel.snapshot();
  require(protectedPanel.screen() ==
                  AirfixWindowsRenderSettingsScreen::controllerButtonBindings &&
              snapshot.status == AirfixWindowsRenderSettingsStatus::
                                     controllerBindingProtectedConflict &&
              snapshot.controllerBindingPickerPhase ==
                  airfix::settings::ControllerInputBindingPickerPhase::
                      choosingControl &&
              !snapshot.conflictingControllerBindingAction.has_value() &&
              !snapshot.controllerProfileDirty,
          "protected Menu assignment mutated the draft or offered Swap");

  auto custom = airfix::input::makeDefaultControllerInputProfileRecord();
  const auto mission = airfix::input::controllerDigitalGameplayBinding(
      custom, airfix::input::ControllerDigitalGameplayAction::missionStatus);
  require(mission.editable() &&
              custom.bindingCount <
                  airfix::input::controllerProfileBindingCapacity,
          "custom binding fixture could not be constructed");
  auto duplicate = custom.bindings[mission.bindingIndex];
  duplicate.control = airfix::input::controls::controller::facePrimary;
  custom.bindings[custom.bindingCount] = duplicate;
  ++custom.bindingCount;

  auto customPanel = makeControllerPanelWithProfile(custom);
  openControllerCalibration(customPanel);
  openControllerButtonBindings(customPanel);
  incrementBindingValue(customPanel,
                        AirfixWindowsRenderSettingsItem::bindingAction, 6U);
  snapshot = customPanel.snapshot();
  require(
      snapshot.selectedControllerBindingAction ==
              airfix::input::ControllerDigitalGameplayAction::missionStatus &&
          snapshot.selectedControllerBindingStatus ==
              airfix::input::ControllerDigitalGameplayBindingStatus::
                  ambiguous &&
          snapshot.selectedControllerBindingControlIndex ==
              airfix::windows::airfixWindowsControllerBindingNoControlIndex &&
          snapshot.controllerBindingPickerPhase ==
              airfix::settings::ControllerInputBindingPickerPhase::closed &&
          snapshot.status == AirfixWindowsRenderSettingsStatus::
                                 controllerBindingActionUnavailable &&
          !findItem(snapshot,
                    AirfixWindowsRenderSettingsItem::bindingAssignment)
               .enabled &&
          !findItem(snapshot, AirfixWindowsRenderSettingsItem::moveBinding)
               .enabled,
      "custom multi-binding action was guessed by the native picker");
  const auto before = customPanel.snapshot();
  require(customPanel
                  .consumePointer(click(findItem(
                      before, AirfixWindowsRenderSettingsItem::moveBinding)))
                  .empty() &&
              !customPanel.snapshot().controllerProfileDirty &&
              customPanel.screen() ==
                  AirfixWindowsRenderSettingsScreen::controllerButtonBindings,
          "disabled custom action Move mutated or left the picker");
}

void controllerBindingResetPreservesCalibrationAndOuterCancelOwnsDraft() {
  auto panel = makeControllerPanel();
  openControllerCalibration(panel);
  openLeftStickXCalibration(panel);
  static_cast<void>(panel.consumePointer(clickNext(findItem(
      panel.snapshot(), AirfixWindowsRenderSettingsItem::sensitivity))));
  activateItem(panel, AirfixWindowsRenderSettingsItem::back);
  const auto editedAxes = panel.snapshot().controllerDraftAxes;
  openControllerButtonBindings(panel);
  incrementBindingValue(
      panel, AirfixWindowsRenderSettingsItem::bindingAssignment, 11U);
  activateItem(panel, AirfixWindowsRenderSettingsItem::moveBinding);
  activateItem(panel, AirfixWindowsRenderSettingsItem::resetAllAssignments);
  auto snapshot = panel.snapshot();
  require(snapshot.controllerDraftAxes == editedAxes &&
              snapshot.selectedControllerBindingControlIndex == 0U &&
              snapshot.controllerProfileDirty,
          "Reset all assignments changed calibration or failed to restore "
          "defaults");
  activateItem(panel, AirfixWindowsRenderSettingsItem::back);
  require(panel.screen() ==
                  AirfixWindowsRenderSettingsScreen::controllerCalibration &&
              panel.snapshot().controllerProfileDirty,
          "picker Back discarded the shared controller-profile draft");
  const auto resetSave = panel.consumePointer(
      click(findItem(panel.snapshot(),
                     AirfixWindowsRenderSettingsItem::saveControllerProfile)));
  const auto defaults =
      airfix::input::makeDefaultControllerInputProfileRecord();
  require(
      resetSave.controllerProfileSaveTicket.has_value() &&
          resetSave.controllerProfileSaveTicket->candidate.axes == editedAxes &&
          resetSave.controllerProfileSaveTicket->candidate.bindings ==
              defaults.bindings,
      "reset save ticket did not preserve calibration and default bindings");

  auto cancelled = makeControllerPanel();
  openControllerCalibration(cancelled);
  openControllerButtonBindings(cancelled);
  incrementBindingValue(
      cancelled, AirfixWindowsRenderSettingsItem::bindingAssignment, 11U);
  activateItem(cancelled, AirfixWindowsRenderSettingsItem::moveBinding);
  activateItem(cancelled, AirfixWindowsRenderSettingsItem::back);
  static_cast<void>(
      cancelled.consumeInputFrame(pressedFrame(DigitalAction::uiCancel)));
  require(cancelled.screen() == AirfixWindowsRenderSettingsScreen::pause &&
              !cancelled.snapshot().controllerProfileDirty,
          "outer controller Cancel did not discard the complete shared draft");
  openControllerCalibration(cancelled);
  openControllerButtonBindings(cancelled);
  require(cancelled.snapshot().selectedControllerBindingControlIndex == 0U,
          "reopened picker retained a remap discarded by outer Cancel");
}

void controllerLayoutsFitSupportedOutputs() {
  constexpr std::array outputs{
      AirfixWindowsUiPixelExtent{
          .width = 1920U, .height = 1080U, .dpiScale = 1.0F},
      AirfixWindowsUiPixelExtent{
          .width = 2560U, .height = 1440U, .dpiScale = 1.5F},
      AirfixWindowsUiPixelExtent{
          .width = 3440U, .height = 1440U, .dpiScale = 1.0F},
      AirfixWindowsUiPixelExtent{
          .width = 640U, .height = 360U, .dpiScale = 1.0F},
  };
  for (const auto output : outputs) {
    auto panel = makeControllerPanel(true, false, output);
    requireSnapshotInsideOutput(panel.snapshot());
    openControllerCalibration(panel);
    requireSnapshotInsideOutput(panel.snapshot());
    openLeftStickXCalibration(panel);
    requireSnapshotInsideOutput(panel.snapshot());
    activateItem(panel, AirfixWindowsRenderSettingsItem::back);
    openControllerButtonBindings(panel);
    requireSnapshotInsideOutput(panel.snapshot());
    incrementBindingValue(
        panel, AirfixWindowsRenderSettingsItem::bindingAssignment, 1U);
    activateItem(panel, AirfixWindowsRenderSettingsItem::moveBinding);
    requireSnapshotInsideOutput(panel.snapshot());
  }
}

void snapshotExposesOnlyBoundedOperationalMetadata() {
  auto panel = makePanel();
  panel.setSessionOverrideMask(0xFFU);
  panel.setPersistenceAvailable(false);
  const auto snapshot = panel.snapshot();
  const auto expectedMask = static_cast<std::uint8_t>(
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
              verticalFovAdjustment) |
      static_cast<std::uint8_t>(
          AirfixWindowsRenderSettingsSessionOverride::textureMode));
  require(snapshot.sessionOverrideMask == expectedMask,
          "snapshot did not sanitize its session-override mask");
  require(!snapshot.persistenceAvailable && !snapshot.dirty &&
              !snapshot.applying &&
              snapshot.status ==
                  AirfixWindowsRenderSettingsStatus::persistenceUnavailable,
          "snapshot omitted operational availability/status fields");
}

} // namespace

int main() {
  try {
    invalidAppliedSettingsFailClosed();
    navigationUsesHysteresisAndBoundedRows();
    editsUseStepsShouldersAndBounds();
    textureModeAvailabilityAndReloadOutcomeAreExplicit();
    applyCancelAndResumeAreDistinctIntents();
    resumeRemainsDisabledWithoutReadyGameplay();
    layoutFitsDefaultSmallAndHighDpiOutputs();
    interfaceScaleUsesNativeRasterAndScrollableRows();
    pointerUsesPhysicalLayoutAndWheel();
    controllerCalibrationUsesSharedPreviewAndSavesForNextLaunch();
    controllerCalibrationCancelFailureAndRetryAreAtomic();
    controllerPersistenceCapabilitiesFailClosedAndPermitRepair();
    controllerBindingPickerMovesCancelsAndSwapsExplicitly();
    controllerBindingPickerProtectsCustomAndReservedAssignments();
    controllerBindingResetPreservesCalibrationAndOuterCancelOwnsDraft();
    controllerLayoutsFitSupportedOutputs();
    snapshotExposesOnlyBoundedOperationalMetadata();
  } catch (const std::exception &error) {
    std::cerr << "AirfixWindowsRenderSettingsPanelTests failed: "
              << error.what() << '\n';
    return 1;
  }
  return 0;
}
