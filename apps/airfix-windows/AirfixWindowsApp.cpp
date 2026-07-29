#include "AirfixD3D11Renderer.hpp"

#include "airfix/runtime/AppSession.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

struct SdlWindowDeleter {
  void operator()(SDL_Window *window) const noexcept {
    SDL_DestroyWindow(window);
  }
};

using SdlWindow = std::unique_ptr<SDL_Window, SdlWindowDeleter>;

[[nodiscard]] bool isSmokeTest(const int argumentCount, char *arguments[]) {
  if (argumentCount == 1) {
    return false;
  }
  if (argumentCount == 2 && std::string_view(arguments[1]) == "--smoke-test") {
    return true;
  }
  throw std::runtime_error("usage: AirfixDogfighter.exe [--smoke-test]");
}

int run(const int argumentCount, char *arguments[]) {
  const bool smokeTest = isSmokeTest(argumentCount, arguments);

  if (!SDL_SetAppMetadata("Airfix Dogfighter Reconstruction", "0.1.0",
                          "com.tryk016.airfixdogfighter")) {
    throw std::runtime_error(SDL_GetError());
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    throw std::runtime_error(SDL_GetError());
  }

  struct SdlQuitGuard {
    ~SdlQuitGuard() { SDL_Quit(); }
  } quitGuard;

  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
  flags |= smokeTest ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
  SdlWindow window{
      SDL_CreateWindow("Airfix Dogfighter Reconstruction", 960, 540, flags)};
  if (!window) {
    throw std::runtime_error(SDL_GetError());
  }

  airfix::runtime::AppSession session;
  airfix::windows::AirfixD3D11Renderer renderer{*window};

  if (smokeTest) {
    if (!renderer.renderFrame(true)) {
      throw std::runtime_error(
          "D3D11 smoke frame contains no rendered geometry");
    }
    renderer.resize();
    if (!renderer.renderFrame(true)) {
      throw std::runtime_error(
          "D3D11 smoke frame failed after swap-chain resize");
    }
    std::cout << "D3D11 GPU smoke test passed\n";
    return 0;
  }

  if (!SDL_ShowWindow(window.get())) {
    throw std::runtime_error(SDL_GetError());
  }

  bool running = true;
  while (running) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        running = false;
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        renderer.resize();
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        session.enterInactive();
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        session.enterForeground();
        break;
      case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_ESCAPE) {
          running = false;
        } else {
          session.noteInputActivity();
        }
        break;
      default:
        break;
      }
    }

    (void)renderer.renderFrame(false);
    SDL_Delay(1U);
  }

  return 0;
}

} // namespace

int main(int argumentCount, char *arguments[]) {
  try {
    return run(argumentCount, arguments);
  } catch (const std::exception &error) {
    std::cerr << "Airfix Windows shell failed: " << error.what() << '\n';
    return 1;
  }
}
