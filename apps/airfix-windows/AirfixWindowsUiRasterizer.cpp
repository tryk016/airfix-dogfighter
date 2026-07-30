#include "AirfixWindowsUiRasterizer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace airfix::windows {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t maximumRasterDimension = 16384U;
constexpr std::uint64_t maximumRasterBytes =
    static_cast<std::uint64_t>(maximumRasterDimension) * 8192U * 4U;

class ComApartment final {
public:
  ComApartment() noexcept {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    available_ = SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    release_ = SUCCEEDED(result);
  }

  ~ComApartment() {
    if (release_) {
      CoUninitialize();
    }
  }

  ComApartment(const ComApartment &) = delete;
  ComApartment &operator=(const ComApartment &) = delete;

  [[nodiscard]] bool available() const noexcept { return available_; }

private:
  bool available_{};
  bool release_{};
};

[[nodiscard]] constexpr D2D1_RECT_F
d2dRect(const AirfixWindowsUiPixelRect rect) noexcept {
  return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
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

[[nodiscard]] bool validSnapshot(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  const auto screenValue = static_cast<std::uint8_t>(snapshot.screen);
  const auto selectedItemValue =
      static_cast<std::uint8_t>(snapshot.selectedItem);
  const auto statusValue = static_cast<std::uint8_t>(snapshot.status);
  if (snapshot.output.width == 0U || snapshot.output.height == 0U ||
      snapshot.output.width > maximumRasterDimension ||
      snapshot.output.height > maximumRasterDimension ||
      !std::isfinite(snapshot.output.dpiScale) ||
      snapshot.output.dpiScale <= 0.0F ||
      !std::isfinite(snapshot.layoutScale) || snapshot.layoutScale <= 0.0F ||
      screenValue >
          static_cast<std::uint8_t>(
              AirfixWindowsRenderSettingsScreen::controllerAxisCalibration) ||
      selectedItemValue >=
          static_cast<std::uint8_t>(AirfixWindowsRenderSettingsItem::count) ||
      statusValue >
          static_cast<std::uint8_t>(
              AirfixWindowsRenderSettingsStatus::invalidControllerProfile) ||
      static_cast<std::size_t>(snapshot.itemCount) > snapshot.items.size() ||
      !rectInsideOutput(snapshot.panelBounds, snapshot.output) ||
      !rectInsideOutput(snapshot.titleBounds, snapshot.output) ||
      !rectInsideOutput(snapshot.statusBounds, snapshot.output)) {
    return false;
  }
  const bool controllerScreen =
      snapshot.screen ==
          AirfixWindowsRenderSettingsScreen::controllerCalibration ||
      snapshot.screen ==
          AirfixWindowsRenderSettingsScreen::controllerAxisCalibration;
  const auto axisIndex =
      static_cast<std::size_t>(snapshot.selectedControllerAxis);
  if ((controllerScreen && !snapshot.controllerProfileAvailable) ||
      axisIndex >= snapshot.controllerDraftAxes.size() ||
      snapshot.controllerPreviewRaw < -input::q15One ||
      snapshot.controllerPreviewEffective < -input::q15One) {
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
  const auto byteCount = static_cast<std::uint64_t>(snapshot.output.width) *
                         static_cast<std::uint64_t>(snapshot.output.height) *
                         4U;
  if (byteCount > maximumRasterBytes ||
      byteCount > std::numeric_limits<UINT>::max()) {
    return false;
  }
  for (std::uint8_t index = 0U; index < snapshot.itemCount; ++index) {
    if (snapshot.items[index].item >= AirfixWindowsRenderSettingsItem::count ||
        !rectInsideOutput(snapshot.items[index].bounds, snapshot.output) ||
        !rectInsideOutput(snapshot.items[index].previousBounds,
                          snapshot.output) ||
        !rectInsideOutput(snapshot.items[index].nextBounds, snapshot.output)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] AirfixWindowsUiRasterizeResult
failure(const AirfixWindowsUiRasterizeIssue issue) noexcept {
  return {.raster = std::nullopt, .issue = issue};
}

[[nodiscard]] HRESULT
createWicFactory(ComPtr<IWICImagingFactory> &factory) noexcept {
  return CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
}

[[nodiscard]] HRESULT
createDWriteFactory(ComPtr<IDWriteFactory> &factory) noexcept {
  return DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown **>(factory.GetAddressOf()));
}

[[nodiscard]] const wchar_t *
itemLabel(const AirfixWindowsRenderSettingsItem item) noexcept {
  switch (item) {
  case AirfixWindowsRenderSettingsItem::displaySettings:
    return L"Display settings";
  case AirfixWindowsRenderSettingsItem::controllerCalibration:
    return L"Controller calibration";
  case AirfixWindowsRenderSettingsItem::resume:
    return L"Resume";
  case AirfixWindowsRenderSettingsItem::renderScale:
    return L"Render scale";
  case AirfixWindowsRenderSettingsItem::presentation:
    return L"Presentation";
  case AirfixWindowsRenderSettingsItem::visualProfile:
    return L"Visual profile";
  case AirfixWindowsRenderSettingsItem::rendererStatistics:
    return L"Renderer statistics";
  case AirfixWindowsRenderSettingsItem::apply:
    return L"Apply";
  case AirfixWindowsRenderSettingsItem::cancel:
    return L"Cancel";
  case AirfixWindowsRenderSettingsItem::leftStickX:
    return L"Left stick X";
  case AirfixWindowsRenderSettingsItem::leftStickY:
    return L"Left stick Y";
  case AirfixWindowsRenderSettingsItem::rightStickX:
    return L"Right stick X";
  case AirfixWindowsRenderSettingsItem::rightStickY:
    return L"Right stick Y";
  case AirfixWindowsRenderSettingsItem::innerDeadzone:
    return L"Inner deadzone";
  case AirfixWindowsRenderSettingsItem::outerSaturation:
    return L"Outer saturation";
  case AirfixWindowsRenderSettingsItem::sensitivity:
    return L"Sensitivity";
  case AirfixWindowsRenderSettingsItem::responseCurve:
    return L"Response curve";
  case AirfixWindowsRenderSettingsItem::inversion:
    return L"Invert axis";
  case AirfixWindowsRenderSettingsItem::resetAxis:
    return L"Reset selected axis";
  case AirfixWindowsRenderSettingsItem::resetAllCalibration:
    return L"Reset all calibration";
  case AirfixWindowsRenderSettingsItem::saveControllerProfile:
    return L"Save for next launch";
  case AirfixWindowsRenderSettingsItem::back:
    return L"Back";
  case AirfixWindowsRenderSettingsItem::count:
    break;
  }
  return L"";
}

[[nodiscard]] constexpr bool
isValueItem(const AirfixWindowsRenderSettingsItem item) noexcept {
  switch (item) {
  case AirfixWindowsRenderSettingsItem::renderScale:
  case AirfixWindowsRenderSettingsItem::presentation:
  case AirfixWindowsRenderSettingsItem::visualProfile:
  case AirfixWindowsRenderSettingsItem::rendererStatistics:
  case AirfixWindowsRenderSettingsItem::innerDeadzone:
  case AirfixWindowsRenderSettingsItem::outerSaturation:
  case AirfixWindowsRenderSettingsItem::sensitivity:
  case AirfixWindowsRenderSettingsItem::responseCurve:
  case AirfixWindowsRenderSettingsItem::inversion:
    return true;
  case AirfixWindowsRenderSettingsItem::displaySettings:
  case AirfixWindowsRenderSettingsItem::controllerCalibration:
  case AirfixWindowsRenderSettingsItem::resume:
  case AirfixWindowsRenderSettingsItem::apply:
  case AirfixWindowsRenderSettingsItem::cancel:
  case AirfixWindowsRenderSettingsItem::leftStickX:
  case AirfixWindowsRenderSettingsItem::leftStickY:
  case AirfixWindowsRenderSettingsItem::rightStickX:
  case AirfixWindowsRenderSettingsItem::rightStickY:
  case AirfixWindowsRenderSettingsItem::resetAxis:
  case AirfixWindowsRenderSettingsItem::resetAllCalibration:
  case AirfixWindowsRenderSettingsItem::saveControllerProfile:
  case AirfixWindowsRenderSettingsItem::back:
  case AirfixWindowsRenderSettingsItem::count:
    return false;
  }
  return false;
}

[[nodiscard]] constexpr bool
hasChevron(const AirfixWindowsRenderSettingsItem item) noexcept {
  switch (item) {
  case AirfixWindowsRenderSettingsItem::displaySettings:
  case AirfixWindowsRenderSettingsItem::controllerCalibration:
  case AirfixWindowsRenderSettingsItem::leftStickX:
  case AirfixWindowsRenderSettingsItem::leftStickY:
  case AirfixWindowsRenderSettingsItem::rightStickX:
  case AirfixWindowsRenderSettingsItem::rightStickY:
  case AirfixWindowsRenderSettingsItem::back:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] const wchar_t *
itemValue(const AirfixWindowsRenderSettingsItem item,
          const AirfixWindowsRenderSettingsViewSnapshot &snapshot,
          std::array<wchar_t, 32U> &scratch) noexcept {
  const auto &draft = snapshot.draftSettings;
  switch (item) {
  case AirfixWindowsRenderSettingsItem::renderScale:
    static_cast<void>(
        swprintf_s(scratch.data(), scratch.size(), L"%.0f%%",
                   static_cast<double>(draft.renderScalePercent)));
    return scratch.data();
  case AirfixWindowsRenderSettingsItem::presentation:
    return draft.scenePresentation ==
                   render::ScenePresentationMode::originalFourByThree
               ? L"Original 4:3"
               : L"Hor+";
  case AirfixWindowsRenderSettingsItem::visualProfile:
    return draft.visualProfile == render::VisualProfile::enhanced
               ? L"Enhanced preview"
               : L"Classic";
  case AirfixWindowsRenderSettingsItem::rendererStatistics:
    return draft.diagnosticsOverlayEnabled ? L"On" : L"Off";
  case AirfixWindowsRenderSettingsItem::innerDeadzone:
  case AirfixWindowsRenderSettingsItem::outerSaturation:
  case AirfixWindowsRenderSettingsItem::sensitivity:
  case AirfixWindowsRenderSettingsItem::responseCurve:
  case AirfixWindowsRenderSettingsItem::inversion: {
    const auto axis = static_cast<std::size_t>(snapshot.selectedControllerAxis);
    if (!snapshot.controllerProfileAvailable ||
        axis >= snapshot.controllerDraftAxes.size()) {
      return L"Unavailable";
    }
    const auto &calibration = snapshot.controllerDraftAxes[axis];
    if (item == AirfixWindowsRenderSettingsItem::innerDeadzone ||
        item == AirfixWindowsRenderSettingsItem::outerSaturation) {
      const auto value = item == AirfixWindowsRenderSettingsItem::innerDeadzone
                             ? calibration.innerDeadzoneQ15
                             : calibration.outerSaturationQ15;
      static_cast<void>(swprintf_s(scratch.data(), scratch.size(), L"%.1f%%",
                                   static_cast<double>(value) * 100.0 /
                                       static_cast<double>(input::q15One)));
      return scratch.data();
    }
    if (item == AirfixWindowsRenderSettingsItem::sensitivity) {
      static_cast<void>(swprintf_s(
          scratch.data(), scratch.size(), L"%.0f%%",
          static_cast<double>(calibration.sensitivityPermille) / 10.0));
      return scratch.data();
    }
    if (item == AirfixWindowsRenderSettingsItem::responseCurve) {
      switch (calibration.responseCurve) {
      case input::ControllerResponseCurve::linear:
        return L"Linear";
      case input::ControllerResponseCurve::squared:
        return L"Squared";
      case input::ControllerResponseCurve::cubic:
        return L"Cubic";
      case input::ControllerResponseCurve::count:
        return L"Invalid";
      }
    }
    return calibration.inverted != 0U ? L"On" : L"Off";
  }
  case AirfixWindowsRenderSettingsItem::displaySettings:
  case AirfixWindowsRenderSettingsItem::controllerCalibration:
  case AirfixWindowsRenderSettingsItem::resume:
  case AirfixWindowsRenderSettingsItem::apply:
  case AirfixWindowsRenderSettingsItem::cancel:
  case AirfixWindowsRenderSettingsItem::leftStickX:
  case AirfixWindowsRenderSettingsItem::leftStickY:
  case AirfixWindowsRenderSettingsItem::rightStickX:
  case AirfixWindowsRenderSettingsItem::rightStickY:
  case AirfixWindowsRenderSettingsItem::resetAxis:
  case AirfixWindowsRenderSettingsItem::resetAllCalibration:
  case AirfixWindowsRenderSettingsItem::saveControllerProfile:
  case AirfixWindowsRenderSettingsItem::back:
  case AirfixWindowsRenderSettingsItem::count:
    break;
  }
  return L"";
}

[[nodiscard]] const wchar_t *
titleText(const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  switch (snapshot.screen) {
  case AirfixWindowsRenderSettingsScreen::pause:
    return L"Paused";
  case AirfixWindowsRenderSettingsScreen::displaySettings:
    return L"Display settings";
  case AirfixWindowsRenderSettingsScreen::controllerCalibration:
    return L"Controller calibration";
  case AirfixWindowsRenderSettingsScreen::controllerAxisCalibration:
    switch (snapshot.selectedControllerAxis) {
    case input::ControllerAxisElement::leftStickX:
      return L"Left stick X calibration";
    case input::ControllerAxisElement::leftStickY:
      return L"Left stick Y calibration";
    case input::ControllerAxisElement::rightStickX:
      return L"Right stick X calibration";
    case input::ControllerAxisElement::rightStickY:
      return L"Right stick Y calibration";
    case input::ControllerAxisElement::count:
      break;
    }
    break;
  }
  return L"";
}

[[nodiscard]] constexpr bool
statusIsWarning(const AirfixWindowsRenderSettingsStatus status) noexcept {
  switch (status) {
  case AirfixWindowsRenderSettingsStatus::applyFailed:
  case AirfixWindowsRenderSettingsStatus::persistenceUnavailable:
  case AirfixWindowsRenderSettingsStatus::invalidSettings:
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaveFailed:
  case AirfixWindowsRenderSettingsStatus::
      controllerProfilePersistenceUnavailable:
  case AirfixWindowsRenderSettingsStatus::invalidControllerProfile:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] const wchar_t *
statusText(const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
  switch (snapshot.status) {
  case AirfixWindowsRenderSettingsStatus::ready:
    if (snapshot.sessionOverrideMask != 0U) {
      return L"Session overrides are active";
    }
    if (snapshot.screen == AirfixWindowsRenderSettingsScreen::displaySettings) {
      return snapshot.dirty ? L"Changes are ready to apply"
                            : L"Presentation only - gameplay is unchanged";
    }
    return L"Paused";
  case AirfixWindowsRenderSettingsStatus::noChanges:
    return L"No display changes to apply";
  case AirfixWindowsRenderSettingsStatus::applying:
    return L"Applying display settings...";
  case AirfixWindowsRenderSettingsStatus::applied:
    return L"Display settings applied";
  case AirfixWindowsRenderSettingsStatus::applyFailed:
    return L"Display settings were not changed";
  case AirfixWindowsRenderSettingsStatus::persistenceUnavailable:
    return L"Display settings cannot be saved";
  case AirfixWindowsRenderSettingsStatus::invalidSettings:
    return L"The selected display settings are invalid";
  case AirfixWindowsRenderSettingsStatus::controllerProfileReady:
    if (snapshot.screen ==
        AirfixWindowsRenderSettingsScreen::controllerAxisCalibration) {
      return snapshot.controllerConnected
                 ? L"Live preview uses the same transform as gameplay"
                 : L"Connect a controller to preview this axis";
    }
    if (snapshot.controllerProfileRepairRequired &&
        !snapshot.controllerProfileDirty) {
      return L"Recovered profile is ready to repair";
    }
    return snapshot.controllerProfileDirty
               ? L"Calibration changes are ready to save"
               : L"Choose an axis to inspect or adjust";
  case AirfixWindowsRenderSettingsStatus::controllerProfileNoChanges:
    return L"No controller calibration changes to save";
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaving:
    return L"Saving controller profile...";
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaved:
    return L"Controller profile repaired";
  case AirfixWindowsRenderSettingsStatus::controllerProfileSavedRestartRequired:
    return L"Saved - controller changes take effect after restart";
  case AirfixWindowsRenderSettingsStatus::controllerProfileSaveFailed:
    return L"Controller profile was not saved - retry is available";
  case AirfixWindowsRenderSettingsStatus::
      controllerProfilePersistenceUnavailable:
    return L"Controller calibration cannot be saved";
  case AirfixWindowsRenderSettingsStatus::invalidControllerProfile:
    return L"The selected controller calibration is invalid";
  }
  return L"";
}

[[nodiscard]] D2D1_COLOR_F color(const std::uint32_t rgb,
                                 const float alpha = 1.0F) noexcept {
  return D2D1::ColorF(rgb, alpha);
}

class DrawingContext final {
public:
  DrawingContext(ID2D1RenderTarget *target, IDWriteFactory *writeFactory,
                 const float dpi) noexcept
      : target_(target), writeFactory_(writeFactory), dpi_(dpi) {}

  [[nodiscard]] HRESULT initialize() noexcept {
    HRESULT result =
        target_->CreateSolidColorBrush(color(0xF4F7FF), text_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result =
        target_->CreateSolidColorBrush(color(0xAAB4C8), muted_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result =
        target_->CreateSolidColorBrush(color(0xF4BC46), accent_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = target_->CreateSolidColorBrush(color(0x192236, 0.96F),
                                            panel_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = target_->CreateSolidColorBrush(color(0x253149, 0.88F),
                                            row_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = target_->CreateSolidColorBrush(color(0x33425F, 0.90F),
                                            selected_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = target_->CreateSolidColorBrush(color(0x0B1020, 0.96F),
                                            background_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = target_->CreateSolidColorBrush(color(0x8390A8, 0.55F),
                                            disabled_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }

    result = createFormat(36.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                          titleFormat_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = createFormat(21.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                          rowFormat_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = createFormat(18.0F, DWRITE_FONT_WEIGHT_NORMAL,
                          valueFormat_.GetAddressOf());
    if (FAILED(result)) {
      return result;
    }
    result = createFormat(15.0F, DWRITE_FONT_WEIGHT_NORMAL,
                          statusFormat_.GetAddressOf());
    return result;
  }

  void draw(const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
    target_->Clear(color(0x070B14, 0.97F));
    const auto outputRect =
        D2D1::RectF(0.0F, 0.0F, static_cast<float>(snapshot.output.width),
                    static_cast<float>(snapshot.output.height));
    target_->FillRectangle(outputRect, background_.Get());

    const float radius = 18.0F * dpi_;
    const auto panelRound =
        D2D1::RoundedRect(d2dRect(snapshot.panelBounds), radius, radius);
    target_->FillRoundedRectangle(panelRound, panel_.Get());
    target_->DrawRoundedRectangle(panelRound, muted_.Get(),
                                  std::max(1.0F, dpi_));

    const float accentWidth = 4.0F * dpi_;
    target_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(snapshot.panelBounds.x,
                                      snapshot.panelBounds.y + 24.0F * dpi_,
                                      snapshot.panelBounds.x + accentWidth,
                                      snapshot.panelBounds.y +
                                          snapshot.panelBounds.height -
                                          24.0F * dpi_),
                          accentWidth * 0.5F, accentWidth * 0.5F),
        accent_.Get());

    drawText(titleText(snapshot), titleFormat_.Get(), snapshot.titleBounds,
             text_.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

    for (std::uint8_t index = 0U; index < snapshot.itemCount; ++index) {
      drawItem(snapshot.items[index], snapshot);
    }

    auto statusBounds = snapshot.statusBounds;
    if (snapshot.screen ==
        AirfixWindowsRenderSettingsScreen::controllerAxisCalibration) {
      drawControllerAxisPreview(snapshot);
      const float previewHeight =
          std::max(0.0F, statusBounds.height - 31.0F * dpi_);
      statusBounds.y += previewHeight;
      statusBounds.height -= previewHeight;
    }
    drawText(statusText(snapshot), statusFormat_.Get(), statusBounds,
             statusIsWarning(snapshot.status) ? accent_.Get() : muted_.Get(),
             DWRITE_TEXT_ALIGNMENT_CENTER);
  }

private:
  [[nodiscard]] HRESULT createFormat(const float size,
                                     const DWRITE_FONT_WEIGHT weight,
                                     IDWriteTextFormat **format) noexcept {
    const HRESULT result = writeFactory_->CreateTextFormat(
        L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size * dpi_, L"en-US", format);
    if (SUCCEEDED(result)) {
      (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return result;
  }

  void drawText(const wchar_t *text, IDWriteTextFormat *format,
                const AirfixWindowsUiPixelRect bounds, ID2D1Brush *brush,
                const DWRITE_TEXT_ALIGNMENT alignment) noexcept {
    format->SetTextAlignment(alignment);
    const auto length = static_cast<UINT32>(std::wcslen(text));
    target_->DrawText(text, length, format, d2dRect(bounds), brush,
                      D2D1_DRAW_TEXT_OPTIONS_CLIP,
                      DWRITE_MEASURING_MODE_NATURAL);
  }

  void
  drawItem(const AirfixWindowsRenderSettingsViewItem &item,
           const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
    const float radius = 11.0F * dpi_;
    const auto rounded =
        D2D1::RoundedRect(d2dRect(item.bounds), radius, radius);
    target_->FillRoundedRectangle(rounded,
                                  item.selected ? selected_.Get() : row_.Get());
    target_->DrawRoundedRectangle(
        rounded, item.selected ? accent_.Get() : muted_.Get(),
        item.selected ? std::max(2.0F, 2.0F * dpi_) : std::max(1.0F, dpi_));

    const float horizontalPadding = 22.0F * dpi_;
    const bool valueRow = isValueItem(item.item);
    auto labelBounds = item.bounds;
    labelBounds.x += horizontalPadding;
    labelBounds.width -= horizontalPadding * 2.0F;
    if (valueRow) {
      const float valueWidth =
          item.bounds.x + item.bounds.width - item.previousBounds.x;
      labelBounds.width = std::max(0.0F, item.bounds.width - valueWidth -
                                             horizontalPadding * 1.5F);
    }

    ID2D1Brush *foreground = item.enabled ? text_.Get() : disabled_.Get();
    drawText(itemLabel(item.item), rowFormat_.Get(), labelBounds, foreground,
             DWRITE_TEXT_ALIGNMENT_LEADING);

    if (!valueRow) {
      if (hasChevron(item.item)) {
        auto chevronBounds = item.bounds;
        chevronBounds.x = item.bounds.x + item.bounds.width - 54.0F * dpi_;
        chevronBounds.width = 30.0F * dpi_;
        drawText(L">", valueFormat_.Get(), chevronBounds, foreground,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
      }
      return;
    }

    const float valueX = item.previousBounds.x + item.previousBounds.width;
    const float valueRight = item.nextBounds.x;
    const AirfixWindowsUiPixelRect valueBounds{
        valueX, item.bounds.y, std::max(0.0F, valueRight - valueX),
        item.bounds.height};
    std::array<wchar_t, 32U> scratch{};
    drawText(itemValue(item.item, snapshot, scratch), valueFormat_.Get(),
             valueBounds, foreground, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"-", valueFormat_.Get(), item.previousBounds, foreground,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"+", valueFormat_.Get(), item.nextBounds, foreground,
             DWRITE_TEXT_ALIGNMENT_CENTER);
  }

  void drawControllerAxisPreview(
      const AirfixWindowsRenderSettingsViewSnapshot &snapshot) noexcept {
    auto previewBounds = snapshot.statusBounds;
    previewBounds.height = std::max(0.0F, previewBounds.height - 31.0F * dpi_);
    if (previewBounds.height <= 0.0F) {
      return;
    }

    const float gap = 7.0F * dpi_;
    const float rowHeight = std::max(1.0F, (previewBounds.height - gap) * 0.5F);
    drawAxisPreviewRow(
        L"Raw", snapshot.controllerPreviewRaw,
        {previewBounds.x, previewBounds.y, previewBounds.width, rowHeight});
    drawAxisPreviewRow(L"Adjusted", snapshot.controllerPreviewEffective,
                       {previewBounds.x, previewBounds.y + rowHeight + gap,
                        previewBounds.width, rowHeight});
  }

  void drawAxisPreviewRow(const wchar_t *label, const input::Q15 value,
                          const AirfixWindowsUiPixelRect bounds) noexcept {
    const float labelWidth = std::min(150.0F * dpi_, bounds.width * 0.24F);
    const float valueWidth = std::min(86.0F * dpi_, bounds.width * 0.17F);
    const float verticalPadding = std::min(5.0F * dpi_, bounds.height * 0.2F);
    auto labelBounds = bounds;
    labelBounds.width = labelWidth;
    drawText(label, statusFormat_.Get(), labelBounds, muted_.Get(),
             DWRITE_TEXT_ALIGNMENT_LEADING);

    auto valueBounds = bounds;
    valueBounds.x = bounds.x + bounds.width - valueWidth;
    valueBounds.width = valueWidth;
    std::array<wchar_t, 32U> scratch{};
    static_cast<void>(swprintf_s(scratch.data(), scratch.size(), L"%+.1f%%",
                                 static_cast<double>(value) * 100.0 /
                                     static_cast<double>(input::q15One)));
    drawText(scratch.data(), statusFormat_.Get(), valueBounds, text_.Get(),
             DWRITE_TEXT_ALIGNMENT_TRAILING);

    const AirfixWindowsUiPixelRect barBounds{
        bounds.x + labelWidth,
        bounds.y + verticalPadding,
        std::max(0.0F, bounds.width - labelWidth - valueWidth),
        std::max(0.0F, bounds.height - verticalPadding * 2.0F),
    };
    if (barBounds.width <= 0.0F || barBounds.height <= 0.0F) {
      return;
    }
    const float radius = barBounds.height * 0.5F;
    const auto rounded = D2D1::RoundedRect(d2dRect(barBounds), radius, radius);
    target_->FillRoundedRectangle(rounded, row_.Get());
    target_->DrawRoundedRectangle(rounded, muted_.Get(), std::max(1.0F, dpi_));

    const float center = barBounds.x + barBounds.width * 0.5F;
    const float normalized = std::clamp(static_cast<float>(value) /
                                            static_cast<float>(input::q15One),
                                        -1.0F, 1.0F);
    const float end = center + normalized * barBounds.width * 0.5F;
    if (end != center) {
      target_->FillRectangle(D2D1::RectF(std::min(center, end), barBounds.y,
                                         std::max(center, end),
                                         barBounds.y + barBounds.height),
                             accent_.Get());
    }
    target_->DrawLine(D2D1::Point2F(center, barBounds.y),
                      D2D1::Point2F(center, barBounds.y + barBounds.height),
                      text_.Get(), std::max(1.0F, dpi_));
  }

  ID2D1RenderTarget *target_{};
  IDWriteFactory *writeFactory_{};
  float dpi_{1.0F};
  ComPtr<ID2D1SolidColorBrush> text_;
  ComPtr<ID2D1SolidColorBrush> muted_;
  ComPtr<ID2D1SolidColorBrush> accent_;
  ComPtr<ID2D1SolidColorBrush> panel_;
  ComPtr<ID2D1SolidColorBrush> row_;
  ComPtr<ID2D1SolidColorBrush> selected_;
  ComPtr<ID2D1SolidColorBrush> background_;
  ComPtr<ID2D1SolidColorBrush> disabled_;
  ComPtr<IDWriteTextFormat> titleFormat_;
  ComPtr<IDWriteTextFormat> rowFormat_;
  ComPtr<IDWriteTextFormat> valueFormat_;
  ComPtr<IDWriteTextFormat> statusFormat_;
};

} // namespace

bool AirfixWindowsUiRaster::complete() const noexcept {
  if (width == 0U || height == 0U || rowPitchBytes != width * 4U) {
    return false;
  }
  const auto expected = static_cast<std::uint64_t>(rowPitchBytes) *
                        static_cast<std::uint64_t>(height);
  return expected == premultipliedBgra8.size();
}

AirfixWindowsUiRasterizeResult AirfixWindowsUiRasterizer::rasterize(
    const AirfixWindowsRenderSettingsViewSnapshot &snapshot) const noexcept {
  if (!validSnapshot(snapshot)) {
    return failure(AirfixWindowsUiRasterizeIssue::invalidSnapshot);
  }

  try {
    ComApartment apartment;
    if (!apartment.available()) {
      return failure(
          AirfixWindowsUiRasterizeIssue::platformInitializationFailed);
    }

    ComPtr<IWICImagingFactory> wicFactory;
    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> writeFactory;
    if (FAILED(createWicFactory(wicFactory)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2dFactory.GetAddressOf())) ||
        FAILED(createDWriteFactory(writeFactory))) {
      return failure(
          AirfixWindowsUiRasterizeIssue::platformInitializationFailed);
    }

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wicFactory->CreateBitmap(
            snapshot.output.width, snapshot.output.height,
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
            bitmap.GetAddressOf()))) {
      return failure(AirfixWindowsUiRasterizeIssue::targetCreationFailed);
    }

    const auto targetProperties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0F, 96.0F, D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);
    ComPtr<ID2D1RenderTarget> renderTarget;
    if (FAILED(d2dFactory->CreateWicBitmapRenderTarget(
            bitmap.Get(), targetProperties, renderTarget.GetAddressOf()))) {
      return failure(AirfixWindowsUiRasterizeIssue::targetCreationFailed);
    }

    DrawingContext drawing(renderTarget.Get(), writeFactory.Get(),
                           snapshot.layoutScale);
    if (FAILED(drawing.initialize())) {
      return failure(AirfixWindowsUiRasterizeIssue::targetCreationFailed);
    }

    renderTarget->BeginDraw();
    drawing.draw(snapshot);
    if (FAILED(renderTarget->EndDraw())) {
      return failure(AirfixWindowsUiRasterizeIssue::drawingFailed);
    }

    const WICRect lockArea{
        0,
        0,
        static_cast<INT>(snapshot.output.width),
        static_cast<INT>(snapshot.output.height),
    };
    ComPtr<IWICBitmapLock> bitmapLock;
    if (FAILED(bitmap->Lock(&lockArea, WICBitmapLockRead,
                            bitmapLock.GetAddressOf()))) {
      return failure(AirfixWindowsUiRasterizeIssue::pixelCopyFailed);
    }
    UINT sourceStride{};
    UINT sourceSize{};
    BYTE *sourcePixels{};
    if (FAILED(bitmapLock->GetStride(&sourceStride)) ||
        FAILED(bitmapLock->GetDataPointer(&sourceSize, &sourcePixels)) ||
        sourcePixels == nullptr || sourceStride < snapshot.output.width * 4U) {
      return failure(AirfixWindowsUiRasterizeIssue::pixelCopyFailed);
    }

    AirfixWindowsUiRaster raster{
        .width = snapshot.output.width,
        .height = snapshot.output.height,
        .rowPitchBytes = snapshot.output.width * 4U,
        .premultipliedBgra8 = {},
    };
    const auto destinationSize =
        static_cast<std::size_t>(raster.rowPitchBytes) * raster.height;
    raster.premultipliedBgra8.resize(destinationSize);
    const auto requiredSourceSize =
        static_cast<std::uint64_t>(sourceStride) * raster.height;
    if (requiredSourceSize > sourceSize) {
      return failure(AirfixWindowsUiRasterizeIssue::pixelCopyFailed);
    }
    for (std::uint32_t row = 0U; row < raster.height; ++row) {
      std::memcpy(raster.premultipliedBgra8.data() +
                      static_cast<std::size_t>(row) * raster.rowPitchBytes,
                  sourcePixels + static_cast<std::size_t>(row) * sourceStride,
                  raster.rowPitchBytes);
    }
    if (!raster.complete()) {
      return failure(AirfixWindowsUiRasterizeIssue::pixelCopyFailed);
    }
    return {.raster = std::move(raster), .issue = std::nullopt};
  } catch (const std::bad_alloc &) {
    return failure(AirfixWindowsUiRasterizeIssue::allocationFailed);
  } catch (...) {
    return failure(AirfixWindowsUiRasterizeIssue::platformInitializationFailed);
  }
}

} // namespace airfix::windows
