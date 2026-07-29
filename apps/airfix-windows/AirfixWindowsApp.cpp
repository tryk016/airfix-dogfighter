#include "AirfixD3D11Renderer.hpp"
#include "AirfixSdlInputAdapter.hpp"

#include "airfix/runtime/AppSession.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

constexpr std::uint64_t inputStepNanoseconds = 1'000'000'000ULL / 60ULL;
constexpr std::uint64_t maximumFrameNanoseconds = 250'000'000ULL;

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

  airfix::windows::AirfixSdlInputAdapter input;
  std::uint64_t inputTick = 0U;
  std::uint64_t inputAccumulator = 0U;
  std::uint64_t previousTime = SDL_GetTicksNS();
  bool running = true;
  while (running) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
      const auto inputResult = input.handleEvent(event);
      if (!inputResult.accepted) {
        throw std::runtime_error("SDL3 input pipeline failed closed");
      }
      if (inputResult.meaningfulInput) {
        session.noteInputActivity();
      }
      if (inputResult.controllerDisconnected) {
        session.pause();
      }

      switch (event.type) {
      case SDL_EVENT_QUIT:
        running = false;
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        renderer.resize();
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        input.focusLost();
        session.enterInactive();
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (!input.focusGained()) {
          throw std::runtime_error(
              "SDL3 controller state failed after focus regain");
        }
        session.enterForeground();
        break;
      default:
        break;
      }
    }

    const std::uint64_t currentTime = SDL_GetTicksNS();
    const std::uint64_t elapsed =
        currentTime >= previousTime ? currentTime - previousTime : 0U;
    previousTime = currentTime;
    inputAccumulator += std::min(elapsed, maximumFrameNanoseconds);
    while (inputAccumulator >= inputStepNanoseconds) {
      if (inputTick == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("SDL3 input tick counter exhausted");
      }
      ++inputTick;
      const auto inputFrame = input.tick(inputTick);
      if (!inputFrame.accepted) {
        throw std::runtime_error("SDL3 input frame generation failed");
      }
      inputAccumulator -= inputStepNanoseconds;
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
