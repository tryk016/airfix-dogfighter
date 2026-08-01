#include "AirfixD3D11Renderer.hpp"

#include "AirfixEmbeddedShader.hpp"

#include "airfix/content/LegacyAircraftHealthGaugeSubmission.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsSubmission.hpp"
#include "airfix/content/LegacyWeaponCrosshairSpriteSubmission.hpp"
#include "airfix/content/MissionWorldRoomPublication.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/LegacyDepthState.hpp"
#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"
#include "airfix/render/NativeRenderLayout.hpp"
#include "airfix/render/PlayerActorPoseRuntimePreparation.hpp"
#include "airfix/render/PublicRenderSmokeScene.hpp"
#include "airfix/render/RenderFrameDiagnostics.hpp"
#include "airfix/render/SceneOverviewCamera.hpp"
#include "airfix/render/SceneTextureSampling.hpp"

#include <SDL3/SDL.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace airfix::windows {
namespace {

using Microsoft::WRL::ComPtr;

static_assert(sizeof(BITMAPFILEHEADER) == 14U);
static_assert(sizeof(BITMAPINFOHEADER) == 40U);

[[noreturn]] void throwFailure(const char *operation, const HRESULT result) {
  std::ostringstream message;
  message << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
          << static_cast<unsigned long>(result) << ')';
  throw std::runtime_error(message.str());
}

void requireSuccess(const HRESULT result, const char *operation) {
  if (FAILED(result)) {
    throwFailure(operation, result);
  }
}

[[nodiscard]] std::optional<D3D11_FILTER> d3dSamplerFilter(
    const airfix::render::SceneTextureSamplingPolicy &policy) noexcept {
  if (!airfix::render::validateSceneTextureSamplingPolicy(policy)) {
    return std::nullopt;
  }
  switch (policy.mode) {
  case airfix::render::SceneTextureSamplingMode::nearestMipPoint:
    return D3D11_FILTER_MIN_MAG_MIP_POINT;
  case airfix::render::SceneTextureSamplingMode::linearMipPoint:
    return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  case airfix::render::SceneTextureSamplingMode::linearMipLinear:
    return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  case airfix::render::SceneTextureSamplingMode::anisotropicMipLinear:
    return D3D11_FILTER_ANISOTROPIC;
  }
  return std::nullopt;
}

[[nodiscard]] UINT checkedByteWidth(const std::size_t elementCount,
                                    const std::size_t elementSize,
                                    const char *resourceName) {
  if (elementCount == 0U || elementSize > std::numeric_limits<UINT>::max() ||
      elementCount > std::numeric_limits<UINT>::max() / elementSize) {
    throw std::runtime_error(std::string(resourceName) +
                             " has an invalid byte width");
  }
  return static_cast<UINT>(elementCount * elementSize);
}

[[nodiscard]] UINT checkedUint(const std::uint64_t value,
                               const char *resourceName) {
  if (value == 0U || value > std::numeric_limits<UINT>::max()) {
    throw std::runtime_error(std::string(resourceName) +
                             " exceeds the D3D11 UINT contract");
  }
  return static_cast<UINT>(value);
}

[[nodiscard]] bool
validTextureLevel(const airfix::render::GtiUploadLevel &plan,
                  const airfix::assets::RgbaImage &image) noexcept {
  if (plan.width == 0U || plan.height == 0U || image.width != plan.width ||
      image.height != plan.height) {
    return false;
  }
  const std::uint64_t expectedRowBytes =
      static_cast<std::uint64_t>(plan.width) * 4U;
  const std::uint64_t expectedRgbaBytes =
      expectedRowBytes * static_cast<std::uint64_t>(plan.height);
  return plan.bytesPerRow == expectedRowBytes &&
         plan.rgbaBytes == expectedRgbaBytes &&
         static_cast<std::uint64_t>(image.pixels.size()) == expectedRgbaBytes;
}

[[nodiscard]] ComPtr<ID3DBlob> compileShader(const char *entryPoint,
                                             const char *profile) {
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  ComPtr<ID3DBlob> shader;
  ComPtr<ID3DBlob> diagnostics;
  const HRESULT result =
      D3DCompile(smokeHlsl, sizeof(smokeHlsl) - 1U, "AirfixSmoke.hlsl", nullptr,
                 nullptr, entryPoint, profile, flags, 0U, shader.GetAddressOf(),
                 diagnostics.GetAddressOf());
  if (FAILED(result)) {
    std::string message = "D3DCompile failed";
    if (diagnostics) {
      message.append(": ");
      message.append(static_cast<const char *>(diagnostics->GetBufferPointer()),
                     diagnostics->GetBufferSize());
    }
    throw std::runtime_error(message);
  }
  return shader;
}

struct alignas(16) GpuVertex {
  std::array<float, 4U> position{};
  std::array<float, 4U> normal{};
  std::array<float, 2U> uv{};
  std::array<float, 2U> padding{};
};

static_assert(sizeof(GpuVertex) == 48U);
static_assert(alignof(GpuVertex) == 16U);

struct alignas(16) SmokeUniforms {
  std::array<float, 16U> modelFromLocal{};
};

static_assert(sizeof(SmokeUniforms) == 64U);

struct alignas(16) GameplayUniforms {
  std::array<float, 16U> modelFromLocal{};
  std::array<float, 4U> cameraAxisX{};
  std::array<float, 4U> cameraAxisY{};
  std::array<float, 4U> cameraAxisZ{};
  std::array<float, 4U> cameraTranslationAndInverseScaleSquared{};
  std::array<float, 4U> projection{};
  std::array<float, 4U> logicalCanvas{};
};

static_assert(sizeof(GameplayUniforms) == 160U);
static_assert(alignof(GameplayUniforms) == 16U);

struct alignas(16) OverlayUniforms {
  std::array<float, 4U> outputAndPanelSize{};
  std::array<float, 4U> panelOrigin{};
  std::array<float, 4U> tint{};
  std::array<float, 4U> uvRect{};
};

static_assert(sizeof(OverlayUniforms) == 64U);
static_assert(alignof(OverlayUniforms) == 16U);

struct alignas(16) GaugeUniforms {
  std::array<std::array<float, 4U>, 4U> outputQuad{};
  std::array<float, 4U> outputSize{};
  std::array<float, 4U> tint{};
};

static_assert(sizeof(GaugeUniforms) == 96U);
static_assert(alignof(GaugeUniforms) == 16U);

[[nodiscard]] constexpr std::array<float, 4U>
normalizedArgb(const std::uint32_t argb) noexcept {
  constexpr float inverseByte = 1.0F / 255.0F;
  return {
      static_cast<float>((argb >> 16U) & 0xFFU) * inverseByte,
      static_cast<float>((argb >> 8U) & 0xFFU) * inverseByte,
      static_cast<float>(argb & 0xFFU) * inverseByte,
      static_cast<float>((argb >> 24U) & 0xFFU) * inverseByte,
  };
}

struct CapturedBackBuffer final {
  UINT width{};
  UINT height{};
  std::vector<std::uint8_t> bgra8;
};

[[nodiscard]] GpuVertex repackVertex(const airfix::render::DrawVertex &source) {
  return GpuVertex{
      .position = {source.position.x, source.position.y, source.position.z,
                   1.0F},
      .normal = {source.normal.x, source.normal.y, source.normal.z, 0.0F},
      .uv = {source.uv.u, source.uv.v},
      .padding = {},
  };
}

[[nodiscard]] std::array<float, 16U>
modelFromLocal(const airfix::render::Mat3 &linear,
               const airfix::render::Vec3 &translation) {
  // HLSL consumes a row-vector matrix. Each mathematical column of the
  // portable column-vector transform therefore becomes one stored row.
  const auto &columns = linear.columns;
  return {{
      columns[0].x,
      columns[0].y,
      columns[0].z,
      0.0F,
      columns[1].x,
      columns[1].y,
      columns[1].z,
      0.0F,
      columns[2].x,
      columns[2].y,
      columns[2].z,
      0.0F,
      translation.x,
      translation.y,
      translation.z,
      1.0F,
  }};
}

[[nodiscard]] std::array<float, 16U>
modelFromLocal(const airfix::render::DrawMeshInstance &instance) {
  return modelFromLocal(instance.modelLinear, instance.modelTranslation);
}

[[nodiscard]] SmokeUniforms
makeSmokeUniforms(const airfix::render::DrawMeshInstance &instance) {
  return SmokeUniforms{.modelFromLocal = modelFromLocal(instance)};
}

[[nodiscard]] GameplayUniforms makeGameplayUniforms(
    const airfix::render::ResolvedInstancePose &pose,
    const airfix::render::LegacyGameplayCameraClipPacket &camera,
    const airfix::render::NativeRenderLayout &layout) {
  const auto &transform = camera.pose().worldToView();
  const auto &linear = transform.linear();
  const auto &translation = transform.translation();
  const auto &projection = camera.pose().projection();
  const auto logicalCentre = layout.mapReferenceCameraPoint(
      {projection.centre().x, projection.centre().y});
  const auto logicalExtent = layout.cameraLogicalExtent();
  return {
      .modelFromLocal =
          modelFromLocal(pose.modelLinear, pose.modelTranslation),
      .cameraAxisX = {linear.columns[0].x, linear.columns[0].y,
                      linear.columns[0].z, 0.0F},
      .cameraAxisY = {linear.columns[1].x, linear.columns[1].y,
                      linear.columns[1].z, 0.0F},
      .cameraAxisZ = {linear.columns[2].x, linear.columns[2].y,
                      linear.columns[2].z, 0.0F},
      .cameraTranslationAndInverseScaleSquared =
          {translation.x, translation.y, translation.z,
           transform.inverseScaleSquared()},
      .projection = {projection.nearDistance(), projection.farDistance(),
                     projection.projectScale(), 0.0F},
      .logicalCanvas = {logicalCentre.x, logicalCentre.y, logicalExtent.width,
                        logicalExtent.height},
  };
}

[[nodiscard]] airfix::render::ConvertedNodeTransform actorWorldFrom(
    const airfix::simulation::PlayerSpawnPose &pose) noexcept {
  const auto vectorAt = [](const std::array<float, 3U> &value) {
    return airfix::render::Vec3{value[0], value[1], value[2]};
  };
  return {
      .linear =
          {
              .columns =
                  {
                      vectorAt(pose.runtimeWorldRotationColumns[0]),
                      vectorAt(pose.runtimeWorldRotationColumns[1]),
                      vectorAt(pose.runtimeWorldRotationColumns[2]),
                  },
          },
      .translation = vectorAt(pose.runtimeWorldPosition),
      .rawScalar = 1.0F,
  };
}

[[nodiscard]]
airfix::render::LegacyGameplayCameraStepCoordinatorInitializeInput
gameplayCameraInitializeInput(
    const airfix::content::LoadedMissionWorldRoom &room) noexcept {
  const auto vectorAt = [](const std::array<float, 3U> &value) {
    return airfix::render::Vec3{value[0], value[1], value[2]};
  };
  return {
      .vehicleWorldPosition =
          vectorAt(room.playerSpawnPose.runtimeWorldPosition),
      .vehicleWorldRotation =
          {
              .columns =
                  {
                      vectorAt(
                          room.playerSpawnPose.runtimeWorldRotationColumns[0]),
                      vectorAt(
                          room.playerSpawnPose.runtimeWorldRotationColumns[1]),
                      vectorAt(
                          room.playerSpawnPose.runtimeWorldRotationColumns[2]),
                  },
          },
      .worldRoomIndex = room.playerSpawnPose.worldRoomIndex,
      .cameraCyclePressCount = 0U,
  };
}

[[nodiscard]] std::pair<int, int> pixelSize(SDL_Window &window) {
  int width = 0;
  int height = 0;
  if (!SDL_GetWindowSizeInPixels(&window, &width, &height)) {
    throw std::runtime_error(SDL_GetError());
  }
  return {width, height};
}

} // namespace

class AirfixD3D11Renderer::Implementation final {
  struct MeshResources {
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11Buffer> indices;
  };

  struct GpuTimestampQueries {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> start;
    ComPtr<ID3D11Query> end;
    bool issued{};
  };

  struct ScaledSceneTargets {
    ComPtr<ID3D11Texture2D> colorTexture;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11ShaderResourceView> shaderResource;
    ComPtr<ID3D11Texture2D> depthTexture;
    ComPtr<ID3D11DepthStencilView> depthView;
    airfix::render::RenderTargetPixelExtent extent{};

    [[nodiscard]] bool complete() const noexcept {
      return colorTexture && renderTarget && shaderResource &&
             depthTexture && depthView && extent.width != 0U &&
             extent.height != 0U;
    }

    void swap(ScaledSceneTargets &other) noexcept {
      colorTexture.Swap(other.colorTexture);
      renderTarget.Swap(other.renderTarget);
      shaderResource.Swap(other.shaderResource);
      depthTexture.Swap(other.depthTexture);
      depthView.Swap(other.depthView);
      std::swap(extent, other.extent);
    }

  };

  struct MissionResources {
    airfix::content::LoadedMissionWorldRoom room;
    std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>
        crosshairs;
    std::optional<airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>
        healthGauge;
    std::optional<
        airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>
        rollingDigits;
    std::shared_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
        cameraMissionRuntime;
    std::shared_ptr<airfix::render::PlayerActorPoseRuntime> poseRuntime;
    std::vector<MeshResources> meshes;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
    std::vector<ComPtr<ID3D11ShaderResourceView>> crosshairTextures;
    std::vector<ComPtr<ID3D11ShaderResourceView>> healthGaugeTextures;
    std::vector<ComPtr<ID3D11ShaderResourceView>> rollingDigitTextures;
    airfix::render::CameraLogicalExtent referenceCameraCanvas{};
    float referenceHorizontalFovDegrees{};

    MissionResources(
        airfix::content::LoadedMissionWorldRoom &&loadedRoom,
        std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>
            &&loadedCrosshairs,
        std::optional<
            airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>
            &&loadedHealthGauge,
        std::optional<
            airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>
            &&loadedRollingDigits,
        std::shared_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
            gameplayCameraRuntime,
        std::shared_ptr<airfix::render::PlayerActorPoseRuntime>
            playerPoseRuntime,
        std::vector<MeshResources> &&meshResources,
        std::vector<ComPtr<ID3D11ShaderResourceView>> &&textureResources,
        std::vector<ComPtr<ID3D11ShaderResourceView>>
            &&crosshairTextureResources,
        std::vector<ComPtr<ID3D11ShaderResourceView>>
            &&healthGaugeTextureResources,
        std::vector<ComPtr<ID3D11ShaderResourceView>>
            &&rollingDigitTextureResources,
        const airfix::render::CameraLogicalExtent cameraCanvas,
        const float horizontalFovDegrees)
        : room(std::move(loadedRoom)), crosshairs(std::move(loadedCrosshairs)),
          healthGauge(std::move(loadedHealthGauge)),
          rollingDigits(std::move(loadedRollingDigits)),
          cameraMissionRuntime(std::move(gameplayCameraRuntime)),
          poseRuntime(std::move(playerPoseRuntime)),
          meshes(std::move(meshResources)),
          textures(std::move(textureResources)),
          crosshairTextures(std::move(crosshairTextureResources)),
          healthGaugeTextures(std::move(healthGaugeTextureResources)),
          rollingDigitTextures(std::move(rollingDigitTextureResources)),
          referenceCameraCanvas(cameraCanvas),
          referenceHorizontalFovDegrees(horizontalFovDegrees) {}
  };

  enum class ResizeAttemptKind : std::uint8_t {
    explicitSignal,
    pendingRetry,
  };

public:
  explicit Implementation(SDL_Window &window)
      : window_(&window), scene_(airfix::render::makePublicRenderSmokeScene()) {
    const auto submission =
        airfix::render::buildDrawSubmissionPlan(scene_.model, 1U);
    if (!submission.plan || !submission.issues.empty()) {
      throw std::runtime_error(
          "public render smoke scene did not produce a valid draw plan");
    }
    plan_ = *submission.plan;

    createDeviceAndSwapChain();
    createPipeline();
    meshResources_ = createGeometryResources(scene_.model);
    createTextures();
    const auto [initialWidth, initialHeight] = pixelSize(*window_);
    createSwapTargets(initialWidth, initialHeight);
  }

