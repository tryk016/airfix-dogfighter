#include "AirfixD3D11Renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace airfix::windows {

struct AirfixD3D11RendererTestAccess final {
  static void failNextScaledTargetPreparationAfterColor(
      AirfixD3D11Renderer &renderer,
      const std::uint32_t failureCount = 1U) noexcept {
    renderer.failNextScaledTargetPreparationsAfterColorForTesting(
        failureCount);
  }

  static void reportSurfaceUnavailableForNextApply(
      AirfixD3D11Renderer &renderer) noexcept {
    renderer.reportSurfaceUnavailableForNextApplyForTesting();
  }

  [[nodiscard]] static bool resizeToPixelExtent(
      AirfixD3D11Renderer &renderer,
      const int width, const int height) {
    return renderer.resizeToPixelExtentForTesting(width, height);
  }

  [[nodiscard]] static std::array<const void *, 5U>
  scaledSceneTargetIdentity(
      const AirfixD3D11Renderer &renderer) noexcept {
    return renderer.scaledSceneTargetIdentityForTesting();
  }

  [[nodiscard]] static
  std::optional<airfix::render::RenderTargetPixelRect>
  lastSceneViewport(
      const AirfixD3D11Renderer &renderer) noexcept {
    return renderer.lastSceneViewportForTesting();
  }

  [[nodiscard]] static
  std::optional<airfix::render::ScenePresentationMode>
  lastScenePresentation(
      const AirfixD3D11Renderer &renderer) noexcept {
    return renderer.lastScenePresentationForTesting();
  }

  [[nodiscard]] static bool hasDiagnosticsOverlayResources(
      const AirfixD3D11Renderer &renderer) noexcept {
    return renderer.hasDiagnosticsOverlayResourcesForTesting();
  }

  [[nodiscard]] static bool
  hasProductUiOverlayResources(const AirfixD3D11Renderer &renderer) noexcept {
    return renderer.hasProductUiOverlayResourcesForTesting();
  }
};

} // namespace airfix::windows

