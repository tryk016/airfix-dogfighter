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
  if (snapshot.output.width == 0U || snapshot.output.height == 0U ||
      snapshot.output.width > maximumRasterDimension ||
      snapshot.output.height > maximumRasterDimension ||
      !std::isfinite(snapshot.output.dpiScale) ||
      snapshot.output.dpiScale <= 0.0F ||
      !std::isfinite(snapshot.layoutScale) || snapshot.layoutScale <= 0.0F ||
      static_cast<std::size_t>(snapshot.itemCount) > snapshot.items.size() ||
      !rectInsideOutput(snapshot.panelBounds, snapshot.output) ||
      !rectInsideOutput(snapshot.titleBounds, snapshot.output) ||
      !rectInsideOutput(snapshot.statusBounds, snapshot.output)) {
    return false;
  }
  const auto byteCount = static_cast<std::uint64_t>(snapshot.output.width) *
                         static_cast<std::uint64_t>(snapshot.output.height) *
                         4U;
  if (byteCount > maximumRasterBytes ||
      byteCount > std::numeric_limits<UINT>::max()) {
    return false;
  }
  for (std::uint8_t index = 0U; index < snapshot.itemCount; ++index) {
    if (!rectInsideOutput(snapshot.items[index].bounds, snapshot.output) ||
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
  case AirfixWindowsRenderSettingsItem::count:
    break;
  }
  return L"";
}

[[nodiscard]] const wchar_t *
itemValue(const AirfixWindowsRenderSettingsItem item,
          const render::RenderPresentationSettings &draft,
          std::array<wchar_t, 32U> &scratch) noexcept {
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
  case AirfixWindowsRenderSettingsItem::displaySettings:
  case AirfixWindowsRenderSettingsItem::resume:
  case AirfixWindowsRenderSettingsItem::apply:
  case AirfixWindowsRenderSettingsItem::cancel:
  case AirfixWindowsRenderSettingsItem::count:
    break;
  }
  return L"";
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

    const wchar_t *title =
        snapshot.screen == AirfixWindowsRenderSettingsScreen::pause
            ? L"Paused"
            : L"Display settings";
    drawText(title, titleFormat_.Get(), snapshot.titleBounds, text_.Get(),
             DWRITE_TEXT_ALIGNMENT_LEADING);

    for (std::uint8_t index = 0U; index < snapshot.itemCount; ++index) {
      drawItem(snapshot.items[index], snapshot.draftSettings);
    }

    drawText(statusText(snapshot), statusFormat_.Get(), snapshot.statusBounds,
             snapshot.status ==
                     AirfixWindowsRenderSettingsStatus::persistenceUnavailable
                 ? accent_.Get()
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

  void drawItem(const AirfixWindowsRenderSettingsViewItem &item,
                const render::RenderPresentationSettings &draft) noexcept {
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
        item.item >= AirfixWindowsRenderSettingsItem::renderScale &&
        item.item <= AirfixWindowsRenderSettingsItem::rendererStatistics;
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
      if (item.item == AirfixWindowsRenderSettingsItem::displaySettings) {
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
    drawText(itemValue(item.item, draft, scratch), valueFormat_.Get(),
             valueBounds, foreground, DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"-", valueFormat_.Get(), item.previousBounds, foreground,
             DWRITE_TEXT_ALIGNMENT_CENTER);
    drawText(L"+", valueFormat_.Get(), item.nextBounds, foreground,
             DWRITE_TEXT_ALIGNMENT_CENTER);
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