  void resize() {
    const auto [newWidth, newHeight] = pixelSize(*window_);
    (void)resizeToPixelExtent(
        newWidth, newHeight, ResizeAttemptKind::explicitSignal);
  }

  [[nodiscard]] bool resizeToPixelExtent(
      const int newWidth, const int newHeight,
      const ResizeAttemptKind attemptKind) {
    if (attemptKind == ResizeAttemptKind::explicitSignal) {
      resetPendingResizeBackoff();
    }
    if (newWidth <= 0 || newHeight <= 0) {
      releaseSwapTargets();
      width_ = 0;
      height_ = 0;
      viewport_ = {};
      clearPendingResize();
      return true;
    }

    const auto layout = buildLayout(
        settings_, static_cast<std::uint32_t>(newWidth),
        static_cast<std::uint32_t>(newHeight));
    if (!layout.complete()) {
      throw std::runtime_error(
          "native render layout is invalid after swap-chain resize");
    }
    const auto renderExtent = layout.layout->renderTargetExtent();
    if (!targetExtentSupported(renderExtent)) {
      throw std::runtime_error(
          "scaled 3D target exceeds the D3D11 texture dimension limit");
    }

    ScaledSceneTargets preparedTargets;
    const bool replacesScaledTargets =
        usesScaledSceneTarget(settings_) &&
        (!scaledSceneTargets_.complete() ||
         scaledSceneTargets_.extent != renderExtent);
    if (replacesScaledTargets &&
        !prepareScaledSceneTargets(renderExtent, preparedTargets)) {
      recordPendingResizeFailure(
          {
              static_cast<std::uint32_t>(newWidth),
              static_cast<std::uint32_t>(newHeight),
          },
          attemptKind);
      return false;
    }

    releaseSwapTargets();
    requireSuccess(swapChain_->ResizeBuffers(0U, static_cast<UINT>(newWidth),
                                             static_cast<UINT>(newHeight),
                                             DXGI_FORMAT_UNKNOWN, 0U),
                   "IDXGISwapChain::ResizeBuffers");
    createSwapTargets(newWidth, newHeight);
    if (replacesScaledTargets) {
      releaseScaledSceneTargetBindings();
      scaledSceneTargets_.swap(preparedTargets);
    }
    clearPendingResize();
    return true;
  }

  [[nodiscard]] RenderPresentationSettingsApplyResult
  applyRenderPresentationSettings(
      const airfix::render::RenderPresentationSettings &candidate,
      const RenderPresentationSettingsPublicationGate
          publicationGate) noexcept {
    const auto delta =
        airfix::render::diffRenderPresentationSettings(settings_, candidate);
    if (!delta.complete()) {
      return {
          .changed = false,
          .issue =
              RenderPresentationSettingsApplyIssueKind::invalidSettings,
      };
    }
    if (!delta.delta->anyChanged()) {
      return {
          .changed = false,
          .issue = std::nullopt,
      };
    }

    const bool reportUnavailable =
        std::exchange(reportSurfaceUnavailableForNextApply_, false);
    if (reportUnavailable || width_ <= 0 || height_ <= 0 ||
        !renderTarget_ || !depthView_) {
      return {
          .changed = false,
          .issue =
              RenderPresentationSettingsApplyIssueKind::surfaceUnavailable,
      };
    }

    const auto layout = buildLayout(
        candidate, static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_));
    if (!layout.complete()) {
      return {
          .changed = false,
          .issue =
              RenderPresentationSettingsApplyIssueKind::invalidLayout,
      };
    }
    const auto renderExtent = layout.layout->renderTargetExtent();
    if (!targetExtentSupported(renderExtent)) {
      return {
          .changed = false,
          .issue =
              RenderPresentationSettingsApplyIssueKind::
                  unsupportedTargetExtent,
      };
    }

    ScaledSceneTargets preparedTargets;
    if (delta.delta->scaleTargetsChanged &&
        usesScaledSceneTarget(candidate) &&
        !prepareScaledSceneTargets(renderExtent, preparedTargets)) {
      return {
          .changed = false,
          .issue =
              RenderPresentationSettingsApplyIssueKind::
                  targetPreparationFailed,
      };
    }

    if (!delta.delta->scaleTargetsChanged &&
        usesScaledSceneTarget(candidate) &&
        (!scaledSceneTargets_.complete() ||
         scaledSceneTargets_.extent != renderExtent)) {
      return {
          .changed = false,
          .issue =
              RenderPresentationSettingsApplyIssueKind::
                  targetPreparationFailed,
      };
    }

    if (!publicationGate.accepts(candidate)) {
      return {
          .changed = false,
          .issue =
              RenderPresentationSettingsApplyIssueKind::
                  publicationGateRejected,
      };
    }