namespace {

using airfix::render::RenderPresentationSettings;
using airfix::windows::AirfixD3D11Renderer;
using airfix::windows::AirfixD3D11RendererTestAccess;
using airfix::windows::AirfixWindowsUiRaster;
using airfix::windows::RenderPresentationSettingsApplyIssueKind;
using airfix::windows::RenderPresentationSettingsApplyResult;
using airfix::windows::RenderPresentationSettingsPublicationGate;
using ScaledTargetIdentity = std::array<const void *, 5U>;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct SdlQuitGuard final {
  ~SdlQuitGuard() { SDL_Quit(); }
};

struct WindowDeleter final {
  void operator()(SDL_Window *const window) const noexcept {
    SDL_DestroyWindow(window);
  }
};

struct PublicationGateState final {
  bool accept{};
  std::uint32_t callCount{};
  RenderPresentationSettings observed;
};

[[nodiscard]] bool publicationGate(
    void *const context,
    const RenderPresentationSettings &candidate) noexcept {
  auto &state = *static_cast<PublicationGateState *>(context);
  ++state.callCount;
  state.observed = candidate;
  return state.accept;
}

void requireApplied(
    const RenderPresentationSettingsApplyResult &result,
    const char *const context) {
  require(
      result.accepted() && result.changed,
      std::string(context) + " was not committed");
}

void requireRejected(
    const RenderPresentationSettingsApplyResult &result,
    const RenderPresentationSettingsApplyIssueKind expected,
    const char *const context) {
  require(
      !result.accepted() && !result.changed &&
          result.issue == expected,
      std::string(context) + " reported the wrong failure");
}

[[nodiscard]] bool completeIdentity(
    const ScaledTargetIdentity &identity) noexcept {
  return std::all_of(
      identity.begin(), identity.end(),
      [](const void *const value) { return value != nullptr; });
}

[[nodiscard]] bool emptyIdentity(
    const ScaledTargetIdentity &identity) noexcept {
  return std::all_of(
      identity.begin(), identity.end(),
      [](const void *const value) { return value == nullptr; });
}

airfix::render::RenderFrameDiagnostics
renderAndRequireLayout(
    AirfixD3D11Renderer &renderer,
    const RenderPresentationSettings &expected,
    const char *const context) {
  require(
      renderer.renderFrame(true),
      std::string(context) + " produced no visible GPU output");
  const auto diagnostics = renderer.frameDiagnostics();
  require(
      diagnostics.has_value(),
      std::string(context) + " published no frame diagnostics");
  const auto layout = airfix::render::buildNativeRenderLayout({
      .outputExtent = diagnostics->outputExtent,
      .renderScalePercent = expected.renderScalePercent,
      .scenePresentation = expected.scenePresentation,
      .verticalFovAdjustmentDegrees =
          expected.verticalFovAdjustmentDegrees,
  });
  require(
      layout.complete() &&
          diagnostics->renderScalePercent ==
              expected.renderScalePercent &&
          diagnostics->renderTargetExtent ==
              layout.layout->renderTargetExtent(),
      std::string(context) + " used the wrong render layout");
  return *diagnostics;
}

void testTransactionalSettingsRuntime() {
  require(
      SDL_SetAppMetadata(
          "Airfix D3D11 renderer tests", "0.1.0",
          "com.tryk016.airfixdogfighter.renderer-tests"),
      SDL_GetError());
  require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
  const SdlQuitGuard quitGuard;

  std::unique_ptr<SDL_Window, WindowDeleter> window{
      SDL_CreateWindow(
          "Airfix D3D11 renderer tests", 960, 540,
          SDL_WINDOW_HIDDEN)};
  require(window != nullptr, SDL_GetError());

  AirfixD3D11Renderer renderer{*window};
  RenderPresentationSettings settings;
  require(
      renderer.renderPresentationSettings() == settings,
      "renderer did not start with canonical presentation settings");
  auto identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      emptyIdentity(identity),
      "100 percent unexpectedly allocated scaled scene targets");
  renderAndRequireLayout(renderer, settings, "initial 100 percent frame");

  AirfixWindowsUiRaster productUi{
      .width = 960U,
      .height = 540U,
      .rowPitchBytes = 960U * 4U,
      .premultipliedBgra8 = std::vector<std::uint8_t>(960U * 540U * 4U),
  };
  for (std::size_t offset = 0U;
       offset + 3U < productUi.premultipliedBgra8.size(); offset += 4U) {
    productUi.premultipliedBgra8[offset] = 16U;
    productUi.premultipliedBgra8[offset + 1U] = 32U;
    productUi.premultipliedBgra8[offset + 2U] = 64U;
    productUi.premultipliedBgra8[offset + 3U] = 128U;
  }
  require(renderer.setProductUiRaster(productUi) &&
              AirfixD3D11RendererTestAccess::hasProductUiOverlayResources(
                  renderer) &&
              renderer.renderFrame(true),
          "premultiplied product UI was not published to the backbuffer");
  auto invalidUi = productUi;
  invalidUi.width = 959U;
  require(
      !renderer.setProductUiRaster(invalidUi) &&
          AirfixD3D11RendererTestAccess::hasProductUiOverlayResources(renderer),
      "invalid product UI replaced the published resources");
  renderer.clearProductUiRaster();
  require(
      !AirfixD3D11RendererTestAccess::hasProductUiOverlayResources(renderer),
      "clearing product UI retained its GPU resources");

