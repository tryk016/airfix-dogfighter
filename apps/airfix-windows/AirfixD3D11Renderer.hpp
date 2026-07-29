#pragma once

#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/render/NativeRenderLayout.hpp"
#include "airfix/render/RenderFrameDiagnostics.hpp"

#include <filesystem>
#include <memory>
#include <optional>

struct SDL_Window;

namespace airfix::windows {

class AirfixD3D11Renderer final {
public:
  explicit AirfixD3D11Renderer(SDL_Window &window);
  ~AirfixD3D11Renderer();

  AirfixD3D11Renderer(const AirfixD3D11Renderer &) = delete;
  AirfixD3D11Renderer &operator=(const AirfixD3D11Renderer &) = delete;
  AirfixD3D11Renderer(AirfixD3D11Renderer &&) = delete;
  AirfixD3D11Renderer &operator=(AirfixD3D11Renderer &&) = delete;

  void resize();

  // Applies to the complete 3D scene while leaving the swapchain/output
  // resolution unchanged. Invalid values are rejected before publication.
  void setRenderScalePercent(float renderScalePercent);
  void setScenePresentationMode(
      airfix::render::ScenePresentationMode mode) noexcept;
  void setDiagnosticsOverlayEnabled(bool enabled) noexcept;

  // Builds every private GPU resource before replacing the currently visible
  // scene. A failure leaves the public diagnostic scene installed.
  void installLoadedMissionRoom(
      airfix::content::LoadedMissionWorldRoom &&room,
      const airfix::content::ContentRevision &expectedRevision);

  [[nodiscard]] bool missionWorldRoomInstalled() const noexcept;

  // Renders and writes one private local D3D11 frame as a top-down BGRA8 BMP.
  // Callers must keep the derived screenshot outside public source control.
  void captureFrameToBmp(const std::filesystem::path &outputPath);

  // Captures the public synthetic scene and developer overlay. This contains
  // no owner content and exists for repeatable renderer diagnostics.
  void capturePublicDiagnosticFrameToBmp(
      const std::filesystem::path &outputPath);

  [[nodiscard]] std::optional<airfix::render::RenderFrameDiagnostics>
  frameDiagnostics() const noexcept;

  // When validation is requested, the back buffer is read before Present and
  // the result proves that non-clear pixels reached the actual D3D11 target.
  [[nodiscard]] bool renderFrame(bool validateGpuOutput);

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace airfix::windows