    if (delta.delta->scaleTargetsChanged) {
      releaseScaledSceneTargetBindings();
      scaledSceneTargets_.swap(preparedTargets);
    }
    if (delta.delta->diagnosticsChanged) {
      overlaySuppressed_ = false;
      if (!candidate.diagnosticsOverlayEnabled) {
        releaseDiagnosticsOverlayResources();
      }
    }
    settings_ = candidate;
    return {
        .changed = true,
        .issue = std::nullopt,
    };
  }

  [[nodiscard]] airfix::render::RenderPresentationSettings
  renderPresentationSettings() const noexcept {
    return settings_;
  }

  void failNextScaledTargetPreparationsAfterColorForTesting(
      const std::uint32_t failureCount) noexcept {
    remainingScaledTargetPreparationFailuresAfterColor_ =
        failureCount;
  }

  void reportSurfaceUnavailableForNextApplyForTesting() noexcept {
    reportSurfaceUnavailableForNextApply_ = true;
  }

  [[nodiscard]] bool resizeToPixelExtentForTesting(
      const int width, const int height) {
    return resizeToPixelExtent(
        width, height, ResizeAttemptKind::explicitSignal);
  }

  [[nodiscard]] std::array<const void *, 5U>
  scaledSceneTargetIdentityForTesting() const noexcept {
    return {
        scaledSceneTargets_.colorTexture.Get(),
        scaledSceneTargets_.renderTarget.Get(),
        scaledSceneTargets_.shaderResource.Get(),
        scaledSceneTargets_.depthTexture.Get(),
        scaledSceneTargets_.depthView.Get(),
    };
  }

  [[nodiscard]] std::optional<airfix::render::RenderTargetPixelRect>
  lastSceneViewportForTesting() const noexcept {
    return lastRenderedSceneViewport_;
  }

  [[nodiscard]] std::optional<airfix::render::ScenePresentationMode>
  lastScenePresentationForTesting() const noexcept {
    return lastRenderedScenePresentation_;
  }

  [[nodiscard]] std::optional<
      airfix::render::SceneTextureSamplingPolicy>
  lastSceneTextureSamplingPolicyForTesting() const noexcept {
    return lastRenderedSceneTextureSamplingPolicy_;
  }

  [[nodiscard]] bool
  hasDiagnosticsOverlayResourcesForTesting() const noexcept {
    return overlayTexture_ && overlayShaderResource_ &&
           overlayExtent_.width != 0U && overlayExtent_.height != 0U;
  }

  [[nodiscard]] bool hasProductUiOverlayResourcesForTesting() const noexcept {
    return productUiTexture_ && productUiShaderResource_ &&
           productUiExtent_.width != 0U && productUiExtent_.height != 0U;
  }

  void installLoadedMissionRoom(
      airfix::content::LoadedMissionWorldRoom &&room,
      const airfix::content::ContentRevision &expectedRevision) {
    installLoadedMissionRoomTransaction(std::move(room), std::nullopt,
                                        std::nullopt, std::nullopt,
                                        expectedRevision);
  }

  void installLoadedMissionRoom(
      airfix::content::LoadedMissionWorldRoom &&room,
      airfix::content::LoadedLegacyWeaponCrosshairTextureSet &&crosshairs,
      airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet &&healthGauge,
      airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet
          &&rollingDigits,
      const airfix::content::ContentRevision &expectedRevision) {
    installLoadedMissionRoomTransaction(
        std::move(room),
        std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>{
            std::move(crosshairs)},
        std::optional<
            airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>{
            std::move(healthGauge)},
        std::optional<
            airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>{
            std::move(rollingDigits)},
        expectedRevision);
  }

  void installLoadedMissionRoomTransaction(
      airfix::content::LoadedMissionWorldRoom &&room,
      std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>
          &&crosshairs,
      std::optional<airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>
          &&healthGauge,
      std::optional<
          airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>
          &&rollingDigits,
      const airfix::content::ContentRevision &expectedRevision) {
    if (airfix::content::validateMissionWorldRoomPublication(room,
                                                             expectedRevision)
            .has_value()) {
      throw std::runtime_error(
          "authenticated mission room failed its publication contract");
    }

    const auto rebuiltSubmission = airfix::render::buildDrawSubmissionPlan(
        room.model, room.textures.size());
    if (!rebuiltSubmission.plan || !rebuiltSubmission.issues.empty() ||
        rebuiltSubmission.plan->meshUploads != room.submission.meshUploads ||
        rebuiltSubmission.plan->commands != room.submission.commands) {
      throw std::runtime_error(
          "authenticated mission room draw submission is inconsistent");
    }
    if (crosshairs.has_value() &&
        (!crosshairs->valid() || crosshairs->revision != expectedRevision)) {
      throw std::runtime_error(
          "authenticated weapon crosshair set failed its publication contract");
    }
    if (healthGauge.has_value() &&
        (!healthGauge->valid() || healthGauge->revision != expectedRevision)) {
      throw std::runtime_error("authenticated aircraft health gauge set failed "
                               "its publication contract");
    }
    if (rollingDigits.has_value() &&
        (!rollingDigits->valid() ||
         rollingDigits->revision != expectedRevision)) {
      throw std::runtime_error("authenticated aircraft rolling digit atlas "
                               "failed its publication contract");
    }

    const auto cameraInitializeInput = gameplayCameraInitializeInput(room);

    const auto posePlan =
        airfix::render::planPlayerActorPoseRuntime(
            room.playerActorBinding, room.playerActorInstanceProvenance,
            room.model.instances);
    if (posePlan.status ==
        airfix::render::PlayerActorPoseRuntimePreparationStatus::
            resourceLimit) {
      throw std::runtime_error(
          "authenticated mission pose runtime exceeds its bounded resources");
    }
    auto posePreparation =
        airfix::render::preparePlayerActorPoseRuntime(
            room.playerActorBinding, room.playerActorInstanceProvenance,
            room.model.instances, actorWorldFrom(room.playerSpawnPose),
            posePlan);
    if (posePreparation.status ==
        airfix::render::PlayerActorPoseRuntimePreparationStatus::
            resourceLimit) {
      throw std::runtime_error(
          "authenticated mission pose runtime allocation failed");
    }
    if (posePreparation.status ==
            airfix::render::PlayerActorPoseRuntimePreparationStatus::
                invalidPayload ||
        (posePreparation.status ==
             airfix::render::PlayerActorPoseRuntimePreparationStatus::
                 ready &&
         posePreparation.runtime == nullptr) ||
        (posePreparation.status ==
             airfix::render::PlayerActorPoseRuntimePreparationStatus::
                 noPlayer &&
         posePreparation.runtime != nullptr)) {
      throw std::runtime_error(
          "authenticated mission pose runtime is inconsistent");
    }

    auto meshes = createGeometryResources(room.model);
    auto textures = createMissionTextures(room.textures);
    auto crosshairTextures =
        crosshairs.has_value()
            ? createWeaponCrosshairTextures(*crosshairs)
            : std::vector<ComPtr<ID3D11ShaderResourceView>>{};
    auto healthGaugeTextures =
        healthGauge.has_value()
            ? createAircraftHealthGaugeTextures(*healthGauge)
            : std::vector<ComPtr<ID3D11ShaderResourceView>>{};
    auto rollingDigitTextures =
        rollingDigits.has_value()
            ? createAircraftHudRollingDigitTextures(*rollingDigits)
            : std::vector<ComPtr<ID3D11ShaderResourceView>>{};

    // Full validation and GPU preparation must complete before mission-owned
    // collision storage is moved. The active mission remains untouched on
    // every failure before the final no-fail publication.
    auto cameraRuntimeBuild =
        airfix::render::LegacyGameplayCameraMissionRuntime::create(
            std::move(room.spatialArena),
            std::move(room.placedDynamicCollision),
            std::move(room.playerActorCollision), room.runtimeBasis,
            cameraInitializeInput);
    if (!cameraRuntimeBuild.complete()) {
      throw std::runtime_error(
          "authenticated mission gameplay camera runtime is invalid");
    }
    auto cameraMissionRuntime = std::shared_ptr<
        airfix::render::LegacyGameplayCameraMissionRuntime>(
        std::move(cameraRuntimeBuild.runtime));
    const auto initialCamera = cameraMissionRuntime->tryAcquire();
    if (!initialCamera.has_value() || !initialCamera->valid() ||
        initialCamera->packet() == nullptr ||
        initialCamera->simulationStep() != 0U ||
        initialCamera->cameraPublicationGeneration() != 1U) {
      throw std::runtime_error(
          "authenticated mission gameplay camera bootstrap is invalid");
    }
    const auto *const initialCameraPacket = initialCamera->packet();
    const airfix::render::CameraLogicalExtent referenceCameraCanvas{
        initialCameraPacket->logicalCanvasWidth(),
        initialCameraPacket->logicalCanvasHeight(),
    };
    const float referenceHorizontalFovDegrees =
        initialCameraPacket->pose().projection().horizontalFovDegrees();

    std::vector<airfix::content::LoadedTextureAsset>().swap(room.textures);
    auto candidate = std::make_unique<MissionResources>(
        std::move(room), std::move(crosshairs), std::move(healthGauge),
        std::move(rollingDigits),
        std::move(cameraMissionRuntime), std::move(posePreparation.runtime),
        std::move(meshes), std::move(textures), std::move(crosshairTextures),
        std::move(healthGaugeTextures), std::move(rollingDigitTextures),
        referenceCameraCanvas,
        referenceHorizontalFovDegrees);
    mission_ = std::move(candidate);
  }

  [[nodiscard]] bool missionWorldRoomInstalled() const noexcept {
    return mission_ != nullptr;
  }

  [[nodiscard]] std::optional<
      std::weak_ptr<airfix::render::PlayerActorPoseRuntime>>
  playerActorPoseRuntimeEndpoint() const noexcept {
    if (mission_ == nullptr || mission_->poseRuntime == nullptr) {
      return std::nullopt;
    }
    return std::weak_ptr<airfix::render::PlayerActorPoseRuntime>{
        mission_->poseRuntime};
  }

  [[nodiscard]]
  std::weak_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
  gameplayCameraMissionRuntimeEndpoint() const noexcept {
    if (mission_ == nullptr || mission_->cameraMissionRuntime == nullptr) {
      return {};
    }
    return mission_->cameraMissionRuntime;
  }

  [[nodiscard]] std::optional<airfix::render::RenderFrameDiagnostics>
  frameDiagnostics() const noexcept {
    return diagnostics_.latest();
  }

  [[nodiscard]] bool renderFrame(
      const bool validateGpuOutput,
      const std::filesystem::path *captureOutput = nullptr,
      const airfix::render::LegacyGameplayCameraClipPacket *cameraOverride =
          nullptr,
      const airfix::content::LegacyWeaponCrosshairSpriteSubmission
          *crosshairSubmission = nullptr,
      const airfix::content::LegacyAircraftHealthGaugeSubmission
          *healthGaugeSubmission = nullptr) {
    const auto frameStarted = std::chrono::steady_clock::now();
    double frameIntervalMilliseconds = 1000.0 / 60.0;
    if (previousFrameStart_.has_value()) {
      frameIntervalMilliseconds =
          std::chrono::duration<double, std::milli>(
              frameStarted - *previousFrameStart_)
              .count();
      if (!std::isfinite(frameIntervalMilliseconds) ||
          frameIntervalMilliseconds <= 0.0) {
        frameIntervalMilliseconds = 1000.0 / 60.0;
      }
    }
    previousFrameStart_ = frameStarted;
    collectGpuTimestamps();

    if (pendingResizeExtent_.has_value()) {
      if (pendingResizeRetryDelayFrames_ != 0U) {
        --pendingResizeRetryDelayFrames_;
      } else {
        const auto pending = *pendingResizeExtent_;
        (void)resizeToPixelExtent(
            static_cast<int>(pending.width),
            static_cast<int>(pending.height),
            ResizeAttemptKind::pendingRetry);
      }
    }
    if (!renderTarget_) {
      if (pendingResizeExtent_.has_value()) {
        return !validateGpuOutput;
      }
      resize();
      if (!renderTarget_) {
        return !validateGpuOutput;
      }
    }

    constexpr std::array<float, 4U> clearColor{0.035F, 0.055F, 0.085F, 1.0F};
    const bool gameplay = mission_ != nullptr;
    std::optional<airfix::render::LegacyGameplayCameraPacketLease>
        gameplayCameraLease;
    if (gameplay && cameraOverride == nullptr) {
      if (mission_->cameraMissionRuntime == nullptr) {
        return false;
      }
      // Exactly one read lease owns the camera packet for the entire frame.
      // A bounded acquisition miss drops this frame; private gameplay never
      // falls back to the immutable bootstrap camera.
      gameplayCameraLease = mission_->cameraMissionRuntime->tryAcquire();
      if (!gameplayCameraLease.has_value() ||
          !gameplayCameraLease->valid() ||
          gameplayCameraLease->packet() == nullptr) {
        return false;
      }
    }
    const auto *const gameplayCamera =
        cameraOverride != nullptr
        ? cameraOverride
        : (gameplayCameraLease.has_value()
               ? gameplayCameraLease->packet()
               : nullptr);
    const auto layout = buildLayout(
        settings_, static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_), gameplayCamera);
    if (!layout.complete()) {
      throw std::runtime_error("native render layout is invalid");
    }
    const auto renderExtent = layout.layout->renderTargetExtent();
    if (!targetExtentSupported(renderExtent)) {
      throw std::runtime_error(
          "scaled 3D target exceeds the D3D11 texture dimension limit");
    }
    const bool usesScaledSceneTarget =
        Implementation::usesScaledSceneTarget(settings_);
    if (usesScaledSceneTarget &&
        (!scaledSceneTargets_.complete() ||
         scaledSceneTargets_.extent != renderExtent)) {
      throw std::runtime_error(
          "published render settings have no prepared scaled targets");
    }
    GpuTimestampQueries *const gpuTimestamp =
        beginGpuTimestamp();

    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor.data());
    ID3D11RenderTargetView *sceneRenderTarget =
        usesScaledSceneTarget
        ? scaledSceneTargets_.renderTarget.Get()
        : renderTarget_.Get();
    ID3D11DepthStencilView *sceneDepthView =
        usesScaledSceneTarget
        ? scaledSceneTargets_.depthView.Get()
        : depthView_.Get();
    if (usesScaledSceneTarget) {
      context_->ClearRenderTargetView(
          sceneRenderTarget, clearColor.data());
    }
    context_->ClearDepthStencilView(
        sceneDepthView, D3D11_CLEAR_DEPTH,
        gameplay ? airfix::render::legacyReverseDepthClearValue : 1.0F, 0U);

    context_->OMSetRenderTargets(
        1U, &sceneRenderTarget, sceneDepthView);
    const auto fitted =
        layout.layout->sceneViewportInRenderTarget();
    lastRenderedSceneViewport_ = fitted;
    lastRenderedScenePresentation_ =
        layout.layout->scenePresentation();
    const D3D11_VIEWPORT activeViewport{
        fitted.x, fitted.y, fitted.width, fitted.height, 0.0F, 1.0F,
    };
    context_->RSSetViewports(1U, &activeViewport);
    context_->RSSetState(rasterizer_.Get());
    constexpr std::array<float, 4U> blendFactor{};
    context_->OMSetBlendState(
        nullptr, blendFactor.data(), 0xFFFFFFFFU);
    context_->OMSetDepthStencilState(
        gameplay ? gameplayDepthState_.Get() : smokeDepthState_.Get(), 0U);
    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(gameplay ? gameplayVertexShader_.Get()
                                   : smokeVertexShader_.Get(),
                          nullptr, 0U);
    context_->PSSetShader(gameplay ? gameplayPixelShader_.Get()
                                   : smokePixelShader_.Get(),
                          nullptr, 0U);

    const auto sceneTextureSampling =
        airfix::render::sceneTextureSamplingPolicyForProfile(
            settings_.visualProfile);
    if (!sceneTextureSampling.has_value()) {
      throw std::runtime_error(
          "published visual profile has no scene texture sampler policy");
    }
    ID3D11SamplerState *sampler =
        sceneTextureSampling->mode ==
                airfix::render::SceneTextureSamplingMode::
                    anisotropicMipLinear
            ? enhancedSceneSampler_.Get()
            : classicSceneSampler_.Get();
    if (sampler == nullptr) {
      throw std::runtime_error("scene texture sampler is unavailable");
    }
    context_->PSSetSamplers(0U, 1U, &sampler);
    lastRenderedSceneTextureSamplingPolicy_ = *sceneTextureSampling;

    const auto &model = gameplay ? mission_->room.model : scene_.model;
    const auto &submission = gameplay ? mission_->room.submission : plan_;
    const auto &meshes = gameplay ? mission_->meshes : meshResources_;
    // One lease covers the complete encoded frame. A missed acquisition keeps
    // the immutable authored pose, matching the Metal consumer contract.
    std::optional<airfix::render::DynamicInstancePoseLease> poseLease;
    if (gameplay && mission_->poseRuntime != nullptr) {
      poseLease = mission_->poseRuntime->tryAcquire();
    }
    std::uint64_t sceneTriangleCount = 0U;
    for (const auto &command : submission.commands) {
      sceneTriangleCount += command.indexCount / 3U;
      const auto meshSlot = static_cast<std::size_t>(command.meshSlot);
      if (meshSlot >= meshes.size() ||
          command.instanceIndex >= model.instances.size()) {
        throw std::runtime_error("draw plan references a missing GPU resource");
      }

      const auto &mesh = meshes[meshSlot];
      if (!mesh.vertices || !mesh.indices) {
        throw std::runtime_error("draw command references an empty GPU mesh");
      }
      if (cameraOverride != nullptr) {
        const auto instanceIndex =
            static_cast<std::size_t>(command.instanceIndex);
        // The capture-only cutaway hides exterior-facing room-shell polygons.
        // Placed objects and the player keep the normal two-sided state.
        const bool roomShell =
            instanceIndex < mission_->room.instanceProvenance.size() &&
            mission_->room.instanceProvenance[instanceIndex]
                .physicalRoomIndex.has_value();
        context_->RSSetState(
            roomShell ? overviewRasterizer_.Get() : rasterizer_.Get());
      }
      constexpr UINT stride = sizeof(GpuVertex);
      constexpr UINT offset = 0U;
      ID3D11Buffer *vertexBuffer = mesh.vertices.Get();
      context_->IASetVertexBuffers(0U, 1U, &vertexBuffer, &stride, &offset);
      context_->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R32_UINT, 0U);

      if (gameplay) {
        const auto &authoredInstance =
            model.instances[command.instanceIndex];
        const auto resolvedPose =
            poseLease.has_value()
            ? poseLease->resolve(
                  static_cast<std::uint32_t>(command.instanceIndex),
                  authoredInstance.modelLinear,
                  authoredInstance.modelTranslation)
            : airfix::render::ResolvedInstancePose{
                  .modelLinear = authoredInstance.modelLinear,
                  .modelTranslation =
                      authoredInstance.modelTranslation,
              };
        const GameplayUniforms uniforms = makeGameplayUniforms(
            resolvedPose, *gameplayCamera, *layout.layout);
        context_->UpdateSubresource(gameplayUniforms_.Get(), 0U, nullptr,
                                    &uniforms, 0U, 0U);
        ID3D11Buffer *constantBuffer = gameplayUniforms_.Get();
        context_->VSSetConstantBuffers(1U, 1U, &constantBuffer);
      } else {
        const SmokeUniforms uniforms =
            makeSmokeUniforms(model.instances[command.instanceIndex]);
        context_->UpdateSubresource(smokeUniforms_.Get(), 0U, nullptr,
                                    &uniforms, 0U, 0U);
        ID3D11Buffer *constantBuffer = smokeUniforms_.Get();
        context_->VSSetConstantBuffers(0U, 1U, &constantBuffer);
      }

      ID3D11ShaderResourceView *texture = fallbackTexture_.Get();
      if (command.primary.has_value() &&
          command.texcoordMode == airfix::render::TexcoordMode::uv0) {
        if (gameplay) {
          const auto assetIndex =
              static_cast<std::size_t>(command.primary->value);
          if (assetIndex < mission_->textures.size()) {
            texture = mission_->textures[assetIndex].Get();
          }
        } else {
          texture = texture_.Get();
        }
      }
      context_->PSSetShaderResources(0U, 1U, &texture);

      context_->DrawIndexed(command.indexCount, command.firstIndex, 0);
    }
    poseLease.reset();

    if (usesScaledSceneTarget) {
      presentScaledScene();
    }

    const std::size_t healthGaugeDrawCallCount =
        healthGaugeSubmission != nullptr
            ? drawLegacyAircraftHealthGauge(*healthGaugeSubmission)
            : 0U;
    const bool crosshairDrawn =
        crosshairSubmission != nullptr &&
        drawLegacyWeaponCrosshair(*crosshairSubmission);

    const std::uint64_t sceneDrawCallCount =
        static_cast<std::uint64_t>(submission.commands.size());
    const std::uint64_t auxiliaryDrawCallCount =
        (usesScaledSceneTarget ? 1U : 0U) +
        static_cast<std::uint64_t>(healthGaugeDrawCallCount) +
        (crosshairDrawn ? 1U : 0U);
    const std::uint64_t auxiliaryTriangleCount =
        (usesScaledSceneTarget ? 1U : 0U) +
        static_cast<std::uint64_t>(healthGaugeDrawCallCount) * 2U +
        (crosshairDrawn ? 2U : 0U);
    const auto cpuSampled = std::chrono::steady_clock::now();
    const double cpuFrameMilliseconds =
        std::chrono::duration<double, std::milli>(
            cpuSampled - frameStarted)
            .count();
    const auto memoryBytes = estimateGpuMemoryBytes(
        model, meshes, gameplay, renderExtent, usesScaledSceneTarget);
    const bool diagnosticAccepted = diagnostics_.record({
        .outputExtent =
            {
                static_cast<std::uint32_t>(width_),
                static_cast<std::uint32_t>(height_),
            },
        .renderTargetExtent = renderExtent,
        .renderScalePercent = settings_.renderScalePercent,
        .visualProfile = settings_.visualProfile,
        .sceneTextureSampling = *sceneTextureSampling,
        .frameIntervalMilliseconds = frameIntervalMilliseconds,
        .cpuFrameMilliseconds = cpuFrameMilliseconds,
        .gpuFrameMilliseconds = latestGpuFrameMilliseconds_,
        .drawCallCount =
            sceneDrawCallCount + auxiliaryDrawCallCount,
        .sceneDrawCallCount = sceneDrawCallCount,
        .triangleCount =
            sceneTriangleCount + auxiliaryTriangleCount,
        .sceneTriangleCount = sceneTriangleCount,
        .activeLightCount = 0U,
        .gpuMemoryBytes = memoryBytes,
        .gpuMemoryMeasurement =
            airfix::render::GpuMemoryMeasurement::estimated,
    });
    if (diagnosticAccepted && settings_.diagnosticsOverlayEnabled &&
        !overlaySuppressed_) {
      try {
        drawDiagnosticsOverlay(frameStarted);
      } catch (...) {
        // Developer instrumentation is best-effort and cannot suppress a
        // valid game frame or influence deterministic simulation.
        overlaySuppressed_ = true;
        releaseDiagnosticsOverlayResources();
      }
    }
    if (productUiShaderResource_) {
      drawProductUiOverlay();
    }
    endGpuTimestamp(gpuTimestamp);

    std::optional<CapturedBackBuffer> captured;
    if (validateGpuOutput || captureOutput != nullptr) {
      captured = captureBackBuffer();
    }
    const bool outputValid =
        !validateGpuOutput || hasVisibleGpuOutput(*captured);
    if (captureOutput != nullptr) {
      writeBmp(*captureOutput, *captured);
    }
    requireSuccess(swapChain_->Present(validateGpuOutput ? 0U : 1U, 0U),
                   "IDXGISwapChain::Present");
    return outputValid;
  }

  void captureFrameToBmp(const std::filesystem::path &outputPath) {
    if (!missionWorldRoomInstalled()) {
      throw std::runtime_error(
          "private frame capture requires an installed mission");
    }
    if (!renderFrame(true, &outputPath)) {
      throw std::runtime_error(
          "private frame capture produced no visible D3D11 output");
    }
  }

  void captureMissionOverviewFrameToBmp(
      const std::filesystem::path &outputPath) {
    if (!missionWorldRoomInstalled()) {
      throw std::runtime_error(
          "private overview capture requires an installed mission");
    }
    if (width_ <= 0 || height_ <= 0) {
      throw std::runtime_error(
          "private overview capture requires a drawable surface");
    }
    auto captureSettings = settings_;
    captureSettings.scenePresentation =
        airfix::render::ScenePresentationMode::widescreenHorPlus;
    captureSettings.verticalFovAdjustmentDegrees = 0.0F;
    captureSettings.diagnosticsOverlayEnabled = true;
    if (!applyRenderPresentationSettings(captureSettings, {}).accepted()) {
      throw std::runtime_error(
          "private overview capture settings could not be applied");
    }
    overlaySuppressed_ = false;
    const auto overview = airfix::render::buildSceneOverviewCamera(
        mission_->room.model,
        {
            .logicalCanvasWidth = static_cast<std::uint32_t>(width_),
            .logicalCanvasHeight = static_cast<std::uint32_t>(height_),
        });
    if (!overview.complete()) {
      throw std::runtime_error(
          "private mission did not produce a valid overview camera");
    }
    if (!renderFrame(
            true, &outputPath,
            &overview.snapshot->clipPacket)) {
      throw std::runtime_error(
          "private overview capture produced no visible D3D11 output");
    }
  }

  void captureMissionCrosshairValidationFrameToBmp(
      const std::filesystem::path &outputPath) {
    if (!missionWorldRoomInstalled() || !mission_->crosshairs.has_value() ||
        width_ <= 0 || height_ <= 0) {
      throw std::runtime_error(
          "private crosshair validation requires an installed authenticated "
          "mission and sight set");
    }
    const auto layout = buildLayout(
        settings_, static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_));
    if (!layout.complete()) {
      throw std::runtime_error(
          "private crosshair validation requires a drawable native layout");
    }
    const auto &textures = *mission_->crosshairs;
    const auto &texture = textures.textures.front();
    const float outputSize =
        static_cast<float>(
            airfix::content::legacyWeaponCrosshairTextureWidth) *
        layout.layout->uiScale() * settings_.uiScalePercent / 100.0F;
    const airfix::render::LegacyWeaponCrosshairSpritePlan plan{
        .projectedTarget = {},
        .outputRect =
            {
                (static_cast<float>(width_) - outputSize) * 0.5F,
                (static_cast<float>(height_) - outputSize) * 0.5F,
                outputSize,
                outputSize,
            },
        .logicalDistanceScale = 1.0F,
        .insideSceneViewport = true,
        .withinRecoveredDepthRange = true,
    };
    const airfix::content::LegacyWeaponCrosshairBinding binding{
        .weaponType =
            airfix::simulation::LegacyWeaponTypeId::machineGun,
        .role = texture.role,
        .textureId = texture.textureId,
        .revision = textures.revision,
        .transactionIdentity = textures.transactionIdentity,
    };
    const auto submission =
        airfix::content::buildLegacyWeaponCrosshairSpriteSubmission(
            plan, binding,
            airfix::content::LegacyWeaponCrosshairVisibilityDecision::draw);
    if (!submission.ready() ||
        !renderFrame(true, &outputPath, nullptr, &*submission.submission)) {
      throw std::runtime_error(
          "private crosshair validation produced no visible D3D11 output");
    }
  }

  void captureMissionHealthGaugeValidationFrameToBmp(
      const std::filesystem::path &outputPath) {
    if (!missionWorldRoomInstalled() || !mission_->healthGauge.has_value() ||
        width_ <= 0 || height_ <= 0) {
      throw std::runtime_error(
          "private health-gauge validation requires an installed "
          "authenticated mission and gauge texture set");
    }
    const auto layout =
        buildLayout(settings_, static_cast<std::uint32_t>(width_),
                    static_cast<std::uint32_t>(height_));
    if (!layout.complete()) {
      throw std::runtime_error(
          "private health-gauge validation requires a drawable native "
          "layout");
    }
    const auto plan = airfix::render::buildLegacyAircraftHealthGaugePlan({
        .activeWindowPresent = false,
        .cameraAttachedAtEntry = true,
        .typeHudEnabled = true,
        .cameraAttachedAfterLayout = true,
        .screenWidth = 640U,
        .screenHeight = 480U,
        .displayedHealth = 50.0F,
        .maximumHealth = 100.0F,
        .armourMeterTextureAvailable = true,
        .armourTextureAvailable = true,
    });
    const auto submission =
        airfix::content::buildLegacyAircraftHealthGaugeSubmission(
            plan, *mission_->healthGauge, *layout.layout,
            settings_.uiScalePercent);
    if (!submission.ready() || !renderFrame(true, &outputPath, nullptr, nullptr,
                                            &*submission.submission)) {
      throw std::runtime_error(
          "private health-gauge validation produced no visible D3D11 "
          "output");
    }
  }

  void capturePublicDiagnosticFrameToBmp(
      const std::filesystem::path &outputPath) {
    if (missionWorldRoomInstalled()) {
      throw std::runtime_error(
          "public diagnostic capture rejects installed private content");
    }
    auto captureSettings = settings_;
    captureSettings.diagnosticsOverlayEnabled = true;
    if (!applyRenderPresentationSettings(captureSettings, {}).accepted()) {
      throw std::runtime_error(
          "public diagnostic capture settings could not be applied");
    }
    overlaySuppressed_ = false;
    if (!renderFrame(true, &outputPath)) {
      throw std::runtime_error(
          "public diagnostic capture produced no visible D3D11 output");
    }
  }

  [[nodiscard]] bool
  setProductUiRaster(const AirfixWindowsUiRaster &raster) noexcept {
    if (!raster.complete() || width_ <= 0 || height_ <= 0 ||
        raster.width != static_cast<std::uint32_t>(width_) ||
        raster.height != static_cast<std::uint32_t>(height_)) {
      return false;
    }

    try {
      ComPtr<ID3D11Texture2D> preparedTexture;
      ComPtr<ID3D11ShaderResourceView> preparedView;
      ID3D11Texture2D *targetTexture = productUiTexture_.Get();
      if (!targetTexture || productUiExtent_.width != raster.width ||
          productUiExtent_.height != raster.height) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = raster.width;
        description.Height = raster.height;
        description.MipLevels = 1U;
        description.ArraySize = 1U;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc = {1U, 0U};
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        requireSuccess(device_->CreateTexture2D(&description, nullptr,
                                                preparedTexture.GetAddressOf()),
                       "ID3D11Device::CreateTexture2D(product UI)");
        requireSuccess(
            device_->CreateShaderResourceView(preparedTexture.Get(), nullptr,
                                              preparedView.GetAddressOf()),
            "ID3D11Device::CreateShaderResourceView(product UI)");
        targetTexture = preparedTexture.Get();
      }

      D3D11_MAPPED_SUBRESOURCE mapped{};
      requireSuccess(context_->Map(targetTexture, 0U, D3D11_MAP_WRITE_DISCARD,
                                   0U, &mapped),
                     "ID3D11DeviceContext::Map(product UI)");
      if (mapped.RowPitch < raster.rowPitchBytes) {
        context_->Unmap(targetTexture, 0U);
        return false;
      }
      for (std::uint32_t row = 0U; row < raster.height; ++row) {
        auto *destination = static_cast<std::uint8_t *>(mapped.pData) +
                            static_cast<std::size_t>(row) * mapped.RowPitch;
        const auto *source =
            raster.premultipliedBgra8.data() +
            static_cast<std::size_t>(row) * raster.rowPitchBytes;
        std::copy_n(source, raster.rowPitchBytes, destination);
      }
      context_->Unmap(targetTexture, 0U);

      if (preparedTexture) {
        productUiTexture_ = std::move(preparedTexture);
        productUiShaderResource_ = std::move(preparedView);
        productUiExtent_ = {raster.width, raster.height};
      }
      return true;
    } catch (...) {
      return false;
    }
  }

  void clearProductUiRaster() noexcept {
    if (context_) {
      ID3D11ShaderResourceView *nullView = nullptr;
      context_->PSSetShaderResources(0U, 1U, &nullView);
    }
    productUiShaderResource_.Reset();
    productUiTexture_.Reset();
    productUiExtent_ = {};
  }

  void capturePublicSettingsPanelFrameToBmp(
      const std::filesystem::path &outputPath) {
    if (missionWorldRoomInstalled()) {
      throw std::runtime_error(
          "public settings capture rejects installed private content");
    }
    if (!productUiShaderResource_ || productUiExtent_.width == 0U ||
        productUiExtent_.height == 0U) {
      throw std::runtime_error(
          "public settings capture requires a published product UI");
    }
    if (!renderFrame(true, &outputPath)) {
      throw std::runtime_error(
          "public settings capture produced no visible D3D11 output");
    }
  }

