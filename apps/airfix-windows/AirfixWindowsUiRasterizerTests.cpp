#include "AirfixWindowsUiRasterizer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using airfix::render::RenderPresentationSettings;
using airfix::windows::AirfixWindowsControllerAxisInputSnapshot;
using airfix::windows::AirfixWindowsControllerProfilePanelState;
using airfix::windows::AirfixWindowsRenderSettingsItem;
using airfix::windows::AirfixWindowsRenderSettingsPanel;
using airfix::windows::AirfixWindowsRenderSettingsScreen;
using airfix::windows::AirfixWindowsRenderSettingsViewItem;
using airfix::windows::AirfixWindowsRenderSettingsViewSnapshot;
using airfix::windows::AirfixWindowsUiPixelExtent;
using airfix::windows::AirfixWindowsUiRaster;
using airfix::windows::AirfixWindowsUiRasterizeIssue;
using airfix::windows::AirfixWindowsUiRasterizer;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
panel(const AirfixWindowsUiPixelExtent output) {
  auto result = AirfixWindowsRenderSettingsPanel::create(
      RenderPresentationSettings{}, true, output);
  require(result.has_value(), "valid raster panel fixture was rejected");
  return *result;
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
displaySettingsPanel(const AirfixWindowsUiPixelExtent output) {
  auto result = panel(output);
  const auto pause = result.snapshot();
  const auto &display = pause.items[0];
  const auto intent = result.consumePointer({
      .xPixels = display.bounds.x + display.bounds.width * 0.5F,
      .yPixels = display.bounds.y + display.bounds.height * 0.5F,
      .wheelY = 0,
      .primaryPressed = true,
  });
  require(intent.empty(), "opening Display settings emitted an intent");
  return result;
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
controllerAxisPanel(const AirfixWindowsUiPixelExtent output) {
  const auto profile = airfix::input::makeDefaultControllerInputProfileRecord();
  auto created = AirfixWindowsRenderSettingsPanel::create(
      RenderPresentationSettings{}, true, output, 0U, false,
      AirfixWindowsControllerProfilePanelState{
          .active = profile,
          .persisted = profile,
          .capabilities =
              {
                  .persistenceAvailable = true,
                  .repairRequired = false,
              },
      });
  require(created.has_value(),
          "valid controller raster panel fixture was rejected");
  auto result = *created;
  const auto activate = [&](const AirfixWindowsRenderSettingsItem item) {
    const auto snapshot = result.snapshot();
    const auto found = std::find_if(
        snapshot.items.begin(), snapshot.items.begin() + snapshot.itemCount,
        [item](const auto &candidate) { return candidate.item == item; });
    require(found != snapshot.items.begin() + snapshot.itemCount,
            "controller raster fixture item was not found");
    const auto intent = result.consumePointer({
        .xPixels = found->bounds.x + found->bounds.width * 0.5F,
        .yPixels = found->bounds.y + found->bounds.height * 0.5F,
        .wheelY = 0,
        .primaryPressed = true,
    });
    require(intent.empty(), "controller raster navigation emitted an intent");
  };
  activate(AirfixWindowsRenderSettingsItem::controllerCalibration);
  activate(AirfixWindowsRenderSettingsItem::leftStickX);
  AirfixWindowsControllerAxisInputSnapshot input{};
  input.rawAxes[0] = 19661;
  input.connected = true;
  result.setControllerAxisInput(input);
  return result;
}

[[nodiscard]] const AirfixWindowsRenderSettingsViewItem &
findItem(const AirfixWindowsRenderSettingsViewSnapshot &snapshot,
         const AirfixWindowsRenderSettingsItem item) {
  const auto found = std::find_if(
      snapshot.items.begin(), snapshot.items.begin() + snapshot.itemCount,
      [item](const auto &candidate) { return candidate.item == item; });
  require(found != snapshot.items.begin() + snapshot.itemCount,
          "raster fixture item was not found");
  return *found;
}

void activate(AirfixWindowsRenderSettingsPanel &panel,
              const AirfixWindowsRenderSettingsItem item) {
  const auto snapshot = panel.snapshot();
  const auto &found = findItem(snapshot, item);
  const auto intent = panel.consumePointer({
      .xPixels = found.bounds.x + found.bounds.width * 0.5F,
      .yPixels = found.bounds.y + found.bounds.height * 0.5F,
      .wheelY = 0,
      .primaryPressed = true,
  });
  require(intent.empty(), "raster fixture navigation emitted an intent");
}

void increment(AirfixWindowsRenderSettingsPanel &panel,
               const AirfixWindowsRenderSettingsItem item,
               const std::size_t count = 1U) {
  for (std::size_t index = 0U; index < count; ++index) {
    const auto snapshot = panel.snapshot();
    const auto &found = findItem(snapshot, item);
    const auto intent = panel.consumePointer({
        .xPixels = found.nextBounds.x + found.nextBounds.width * 0.5F,
        .yPixels = found.nextBounds.y + found.nextBounds.height * 0.5F,
        .wheelY = 0,
        .primaryPressed = true,
    });
    require(intent.empty(), "raster fixture carousel emitted an intent");
  }
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
controllerBindingsPanelWithProfile(
    const AirfixWindowsUiPixelExtent output,
    const airfix::input::ControllerInputProfileRecord &profile) {
  auto created = AirfixWindowsRenderSettingsPanel::create(
      RenderPresentationSettings{}, true, output, 0U, false,
      AirfixWindowsControllerProfilePanelState{
          .active = profile,
          .persisted = profile,
          .capabilities =
              {
                  .persistenceAvailable = true,
                  .repairRequired = false,
              },
      });
  require(created.has_value(),
          "valid binding raster panel fixture was rejected");
  auto result = *created;
  activate(result, AirfixWindowsRenderSettingsItem::controllerCalibration);
  activate(result, AirfixWindowsRenderSettingsItem::buttonBindings);
  require(result.screen() ==
              AirfixWindowsRenderSettingsScreen::controllerButtonBindings,
          "binding raster picker did not open");
  return result;
}

[[nodiscard]] AirfixWindowsRenderSettingsPanel
controllerBindingsPanel(const AirfixWindowsUiPixelExtent output) {
  return controllerBindingsPanelWithProfile(
      output, airfix::input::makeDefaultControllerInputProfileRecord());
}

[[nodiscard]] bool containsBytes(const AirfixWindowsUiRaster &raster,
                                 const std::string_view needle) {
  if (needle.empty() || needle.size() > raster.premultipliedBgra8.size()) {
    return false;
  }
  return std::search(raster.premultipliedBgra8.begin(),
                     raster.premultipliedBgra8.end(), needle.begin(),
                     needle.end()) != raster.premultipliedBgra8.end();
}

void requireValidPremultipliedRaster(const AirfixWindowsUiRaster &raster,
                                     const std::uint32_t expectedWidth,
                                     const std::uint32_t expectedHeight) {
  require(raster.complete(), "raster did not report a complete BGRA8 image");
  require(raster.width == expectedWidth && raster.height == expectedHeight &&
              raster.rowPitchBytes == expectedWidth * 4U,
          "raster dimensions or row pitch did not match the physical output");

  bool foundVisiblePixel = false;
  bool foundColorVariation = false;
  const auto &bytes = raster.premultipliedBgra8;
  const std::array<std::uint8_t, 4U> first{bytes[0], bytes[1], bytes[2],
                                           bytes[3]};
  for (std::size_t index = 0U; index + 3U < bytes.size(); index += 4U) {
    const auto blue = bytes[index];
    const auto green = bytes[index + 1U];
    const auto red = bytes[index + 2U];
    const auto alpha = bytes[index + 3U];
    require(blue <= alpha && green <= alpha && red <= alpha,
            "raster contained a non-premultiplied BGRA pixel");
    foundVisiblePixel = foundVisiblePixel || alpha != 0U;
    foundColorVariation = foundColorVariation || blue != first[0] ||
                          green != first[1] || red != first[2] ||
                          alpha != first[3];
  }
  require(foundVisiblePixel, "raster was fully transparent");
  require(foundColorVariation, "raster contained no visible panel content");

  // The public raster contract cannot accept arbitrary caller strings. These
  // checks guard against accidentally embedding common path/checksum markers
  // into a future intermediate surface.
  require(!containsBytes(raster, "C:\\Users\\Private\\settings"),
          "raster leaked a private filesystem path");
  require(!containsBytes(raster, "sha256:private-content-checksum"),
          "raster leaked a private checksum");
}

void rasterizesFullHdAndUltrawideAtDpiScale() {
  AirfixWindowsUiRasterizer rasterizer;

  auto fullHdPanel = panel({.width = 1920U, .height = 1080U, .dpiScale = 1.0F});
  const auto fullHd = rasterizer.rasterize(fullHdPanel.snapshot());
  require(fullHd.complete(), "1920x1080 rasterization failed");
  requireValidPremultipliedRaster(*fullHd.raster, 1920U, 1080U);

  auto ultrawidePanel =
      panel({.width = 3440U, .height = 1440U, .dpiScale = 1.5F});
  const auto ultrawide = rasterizer.rasterize(ultrawidePanel.snapshot());
  require(ultrawide.complete(), "ultrawide DPI rasterization failed");
  requireValidPremultipliedRaster(*ultrawide.raster, 3440U, 1440U);
}

void rasterizesAdaptiveSettingsAtSupportedSmallOutputs() {
  AirfixWindowsUiRasterizer rasterizer;
  constexpr std::array outputs{
      AirfixWindowsUiPixelExtent{
          .width = 960U, .height = 540U, .dpiScale = 1.0F},
      AirfixWindowsUiPixelExtent{
          .width = 1280U, .height = 720U, .dpiScale = 1.5F},
      AirfixWindowsUiPixelExtent{
          .width = 640U, .height = 360U, .dpiScale = 1.0F},
  };
  for (const auto output : outputs) {
    auto subject = displaySettingsPanel(output);
    const auto raster = rasterizer.rasterize(subject.snapshot());
    require(raster.complete(),
            "adaptive Display settings rasterization failed");
    requireValidPremultipliedRaster(*raster.raster, output.width,
                                    output.height);
  }
}

void rasterizesControllerCalibrationPreviewAtRepresentativeOutputs() {
  AirfixWindowsUiRasterizer rasterizer;
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
    auto subject = controllerAxisPanel(output);
    const auto raster = rasterizer.rasterize(subject.snapshot());
    require(raster.complete(),
            "controller calibration preview rasterization failed");
    requireValidPremultipliedRaster(*raster.raster, output.width,
                                    output.height);
  }
}

void rasterizesTextOnlyBindingPickerConflictAndProtectedStates() {
  AirfixWindowsUiRasterizer rasterizer;
  constexpr std::array outputs{
      AirfixWindowsUiPixelExtent{
          .width = 1920U, .height = 1080U, .dpiScale = 1.0F},
      AirfixWindowsUiPixelExtent{
          .width = 640U, .height = 360U, .dpiScale = 1.0F},
  };
  for (const auto output : outputs) {
    auto picker = controllerBindingsPanel(output);
    const auto initial = rasterizer.rasterize(picker.snapshot());
    require(initial.complete(), "text-only binding picker failed to rasterize");
    requireValidPremultipliedRaster(*initial.raster, output.width,
                                    output.height);

    increment(picker, AirfixWindowsRenderSettingsItem::bindingAction);
    const auto nextAction = rasterizer.rasterize(picker.snapshot());
    require(nextAction.complete() && nextAction.raster->premultipliedBgra8 !=
                                         initial.raster->premultipliedBgra8,
            "text action/assignment names did not affect the picker raster");

    auto conflict = controllerBindingsPanel(output);
    increment(conflict, AirfixWindowsRenderSettingsItem::bindingAssignment);
    activate(conflict, AirfixWindowsRenderSettingsItem::moveBinding);
    require(conflict.screen() ==
                AirfixWindowsRenderSettingsScreen::controllerBindingConflict,
            "conflict raster fixture did not enter confirmation");
    const auto conflictRaster = rasterizer.rasterize(conflict.snapshot());
    require(conflictRaster.complete(),
            "cancel-first binding conflict failed to rasterize");
    requireValidPremultipliedRaster(*conflictRaster.raster, output.width,
                                    output.height);

    auto protectedPanel = controllerBindingsPanel(output);
    increment(protectedPanel,
              AirfixWindowsRenderSettingsItem::bindingAssignment, 13U);
    activate(protectedPanel, AirfixWindowsRenderSettingsItem::moveBinding);
    const auto protectedRaster =
        rasterizer.rasterize(protectedPanel.snapshot());
    require(protectedRaster.complete(),
            "protected assignment status failed to rasterize");
    requireValidPremultipliedRaster(*protectedRaster.raster, output.width,
                                    output.height);
  }
}

void invalidDimensionsFailClosed() {
  auto subject = panel({.width = 1920U, .height = 1080U, .dpiScale = 1.0F});
  auto snapshot = subject.snapshot();
  snapshot.output.width = 0U;
  const auto result = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!result.complete() && result.issue.has_value() &&
              *result.issue == AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "zero-width raster snapshot did not fail closed");

  snapshot = subject.snapshot();
  snapshot.panelBounds.y = -1.0F;
  const auto escaped = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!escaped.complete() && escaped.issue.has_value() &&
              *escaped.issue == AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "out-of-output UI geometry did not fail closed");

  auto controller =
      controllerAxisPanel({.width = 1920U, .height = 1080U, .dpiScale = 1.0F});
  snapshot = controller.snapshot();
  snapshot.controllerDraftAxes[0].outerSaturationQ15 =
      snapshot.controllerDraftAxes[0].innerDeadzoneQ15;
  const auto invalidCalibration =
      AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!invalidCalibration.complete() &&
              invalidCalibration.issue.has_value() &&
              *invalidCalibration.issue ==
                  AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "invalid controller calibration snapshot did not fail closed");

  snapshot = controller.snapshot();
  snapshot.selectedControllerAxis = airfix::input::ControllerAxisElement::count;
  const auto invalidAxis = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!invalidAxis.complete() && invalidAxis.issue.has_value() &&
              *invalidAxis.issue ==
                  AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "invalid controller axis snapshot did not fail closed");

  auto bindings = controllerBindingsPanel(
      {.width = 1920U, .height = 1080U, .dpiScale = 1.0F});
  snapshot = bindings.snapshot();
  snapshot.selectedControllerBindingAction =
      airfix::input::ControllerDigitalGameplayAction::count;
  const auto invalidAction = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!invalidAction.complete() && invalidAction.issue.has_value() &&
              *invalidAction.issue ==
                  AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "out-of-catalog binding action did not fail closed");

  snapshot = bindings.snapshot();
  snapshot.selectedControllerBindingControlIndex = static_cast<std::uint8_t>(
      airfix::input::controllerAssignableControlCount);
  const auto invalidControl = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!invalidControl.complete() && invalidControl.issue.has_value() &&
              *invalidControl.issue ==
                  AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "editable binding with no control did not fail closed");

  snapshot = bindings.snapshot();
  snapshot.controllerBindingPickerPhase =
      airfix::settings::ControllerInputBindingPickerPhase::confirmingSwap;
  const auto invalidPhase = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(!invalidPhase.complete() && invalidPhase.issue.has_value() &&
              *invalidPhase.issue ==
                  AirfixWindowsUiRasterizeIssue::invalidSnapshot,
          "inconsistent binding picker phase did not fail closed");

  increment(bindings, AirfixWindowsRenderSettingsItem::bindingAssignment);
  activate(bindings, AirfixWindowsRenderSettingsItem::moveBinding);
  snapshot = bindings.snapshot();
  snapshot.conflictingControllerBindingAction.reset();
  const auto missingConflict = AirfixWindowsUiRasterizer{}.rasterize(snapshot);
  require(
      !missingConflict.complete() && missingConflict.issue.has_value() &&
          *missingConflict.issue ==
              AirfixWindowsUiRasterizeIssue::invalidSnapshot,
      "conflict screen without typed conflicting action did not fail closed");
}

} // namespace

int main() {
  try {
    rasterizesFullHdAndUltrawideAtDpiScale();
    rasterizesAdaptiveSettingsAtSupportedSmallOutputs();
    rasterizesControllerCalibrationPreviewAtRepresentativeOutputs();
    rasterizesTextOnlyBindingPickerConflictAndProtectedStates();
    invalidDimensionsFailClosed();
  } catch (const std::exception &error) {
    std::cerr << "AirfixWindowsUiRasterizerTests failed: " << error.what()
              << '\n';
    return 1;
  }
  return 0;
}