  settings.renderScalePercent = 50.0F;
  requireApplied(
      renderer.applyRenderPresentationSettings(settings),
      "100 to 50 percent transition");
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      completeIdentity(identity),
      "50 percent committed without prepared scaled targets");
  renderAndRequireLayout(renderer, settings, "50 percent frame");

  auto candidate = settings;
  candidate.renderScalePercent = 200.0F;
  AirfixD3D11RendererTestAccess::
      reportSurfaceUnavailableForNextApply(renderer);
  requireRejected(
      renderer.applyRenderPresentationSettings(candidate),
      RenderPresentationSettingsApplyIssueKind::surfaceUnavailable,
      "surface-unavailable scaled transition");
  require(
      renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              identity,
      "surface-unavailable transition changed the scaled snapshot or bundle");
  renderAndRequireLayout(
      renderer, settings, "scaled frame after unavailable-surface rejection");

  const auto beforeFiftyResize = identity;
  require(
      AirfixD3D11RendererTestAccess::resizeToPixelExtent(
          renderer, 800, 450),
      "50 percent resize was not published");
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      completeIdentity(identity) && identity != beforeFiftyResize &&
          renderer.renderPresentationSettings() == settings,
      "50 percent resize did not replace only the scaled bundle");
  const auto fiftyResizeDiagnostics = renderAndRequireLayout(
      renderer, settings, "50 percent resized frame");
  require(
      fiftyResizeDiagnostics.outputExtent ==
          airfix::render::OutputPixelExtent{800U, 450U},
      "50 percent resize used the wrong output extent");

  const auto beforeFailedResize = identity;
  AirfixD3D11RendererTestAccess::
      failNextScaledTargetPreparationAfterColor(renderer, 2U);
  require(
      !AirfixD3D11RendererTestAccess::resizeToPixelExtent(
          renderer, 1024, 576),
      "late scaled resize failure was not reported");
  require(
      renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              beforeFailedResize,
      "failed scaled resize changed the active snapshot or any target view");
  const auto firstRetryFailureDiagnostics = renderAndRequireLayout(
      renderer, settings, "frame after first automatic resize retry failure");
  require(
      firstRetryFailureDiagnostics.outputExtent ==
              airfix::render::OutputPixelExtent{800U, 450U} &&
          renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              beforeFailedResize,
      "first automatic retry changed the old surface or active bundle");
  const auto backoffDiagnostics = renderAndRequireLayout(
      renderer, settings, "frame skipped by scaled resize backoff");
  require(
      backoffDiagnostics.outputExtent ==
              airfix::render::OutputPixelExtent{800U, 450U} &&
          renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              beforeFailedResize,
      "scaled resize retried during its one-frame backoff");
  const auto retryDiagnostics = renderAndRequireLayout(
      renderer, settings, "frame after scaled resize backoff");
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      retryDiagnostics.outputExtent ==
              airfix::render::OutputPixelExtent{1024U, 576U} &&
          completeIdentity(identity) && identity != beforeFailedResize,
      "pending scaled resize did not retry and publish atomically");

  settings.renderScalePercent = 200.0F;
  requireApplied(
      renderer.applyRenderPresentationSettings(settings),
      "50 to 200 percent transition");
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      completeIdentity(identity),
      "200 percent committed without prepared scaled targets");
  renderAndRequireLayout(renderer, settings, "200 percent frame");

  const auto beforeTwoHundredResize = identity;
  require(
      AirfixD3D11RendererTestAccess::resizeToPixelExtent(
          renderer, 640, 360),
      "200 percent resize was not published");
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      completeIdentity(identity) && identity != beforeTwoHundredResize &&
          renderer.renderPresentationSettings() == settings,
      "200 percent resize did not publish a complete replacement bundle");
  const auto twoHundredResizeDiagnostics = renderAndRequireLayout(
      renderer, settings, "200 percent resized frame");
  require(
      twoHundredResizeDiagnostics.outputExtent ==
          airfix::render::OutputPixelExtent{640U, 360U},
      "200 percent resize used the wrong output extent");

  const auto beforeMinimize = identity;
  require(
      AirfixD3D11RendererTestAccess::resizeToPixelExtent(
          renderer, 0, 0),
      "zero-extent suspension failed");
  require(
      renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              beforeMinimize,
      "zero-extent suspension discarded settings or scaled resources");
  candidate = settings;
  candidate.renderScalePercent = 50.0F;
  requireRejected(
      renderer.applyRenderPresentationSettings(candidate),
      RenderPresentationSettingsApplyIssueKind::surfaceUnavailable,
      "minimized scaled transition");
  require(
      renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              beforeMinimize,
      "minimized rejection changed any scaled target identity");
  require(
      AirfixD3D11RendererTestAccess::resizeToPixelExtent(
          renderer, 640, 360),
      "restore after zero-extent suspension failed");
  require(
      AirfixD3D11RendererTestAccess::
              scaledSceneTargetIdentity(renderer) ==
          beforeMinimize,
      "same-size restore unnecessarily replaced the preserved scaled bundle");
  renderAndRequireLayout(renderer, settings, "restored scaled frame");

  settings.renderScalePercent = 100.0F;
  requireApplied(
      renderer.applyRenderPresentationSettings(settings),
      "200 to 100 percent transition");
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      emptyIdentity(identity),
      "100 percent retained an intermediate scene target");
  renderAndRequireLayout(renderer, settings, "restored 100 percent frame");

  candidate = settings;
  candidate.renderScalePercent = 50.0F;
  requireApplied(
      renderer.applyRenderPresentationSettings(candidate),
      "50 percent preparation before apply failure test");
  settings = candidate;
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(completeIdentity(identity), "50 percent bundle is incomplete");
  renderAndRequireLayout(renderer, settings, "prepared 50 percent frame");

  candidate.renderScalePercent = 200.0F;
  AirfixD3D11RendererTestAccess::
      failNextScaledTargetPreparationAfterColor(renderer);
  requireRejected(
      renderer.applyRenderPresentationSettings(candidate),
      RenderPresentationSettingsApplyIssueKind::
          targetPreparationFailed,
      "late scaled-target failure");
  require(
      renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              identity,
      "late target failure changed the active snapshot or any target view");
  renderAndRequireLayout(
      renderer, settings, "frame after late target failure");

  requireApplied(
      renderer.applyRenderPresentationSettings(candidate),
      "retry after one-shot target failure");
  settings = candidate;
  identity =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  renderAndRequireLayout(renderer, settings, "retried 200 percent frame");

  candidate = settings;
  candidate.renderScalePercent = 50.0F;
  PublicationGateState gateState;
  const RenderPresentationSettingsPublicationGate gate{
      .callback = publicationGate,
      .context = &gateState,
  };
  requireRejected(
      renderer.applyRenderPresentationSettings(candidate, gate),
      RenderPresentationSettingsApplyIssueKind::
          publicationGateRejected,
      "publication-gated scaled transition");
  require(
      gateState.callCount == 1U &&
          gateState.observed == candidate &&
          renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              identity,
      "publication gate rejection changed the active snapshot or target bundle");
  renderAndRequireLayout(
      renderer, settings, "frame after publication gate rejection");

  gateState.accept = true;
  requireApplied(
      renderer.applyRenderPresentationSettings(candidate, gate),
      "retry after publication gate rejection");
  settings = candidate;
  const auto identityAfterGateRetry =
      AirfixD3D11RendererTestAccess::scaledSceneTargetIdentity(renderer);
  require(
      gateState.callCount == 2U &&
          gateState.observed == candidate &&
          completeIdentity(identityAfterGateRetry) &&
          identityAfterGateRetry != identity,
      "publication gate retry did not publish the prepared target bundle");
  identity = identityAfterGateRetry;
  renderAndRequireLayout(
      renderer, settings, "frame after publication gate retry");

  AirfixD3D11RendererTestAccess::
      failNextScaledTargetPreparationAfterColor(renderer);
  candidate = settings;
  candidate.visualProfile = airfix::render::VisualProfile::enhanced;
  candidate.scenePresentation =
      airfix::render::ScenePresentationMode::originalFourByThree;
  candidate.diagnosticsOverlayEnabled = true;
  candidate.verticalFovAdjustmentDegrees = 20.0F;
  requireApplied(
      renderer.applyRenderPresentationSettings(candidate),
      "layout, diagnostics, and visual-profile transition");
  settings = candidate;
  require(
      AirfixD3D11RendererTestAccess::
              scaledSceneTargetIdentity(renderer) ==
      identity,
      "orthogonal settings replaced the scaled target bundle");
  const auto enhancedDiagnostics = renderAndRequireLayout(
      renderer, settings,
      "enhanced Original 4:3 diagnostics frame");
  const auto enhancedLayout =
      airfix::render::buildNativeRenderLayout({
          .outputExtent = enhancedDiagnostics.outputExtent,
          .renderScalePercent = settings.renderScalePercent,
          .scenePresentation = settings.scenePresentation,
      });
  require(
      enhancedLayout.complete() &&
          AirfixD3D11RendererTestAccess::
                  lastSceneViewport(renderer) ==
              enhancedLayout.layout->
                  sceneViewportInRenderTarget() &&
          AirfixD3D11RendererTestAccess::
                  lastScenePresentation(renderer) ==
              airfix::render::ScenePresentationMode::
                  originalFourByThree &&
          AirfixD3D11RendererTestAccess::
              hasDiagnosticsOverlayResources(renderer) &&
          renderer.renderPresentationSettings().visualProfile ==
              airfix::render::VisualProfile::enhanced &&
          renderer.renderPresentationSettings()
                  .verticalFovAdjustmentDegrees ==
              20.0F,
      "Original 4:3, diagnostics overlay, or visual profile was not observed");

  candidate = settings;
  candidate.diagnosticsOverlayEnabled = false;
  requireApplied(
      renderer.applyRenderPresentationSettings(candidate),
      "diagnostics overlay disable transition");
  settings = candidate;
  require(
      AirfixD3D11RendererTestAccess::
              scaledSceneTargetIdentity(renderer) ==
          identity &&
          !AirfixD3D11RendererTestAccess::
              hasDiagnosticsOverlayResources(renderer),
      "diagnostics disable replaced scaled targets or retained overlay resources");
  renderAndRequireLayout(
      renderer, settings, "Original 4:3 frame without diagnostics");
  require(
      !AirfixD3D11RendererTestAccess::
          hasDiagnosticsOverlayResources(renderer),
      "diagnostics overlay resources returned while disabled");

  candidate.renderScalePercent = 200.0F;
  requireRejected(
      renderer.applyRenderPresentationSettings(candidate),
      RenderPresentationSettingsApplyIssueKind::
          targetPreparationFailed,
      "deferred one-shot target failure");
  require(
      renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              identity,
      "orthogonal settings consumed the seam or changed target identity");

  candidate = settings;
  candidate.renderScalePercent =
      std::numeric_limits<float>::quiet_NaN();
  requireRejected(
      renderer.applyRenderPresentationSettings(candidate),
      RenderPresentationSettingsApplyIssueKind::invalidSettings,
      "non-finite settings transition");
  require(
      renderer.renderPresentationSettings() == settings &&
          AirfixD3D11RendererTestAccess::
                  scaledSceneTargetIdentity(renderer) ==
              identity,
      "invalid candidate changed the active settings or scaled bundle");

  candidate = settings;
  candidate.renderScalePercent = 100.0F;
  requireApplied(
      renderer.applyRenderPresentationSettings(candidate),
      "final direct-render transition");
  require(
      emptyIdentity(
          AirfixD3D11RendererTestAccess::
              scaledSceneTargetIdentity(renderer)),
      "final 100 percent transition retained scaled targets");
  renderAndRequireLayout(renderer, candidate, "final direct frame");
}

} // namespace

int main() {
  try {
    testTransactionalSettingsRuntime();
    std::cout << "Airfix D3D11 renderer tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Airfix D3D11 renderer tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