private:
  [[nodiscard]] static bool usesScaledSceneTarget(
      const airfix::render::RenderPresentationSettings &settings) noexcept {
    return settings.renderScalePercent !=
           airfix::render::native_render_policy::
               defaultRenderScalePercent;
  }

  [[nodiscard]] static bool targetExtentSupported(
      const airfix::render::RenderTargetPixelExtent extent) noexcept {
    return extent.width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
           extent.height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  }

  [[nodiscard]] airfix::render::NativeRenderLayoutBuildResult
  buildLayout(
      const airfix::render::RenderPresentationSettings &settings,
      const std::uint32_t outputWidth,
      const std::uint32_t outputHeight,
      const airfix::render::LegacyGameplayCameraClipPacket
          *const gameplayCamera = nullptr) const noexcept {
    auto config = airfix::render::NativeRenderLayoutConfig{
        .outputExtent = {
            outputWidth,
            outputHeight,
        },
        .renderScalePercent = settings.renderScalePercent,
        .scenePresentation = settings.scenePresentation,
        .verticalFovAdjustmentDegrees =
            settings.verticalFovAdjustmentDegrees,
    };
    if (gameplayCamera != nullptr) {
      config.referenceCameraCanvas = {
          gameplayCamera->logicalCanvasWidth(),
          gameplayCamera->logicalCanvasHeight(),
      };
      config.referenceHorizontalFovDegrees =
          gameplayCamera->pose().projection().horizontalFovDegrees();
    } else if (mission_ != nullptr) {
      // Target preparation outside a render frame cannot borrow the SPSC
      // consumer endpoint. Gameplay frames always provide the leased packet.
      config.referenceCameraCanvas = mission_->referenceCameraCanvas;
      config.referenceHorizontalFovDegrees =
          mission_->referenceHorizontalFovDegrees;
    }
    return airfix::render::buildNativeRenderLayout(config);
  }

  void releaseDiagnosticsOverlayResources() noexcept {
    if (context_) {
      ID3D11ShaderResourceView *nullView = nullptr;
      context_->PSSetShaderResources(0U, 1U, &nullView);
    }
    overlayShaderResource_.Reset();
    overlayTexture_.Reset();
    overlayExtent_ = {};
    lastOverlayRefresh_.reset();
  }

  void releaseScaledSceneTargetBindings() noexcept {
    if (context_) {
      ID3D11ShaderResourceView *nullView = nullptr;
      context_->PSSetShaderResources(0U, 1U, &nullView);
    }
  }

  void collectGpuTimestamps() noexcept {
    for (auto &queries : gpuTimestampQueries_) {
      if (!queries.issued) {
        continue;
      }
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
      const HRESULT disjointResult = context_->GetData(
          queries.disjoint.Get(), &disjoint, sizeof(disjoint),
          D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if (disjointResult == S_FALSE) {
        continue;
      }
      if (FAILED(disjointResult)) {
        queries.issued = false;
        continue;
      }
      UINT64 start = 0U;
      UINT64 end = 0U;
      const HRESULT startResult = context_->GetData(
          queries.start.Get(), &start, sizeof(start),
          D3D11_ASYNC_GETDATA_DONOTFLUSH);
      const HRESULT endResult = context_->GetData(
          queries.end.Get(), &end, sizeof(end),
          D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if (startResult == S_FALSE || endResult == S_FALSE) {
        continue;
      }
      queries.issued = false;
      if (FAILED(startResult) || FAILED(endResult) ||
          disjoint.Disjoint || disjoint.Frequency == 0U ||
          end < start) {
        continue;
      }
      latestGpuFrameMilliseconds_ =
          static_cast<double>(end - start) * 1000.0 /
          static_cast<double>(disjoint.Frequency);
    }
  }

  [[nodiscard]] GpuTimestampQueries *beginGpuTimestamp() noexcept {
    for (std::size_t offset = 0U;
         offset < gpuTimestampQueries_.size();
         ++offset) {
      const auto index =
          (nextGpuTimestampQuery_ + offset) %
          gpuTimestampQueries_.size();
      auto &queries = gpuTimestampQueries_[index];
      if (queries.issued) {
        continue;
      }
      nextGpuTimestampQuery_ =
          (index + 1U) % gpuTimestampQueries_.size();
      context_->Begin(queries.disjoint.Get());
      context_->End(queries.start.Get());
      return &queries;
    }
    return nullptr;
  }

  void endGpuTimestamp(GpuTimestampQueries *const queries) noexcept {
    if (queries == nullptr) {
      return;
    }
    context_->End(queries->end.Get());
    context_->End(queries->disjoint.Get());
    queries->issued = true;
  }

  [[nodiscard]] static std::uint64_t saturatingAdd(
      const std::uint64_t left,
      const std::uint64_t right) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
  }

  [[nodiscard]] static std::uint64_t textureBytes(
      ID3D11ShaderResourceView *const view) noexcept {
    if (view == nullptr) {
      return 0U;
    }
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    if (!resource || FAILED(resource.As(&texture)) || !texture) {
      return 0U;
    }
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    std::uint64_t total = 0U;
    UINT width = description.Width;
    UINT height = description.Height;
    const UINT levels = std::max(1U, description.MipLevels);
    for (UINT level = 0U; level < levels; ++level) {
      const auto pixels =
          static_cast<std::uint64_t>(width) * height;
      total = saturatingAdd(
          total, pixels * 4U * description.ArraySize);
      width = std::max(1U, width / 2U);
      height = std::max(1U, height / 2U);
    }
    return total;
  }

  [[nodiscard]] std::uint64_t estimateGpuMemoryBytes(
      const airfix::render::DrawModelPayload &,
      const std::vector<MeshResources> &meshes,
      const bool gameplay,
      const airfix::render::RenderTargetPixelExtent renderExtent,
      const bool usesScaledSceneTarget) const noexcept {
    const auto outputPixels =
        static_cast<std::uint64_t>(width_) *
        static_cast<std::uint64_t>(height_);
    // Two BGRA8 swap-chain buffers plus one 32-bit depth target.
    std::uint64_t total = outputPixels * 12U;
    if (usesScaledSceneTarget) {
      const auto scaledPixels =
          static_cast<std::uint64_t>(renderExtent.width) *
          renderExtent.height;
      total = saturatingAdd(total, scaledPixels * 8U);
    }
    for (const auto &mesh : meshes) {
      for (ID3D11Buffer *buffer :
           {mesh.vertices.Get(), mesh.indices.Get()}) {
        if (buffer == nullptr) {
          continue;
        }
        D3D11_BUFFER_DESC description{};
        buffer->GetDesc(&description);
        total = saturatingAdd(total, description.ByteWidth);
      }
    }
    total = saturatingAdd(total, textureBytes(fallbackTexture_.Get()));
    if (gameplay && mission_) {
      for (const auto &texture : mission_->textures) {
        total = saturatingAdd(total, textureBytes(texture.Get()));
      }
      for (const auto &texture : mission_->crosshairTextures) {
        total = saturatingAdd(total, textureBytes(texture.Get()));
      }
      for (const auto &texture : mission_->healthGaugeTextures) {
        total = saturatingAdd(total, textureBytes(texture.Get()));
      }
    } else {
      total = saturatingAdd(total, textureBytes(texture_.Get()));
    }
    total = saturatingAdd(
        total, sizeof(SmokeUniforms) + sizeof(GameplayUniforms) +
                   sizeof(OverlayUniforms));
    if (overlayExtent_.width != 0U &&
        overlayExtent_.height != 0U) {
      total = saturatingAdd(
          total,
          static_cast<std::uint64_t>(overlayExtent_.width) *
              overlayExtent_.height * 4U);
    }
    if (productUiExtent_.width != 0U && productUiExtent_.height != 0U) {
      total = saturatingAdd(total,
                            static_cast<std::uint64_t>(productUiExtent_.width) *
                                productUiExtent_.height * 4U);
    }
    return total;
  }

  void updateDiagnosticsOverlayTexture(
      const airfix::render::RenderDiagnosticsImage &image) {
    if (!image.complete()) {
      throw std::runtime_error(
          "portable render diagnostics image is incomplete");
    }
    if (!overlayTexture_ ||
        overlayExtent_.width != image.width ||
        overlayExtent_.height != image.height) {
      D3D11_TEXTURE2D_DESC description{};
      description.Width = image.width;
      description.Height = image.height;
      description.MipLevels = 1U;
      description.ArraySize = 1U;
      description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      description.SampleDesc = {1U, 0U};
      description.Usage = D3D11_USAGE_DYNAMIC;
      description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

      ComPtr<ID3D11Texture2D> texture;
      ComPtr<ID3D11ShaderResourceView> view;
      requireSuccess(
          device_->CreateTexture2D(
              &description, nullptr, texture.GetAddressOf()),
          "ID3D11Device::CreateTexture2D(diagnostics overlay)");
      requireSuccess(
          device_->CreateShaderResourceView(
              texture.Get(), nullptr, view.GetAddressOf()),
          "ID3D11Device::CreateShaderResourceView(diagnostics overlay)");
      overlayTexture_ = std::move(texture);
      overlayShaderResource_ = std::move(view);
      overlayExtent_ = {image.width, image.height};
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    requireSuccess(
        context_->Map(
            overlayTexture_.Get(), 0U, D3D11_MAP_WRITE_DISCARD, 0U,
            &mapped),
        "ID3D11DeviceContext::Map(diagnostics overlay)");
    if (mapped.RowPitch < image.bytesPerRow) {
      context_->Unmap(overlayTexture_.Get(), 0U);
      throw std::runtime_error(
          "D3D11 diagnostics overlay row pitch is invalid");
    }
    for (std::uint32_t row = 0U; row < image.height; ++row) {
      auto *destination =
          static_cast<std::uint8_t *>(mapped.pData) +
          static_cast<std::size_t>(row) * mapped.RowPitch;
      const auto *source =
          image.rgba8.data() +
          static_cast<std::size_t>(row) * image.bytesPerRow;
      std::copy_n(source, image.bytesPerRow, destination);
    }
    context_->Unmap(overlayTexture_.Get(), 0U);
    overlayPixelScale_ = image.pixelScale;
  }

  void drawDiagnosticsOverlay(
      const std::chrono::steady_clock::time_point frameStarted) {
    if (!diagnostics_.latest().has_value()) {
      return;
    }
    constexpr auto refreshInterval =
        std::chrono::milliseconds(250);
    if (!lastOverlayRefresh_.has_value() ||
        frameStarted - *lastOverlayRefresh_ >= refreshInterval ||
        !overlayShaderResource_) {
      const auto image =
          airfix::render::rasterizeRenderFrameDiagnostics(
              *diagnostics_.latest(), settings_.uiScalePercent);
      updateDiagnosticsOverlayTexture(image);
      lastOverlayRefresh_ = frameStarted;
    }
    if (!overlayShaderResource_ || !renderTarget_ ||
        overlayExtent_.width == 0U || overlayExtent_.height == 0U) {
      return;
    }

    const float margin =
        static_cast<float>(overlayPixelScale_ * 8U);
    const OverlayUniforms uniforms{
        .outputAndPanelSize =
            {
                static_cast<float>(width_),
                static_cast<float>(height_),
                static_cast<float>(overlayExtent_.width),
                static_cast<float>(overlayExtent_.height),
            },
        .panelOrigin = {margin, margin, 0.0F, 0.0F},
        .tint = {1.0F, 1.0F, 1.0F, 1.0F},
        .uvRect = {0.0F, 0.0F, 1.0F, 1.0F},
    };
    context_->UpdateSubresource(
        overlayUniforms_.Get(), 0U, nullptr, &uniforms, 0U, 0U);

    ID3D11RenderTargetView *outputTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1U, &outputTarget, nullptr);
    context_->RSSetViewports(1U, &viewport_);
    context_->RSSetState(rasterizer_.Get());
    context_->OMSetDepthStencilState(presentationDepthState_.Get(), 0U);
    constexpr std::array<float, 4U> blendFactor{};
    context_->OMSetBlendState(
        overlayBlendState_.Get(), blendFactor.data(), 0xFFFFFFFFU);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(overlayVertexShader_.Get(), nullptr, 0U);
    ID3D11Buffer *uniformBuffer = overlayUniforms_.Get();
    context_->VSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetShader(overlayPixelShader_.Get(), nullptr, 0U);
    ID3D11SamplerState *sampler = uiSampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);
    ID3D11ShaderResourceView *view =
        overlayShaderResource_.Get();
    context_->PSSetShaderResources(0U, 1U, &view);
    context_->Draw(6U, 0U);

    ID3D11ShaderResourceView *nullView = nullptr;
    context_->PSSetShaderResources(0U, 1U, &nullView);
    context_->OMSetBlendState(
        nullptr, blendFactor.data(), 0xFFFFFFFFU);
  }

  void drawProductUiOverlay() {
    if (!productUiShaderResource_ || !renderTarget_ ||
        productUiExtent_.width == 0U || productUiExtent_.height == 0U) {
      return;
    }

    const OverlayUniforms uniforms{
        .outputAndPanelSize =
            {
                static_cast<float>(width_),
                static_cast<float>(height_),
                static_cast<float>(productUiExtent_.width),
                static_cast<float>(productUiExtent_.height),
            },
        .panelOrigin = {0.0F, 0.0F, 0.0F, 0.0F},
        .tint = {1.0F, 1.0F, 1.0F, 1.0F},
        .uvRect = {0.0F, 0.0F, 1.0F, 1.0F},
    };
    context_->UpdateSubresource(overlayUniforms_.Get(), 0U, nullptr, &uniforms,
                                0U, 0U);

    ID3D11RenderTargetView *outputTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1U, &outputTarget, nullptr);
    context_->RSSetViewports(1U, &viewport_);
    context_->RSSetState(rasterizer_.Get());
    context_->OMSetDepthStencilState(presentationDepthState_.Get(), 0U);
    constexpr std::array<float, 4U> blendFactor{};
    context_->OMSetBlendState(productUiBlendState_.Get(), blendFactor.data(),
                              0xFFFFFFFFU);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(overlayVertexShader_.Get(), nullptr, 0U);
    ID3D11Buffer *uniformBuffer = overlayUniforms_.Get();
    context_->VSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetShader(overlayPixelShader_.Get(), nullptr, 0U);
    ID3D11SamplerState *sampler = uiSampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);
    ID3D11ShaderResourceView *view = productUiShaderResource_.Get();
    context_->PSSetShaderResources(0U, 1U, &view);
    context_->Draw(6U, 0U);

    ID3D11ShaderResourceView *nullView = nullptr;
    context_->PSSetShaderResources(0U, 1U, &nullView);
    context_->OMSetBlendState(nullptr, blendFactor.data(), 0xFFFFFFFFU);
  }

  [[nodiscard]] bool drawLegacyWeaponCrosshair(
      const airfix::content::LegacyWeaponCrosshairSpriteSubmission
          &submission) {
    if (!mission_ || !mission_->crosshairs.has_value() || !renderTarget_ ||
        submission.blendMode !=
            airfix::content::LegacyWeaponCrosshairBlendMode::
                sourceAlphaOneMinusSourceAlpha ||
        submission.depthMode !=
            airfix::content::LegacyWeaponCrosshairDepthMode::alwaysWrite ||
        submission.tintArgb !=
            airfix::content::legacyWeaponCrosshairTintArgb ||
        submission.uv != airfix::content::LegacyWeaponCrosshairUvRect{} ||
        !submission.belongsTo(*mission_->crosshairs)) {
      return false;
    }
    const auto textureIndex =
        static_cast<std::size_t>(submission.textureId.value);
    if (textureIndex >= mission_->crosshairTextures.size() ||
        !mission_->crosshairTextures[textureIndex]) {
      return false;
    }

    const auto &rectangle = submission.outputRect;
    if (!std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
        !std::isfinite(rectangle.width) ||
        !std::isfinite(rectangle.height) || rectangle.width <= 0.0F ||
        rectangle.height <= 0.0F) {
      return false;
    }
    const OverlayUniforms uniforms{
        .outputAndPanelSize =
            {
                static_cast<float>(width_),
                static_cast<float>(height_),
                rectangle.width,
                rectangle.height,
            },
        .panelOrigin = {rectangle.x, rectangle.y, 0.0F, 0.0F},
        .tint = normalizedArgb(submission.tintArgb),
        .uvRect = {0.0F, 0.0F, 1.0F, 1.0F},
    };
    context_->UpdateSubresource(overlayUniforms_.Get(), 0U, nullptr, &uniforms,
                                0U, 0U);

    ID3D11RenderTargetView *outputTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1U, &outputTarget, depthView_.Get());
    context_->RSSetViewports(1U, &viewport_);
    context_->RSSetState(rasterizer_.Get());
    context_->OMSetDepthStencilState(crosshairDepthState_.Get(), 0U);
    constexpr std::array<float, 4U> blendFactor{};
    context_->OMSetBlendState(overlayBlendState_.Get(), blendFactor.data(),
                              0xFFFFFFFFU);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(overlayVertexShader_.Get(), nullptr, 0U);
    ID3D11Buffer *uniformBuffer = overlayUniforms_.Get();
    context_->VSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetShader(overlayPixelShader_.Get(), nullptr, 0U);
    ID3D11SamplerState *sampler = crosshairSampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);
    ID3D11ShaderResourceView *view =
        mission_->crosshairTextures[textureIndex].Get();
    context_->PSSetShaderResources(0U, 1U, &view);
    context_->Draw(6U, 0U);

    ID3D11ShaderResourceView *nullView = nullptr;
    context_->PSSetShaderResources(0U, 1U, &nullView);
    context_->OMSetBlendState(nullptr, blendFactor.data(), 0xFFFFFFFFU);
    return true;
  }

  [[nodiscard]] std::size_t drawLegacyAircraftHealthGauge(
      const airfix::content::LegacyAircraftHealthGaugeSubmission &submission) {
    if (!mission_ || !mission_->healthGauge.has_value() || !renderTarget_ ||
        !submission.belongsTo(*mission_->healthGauge) ||
        submission.commandCount == 0U ||
        submission.commandCount > submission.orderedCommands.size()) {
      return 0U;
    }

    const auto validPoint = [&](const airfix::render::OutputPixelPoint point) {
      return std::isfinite(point.x) && std::isfinite(point.y) &&
             point.x >= 0.0F && point.y >= 0.0F &&
             point.x <= static_cast<float>(width_) &&
             point.y <= static_cast<float>(height_);
    };
    for (std::size_t index = 0U; index < submission.commandCount; ++index) {
      const auto &command = submission.orderedCommands[index];
      if (command.depthMode !=
              airfix::content::LegacyAircraftHealthGaugeDepthMode::
                  alwaysWrite ||
          command.samplingMode !=
              airfix::content::LegacyAircraftHealthGaugeSamplingMode::
                  linearClamp ||
          !std::all_of(command.outputQuad.begin(), command.outputQuad.end(),
                       validPoint)) {
        return 0U;
      }
      if (command.textured()) {
        if (command.blendMode !=
                airfix::content::LegacyAircraftHealthGaugeBlendMode::
                    sourceAlphaOneMinusSourceAlpha ||
            command.colourArgb != 0xFFFFFFFFU ||
            command.uv != airfix::content::LegacyAircraftHealthGaugeUvRect{}) {
          return 0U;
        }
        const auto textureIndex =
            static_cast<std::size_t>(command.textureId.value);
        if (textureIndex >= mission_->healthGaugeTextures.size() ||
            !mission_->healthGaugeTextures[textureIndex]) {
          return 0U;
        }
      } else if (command.kind !=
                     airfix::render::LegacyAircraftHealthGaugeCommandKind::
                         damageMaskQuad ||
                 command.blendMode !=
                     airfix::content::LegacyAircraftHealthGaugeBlendMode::
                         opaque ||
                 command.colourArgb != 0xFF000000U) {
        return 0U;
      }
    }

    ID3D11RenderTargetView *outputTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1U, &outputTarget, depthView_.Get());
    context_->RSSetViewports(1U, &viewport_);
    context_->RSSetState(rasterizer_.Get());
    context_->OMSetDepthStencilState(crosshairDepthState_.Get(), 0U);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(gaugeVertexShader_.Get(), nullptr, 0U);
    ID3D11Buffer *uniformBuffer = gaugeUniforms_.Get();
    context_->VSSetConstantBuffers(3U, 1U, &uniformBuffer);
    context_->PSSetConstantBuffers(3U, 1U, &uniformBuffer);
    ID3D11SamplerState *sampler = crosshairSampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);
    constexpr std::array<float, 4U> blendFactor{};

    for (std::size_t index = 0U; index < submission.commandCount; ++index) {
      const auto &command = submission.orderedCommands[index];
      GaugeUniforms uniforms{
          .outputSize = {static_cast<float>(width_),
                         static_cast<float>(height_), 0.0F, 0.0F},
          .tint = normalizedArgb(command.colourArgb),
      };
      for (std::size_t pointIndex = 0U; pointIndex < command.outputQuad.size();
           ++pointIndex) {
        uniforms.outputQuad[pointIndex] = {
            command.outputQuad[pointIndex].x,
            command.outputQuad[pointIndex].y,
            0.0F,
            0.0F,
        };
      }
      context_->UpdateSubresource(gaugeUniforms_.Get(), 0U, nullptr, &uniforms,
                                  0U, 0U);

      ID3D11ShaderResourceView *view = nullptr;
      if (command.textured()) {
        context_->OMSetBlendState(overlayBlendState_.Get(), blendFactor.data(),
                                  0xFFFFFFFFU);
        context_->PSSetShader(gaugeTexturePixelShader_.Get(), nullptr, 0U);
        view = mission_->healthGaugeTextures[command.textureId.value].Get();
      } else {
        context_->OMSetBlendState(nullptr, blendFactor.data(), 0xFFFFFFFFU);
        context_->PSSetShader(gaugeSolidPixelShader_.Get(), nullptr, 0U);
      }
      context_->PSSetShaderResources(0U, 1U, &view);
      context_->Draw(6U, 0U);
    }

    ID3D11ShaderResourceView *nullView = nullptr;
    context_->PSSetShaderResources(0U, 1U, &nullView);
    context_->OMSetBlendState(nullptr, blendFactor.data(), 0xFFFFFFFFU);
    return submission.commandCount;
  }

  [[nodiscard]] std::size_t drawLegacyAircraftHudRollingDigits(
      const airfix::content::LegacyAircraftHudRollingDigitsSubmission
          &submission) {
    if (!mission_ || !mission_->rollingDigits.has_value() || !renderTarget_ ||
        !submission.belongsTo(*mission_->rollingDigits) ||
        submission.commandCount == 0U ||
        submission.commandCount > submission.orderedCommands.size()) {
      return 0U;
    }
    for (std::size_t index = 0U; index < submission.commandCount; ++index) {
      const auto &command = submission.orderedCommands[index];
      const auto &rectangle = command.outputRect;
      const auto &uv = command.uv;
      const auto textureIndex =
          static_cast<std::size_t>(command.textureId.value);
      if (command.textureRole !=
              airfix::content::LegacyAircraftHudRollingDigitsTextureRole::
                  digits ||
          command.blendMode !=
              airfix::content::LegacyAircraftHudRollingDigitsBlendMode::
                  sourceAlphaOneMinusSourceAlpha ||
          command.depthMode !=
              airfix::content::LegacyAircraftHudRollingDigitsDepthMode::
                  alwaysWrite ||
          command.samplingMode !=
              airfix::content::LegacyAircraftHudRollingDigitsSamplingMode::
                  linearClamp ||
          !std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
          !std::isfinite(rectangle.width) ||
          !std::isfinite(rectangle.height) || rectangle.x < 0.0F ||
          rectangle.y < 0.0F || rectangle.width <= 0.0F ||
          rectangle.height <= 0.0F ||
          rectangle.x + rectangle.width > static_cast<float>(width_) ||
          rectangle.y + rectangle.height > static_cast<float>(height_) ||
          !std::isfinite(uv.minimumU) || !std::isfinite(uv.minimumV) ||
          !std::isfinite(uv.maximumU) || !std::isfinite(uv.maximumV) ||
          uv.minimumU < 0.0F || uv.minimumV < 0.0F ||
          uv.maximumU > 1.0F || uv.maximumV > 1.0F ||
          uv.minimumU >= uv.maximumU || uv.minimumV >= uv.maximumV ||
          textureIndex >= mission_->rollingDigitTextures.size() ||
          !mission_->rollingDigitTextures[textureIndex]) {
        return 0U;
      }
    }

    ID3D11RenderTargetView *outputTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1U, &outputTarget, depthView_.Get());
    context_->RSSetViewports(1U, &viewport_);
    context_->RSSetState(rasterizer_.Get());
    context_->OMSetDepthStencilState(crosshairDepthState_.Get(), 0U);
    constexpr std::array<float, 4U> blendFactor{};
    context_->OMSetBlendState(overlayBlendState_.Get(), blendFactor.data(),
                              0xFFFFFFFFU);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(overlayVertexShader_.Get(), nullptr, 0U);
    ID3D11Buffer *uniformBuffer = overlayUniforms_.Get();
    context_->VSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetConstantBuffers(2U, 1U, &uniformBuffer);
    context_->PSSetShader(overlayPixelShader_.Get(), nullptr, 0U);
    ID3D11SamplerState *sampler = crosshairSampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);

    for (std::size_t index = 0U; index < submission.commandCount; ++index) {
      const auto &command = submission.orderedCommands[index];
      const auto &rectangle = command.outputRect;
      const OverlayUniforms uniforms{
          .outputAndPanelSize = {static_cast<float>(width_),
                                 static_cast<float>(height_), rectangle.width,
                                 rectangle.height},
          .panelOrigin = {rectangle.x, rectangle.y, 0.0F, 0.0F},
          .tint = normalizedArgb(command.tintArgb),
          .uvRect = {command.uv.minimumU, command.uv.minimumV,
                     command.uv.maximumU, command.uv.maximumV},
      };
      context_->UpdateSubresource(overlayUniforms_.Get(), 0U, nullptr,
                                  &uniforms, 0U, 0U);
      ID3D11ShaderResourceView *view =
          mission_->rollingDigitTextures[command.textureId.value].Get();
      context_->PSSetShaderResources(0U, 1U, &view);
      context_->Draw(6U, 0U);
    }

    ID3D11ShaderResourceView *nullView = nullptr;
    context_->PSSetShaderResources(0U, 1U, &nullView);
    context_->OMSetBlendState(nullptr, blendFactor.data(), 0xFFFFFFFFU);
    return submission.commandCount;
  }

  void releaseSwapTargets() {
    context_->OMSetRenderTargets(0U, nullptr, nullptr);
    renderTarget_.Reset();
    depthView_.Reset();
    // ResizeBuffers requires every immediate-context reference to the old
    // back buffer to be released before the call.
    context_->Flush();
  }

  void resetPendingResizeBackoff() noexcept {
    pendingResizeRetryDelayFrames_ = 0U;
    pendingResizeRetryFailureCount_ = 0U;
  }

  void clearPendingResize() noexcept {
    pendingResizeExtent_.reset();
    resetPendingResizeBackoff();
  }

  void recordPendingResizeFailure(
      const airfix::render::OutputPixelExtent extent,
      const ResizeAttemptKind attemptKind) noexcept {
    pendingResizeExtent_ = extent;
    if (attemptKind == ResizeAttemptKind::explicitSignal) {
      resetPendingResizeBackoff();
      return;
    }

    constexpr std::uint32_t maximumDelayFrames = 120U;
    constexpr std::uint32_t maximumShift = 7U;
    const auto shift =
        std::min(pendingResizeRetryFailureCount_, maximumShift);
    pendingResizeRetryDelayFrames_ =
        std::min(1U << shift, maximumDelayFrames);
    if (pendingResizeRetryFailureCount_ < maximumShift) {
      ++pendingResizeRetryFailureCount_;
    }
  }

  void createDeviceAndSwapChain() {
    const auto [initialWidth, initialHeight] = pixelSize(*window_);
    if (initialWidth <= 0 || initialHeight <= 0) {
      throw std::runtime_error("Windows render surface has no drawable pixels");
    }

    const SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
    auto *nativeWindow = static_cast<HWND>(SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (nativeWindow == nullptr) {
      throw std::runtime_error(
          "SDL did not expose a Win32 HWND for the window");
    }

    DXGI_SWAP_CHAIN_DESC swapDescription{};
    swapDescription.BufferDesc.Width = static_cast<UINT>(initialWidth);
    swapDescription.BufferDesc.Height = static_cast<UINT>(initialHeight);
    swapDescription.BufferDesc.RefreshRate = {60U, 1U};
    swapDescription.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDescription.SampleDesc = {1U, 0U};
    swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.BufferCount = 2U;
    swapDescription.OutputWindow = nativeWindow;
    swapDescription.Windowed = TRUE;
    swapDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr std::array<D3D_FEATURE_LEVEL, 1U> featureLevels{
        D3D_FEATURE_LEVEL_11_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    auto attempt = [&](const D3D_DRIVER_TYPE driverType,
                       const UINT attemptFlags) {
      device_.Reset();
      context_.Reset();
      swapChain_.Reset();
      return D3D11CreateDeviceAndSwapChain(
          nullptr, driverType, nullptr, attemptFlags, featureLevels.data(),
          static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
          &swapDescription, swapChain_.GetAddressOf(), device_.GetAddressOf(),
          &featureLevel_, context_.GetAddressOf());
    };

    HRESULT result = attempt(D3D_DRIVER_TYPE_HARDWARE, flags);
#if defined(_DEBUG)
    if (FAILED(result)) {
      flags &= ~D3D11_CREATE_DEVICE_DEBUG;
      result = attempt(D3D_DRIVER_TYPE_HARDWARE, flags);
    }
#endif
    if (FAILED(result)) {
      result = attempt(D3D_DRIVER_TYPE_WARP, flags);
    }
    requireSuccess(result, "D3D11CreateDeviceAndSwapChain");

    if (featureLevel_ < D3D_FEATURE_LEVEL_11_0) {
      throw std::runtime_error("Direct3D feature level 11_0 is required");
    }
  }

  void createPipeline() {
    const ComPtr<ID3DBlob> smokeVertexBytecode =
        compileShader("AirfixSmokeVS", "vs_5_0");
    const ComPtr<ID3DBlob> smokePixelBytecode =
        compileShader("AirfixSmokePS", "ps_5_0");
    const ComPtr<ID3DBlob> gameplayVertexBytecode =
        compileShader("AirfixGameplayVS", "vs_5_0");
    const ComPtr<ID3DBlob> gameplayPixelBytecode =
        compileShader("AirfixGameplayPS", "ps_5_0");
    const ComPtr<ID3DBlob> presentationVertexBytecode =
        compileShader("AirfixPresentationVS", "vs_5_0");
    const ComPtr<ID3DBlob> presentationPixelBytecode =
        compileShader("AirfixPresentationPS", "ps_5_0");
    const ComPtr<ID3DBlob> overlayVertexBytecode =
        compileShader("AirfixOverlayVS", "vs_5_0");
    const ComPtr<ID3DBlob> overlayPixelBytecode =
        compileShader("AirfixOverlayPS", "ps_5_0");
    const ComPtr<ID3DBlob> gaugeVertexBytecode =
        compileShader("AirfixGaugeVS", "vs_5_0");
    const ComPtr<ID3DBlob> gaugeTexturePixelBytecode =
        compileShader("AirfixGaugeTexturePS", "ps_5_0");
    const ComPtr<ID3DBlob> gaugeSolidPixelBytecode =
        compileShader("AirfixGaugeSolidPS", "ps_5_0");

    requireSuccess(
        device_->CreateVertexShader(smokeVertexBytecode->GetBufferPointer(),
                                    smokeVertexBytecode->GetBufferSize(),
                                    nullptr, smokeVertexShader_.GetAddressOf()),
        "ID3D11Device::CreateVertexShader(smoke)");
    requireSuccess(
        device_->CreatePixelShader(smokePixelBytecode->GetBufferPointer(),
                                   smokePixelBytecode->GetBufferSize(), nullptr,
                                   smokePixelShader_.GetAddressOf()),
        "ID3D11Device::CreatePixelShader(smoke)");
    requireSuccess(device_->CreateVertexShader(
                       gameplayVertexBytecode->GetBufferPointer(),
                       gameplayVertexBytecode->GetBufferSize(), nullptr,
                       gameplayVertexShader_.GetAddressOf()),
                   "ID3D11Device::CreateVertexShader(gameplay)");
    requireSuccess(device_->CreatePixelShader(
                       gameplayPixelBytecode->GetBufferPointer(),
                       gameplayPixelBytecode->GetBufferSize(), nullptr,
                       gameplayPixelShader_.GetAddressOf()),
                   "ID3D11Device::CreatePixelShader(gameplay)");
    requireSuccess(device_->CreateVertexShader(
                       presentationVertexBytecode->GetBufferPointer(),
                       presentationVertexBytecode->GetBufferSize(), nullptr,
                       presentationVertexShader_.GetAddressOf()),
                   "ID3D11Device::CreateVertexShader(presentation)");
    requireSuccess(device_->CreatePixelShader(
                       presentationPixelBytecode->GetBufferPointer(),
                       presentationPixelBytecode->GetBufferSize(), nullptr,
                       presentationPixelShader_.GetAddressOf()),
                   "ID3D11Device::CreatePixelShader(presentation)");
    requireSuccess(device_->CreateVertexShader(
                       overlayVertexBytecode->GetBufferPointer(),
                       overlayVertexBytecode->GetBufferSize(), nullptr,
                       overlayVertexShader_.GetAddressOf()),
                   "ID3D11Device::CreateVertexShader(overlay)");
    requireSuccess(
        device_->CreatePixelShader(overlayPixelBytecode->GetBufferPointer(),
                                   overlayPixelBytecode->GetBufferSize(),
                                   nullptr, overlayPixelShader_.GetAddressOf()),
        "ID3D11Device::CreatePixelShader(overlay)");
    requireSuccess(
        device_->CreateVertexShader(gaugeVertexBytecode->GetBufferPointer(),
                                    gaugeVertexBytecode->GetBufferSize(),
                                    nullptr, gaugeVertexShader_.GetAddressOf()),
        "ID3D11Device::CreateVertexShader(health gauge)");
    requireSuccess(device_->CreatePixelShader(
                       gaugeTexturePixelBytecode->GetBufferPointer(),
                       gaugeTexturePixelBytecode->GetBufferSize(), nullptr,
                       gaugeTexturePixelShader_.GetAddressOf()),
                   "ID3D11Device::CreatePixelShader(health gauge texture)");
    requireSuccess(device_->CreatePixelShader(
                       gaugeSolidPixelBytecode->GetBufferPointer(),
                       gaugeSolidPixelBytecode->GetBufferSize(), nullptr,
                       gaugeSolidPixelShader_.GetAddressOf()),
                   "ID3D11Device::CreatePixelShader(health gauge solid)");

    constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 3U> inputElements{{
        {
            "POSITION",
            0U,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0U,
            0U,
            D3D11_INPUT_PER_VERTEX_DATA,
            0U,
        },
        {
            "NORMAL",
            0U,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0U,
            16U,
            D3D11_INPUT_PER_VERTEX_DATA,
            0U,
        },
        {
            "TEXCOORD",
            0U,
            DXGI_FORMAT_R32G32_FLOAT,
            0U,
            32U,
            D3D11_INPUT_PER_VERTEX_DATA,
            0U,
        },
    }};
    requireSuccess(
        device_->CreateInputLayout(
            inputElements.data(), static_cast<UINT>(inputElements.size()),
            smokeVertexBytecode->GetBufferPointer(),
            smokeVertexBytecode->GetBufferSize(), inputLayout_.GetAddressOf()),
        "ID3D11Device::CreateInputLayout");

    D3D11_BUFFER_DESC smokeUniformDescription{};
    smokeUniformDescription.ByteWidth = sizeof(SmokeUniforms);
    smokeUniformDescription.Usage = D3D11_USAGE_DEFAULT;
    smokeUniformDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    requireSuccess(device_->CreateBuffer(&smokeUniformDescription, nullptr,
                                         smokeUniforms_.GetAddressOf()),
                   "ID3D11Device::CreateBuffer(smoke uniforms)");

    D3D11_BUFFER_DESC gameplayUniformDescription{};
    gameplayUniformDescription.ByteWidth = sizeof(GameplayUniforms);
    gameplayUniformDescription.Usage = D3D11_USAGE_DEFAULT;
    gameplayUniformDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    requireSuccess(device_->CreateBuffer(&gameplayUniformDescription, nullptr,
                                         gameplayUniforms_.GetAddressOf()),
                   "ID3D11Device::CreateBuffer(gameplay uniforms)");

    D3D11_BUFFER_DESC overlayUniformDescription{};
    overlayUniformDescription.ByteWidth = sizeof(OverlayUniforms);
    overlayUniformDescription.Usage = D3D11_USAGE_DEFAULT;
    overlayUniformDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    requireSuccess(device_->CreateBuffer(&overlayUniformDescription, nullptr,
                                         overlayUniforms_.GetAddressOf()),
                   "ID3D11Device::CreateBuffer(overlay uniforms)");

    D3D11_BUFFER_DESC gaugeUniformDescription{};
    gaugeUniformDescription.ByteWidth = sizeof(GaugeUniforms);
    gaugeUniformDescription.Usage = D3D11_USAGE_DEFAULT;
    gaugeUniformDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    requireSuccess(device_->CreateBuffer(&gaugeUniformDescription, nullptr,
                                         gaugeUniforms_.GetAddressOf()),
                   "ID3D11Device::CreateBuffer(health-gauge uniforms)");

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    const auto classicSampling =
        airfix::render::sceneTextureSamplingPolicyForProfile(
            airfix::render::VisualProfile::classic);
    const auto enhancedSampling =
        airfix::render::sceneTextureSamplingPolicyForProfile(
            airfix::render::VisualProfile::enhanced);
    const auto classicFilter = classicSampling.has_value()
                                   ? d3dSamplerFilter(*classicSampling)
                                   : std::nullopt;
    const auto enhancedFilter = enhancedSampling.has_value()
                                    ? d3dSamplerFilter(*enhancedSampling)
                                    : std::nullopt;
    if (!classicSampling.has_value() || !enhancedSampling.has_value() ||
        !classicFilter.has_value() || !enhancedFilter.has_value()) {
      throw std::runtime_error(
          "portable scene texture sampling policy is invalid");
    }
    samplerDescription.Filter = *classicFilter;
    samplerDescription.MaxAnisotropy =
        static_cast<UINT>(classicSampling->maximumAnisotropy);
    requireSuccess(device_->CreateSamplerState(&samplerDescription,
                                               classicSceneSampler_.GetAddressOf()),
                   "ID3D11Device::CreateSamplerState(classic scene)");
    samplerDescription.Filter = *enhancedFilter;
    samplerDescription.MaxAnisotropy =
        static_cast<UINT>(enhancedSampling->maximumAnisotropy);
    requireSuccess(
        device_->CreateSamplerState(&samplerDescription,
                                    enhancedSceneSampler_.GetAddressOf()),
        "ID3D11Device::CreateSamplerState(enhanced scene)");
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDescription.MaxAnisotropy = 1U;
    requireSuccess(device_->CreateSamplerState(&samplerDescription,
                                               uiSampler_.GetAddressOf()),
                   "ID3D11Device::CreateSamplerState(UI)");
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    requireSuccess(
        device_->CreateSamplerState(&samplerDescription,
                                    crosshairSampler_.GetAddressOf()),
        "ID3D11Device::CreateSamplerState(weapon crosshair)");
    samplerDescription.Filter =
        D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    requireSuccess(device_->CreateSamplerState(
                       &samplerDescription,
                       presentationSampler_.GetAddressOf()),
                   "ID3D11Device::CreateSamplerState(presentation)");

    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;
    requireSuccess(device_->CreateRasterizerState(&rasterizerDescription,
                                                  rasterizer_.GetAddressOf()),
                   "ID3D11Device::CreateRasterizerState");
    // Never selected by ordinary gameplay frames. The provenance-gated
    // overview pass uses it only for physical room-shell contributors.
    rasterizerDescription.CullMode = D3D11_CULL_FRONT;
    requireSuccess(
        device_->CreateRasterizerState(&rasterizerDescription,
                                       overviewRasterizer_.GetAddressOf()),
        "ID3D11Device::CreateRasterizerState(overview)");

    D3D11_DEPTH_STENCIL_DESC smokeDepthDescription{};
    smokeDepthDescription.DepthEnable = TRUE;
    smokeDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    smokeDepthDescription.DepthFunc = D3D11_COMPARISON_LESS;
    requireSuccess(device_->CreateDepthStencilState(
                       &smokeDepthDescription, smokeDepthState_.GetAddressOf()),
                   "ID3D11Device::CreateDepthStencilState(smoke)");

    D3D11_DEPTH_STENCIL_DESC gameplayDepthDescription = smokeDepthDescription;
    gameplayDepthDescription.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    requireSuccess(
        device_->CreateDepthStencilState(&gameplayDepthDescription,
                                         gameplayDepthState_.GetAddressOf()),
        "ID3D11Device::CreateDepthStencilState(gameplay)");

    D3D11_DEPTH_STENCIL_DESC presentationDepthDescription{};
    presentationDepthDescription.DepthEnable = FALSE;
    requireSuccess(device_->CreateDepthStencilState(
                       &presentationDepthDescription,
                       presentationDepthState_.GetAddressOf()),
                   "ID3D11Device::CreateDepthStencilState(presentation)");

    D3D11_DEPTH_STENCIL_DESC crosshairDepthDescription{};
    crosshairDepthDescription.DepthEnable = TRUE;
    crosshairDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    crosshairDepthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
    requireSuccess(
        device_->CreateDepthStencilState(&crosshairDepthDescription,
                                         crosshairDepthState_.GetAddressOf()),
        "ID3D11Device::CreateDepthStencilState(weapon crosshair)");

    D3D11_BLEND_DESC overlayBlendDescription{};
    auto &overlayTarget =
        overlayBlendDescription.RenderTarget[0];
    overlayTarget.BlendEnable = TRUE;
    overlayTarget.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    overlayTarget.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    overlayTarget.BlendOp = D3D11_BLEND_OP_ADD;
    overlayTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
    overlayTarget.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    overlayTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    overlayTarget.RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    requireSuccess(
        device_->CreateBlendState(
            &overlayBlendDescription,
            overlayBlendState_.GetAddressOf()),
        "ID3D11Device::CreateBlendState(overlay)");

    auto productUiBlendDescription = overlayBlendDescription;
    auto &productUiTarget = productUiBlendDescription.RenderTarget[0];
    productUiTarget.SrcBlend = D3D11_BLEND_ONE;
    productUiTarget.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    productUiTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
    productUiTarget.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    requireSuccess(
        device_->CreateBlendState(&productUiBlendDescription,
                                  productUiBlendState_.GetAddressOf()),
        "ID3D11Device::CreateBlendState(product UI)");

    const D3D11_QUERY_DESC disjointDescription{
        .Query = D3D11_QUERY_TIMESTAMP_DISJOINT,
        .MiscFlags = 0U,
    };
    const D3D11_QUERY_DESC timestampDescription{
        .Query = D3D11_QUERY_TIMESTAMP,
        .MiscFlags = 0U,
    };
    for (auto &queries : gpuTimestampQueries_) {
      requireSuccess(
          device_->CreateQuery(
              &disjointDescription, queries.disjoint.GetAddressOf()),
          "ID3D11Device::CreateQuery(timestamp disjoint)");
      requireSuccess(
          device_->CreateQuery(
              &timestampDescription, queries.start.GetAddressOf()),
          "ID3D11Device::CreateQuery(timestamp start)");
      requireSuccess(
          device_->CreateQuery(
              &timestampDescription, queries.end.GetAddressOf()),
          "ID3D11Device::CreateQuery(timestamp end)");
    }
  }

  [[nodiscard]] std::vector<MeshResources>
  createGeometryResources(const airfix::render::DrawModelPayload &model) const {
    std::vector<MeshResources> result;
    result.reserve(model.meshes.size());
    for (const auto &mesh : model.meshes) {
      MeshResources resources;
      if (mesh.vertices.empty() && mesh.indices.empty()) {
        result.push_back(std::move(resources));
        continue;
      }
      if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::runtime_error(
            "GPU mesh has only one of its required buffers");
      }

      std::vector<GpuVertex> vertices;
      vertices.reserve(mesh.vertices.size());
      for (const auto &vertex : mesh.vertices) {
        vertices.push_back(repackVertex(vertex));
      }

      D3D11_BUFFER_DESC vertexDescription{};
      vertexDescription.ByteWidth =
          checkedByteWidth(vertices.size(), sizeof(GpuVertex), "vertex buffer");
      vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
      vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      D3D11_SUBRESOURCE_DATA vertexData{};
      vertexData.pSysMem = vertices.data();

      D3D11_BUFFER_DESC indexDescription{};
      indexDescription.ByteWidth = checkedByteWidth(
          mesh.indices.size(), sizeof(std::uint32_t), "index buffer");
      indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
      indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
      D3D11_SUBRESOURCE_DATA indexData{};
      indexData.pSysMem = mesh.indices.data();

      requireSuccess(device_->CreateBuffer(&vertexDescription, &vertexData,
                                           resources.vertices.GetAddressOf()),
                     "ID3D11Device::CreateBuffer(vertices)");
      requireSuccess(device_->CreateBuffer(&indexDescription, &indexData,
                                           resources.indices.GetAddressOf()),
                     "ID3D11Device::CreateBuffer(indices)");
      result.push_back(std::move(resources));
    }
    return result;
  }

  [[nodiscard]] ComPtr<ID3D11ShaderResourceView>
  createTexture(const UINT width, const UINT height,
                const std::uint8_t *pixels) const {
    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = width;
    textureDescription.Height = height;
    textureDescription.MipLevels = 1U;
    textureDescription.ArraySize = 1U;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc = {1U, 0U};
    textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
    textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA textureData{};
    textureData.pSysMem = pixels;
    textureData.SysMemPitch = width * 4U;

    ComPtr<ID3D11Texture2D> texture;
    requireSuccess(device_->CreateTexture2D(&textureDescription, &textureData,
                                            texture.GetAddressOf()),
                   "ID3D11Device::CreateTexture2D(texture)");

    ComPtr<ID3D11ShaderResourceView> view;
    requireSuccess(device_->CreateShaderResourceView(texture.Get(), nullptr,
                                                     view.GetAddressOf()),
                   "ID3D11Device::CreateShaderResourceView");
    return view;
  }

  template <typename LoadedTexture>
  [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createUploadedTexture(
      const LoadedTexture &source) const {
    const auto &upload = source.upload;
    if (upload.allocatedMipCount == 0U || upload.uploadedMipCount == 0U ||
        upload.uploadLevels.size() != upload.uploadedMipCount ||
        source.uploadLevels.size() != upload.uploadLevels.size()) {
      throw std::runtime_error(
          "mission texture upload metadata is inconsistent");
    }

    const auto &basePlan = upload.uploadLevels.front();
    const auto &baseImage = source.uploadLevels.front();
    if (basePlan.level != 0U || !validTextureLevel(basePlan, baseImage)) {
      throw std::runtime_error("mission texture base level is inconsistent");
    }

    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = checkedUint(baseImage.width, "texture width");
    textureDescription.Height = checkedUint(baseImage.height, "texture height");
    textureDescription.MipLevels = upload.allocatedMipCount;
    textureDescription.ArraySize = 1U;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc = {1U, 0U};
    textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> texture;
    if (upload.mipPolicy == airfix::render::GtiMipPolicy::authoredChain) {
      if (upload.uploadedMipCount != upload.allocatedMipCount) {
        throw std::runtime_error(
            "authored mission texture has an incomplete mip chain");
      }

      std::vector<D3D11_SUBRESOURCE_DATA> initialData(upload.allocatedMipCount);
      for (std::size_t index = 0U; index < source.uploadLevels.size();
           ++index) {
        const auto &level = upload.uploadLevels[index];
        const auto &image = source.uploadLevels[index];
        if (level.level != index || !validTextureLevel(level, image)) {
          throw std::runtime_error(
              "authored mission texture level is inconsistent");
        }
        initialData[index] = {
            .pSysMem = image.pixels.data(),
            .SysMemPitch = checkedUint(level.bytesPerRow, "texture row pitch"),
            .SysMemSlicePitch = 0U,
        };
      }

      textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
      requireSuccess(device_->CreateTexture2D(&textureDescription,
                                              initialData.data(),
                                              texture.GetAddressOf()),
                     "ID3D11Device::CreateTexture2D(authored mission texture)");
    } else {
      if (upload.mipPolicy != airfix::render::GtiMipPolicy::generateFromBase ||
          upload.uploadedMipCount != 1U || source.uploadLevels.size() != 1U) {
        throw std::runtime_error(
            "generated mission texture upload is inconsistent");
      }

      UINT formatSupport = 0U;
      requireSuccess(device_->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                 &formatSupport),
                     "ID3D11Device::CheckFormatSupport(texture autogen)");
      constexpr UINT requiredSupport =
          D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
          D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
      if ((formatSupport & requiredSupport) != requiredSupport) {
        throw std::runtime_error(
            "D3D11 device cannot generate RGBA8 texture mips");
      }

      textureDescription.Usage = D3D11_USAGE_DEFAULT;
      textureDescription.BindFlags |= D3D11_BIND_RENDER_TARGET;
      textureDescription.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
      requireSuccess(
          device_->CreateTexture2D(&textureDescription, nullptr,
                                   texture.GetAddressOf()),
          "ID3D11Device::CreateTexture2D(generated mission texture)");
      context_->UpdateSubresource(
          texture.Get(), 0U, nullptr, baseImage.pixels.data(),
          checkedUint(basePlan.bytesPerRow, "texture row pitch"), 0U);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
    viewDescription.Format = textureDescription.Format;
    viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDescription.Texture2D.MostDetailedMip = 0U;
    viewDescription.Texture2D.MipLevels = upload.allocatedMipCount;
    ComPtr<ID3D11ShaderResourceView> view;
    requireSuccess(device_->CreateShaderResourceView(
                       texture.Get(), &viewDescription, view.GetAddressOf()),
                   "ID3D11Device::CreateShaderResourceView(mission texture)");
    if (upload.mipPolicy == airfix::render::GtiMipPolicy::generateFromBase) {
      context_->GenerateMips(view.Get());
    }
    return view;
  }

  [[nodiscard]] std::vector<ComPtr<ID3D11ShaderResourceView>>
  createMissionTextures(
      const std::vector<airfix::content::LoadedTextureAsset> &sources) const {
    std::vector<ComPtr<ID3D11ShaderResourceView>> result;
    result.reserve(sources.size());
    for (std::size_t index = 0U; index < sources.size(); ++index) {
      if (sources[index].assetId.value != index) {
        throw std::runtime_error("mission textures do not use dense asset IDs");
      }
      result.push_back(createUploadedTexture(sources[index]));
    }
    return result;
  }

  [[nodiscard]] std::vector<ComPtr<ID3D11ShaderResourceView>>
  createWeaponCrosshairTextures(
      const airfix::content::LoadedLegacyWeaponCrosshairTextureSet &sources)
      const {
    if (!sources.valid()) {
      throw std::runtime_error(
          "weapon crosshair textures failed their upload contract");
    }
    std::vector<ComPtr<ID3D11ShaderResourceView>> result;
    result.reserve(sources.textures.size());
    for (std::size_t index = 0U; index < sources.textures.size(); ++index) {
      const auto &source = sources.textures[index];
      if (source.textureId.value != index) {
        throw std::runtime_error(
            "weapon crosshair textures do not use dense HUD-local IDs");
      }
      result.push_back(createUploadedTexture(source));
    }
    return result;
  }

  [[nodiscard]] std::vector<ComPtr<ID3D11ShaderResourceView>>
  createAircraftHealthGaugeTextures(
      const airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet &sources)
      const {
    if (!sources.valid()) {
      throw std::runtime_error(
          "aircraft health gauge textures failed their upload contract");
    }
    std::vector<ComPtr<ID3D11ShaderResourceView>> result;
    result.reserve(sources.textures.size());
    for (std::size_t index = 0U; index < sources.textures.size(); ++index) {
      const auto &source = sources.textures[index];
      if (source.textureId.value != index) {
        throw std::runtime_error(
            "aircraft health gauge textures do not use dense HUD-local IDs");
      }
      result.push_back(createUploadedTexture(source));
    }
    return result;
  }

  [[nodiscard]] std::vector<ComPtr<ID3D11ShaderResourceView>>
  createAircraftHudRollingDigitTextures(
      const airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet
          &sources) const {
    if (!sources.valid()) {
      throw std::runtime_error(
          "aircraft rolling digit atlas failed its upload contract");
    }
    std::vector<ComPtr<ID3D11ShaderResourceView>> result;
    result.reserve(sources.textures.size());
    for (std::size_t index = 0U; index < sources.textures.size(); ++index) {
      const auto &source = sources.textures[index];
      if (source.textureId.value != index) {
        throw std::runtime_error(
            "aircraft rolling digit atlas does not use dense HUD-local IDs");
      }
      result.push_back(createUploadedTexture(source));
    }
    return result;
  }

  void createTextures() {
    texture_ =
        createTexture(airfix::render::PublicRenderSmokeScene::textureWidth,
                      airfix::render::PublicRenderSmokeScene::textureHeight,
                      scene_.textureRgba8.data());
    fallbackTexture_ = createTexture(1U, 1U, scene_.fallbackRgba8.data());
  }

  [[nodiscard]] bool prepareScaledSceneTargets(
      const airfix::render::RenderTargetPixelExtent extent,
      ScaledSceneTargets &prepared) noexcept {
    const bool failAfterColor =
        remainingScaledTargetPreparationFailuresAfterColor_ != 0U;
    if (failAfterColor) {
      --remainingScaledTargetPreparationFailuresAfterColor_;
    }
    D3D11_TEXTURE2D_DESC colorDescription{};
    colorDescription.Width = extent.width;
    colorDescription.Height = extent.height;
    colorDescription.MipLevels = 1U;
    colorDescription.ArraySize = 1U;
    colorDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    colorDescription.SampleDesc = {1U, 0U};
    colorDescription.Usage = D3D11_USAGE_DEFAULT;
    colorDescription.BindFlags =
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ScaledSceneTargets candidate;
    if (FAILED(device_->CreateTexture2D(
            &colorDescription, nullptr,
            candidate.colorTexture.GetAddressOf())) ||
        FAILED(device_->CreateRenderTargetView(
            candidate.colorTexture.Get(), nullptr,
            candidate.renderTarget.GetAddressOf())) ||
        FAILED(device_->CreateShaderResourceView(
            candidate.colorTexture.Get(), nullptr,
            candidate.shaderResource.GetAddressOf())) ||
        failAfterColor) {
      return false;
    }

    D3D11_TEXTURE2D_DESC depthDescription{};
    depthDescription.Width = extent.width;
    depthDescription.Height = extent.height;
    depthDescription.MipLevels = 1U;
    depthDescription.ArraySize = 1U;
    depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDescription.SampleDesc = {1U, 0U};
    depthDescription.Usage = D3D11_USAGE_DEFAULT;
    depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(device_->CreateTexture2D(
            &depthDescription, nullptr,
            candidate.depthTexture.GetAddressOf())) ||
        FAILED(device_->CreateDepthStencilView(
            candidate.depthTexture.Get(), nullptr,
            candidate.depthView.GetAddressOf()))) {
      return false;
    }

    candidate.extent = extent;
    if (!candidate.complete()) {
      return false;
    }
    prepared.swap(candidate);
    return true;
  }

  void presentScaledScene() {
    if (!scaledSceneTargets_.shaderResource || !renderTarget_) {
      throw std::runtime_error(
          "scaled scene presentation resources are unavailable");
    }

    context_->OMSetRenderTargets(0U, nullptr, nullptr);
    ID3D11RenderTargetView *outputTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1U, &outputTarget, nullptr);
    context_->RSSetViewports(1U, &viewport_);
    context_->RSSetState(rasterizer_.Get());
    context_->OMSetDepthStencilState(presentationDepthState_.Get(), 0U);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(presentationVertexShader_.Get(), nullptr, 0U);
    context_->PSSetShader(presentationPixelShader_.Get(), nullptr, 0U);

    ID3D11SamplerState *sampler = presentationSampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);
    ID3D11ShaderResourceView *sceneView =
        scaledSceneTargets_.shaderResource.Get();
    context_->PSSetShaderResources(0U, 1U, &sceneView);
    context_->Draw(3U, 0U);

    ID3D11ShaderResourceView *nullView = nullptr;
    context_->PSSetShaderResources(0U, 1U, &nullView);
  }

  void createSwapTargets(
      const int currentWidth, const int currentHeight) {
    if (currentWidth <= 0 || currentHeight <= 0) {
      width_ = 0;
      height_ = 0;
      return;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    requireSuccess(
        swapChain_->GetBuffer(0U, IID_PPV_ARGS(backBuffer.GetAddressOf())),
        "IDXGISwapChain::GetBuffer");
    ComPtr<ID3D11RenderTargetView> renderTarget;
    requireSuccess(device_->CreateRenderTargetView(
                       backBuffer.Get(), nullptr, renderTarget.GetAddressOf()),
                   "ID3D11Device::CreateRenderTargetView");

    D3D11_TEXTURE2D_DESC depthDescription{};
    depthDescription.Width = static_cast<UINT>(currentWidth);
    depthDescription.Height = static_cast<UINT>(currentHeight);
    depthDescription.MipLevels = 1U;
    depthDescription.ArraySize = 1U;
    depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDescription.SampleDesc = {1U, 0U};
    depthDescription.Usage = D3D11_USAGE_DEFAULT;
    depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthTexture;
    ComPtr<ID3D11DepthStencilView> depthView;
    requireSuccess(device_->CreateTexture2D(&depthDescription, nullptr,
                                            depthTexture.GetAddressOf()),
                   "ID3D11Device::CreateTexture2D(depth)");
    requireSuccess(device_->CreateDepthStencilView(depthTexture.Get(), nullptr,
                                                   depthView.GetAddressOf()),
                   "ID3D11Device::CreateDepthStencilView");

    renderTarget_ = std::move(renderTarget);
    depthView_ = std::move(depthView);
    width_ = currentWidth;
    height_ = currentHeight;
    viewport_ = {
        0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_),
        0.0F, 1.0F,
    };
  }

  [[nodiscard]] CapturedBackBuffer captureBackBuffer() {
    ComPtr<ID3D11Texture2D> backBuffer;
    requireSuccess(
        swapChain_->GetBuffer(0U, IID_PPV_ARGS(backBuffer.GetAddressOf())),
        "IDXGISwapChain::GetBuffer(smoke readback)");

    D3D11_TEXTURE2D_DESC description{};
    backBuffer->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0U;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0U;

    ComPtr<ID3D11Texture2D> staging;
    requireSuccess(
        device_->CreateTexture2D(&description, nullptr, staging.GetAddressOf()),
        "ID3D11Device::CreateTexture2D(readback)");

    const std::uint64_t packedRowBytes =
        static_cast<std::uint64_t>(description.Width) * 4U;
    const std::uint64_t packedBytes =
        packedRowBytes * static_cast<std::uint64_t>(description.Height);
    if (description.Width == 0U || description.Height == 0U ||
        packedRowBytes > std::numeric_limits<std::size_t>::max() ||
        packedBytes > std::numeric_limits<std::size_t>::max()) {
      throw std::runtime_error(
          "D3D11 back-buffer readback dimensions are invalid");
    }

    CapturedBackBuffer result{
        .width = description.Width,
        .height = description.Height,
        .bgra8 =
            std::vector<std::uint8_t>(static_cast<std::size_t>(packedBytes)),
    };
    context_->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    requireSuccess(
        context_->Map(staging.Get(), 0U, D3D11_MAP_READ, 0U, &mapped),
        "ID3D11DeviceContext::Map(readback)");
    if (mapped.RowPitch < packedRowBytes) {
      context_->Unmap(staging.Get(), 0U);
      throw std::runtime_error(
          "D3D11 back-buffer readback row pitch is invalid");
    }
    for (UINT y = 0U; y < description.Height; ++y) {
      const auto *row = static_cast<const std::uint8_t *>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch;
      const auto destinationOffset = static_cast<std::size_t>(y) *
                                     static_cast<std::size_t>(packedRowBytes);
      std::copy_n(row, static_cast<std::size_t>(packedRowBytes),
                  result.bgra8.begin() + destinationOffset);
    }
    context_->Unmap(staging.Get(), 0U);
    return result;
  }

  [[nodiscard]] bool
  hasVisibleGpuOutput(const CapturedBackBuffer &captured) const noexcept {
    std::size_t brightPixelCount = 0U;
    for (std::size_t offset = 0U; offset + 3U < captured.bgra8.size();
         offset += 4U) {
      const auto *pixel = captured.bgra8.data() + offset;
      // Back-buffer storage is BGRA8. Every smoke texture has at
      // least one channel well above the dark clear color.
      if (pixel[0] > 32U || pixel[1] > 32U || pixel[2] > 32U) {
        ++brightPixelCount;
      }
    }

    const std::size_t totalPixels = static_cast<std::size_t>(captured.width) *
                                    static_cast<std::size_t>(captured.height);
    const std::size_t requiredPixels =
        std::max<std::size_t>(64U, totalPixels / 200U);
    return brightPixelCount >= requiredPixels;
  }

  void writeBmp(const std::filesystem::path &outputPath,
                const CapturedBackBuffer &captured) const {
    if (outputPath.empty() || std::filesystem::exists(outputPath) ||
        captured.width > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        captured.height > static_cast<UINT>(std::numeric_limits<LONG>::max())) {
      throw std::runtime_error(
          "private BMP capture path or dimensions are invalid");
    }

    const std::uint64_t pixelBytes = captured.bgra8.size();
    constexpr std::uint64_t headerBytes =
        sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    if (pixelBytes > std::numeric_limits<DWORD>::max() - headerBytes) {
      throw std::runtime_error(
          "private BMP capture exceeds the file format limit");
    }

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42U;
    fileHeader.bfSize = static_cast<DWORD>(headerBytes + pixelBytes);
    fileHeader.bfOffBits = static_cast<DWORD>(headerBytes);

    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = static_cast<LONG>(captured.width);
    infoHeader.biHeight = -static_cast<LONG>(captured.height);
    infoHeader.biPlanes = 1U;
    infoHeader.biBitCount = 32U;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = static_cast<DWORD>(pixelBytes);

    std::ofstream output(outputPath, std::ios::binary | std::ios::out);
    if (!output) {
      throw std::runtime_error("private BMP capture file could not be created");
    }
    output.write(reinterpret_cast<const char *>(&fileHeader),
                 sizeof(fileHeader));
    output.write(reinterpret_cast<const char *>(&infoHeader),
                 sizeof(infoHeader));
    output.write(reinterpret_cast<const char *>(captured.bgra8.data()),
                 static_cast<std::streamsize>(captured.bgra8.size()));
    output.close();
    if (!output) {
      std::error_code removeError;
      std::filesystem::remove(outputPath, removeError);
      throw std::runtime_error(
          "private BMP capture file could not be completed");
    }
  }

  SDL_Window *window_{};
  airfix::render::PublicRenderSmokeScene scene_;
  airfix::render::DrawSubmissionPlan plan_;

  D3D_FEATURE_LEVEL featureLevel_{D3D_FEATURE_LEVEL_11_0};
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain> swapChain_;
  ComPtr<ID3D11RenderTargetView> renderTarget_;
  ComPtr<ID3D11DepthStencilView> depthView_;
  ScaledSceneTargets scaledSceneTargets_;
  ComPtr<ID3D11VertexShader> smokeVertexShader_;
  ComPtr<ID3D11PixelShader> smokePixelShader_;
  ComPtr<ID3D11VertexShader> gameplayVertexShader_;
  ComPtr<ID3D11PixelShader> gameplayPixelShader_;
  ComPtr<ID3D11VertexShader> presentationVertexShader_;
  ComPtr<ID3D11PixelShader> presentationPixelShader_;
  ComPtr<ID3D11VertexShader> overlayVertexShader_;
  ComPtr<ID3D11PixelShader> overlayPixelShader_;
  ComPtr<ID3D11VertexShader> gaugeVertexShader_;
  ComPtr<ID3D11PixelShader> gaugeTexturePixelShader_;
  ComPtr<ID3D11PixelShader> gaugeSolidPixelShader_;
  ComPtr<ID3D11InputLayout> inputLayout_;
  ComPtr<ID3D11Buffer> smokeUniforms_;
  ComPtr<ID3D11Buffer> gameplayUniforms_;
  ComPtr<ID3D11Buffer> overlayUniforms_;
  ComPtr<ID3D11Buffer> gaugeUniforms_;
  ComPtr<ID3D11SamplerState> classicSceneSampler_;
  ComPtr<ID3D11SamplerState> enhancedSceneSampler_;
  ComPtr<ID3D11SamplerState> uiSampler_;
  ComPtr<ID3D11SamplerState> crosshairSampler_;
  ComPtr<ID3D11SamplerState> presentationSampler_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
  ComPtr<ID3D11RasterizerState> overviewRasterizer_;
  ComPtr<ID3D11DepthStencilState> smokeDepthState_;
  ComPtr<ID3D11DepthStencilState> gameplayDepthState_;
  ComPtr<ID3D11DepthStencilState> presentationDepthState_;
  ComPtr<ID3D11DepthStencilState> crosshairDepthState_;
  ComPtr<ID3D11BlendState> overlayBlendState_;
  ComPtr<ID3D11BlendState> productUiBlendState_;
  ComPtr<ID3D11ShaderResourceView> texture_;
  ComPtr<ID3D11ShaderResourceView> fallbackTexture_;
  ComPtr<ID3D11Texture2D> overlayTexture_;
  ComPtr<ID3D11ShaderResourceView> overlayShaderResource_;
  ComPtr<ID3D11Texture2D> productUiTexture_;
  ComPtr<ID3D11ShaderResourceView> productUiShaderResource_;
  std::vector<MeshResources> meshResources_;
  std::unique_ptr<MissionResources> mission_;
  std::array<GpuTimestampQueries, 4U> gpuTimestampQueries_;
  std::size_t nextGpuTimestampQuery_{};
  std::optional<double> latestGpuFrameMilliseconds_;
  airfix::render::RenderFrameDiagnosticsAccumulator diagnostics_;
  airfix::render::RenderTargetPixelExtent overlayExtent_{};
  airfix::render::RenderTargetPixelExtent productUiExtent_{};
  std::optional<std::chrono::steady_clock::time_point>
      previousFrameStart_;
  std::optional<std::chrono::steady_clock::time_point>
      lastOverlayRefresh_;
  std::optional<airfix::render::RenderTargetPixelRect>
      lastRenderedSceneViewport_;
  std::optional<airfix::render::ScenePresentationMode>
      lastRenderedScenePresentation_;
  std::optional<airfix::render::SceneTextureSamplingPolicy>
      lastRenderedSceneTextureSamplingPolicy_;
  std::optional<airfix::render::OutputPixelExtent>
      pendingResizeExtent_;
  std::uint32_t pendingResizeRetryDelayFrames_{};
  std::uint32_t pendingResizeRetryFailureCount_{};
  std::uint32_t overlayPixelScale_{1U};
  airfix::render::RenderPresentationSettings settings_;
  bool overlaySuppressed_{};
  std::uint32_t remainingScaledTargetPreparationFailuresAfterColor_{};
  bool reportSurfaceUnavailableForNextApply_{};
  D3D11_VIEWPORT viewport_{};
  int width_{};
  int height_{};
};

