#include "AirfixD3D11Renderer.hpp"

#include "AirfixEmbeddedShader.hpp"

#include "airfix/content/MissionWorldRoomPublication.hpp"
#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/LegacyDepthState.hpp"
#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"
#include "airfix/render/NativeRenderLayout.hpp"
#include "airfix/render/PublicRenderSmokeScene.hpp"

#include <SDL3/SDL.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
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
modelFromLocal(const airfix::render::DrawMeshInstance &instance) {
  // HLSL consumes a row-vector matrix. Each mathematical column of the
  // portable column-vector transform therefore becomes one stored row.
  const auto &columns = instance.modelLinear.columns;
  const auto &translation = instance.modelTranslation;
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

[[nodiscard]] SmokeUniforms
makeSmokeUniforms(const airfix::render::DrawMeshInstance &instance) {
  return SmokeUniforms{.modelFromLocal = modelFromLocal(instance)};
}

[[nodiscard]] GameplayUniforms makeGameplayUniforms(
    const airfix::render::DrawMeshInstance &instance,
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
      .modelFromLocal = modelFromLocal(instance),
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

[[nodiscard]] airfix::render::LegacyGameplayCameraBootstrapInput
cameraBootstrapInput(
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

  struct MissionResources {
    airfix::content::LoadedMissionWorldRoom room;
    airfix::render::LegacyGameplayCameraClipPacket camera;
    std::vector<MeshResources> meshes;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;

    MissionResources(
        airfix::content::LoadedMissionWorldRoom &&loadedRoom,
        airfix::render::LegacyGameplayCameraClipPacket cameraPacket,
        std::vector<MeshResources> &&meshResources,
        std::vector<ComPtr<ID3D11ShaderResourceView>> &&textureResources)
        : room(std::move(loadedRoom)), camera(std::move(cameraPacket)),
          meshes(std::move(meshResources)),
          textures(std::move(textureResources)) {}
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
    createSwapTargets();
  }

  void resize() {
    const auto [newWidth, newHeight] = pixelSize(*window_);
    releaseSwapTargets();

    if (newWidth <= 0 || newHeight <= 0) {
      width_ = 0;
      height_ = 0;
      return;
    }

    requireSuccess(swapChain_->ResizeBuffers(0U, static_cast<UINT>(newWidth),
                                             static_cast<UINT>(newHeight),
                                             DXGI_FORMAT_UNKNOWN, 0U),
                   "IDXGISwapChain::ResizeBuffers");
    createSwapTargets();
  }

  void setRenderScalePercent(const float renderScalePercent) {
    if (!std::isfinite(renderScalePercent) ||
        renderScalePercent <
            airfix::render::native_render_policy::
                minimumRenderScalePercent ||
        renderScalePercent >
            airfix::render::native_render_policy::
                maximumRenderScalePercent) {
      throw std::runtime_error(
          "render scale must be finite and between 50 and 200 percent");
    }
    if (renderScalePercent_ == renderScalePercent) {
      return;
    }
    renderScalePercent_ = renderScalePercent;
    releaseScaledSceneTargets();
  }

  void setScenePresentationMode(
      const airfix::render::ScenePresentationMode mode) noexcept {
    scenePresentationMode_ = mode;
  }

  void installLoadedMissionRoom(
      airfix::content::LoadedMissionWorldRoom &&room,
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

    auto cameraResult =
        airfix::render::buildLegacyGameplayCameraBootstrapClipPacket(
            cameraBootstrapInput(room));
    if (!cameraResult.complete()) {
      throw std::runtime_error("authenticated mission camera bootstrap failed");
    }

    auto meshes = createGeometryResources(room.model);
    auto textures = createMissionTextures(room.textures);
    std::vector<airfix::content::LoadedTextureAsset>().swap(room.textures);
    auto candidate = std::make_unique<MissionResources>(
        std::move(room), std::move(*cameraResult.packet), std::move(meshes),
        std::move(textures));
    mission_ = std::move(candidate);
  }

  [[nodiscard]] bool missionWorldRoomInstalled() const noexcept {
    return mission_ != nullptr;
  }

  [[nodiscard]] bool
  renderFrame(const bool validateGpuOutput,
              const std::filesystem::path *captureOutput = nullptr) {
    if (!renderTarget_) {
      const auto [currentWidth, currentHeight] = pixelSize(*window_);
      if (currentWidth <= 0 || currentHeight <= 0) {
        return !validateGpuOutput;
      }
      requireSuccess(swapChain_->ResizeBuffers(0U,
                                               static_cast<UINT>(currentWidth),
                                               static_cast<UINT>(currentHeight),
                                               DXGI_FORMAT_UNKNOWN, 0U),
                     "IDXGISwapChain::ResizeBuffers");
      createSwapTargets();
    }

    constexpr std::array<float, 4U> clearColor{0.035F, 0.055F, 0.085F, 1.0F};
    const bool gameplay = mission_ != nullptr;
    auto layoutConfig = airfix::render::NativeRenderLayoutConfig{
        .outputExtent = {
            static_cast<std::uint32_t>(width_),
            static_cast<std::uint32_t>(height_),
        },
        .renderScalePercent = renderScalePercent_,
        .scenePresentation = scenePresentationMode_,
    };
    if (gameplay) {
      const auto &cameraProjection = mission_->camera.pose().projection();
      layoutConfig.referenceCameraCanvas = {
          mission_->camera.logicalCanvasWidth(),
          mission_->camera.logicalCanvasHeight(),
      };
      layoutConfig.referenceHorizontalFovDegrees =
          cameraProjection.horizontalFovDegrees();
    }
    const auto layout =
        airfix::render::buildNativeRenderLayout(layoutConfig);
    if (!layout.complete()) {
      throw std::runtime_error("native render layout is invalid");
    }
    const auto renderExtent = layout.layout->renderTargetExtent();
    if (renderExtent.width >
            D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        renderExtent.height >
            D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
      throw std::runtime_error(
          "scaled 3D target exceeds the D3D11 texture dimension limit");
    }
    const bool usesScaledSceneTarget =
        renderExtent !=
        airfix::render::RenderTargetPixelExtent{
            static_cast<std::uint32_t>(width_),
            static_cast<std::uint32_t>(height_),
        };
    if (usesScaledSceneTarget) {
      ensureScaledSceneTargets(renderExtent);
    }

    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor.data());
    ID3D11RenderTargetView *sceneRenderTarget =
        usesScaledSceneTarget
        ? scaledSceneRenderTarget_.Get()
        : renderTarget_.Get();
    ID3D11DepthStencilView *sceneDepthView =
        usesScaledSceneTarget
        ? scaledSceneDepthView_.Get()
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
    const D3D11_VIEWPORT activeViewport{
        fitted.x, fitted.y, fitted.width, fitted.height, 0.0F, 1.0F,
    };
    context_->RSSetViewports(1U, &activeViewport);
    context_->RSSetState(rasterizer_.Get());
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

    ID3D11SamplerState *sampler = sampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);

    const auto &model = gameplay ? mission_->room.model : scene_.model;
    const auto &submission = gameplay ? mission_->room.submission : plan_;
    const auto &meshes = gameplay ? mission_->meshes : meshResources_;
    for (const auto &command : submission.commands) {
      const auto meshSlot = static_cast<std::size_t>(command.meshSlot);
      if (meshSlot >= meshes.size() ||
          command.instanceIndex >= model.instances.size()) {
        throw std::runtime_error("draw plan references a missing GPU resource");
      }

      const auto &mesh = meshes[meshSlot];
      if (!mesh.vertices || !mesh.indices) {
        throw std::runtime_error("draw command references an empty GPU mesh");
      }
      constexpr UINT stride = sizeof(GpuVertex);
      constexpr UINT offset = 0U;
      ID3D11Buffer *vertexBuffer = mesh.vertices.Get();
      context_->IASetVertexBuffers(0U, 1U, &vertexBuffer, &stride, &offset);
      context_->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R32_UINT, 0U);

      if (gameplay) {
        const GameplayUniforms uniforms = makeGameplayUniforms(
            model.instances[command.instanceIndex], mission_->camera,
            *layout.layout);
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

    if (usesScaledSceneTarget) {
      presentScaledScene();
    }

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

private:
  void releaseScaledSceneTargets() noexcept {
    if (context_) {
      ID3D11ShaderResourceView *nullView = nullptr;
      context_->PSSetShaderResources(0U, 1U, &nullView);
    }
    scaledSceneShaderResource_.Reset();
    scaledSceneRenderTarget_.Reset();
    scaledSceneDepthView_.Reset();
    scaledSceneColorTexture_.Reset();
    scaledSceneExtent_ = {};
  }

  void releaseSwapTargets() {
    context_->OMSetRenderTargets(0U, nullptr, nullptr);
    releaseScaledSceneTargets();
    renderTarget_.Reset();
    depthView_.Reset();
    // ResizeBuffers requires every immediate-context reference to the old
    // back buffer to be released before the call.
    context_->Flush();
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

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    requireSuccess(device_->CreateSamplerState(&samplerDescription,
                                               sampler_.GetAddressOf()),
                   "ID3D11Device::CreateSamplerState");
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

  [[nodiscard]] ComPtr<ID3D11ShaderResourceView> createMissionTexture(
      const airfix::content::LoadedTextureAsset &source) const {
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
      result.push_back(createMissionTexture(sources[index]));
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

  void ensureScaledSceneTargets(
      const airfix::render::RenderTargetPixelExtent extent) {
    if (scaledSceneExtent_ == extent &&
        scaledSceneColorTexture_ &&
        scaledSceneRenderTarget_ &&
        scaledSceneShaderResource_ &&
        scaledSceneDepthView_) {
      return;
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

    ComPtr<ID3D11Texture2D> colorTexture;
    ComPtr<ID3D11RenderTargetView> colorTarget;
    ComPtr<ID3D11ShaderResourceView> colorView;
    requireSuccess(
        device_->CreateTexture2D(
            &colorDescription, nullptr, colorTexture.GetAddressOf()),
        "ID3D11Device::CreateTexture2D(scaled scene color)");
    requireSuccess(
        device_->CreateRenderTargetView(
            colorTexture.Get(), nullptr, colorTarget.GetAddressOf()),
        "ID3D11Device::CreateRenderTargetView(scaled scene)");
    requireSuccess(
        device_->CreateShaderResourceView(
            colorTexture.Get(), nullptr, colorView.GetAddressOf()),
        "ID3D11Device::CreateShaderResourceView(scaled scene)");

    D3D11_TEXTURE2D_DESC depthDescription{};
    depthDescription.Width = extent.width;
    depthDescription.Height = extent.height;
    depthDescription.MipLevels = 1U;
    depthDescription.ArraySize = 1U;
    depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDescription.SampleDesc = {1U, 0U};
    depthDescription.Usage = D3D11_USAGE_DEFAULT;
    depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthTexture;
    ComPtr<ID3D11DepthStencilView> depthView;
    requireSuccess(
        device_->CreateTexture2D(
            &depthDescription, nullptr, depthTexture.GetAddressOf()),
        "ID3D11Device::CreateTexture2D(scaled scene depth)");
    requireSuccess(
        device_->CreateDepthStencilView(
            depthTexture.Get(), nullptr, depthView.GetAddressOf()),
        "ID3D11Device::CreateDepthStencilView(scaled scene)");

    releaseScaledSceneTargets();
    scaledSceneColorTexture_ = std::move(colorTexture);
    scaledSceneRenderTarget_ = std::move(colorTarget);
    scaledSceneShaderResource_ = std::move(colorView);
    scaledSceneDepthView_ = std::move(depthView);
    scaledSceneExtent_ = extent;
  }

  void presentScaledScene() {
    if (!scaledSceneShaderResource_ || !renderTarget_) {
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
        scaledSceneShaderResource_.Get();
    context_->PSSetShaderResources(0U, 1U, &sceneView);
    context_->Draw(3U, 0U);

    ID3D11ShaderResourceView *nullView = nullptr;
    context_->PSSetShaderResources(0U, 1U, &nullView);
  }

  void createSwapTargets() {
    const auto [currentWidth, currentHeight] = pixelSize(*window_);
    if (currentWidth <= 0 || currentHeight <= 0) {
      width_ = 0;
      height_ = 0;
      return;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    requireSuccess(
        swapChain_->GetBuffer(0U, IID_PPV_ARGS(backBuffer.GetAddressOf())),
        "IDXGISwapChain::GetBuffer");
    requireSuccess(device_->CreateRenderTargetView(
                       backBuffer.Get(), nullptr, renderTarget_.GetAddressOf()),
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
    requireSuccess(device_->CreateTexture2D(&depthDescription, nullptr,
                                            depthTexture.GetAddressOf()),
                   "ID3D11Device::CreateTexture2D(depth)");
    requireSuccess(device_->CreateDepthStencilView(depthTexture.Get(), nullptr,
                                                   depthView_.GetAddressOf()),
                   "ID3D11Device::CreateDepthStencilView");

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
  ComPtr<ID3D11Texture2D> scaledSceneColorTexture_;
  ComPtr<ID3D11RenderTargetView> scaledSceneRenderTarget_;
  ComPtr<ID3D11ShaderResourceView> scaledSceneShaderResource_;
  ComPtr<ID3D11DepthStencilView> scaledSceneDepthView_;
  ComPtr<ID3D11VertexShader> smokeVertexShader_;
  ComPtr<ID3D11PixelShader> smokePixelShader_;
  ComPtr<ID3D11VertexShader> gameplayVertexShader_;
  ComPtr<ID3D11PixelShader> gameplayPixelShader_;
  ComPtr<ID3D11VertexShader> presentationVertexShader_;
  ComPtr<ID3D11PixelShader> presentationPixelShader_;
  ComPtr<ID3D11InputLayout> inputLayout_;
  ComPtr<ID3D11Buffer> smokeUniforms_;
  ComPtr<ID3D11Buffer> gameplayUniforms_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11SamplerState> presentationSampler_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
  ComPtr<ID3D11DepthStencilState> smokeDepthState_;
  ComPtr<ID3D11DepthStencilState> gameplayDepthState_;
  ComPtr<ID3D11DepthStencilState> presentationDepthState_;
  ComPtr<ID3D11ShaderResourceView> texture_;
  ComPtr<ID3D11ShaderResourceView> fallbackTexture_;
  std::vector<MeshResources> meshResources_;
  std::unique_ptr<MissionResources> mission_;
  airfix::render::RenderTargetPixelExtent scaledSceneExtent_{};
  float renderScalePercent_{
      airfix::render::native_render_policy::
          defaultRenderScalePercent};
  airfix::render::ScenePresentationMode scenePresentationMode_{
      airfix::render::ScenePresentationMode::widescreenHorPlus};
  D3D11_VIEWPORT viewport_{};
  int width_{};
  int height_{};
};

AirfixD3D11Renderer::AirfixD3D11Renderer(SDL_Window &window)
    : implementation_(std::make_unique<Implementation>(window)) {}

AirfixD3D11Renderer::~AirfixD3D11Renderer() = default;

void AirfixD3D11Renderer::resize() { implementation_->resize(); }

void AirfixD3D11Renderer::setRenderScalePercent(
    const float renderScalePercent) {
  implementation_->setRenderScalePercent(renderScalePercent);
}

void AirfixD3D11Renderer::setScenePresentationMode(
    const airfix::render::ScenePresentationMode mode) noexcept {
  implementation_->setScenePresentationMode(mode);
}

void AirfixD3D11Renderer::installLoadedMissionRoom(
    airfix::content::LoadedMissionWorldRoom &&room,
    const airfix::content::ContentRevision &expectedRevision) {
  implementation_->installLoadedMissionRoom(std::move(room), expectedRevision);
}

bool AirfixD3D11Renderer::missionWorldRoomInstalled() const noexcept {
  return implementation_->missionWorldRoomInstalled();
}

void AirfixD3D11Renderer::captureFrameToBmp(
    const std::filesystem::path &outputPath) {
  implementation_->captureFrameToBmp(outputPath);
}

bool AirfixD3D11Renderer::renderFrame(const bool validateGpuOutput) {
  return implementation_->renderFrame(validateGpuOutput);
}

} // namespace airfix::windows
