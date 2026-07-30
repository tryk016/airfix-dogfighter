#include "AirfixWindowsRenderSettingsPanel.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::DigitalAction;
using airfix::input::InputFrame;
using airfix::render::RenderPresentationSettings;
using airfix::windows::AirfixWindowsPointerInput;
using airfix::windows::AirfixWindowsRenderSettingsItem;
using airfix::windows::AirfixWindowsRenderSettingsPanel;
using airfix::windows::AirfixWindowsRenderSettingsScreen;
using airfix::windows::AirfixWindowsRenderSettingsSessionOverride;
using airfix::windows::AirfixWindowsRenderSettingsStatus;
using airfix::windows::AirfixWindowsRenderSettingsViewItem;
using airfix::windows::AirfixWindowsRenderSettingsViewSnapshot;
using airfix::windows::AirfixWindowsUiPixelExtent;

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
          const AirfixWindowsUiPixelExtent output = {}) {
  RenderPresentationSettings settings;
  settings.renderScalePercent = scale;
  auto panel = AirfixWindowsRenderSettingsPanel::create(
      settings, persistenceAvailable, output);
  require(panel.has_value(), "valid panel fixture was rejected");
  return *panel;
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
  auto panel = makePanel(101.0F);
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
  require(panel.snapshot().draftSettings.scenePresentation ==
              airfix::render::ScenePresentationMode::originalFourByThree,
          "next shoulder did not select Original 4:3");
  move(panel, 1);
  static_cast<void>(
      panel.consumeInputFrame(pressedFrame(DigitalAction::uiConfirm)));
  require(panel.snapshot().draftSettings.visualProfile ==
              airfix::render::VisualProfile::enhanced,
          "confirm did not toggle the visual profile preview");
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
  for (std::uint8_t index = 0U; index < 4U; ++index) {
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
  for (std::uint8_t index = 0U; index < 5U; ++index) {
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
          AirfixWindowsRenderSettingsSessionOverride::rendererStatistics));
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
    applyCancelAndResumeAreDistinctIntents();
    resumeRemainsDisabledWithoutReadyGameplay();
    layoutFitsDefaultSmallAndHighDpiOutputs();
    pointerUsesPhysicalLayoutAndWheel();
    snapshotExposesOnlyBoundedOperationalMetadata();
  } catch (const std::exception &error) {
    std::cerr << "AirfixWindowsRenderSettingsPanelTests failed: "
              << error.what() << '\n';
    return 1;
  }
  return 0;
}