AirfixD3D11Renderer::AirfixD3D11Renderer(SDL_Window &window)
    : implementation_(std::make_unique<Implementation>(window)) {}

AirfixD3D11Renderer::~AirfixD3D11Renderer() = default;

void AirfixD3D11Renderer::resize() { implementation_->resize(); }

RenderPresentationSettingsApplyResult
AirfixD3D11Renderer::applyRenderPresentationSettings(
    const airfix::render::RenderPresentationSettings &candidate,
    const RenderPresentationSettingsPublicationGate
        publicationGate) noexcept {
  return implementation_->applyRenderPresentationSettings(
      candidate, publicationGate);
}

airfix::render::RenderPresentationSettings
AirfixD3D11Renderer::renderPresentationSettings() const noexcept {
  return implementation_->renderPresentationSettings();
}

void AirfixD3D11Renderer::
failNextScaledTargetPreparationsAfterColorForTesting(
    const std::uint32_t failureCount) noexcept {
  implementation_->
      failNextScaledTargetPreparationsAfterColorForTesting(
          failureCount);
}

void AirfixD3D11Renderer::
reportSurfaceUnavailableForNextApplyForTesting() noexcept {
  implementation_->reportSurfaceUnavailableForNextApplyForTesting();
}

bool AirfixD3D11Renderer::resizeToPixelExtentForTesting(
    const int width, const int height) {
  return implementation_->resizeToPixelExtentForTesting(width, height);
}

