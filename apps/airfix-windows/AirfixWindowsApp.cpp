#include "AirfixD3D11Renderer.hpp"
#include "AirfixSdlInputAdapter.hpp"
#include "AirfixXAudio2Backend.hpp"

#include "airfix/audio/AudioCommand.hpp"
#include "airfix/runtime/AppSession.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
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
constexpr airfix::audio::AudioClipId smokeAudioClip{1U};
constexpr airfix::audio::AudioVoiceId smokeAudioVoice{1U};

[[nodiscard]] std::array<std::int16_t, 480U> makeSmokeAudio() noexcept {
  std::array<std::int16_t, 480U> samples{};
  constexpr std::int16_t amplitude = 1'000;
  for (std::size_t index = 0U; index < samples.size(); ++index) {
    const std::size_t phase = index % 48U;
    const std::int32_t ramp = phase < 24U
                                  ? static_cast<std::int32_t>(phase)
                                  : static_cast<std::int32_t>(48U - phase);
    samples[index] = static_cast<std::int16_t>((ramp - 12) * amplitude / 12);
  }
  return samples;
}

void runAudioSmokeTest(airfix::windows::AirfixXAudio2Backend &audio) {
  const auto samples = makeSmokeAudio();
  const auto registration = audio.registerPcm16Clip({
      .id = smokeAudioClip,
      .sampleRate = 48'000U,
      .channelCount = 1U,
      .interleavedSamples = samples,
  });
  if (registration !=
      airfix::windows::AirfixAudioClipRegistrationResult::registered) {
    throw std::runtime_error("synthetic PCM16 registration failed");
  }

  airfix::audio::AudioCommandBatch start{.sequence = 1U};
  if (!airfix::audio::appendAudioCommand(
          start, {
                     .kind = airfix::audio::AudioCommandKind::startVoice,
                     .voice = smokeAudioVoice,
                     .clip = smokeAudioClip,
                     .gain = 0.0F,
                 })) {
    throw std::runtime_error("synthetic audio start command failed");
  }
  const auto startResult = audio.submit(start);
  if (!startResult.accepted ||
      startResult.appliedCommandCount != start.commandCount) {
    throw std::runtime_error("XAudio2 rejected synthetic audio");
  }

  airfix::audio::AudioCommandBatch stop{.sequence = 2U};
  if (!airfix::audio::appendAudioCommand(
          stop, {
                    .kind = airfix::audio::AudioCommandKind::stopVoice,
                    .voice = smokeAudioVoice,
                })) {
    throw std::runtime_error("synthetic audio stop command failed");
  }
  const auto stopResult = audio.submit(stop);
  if (!stopResult.accepted ||
      stopResult.appliedCommandCount != stop.commandCount) {
    throw std::runtime_error("XAudio2 rejected synthetic audio stop");
  }

  std::cout
      << (startResult.outputAvailable
              ? "XAudio2 synthetic smoke test passed\n"
              : "XAudio2 synthetic smoke accepted without an output device\n");
}

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
  airfix::windows::AirfixXAudio2Backend audio;
  if (audio.outputState() ==
      airfix::windows::AirfixXAudio2OutputState::initializationFailed) {
    throw std::runtime_error("XAudio2 2.9 initialization failed");
  }

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
    runAudioSmokeTest(audio);
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
        audio.setActive(false);
        session.enterInactive();
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (!input.focusGained()) {
          throw std::runtime_error(
              "SDL3 controller state failed after focus regain");
        }
        (void)audio.recover();
        audio.setActive(true);
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
