#pragma once

#include "AirfixWindowsUiRasterizer.hpp"

#include "airfix/content/LegacyAircraftHealthGaugeTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudIdentityStatusTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelTextureSet.hpp"
#include "airfix/content/LegacyWeaponCrosshairTextureSet.hpp"
#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/render/PlayerActorPoseRuntime.hpp"
#include "airfix/render/RenderFrameDiagnostics.hpp"
#include "airfix/render/RenderPresentationSettings.hpp"
#include "airfix/render/SceneTextureSampling.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

struct SDL_Window;

namespace airfix::render {
class LegacyGameplayCameraMissionRuntime;
}

namespace airfix::windows {

enum class RenderPresentationSettingsApplyIssueKind : std::uint8_t {
  invalidSettings,
  surfaceUnavailable,
  invalidLayout,
  unsupportedTargetExtent,
  targetPreparationFailed,
  publicationGateRejected,
};

struct RenderPresentationSettingsApplyResult final {
  bool changed{};
  std::optional<RenderPresentationSettingsApplyIssueKind> issue;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return !issue.has_value();
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return accepted();
  }
};

struct RenderPresentationSettingsPublicationGate final {
  using Callback = bool (*)(
      void *context,
      const airfix::render::RenderPresentationSettings &candidate) noexcept;

  Callback callback{};
  void *context{};

  [[nodiscard]] constexpr bool
  accepts(const airfix::render::RenderPresentationSettings &candidate)
      const noexcept {
    return callback == nullptr || callback(context, candidate);
  }
};

struct AirfixD3D11RendererTestAccess;

class AirfixD3D11Renderer final {
public:
  explicit AirfixD3D11Renderer(SDL_Window &window);
  ~AirfixD3D11Renderer();

  AirfixD3D11Renderer(const AirfixD3D11Renderer &) = delete;
  AirfixD3D11Renderer &operator=(const AirfixD3D11Renderer &) = delete;
  AirfixD3D11Renderer(AirfixD3D11Renderer &&) = delete;
  AirfixD3D11Renderer &operator=(AirfixD3D11Renderer &&) = delete;

  void resize();

  // Render-thread-only transaction. A complete candidate and any replacement
  // scaled scene targets are prepared before the no-fail publication step.
  // Rejection preserves both the active snapshot and active target bundle.
  [[nodiscard]] RenderPresentationSettingsApplyResult
  applyRenderPresentationSettings(
      const airfix::render::RenderPresentationSettings &candidate,
      RenderPresentationSettingsPublicationGate publicationGate = {}) noexcept;

  [[nodiscard]] airfix::render::RenderPresentationSettings
  renderPresentationSettings() const noexcept;

  // Builds every private GPU resource before replacing the currently visible
  // scene. A failure leaves the public diagnostic scene installed.
  void installLoadedMissionRoom(
      airfix::content::LoadedMissionWorldRoom &&room,
      const airfix::content::ContentRevision &expectedRevision);

  // Product transaction: the room and all authenticated HUD textures are
  // prepared before a single no-fail ownership swap. The data-less overload
  // above remains available for synthetic renderer tests.
  void installLoadedMissionRoom(
      airfix::content::LoadedMissionWorldRoom &&room,
      airfix::content::LoadedLegacyWeaponCrosshairTextureSet &&crosshairs,
      airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet &&healthGauge,
      airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet
          &&rollingDigits,
      airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet
          &&hudInstruments,
      airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet
          &&weaponPanels,
      airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet
          &&identityStatus,
      const airfix::content::ContentRevision &expectedRevision);

  [[nodiscard]] bool missionWorldRoomInstalled() const noexcept;

  // The renderer remains the strong owner for the installed scene. The
  // simulation producer receives only this replacement-safe weak endpoint.
  [[nodiscard]] std::optional<
      std::weak_ptr<airfix::render::PlayerActorPoseRuntime>>
  playerActorPoseRuntimeEndpoint() const noexcept;

  // The renderer remains the strong owner for the installed camera runtime.
  // Replacement or renderer destruction expires every previously returned
  // endpoint. The simulation producer must not advance this endpoint until it
  // can supply the complete recovered AirCraft input contract.
  [[nodiscard]]
  std::weak_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
  gameplayCameraMissionRuntimeEndpoint() const noexcept;

  // Renders and writes one private local D3D11 frame as a top-down BGRA8 BMP.
  // Callers must keep the derived screenshot outside public source control.
  void captureFrameToBmp(const std::filesystem::path &outputPath);

  // Captures the complete installed mission from a fitted, one-frame
  // developer camera. The gameplay camera runtime is never changed.
  void
  captureMissionOverviewFrameToBmp(const std::filesystem::path &outputPath);

  // Private visual-validation harness. It draws the authenticated machine-gun
  // sight at output centre through the native sprite path, but does not claim
  // to represent live weapon selection or aim state.
  void captureMissionCrosshairValidationFrameToBmp(
      const std::filesystem::path &outputPath);

  // Private visual-validation harness. It supplies a fixed half-health value
  // only to the recovered HUD planner, never to gameplay simulation, and
  // renders the authenticated background/mask/foreground command sequence.
  void captureMissionHealthGaugeValidationFrameToBmp(
      const std::filesystem::path &outputPath);

  // Captures the public synthetic scene and developer overlay. This contains
  // no owner content and exists for repeatable renderer diagnostics.
  void
  capturePublicDiagnosticFrameToBmp(const std::filesystem::path &outputPath);

  // Publishes or clears a product UI layer in physical output pixels. The
  // image is premultiplied BGRA8 and contains only bounded, path-free UI
  // state. Rejection preserves the previously published layer.
  [[nodiscard]] bool
  setProductUiRaster(const AirfixWindowsUiRaster &raster) noexcept;
  void clearProductUiRaster() noexcept;

  // Captures the public synthetic scene with the currently published product
  // UI. Installed private content is rejected.
  void
  capturePublicSettingsPanelFrameToBmp(const std::filesystem::path &outputPath);

  [[nodiscard]] std::optional<airfix::render::RenderFrameDiagnostics>
  frameDiagnostics() const noexcept;

  // When validation is requested, the back buffer is read before Present and
  // the result proves that non-clear pixels reached the actual D3D11 target.
  [[nodiscard]] bool renderFrame(bool validateGpuOutput);

private:
  friend struct AirfixD3D11RendererTestAccess;

  void failNextScaledTargetPreparationsAfterColorForTesting(
      std::uint32_t failureCount) noexcept;
  void reportSurfaceUnavailableForNextApplyForTesting() noexcept;
  [[nodiscard]] bool resizeToPixelExtentForTesting(int width, int height);
  [[nodiscard]] std::array<const void *, 5U>
  scaledSceneTargetIdentityForTesting() const noexcept;
  [[nodiscard]] std::optional<airfix::render::RenderTargetPixelRect>
  lastSceneViewportForTesting() const noexcept;
  [[nodiscard]] std::optional<airfix::render::ScenePresentationMode>
  lastScenePresentationForTesting() const noexcept;
  [[nodiscard]] std::optional<airfix::render::SceneTextureSamplingPolicy>
  lastSceneTextureSamplingPolicyForTesting() const noexcept;
  [[nodiscard]] bool hasDiagnosticsOverlayResourcesForTesting() const noexcept;
  [[nodiscard]] bool hasProductUiOverlayResourcesForTesting() const noexcept;

  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace airfix::windows