std::optional<airfix::render::SceneTextureSamplingPolicy>
AirfixD3D11Renderer::lastSceneTextureSamplingPolicyForTesting()
    const noexcept {
  return implementation_->lastSceneTextureSamplingPolicyForTesting();
}

std::array<const void *, 5U>
AirfixD3D11Renderer::
scaledSceneTargetIdentityForTesting() const noexcept {
  return implementation_->scaledSceneTargetIdentityForTesting();
}

std::optional<airfix::render::RenderTargetPixelRect>
AirfixD3D11Renderer::lastSceneViewportForTesting() const noexcept {
  return implementation_->lastSceneViewportForTesting();
}

std::optional<airfix::render::ScenePresentationMode>
AirfixD3D11Renderer::lastScenePresentationForTesting() const noexcept {
  return implementation_->lastScenePresentationForTesting();
}

bool AirfixD3D11Renderer::
hasDiagnosticsOverlayResourcesForTesting() const noexcept {
  return implementation_->hasDiagnosticsOverlayResourcesForTesting();
}

bool AirfixD3D11Renderer::hasProductUiOverlayResourcesForTesting()
    const noexcept {
  return implementation_->hasProductUiOverlayResourcesForTesting();
}

void AirfixD3D11Renderer::installLoadedMissionRoom(
    airfix::content::LoadedMissionWorldRoom &&room,
    const airfix::content::ContentRevision &expectedRevision) {
  implementation_->installLoadedMissionRoom(std::move(room), expectedRevision);
}

