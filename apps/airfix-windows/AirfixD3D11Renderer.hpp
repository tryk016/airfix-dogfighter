#pragma once

#include <memory>

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

  // When validation is requested, the back buffer is read before Present and
  // the result proves that non-clear pixels reached the actual D3D11 target.
  [[nodiscard]] bool renderFrame(bool validateGpuOutput);

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace airfix::windows
