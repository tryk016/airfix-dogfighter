#include "AirfixD3D11Renderer.hpp"

#include "AirfixEmbeddedShader.hpp"

#include "airfix/render/DrawSubmissionPlan.hpp"
#include "airfix/render/PublicRenderSmokeScene.hpp"

#include <SDL3/SDL.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace airfix::windows {
namespace {

using Microsoft::WRL::ComPtr;

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

[[nodiscard]] GpuVertex repackVertex(const airfix::render::DrawVertex &source) {
  return GpuVertex{
      .position = {source.position.x, source.position.y, source.position.z,
                   1.0F},
      .normal = {source.normal.x, source.normal.y, source.normal.z, 0.0F},
      .uv = {source.uv.u, source.uv.v},
      .padding = {},
  };
}

[[nodiscard]] SmokeUniforms
makeUniforms(const airfix::render::DrawMeshInstance &instance) {
  // HLSL consumes a row-vector matrix. Each mathematical column of the
  // portable column-vector transform therefore becomes one stored row.
  const auto &columns = instance.modelLinear.columns;
  const auto &translation = instance.modelTranslation;
  return SmokeUniforms{{
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
    createGeometry();
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

  [[nodiscard]] bool renderFrame(const bool validateGpuOutput) {
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
    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor.data());
    context_->ClearDepthStencilView(depthView_.Get(), D3D11_CLEAR_DEPTH, 1.0F,
                                    0U);

    ID3D11RenderTargetView *renderTarget = renderTarget_.Get();
    context_->OMSetRenderTargets(1U, &renderTarget, depthView_.Get());
    context_->RSSetViewports(1U, &viewport_);
    context_->RSSetState(rasterizer_.Get());
    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0U);
    context_->PSSetShader(pixelShader_.Get(), nullptr, 0U);

    ID3D11SamplerState *sampler = sampler_.Get();
    context_->PSSetSamplers(0U, 1U, &sampler);

    for (const auto &command : plan_.commands) {
      const auto meshSlot = static_cast<std::size_t>(command.meshSlot);
      if (meshSlot >= meshResources_.size() ||
          command.instanceIndex >= scene_.model.instances.size()) {
        throw std::runtime_error("draw plan references a missing GPU resource");
      }

      const auto &mesh = meshResources_[meshSlot];
      constexpr UINT stride = sizeof(GpuVertex);
      constexpr UINT offset = 0U;
      ID3D11Buffer *vertexBuffer = mesh.vertices.Get();
      context_->IASetVertexBuffers(0U, 1U, &vertexBuffer, &stride, &offset);
      context_->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R32_UINT, 0U);

      const SmokeUniforms uniforms =
          makeUniforms(scene_.model.instances[command.instanceIndex]);
      context_->UpdateSubresource(uniforms_.Get(), 0U, nullptr, &uniforms, 0U,
                                  0U);
      ID3D11Buffer *constantBuffer = uniforms_.Get();
      context_->VSSetConstantBuffers(0U, 1U, &constantBuffer);

      ID3D11ShaderResourceView *texture =
          command.primary.has_value() &&
                  command.texcoordMode == airfix::render::TexcoordMode::uv0
              ? texture_.Get()
              : fallbackTexture_.Get();
      context_->PSSetShaderResources(0U, 1U, &texture);

      context_->DrawIndexed(command.indexCount, command.firstIndex, 0);
    }

    const bool outputValid = !validateGpuOutput || hasVisibleGpuOutput();
    requireSuccess(swapChain_->Present(validateGpuOutput ? 0U : 1U, 0U),
                   "IDXGISwapChain::Present");
    return outputValid;
  }

private:
  struct MeshResources {
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11Buffer> indices;
  };