void AirfixD3D11Renderer::installLoadedMissionRoom(
    airfix::content::LoadedMissionWorldRoom &&room,
    airfix::content::LoadedLegacyWeaponCrosshairTextureSet &&crosshairs,
    airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet &&healthGauge,
    airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet
        &&rollingDigits,
    const airfix::content::ContentRevision &expectedRevision) {
  implementation_->installLoadedMissionRoom(
      std::move(room), std::move(crosshairs), std::move(healthGauge),
      std::move(rollingDigits), expectedRevision);
}

bool AirfixD3D11Renderer::missionWorldRoomInstalled() const noexcept {
  return implementation_->missionWorldRoomInstalled();
}

std::optional<std::weak_ptr<airfix::render::PlayerActorPoseRuntime>>
AirfixD3D11Renderer::playerActorPoseRuntimeEndpoint() const noexcept {
  return implementation_->playerActorPoseRuntimeEndpoint();
}

std::weak_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
AirfixD3D11Renderer::gameplayCameraMissionRuntimeEndpoint() const noexcept {
  return implementation_->gameplayCameraMissionRuntimeEndpoint();
}

void AirfixD3D11Renderer::captureFrameToBmp(
    const std::filesystem::path &outputPath) {
  implementation_->captureFrameToBmp(outputPath);
}

