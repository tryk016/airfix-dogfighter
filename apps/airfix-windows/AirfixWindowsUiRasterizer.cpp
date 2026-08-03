#include "AirfixWindowsUiRasterizer.hpp"
#include "AirfixWindowsUiSemantics.hpp"

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
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>
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
              AirfixWindowsRenderSettingsScreen::controllerBindingConflict) ||
      selectedItemValue >=
          static_cast<std::uint8_t>(AirfixWindowsRenderSettingsItem::count) ||
      statusValue >
          static_cast<std::uint8_t>(AirfixWindowsRenderSettingsStatus::
                                        controllerBindingActionUnavailable) ||
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
          AirfixWindowsRenderSettingsScreen::controllerAxisCalibration ||
      snapshot.screen ==
          AirfixWindowsRenderSettingsScreen::controllerButtonBindings ||
      snapshot.screen ==
          AirfixWindowsRenderSettingsScreen::controllerBindingConflict;
  const auto axisIndex =
      static_cast<std::size_t>(snapshot.selectedControllerAxis);
  const auto bindingActionIndex =
      static_cast<std::size_t>(snapshot.selectedControllerBindingAction);
  const auto bindingStatusValue =
      static_cast<std::uint8_t>(snapshot.selectedControllerBindingStatus);
  const auto pickerPhaseValue =
      static_cast<std::uint8_t>(snapshot.controllerBindingPickerPhase);
  if ((controllerScreen && !snapshot.controllerProfileAvailable) ||
      axisIndex >= snapshot.controllerDraftAxes.size() ||
      bindingActionIndex >= input::controllerDigitalGameplayActionCount ||
      bindingStatusValue > static_cast<std::uint8_t>(
                               input::ControllerDigitalGameplayBindingStatus::
                                   unsupportedLayout) ||
      snapshot.selectedControllerBindingControlIndex >
          airfixWindowsControllerBindingNoControlIndex ||
      pickerPhaseValue >
          static_cast<std::uint8_t>(
              settings::ControllerInputBindingPickerPhase::confirmingSwap) ||
      snapshot.controllerPreviewRaw < -input::q15One ||
      snapshot.controllerPreviewEffective < -input::q15One) {
    return false;
  }
  if (snapshot.conflictingControllerBindingAction.has_value() &&
      static_cast<std::size_t>(*snapshot.conflictingControllerBindingAction) >=
          input::controllerDigitalGameplayActionCount) {
    return false;
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
  return buildAirfixWindowsUiSemanticTree(snapshot).complete();
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

    const auto title = airfixWindowsUiTitle(snapshot);
    drawText(title.c_str(), titleFormat_.Get(), snapshot.titleBounds,
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
    const auto status = airfixWindowsUiStatus(snapshot);
    drawText(status.c_str(), statusFormat_.Get(), statusBounds,
             airfixWindowsUiStatusIsWarning(snapshot.status) ? accent_.Get()
                                                             : muted_.Get(),
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
    const bool valueRow =
        airfixWindowsRenderSettingsItemIsAdjustable(item.item);
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
    const auto label = airfixWindowsUiItemLabel(item.item);
    drawText(label.c_str(), rowFormat_.Get(), labelBounds, foreground,
             DWRITE_TEXT_ALIGNMENT_LEADING);

    if (!valueRow) {
      if (airfixWindowsUiItemHasChevron(item.item)) {
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
    const auto value = airfixWindowsUiItemValue(item.item, snapshot);
    drawText(value.c_str(), valueFormat_.Get(), valueBounds, foreground,
             DWRITE_TEXT_ALIGNMENT_CENTER);
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