  void releaseSwapTargets() {
    context_->OMSetRenderTargets(0U, nullptr, nullptr);
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
    const ComPtr<ID3DBlob> vertexBytecode =
        compileShader("AirfixSmokeVS", "vs_5_0");
    const ComPtr<ID3DBlob> pixelBytecode =
        compileShader("AirfixSmokePS", "ps_5_0");

    requireSuccess(
        device_->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                    vertexBytecode->GetBufferSize(), nullptr,
                                    vertexShader_.GetAddressOf()),
        "ID3D11Device::CreateVertexShader");
    requireSuccess(device_->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                              pixelBytecode->GetBufferSize(),
                                              nullptr,
                                              pixelShader_.GetAddressOf()),
                   "ID3D11Device::CreatePixelShader");

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
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            inputLayout_.GetAddressOf()),
        "ID3D11Device::CreateInputLayout");

    D3D11_BUFFER_DESC uniformDescription{};
    uniformDescription.ByteWidth = sizeof(SmokeUniforms);
    uniformDescription.Usage = D3D11_USAGE_DEFAULT;
    uniformDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    requireSuccess(device_->CreateBuffer(&uniformDescription, nullptr,
                                         uniforms_.GetAddressOf()),
                   "ID3D11Device::CreateBuffer(uniforms)");

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    requireSuccess(device_->CreateSamplerState(&samplerDescription,
                                               sampler_.GetAddressOf()),
                   "ID3D11Device::CreateSamplerState");

    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;
    requireSuccess(device_->CreateRasterizerState(&rasterizerDescription,
                                                  rasterizer_.GetAddressOf()),
                   "ID3D11Device::CreateRasterizerState");
  }

  void createGeometry() {
    meshResources_.reserve(scene_.model.meshes.size());
    for (const auto &mesh : scene_.model.meshes) {
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

      MeshResources resources;
      requireSuccess(device_->CreateBuffer(&vertexDescription, &vertexData,
                                           resources.vertices.GetAddressOf()),
                     "ID3D11Device::CreateBuffer(vertices)");
      requireSuccess(device_->CreateBuffer(&indexDescription, &indexData,
                                           resources.indices.GetAddressOf()),
                     "ID3D11Device::CreateBuffer(indices)");
      meshResources_.push_back(std::move(resources));
    }
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

  void createTextures() {
    texture_ =
        createTexture(airfix::render::PublicRenderSmokeScene::textureWidth,
                      airfix::render::PublicRenderSmokeScene::textureHeight,
                      scene_.textureRgba8.data());
    fallbackTexture_ = createTexture(1U, 1U, scene_.fallbackRgba8.data());
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

  [[nodiscard]] bool hasVisibleGpuOutput() {
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
    context_->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    requireSuccess(
        context_->Map(staging.Get(), 0U, D3D11_MAP_READ, 0U, &mapped),
        "ID3D11DeviceContext::Map(readback)");

    std::size_t brightPixelCount = 0U;
    for (UINT y = 0U; y < description.Height; ++y) {
      const auto *row = static_cast<const std::uint8_t *>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch;
      for (UINT x = 0U; x < description.Width; ++x) {
        const auto *pixel = row + static_cast<std::size_t>(x) * 4U;
        // Back-buffer storage is BGRA8. Every smoke texture has at
        // least one channel well above the dark clear color.
        if (pixel[0] > 32U || pixel[1] > 32U || pixel[2] > 32U) {
          ++brightPixelCount;
        }
      }
    }
    context_->Unmap(staging.Get(), 0U);

    const std::size_t totalPixels =
        static_cast<std::size_t>(description.Width) *
        static_cast<std::size_t>(description.Height);
    const std::size_t requiredPixels =
        std::max<std::size_t>(64U, totalPixels / 200U);
    return brightPixelCount >= requiredPixels;
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
  ComPtr<ID3D11VertexShader> vertexShader_;
  ComPtr<ID3D11PixelShader> pixelShader_;
  ComPtr<ID3D11InputLayout> inputLayout_;
  ComPtr<ID3D11Buffer> uniforms_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11RasterizerState> rasterizer_;
  ComPtr<ID3D11ShaderResourceView> texture_;
  ComPtr<ID3D11ShaderResourceView> fallbackTexture_;
  std::vector<MeshResources> meshResources_;
  D3D11_VIEWPORT viewport_{};
  int width_{};
  int height_{};
};

AirfixD3D11Renderer::AirfixD3D11Renderer(SDL_Window &window)
    : implementation_(std::make_unique<Implementation>(window)) {}

AirfixD3D11Renderer::~AirfixD3D11Renderer() = default;

void AirfixD3D11Renderer::resize() { implementation_->resize(); }

bool AirfixD3D11Renderer::renderFrame(const bool validateGpuOutput) {
  return implementation_->renderFrame(validateGpuOutput);
}

} // namespace airfix::windows