void AirfixD3D11Renderer::captureMissionOverviewFrameToBmp(
    const std::filesystem::path &outputPath) {
  implementation_->captureMissionOverviewFrameToBmp(outputPath);
}

void AirfixD3D11Renderer::captureMissionCrosshairValidationFrameToBmp(
    const std::filesystem::path &outputPath) {
  implementation_->captureMissionCrosshairValidationFrameToBmp(outputPath);
}

void AirfixD3D11Renderer::captureMissionHealthGaugeValidationFrameToBmp(
    const std::filesystem::path &outputPath) {
  implementation_->captureMissionHealthGaugeValidationFrameToBmp(outputPath);
}

void AirfixD3D11Renderer::capturePublicDiagnosticFrameToBmp(
    const std::filesystem::path &outputPath) {
  implementation_->capturePublicDiagnosticFrameToBmp(outputPath);
}

bool AirfixD3D11Renderer::setProductUiRaster(
    const AirfixWindowsUiRaster &raster) noexcept {
  return implementation_->setProductUiRaster(raster);
}

void AirfixD3D11Renderer::clearProductUiRaster() noexcept {
  implementation_->clearProductUiRaster();
}

void AirfixD3D11Renderer::capturePublicSettingsPanelFrameToBmp(
    const std::filesystem::path &outputPath) {
  implementation_->capturePublicSettingsPanelFrameToBmp(outputPath);
}

std::optional<airfix::render::RenderFrameDiagnostics>
AirfixD3D11Renderer::frameDiagnostics() const noexcept {
  return implementation_->frameDiagnostics();
}

bool AirfixD3D11Renderer::renderFrame(const bool validateGpuOutput) {
  return implementation_->renderFrame(validateGpuOutput);
}

} // namespace airfix::windows
