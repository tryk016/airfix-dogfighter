#include "AirfixD3D11Renderer.hpp"
#include "AirfixSdlInputAdapter.hpp"
#include "AirfixXAudio2Backend.hpp"

#include "airfix/audio/AudioCommand.hpp"
#include "airfix/runtime/AppSession.hpp"
#include "airfix/simulation/LegacyAircraftAudioCoordinator.hpp"

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

[[nodiscard]] std::array<std::int16_t, 480U> makeSmokeAudio() noexcept {
  return {};
}

[[nodiscard]] airfix::simulation::LegacyAircraftAudioBindings
makeSmokeAudioBindings() noexcept {
  using airfix::simulation::LegacyAircraftAudioBinding;
  using airfix::simulation::LegacyAircraftEngineSound;

  return {{
      LegacyAircraftAudioBinding{
          .sound = LegacyAircraftEngineSound::engineOn,
          .clip = smokeAudioClip,
          .voice = airfix::audio::AudioVoiceId{1U},
          .looping = true,
      },
      LegacyAircraftAudioBinding{
          .sound = LegacyAircraftEngineSound::engineIdle,
          .clip = smokeAudioClip,
          .voice = airfix::audio::AudioVoiceId{2U},
          .looping = true,
      },
      LegacyAircraftAudioBinding{
          .sound = LegacyAircraftEngineSound::engineTurn,
          .clip = smokeAudioClip,
          .voice = airfix::audio::AudioVoiceId{3U},
          .looping = true,
      },
      LegacyAircraftAudioBinding{
          .sound = LegacyAircraftEngineSound::engineStart,
          .clip = smokeAudioClip,
          .voice = airfix::audio::AudioVoiceId{4U},
          .looping = true,
      },
      LegacyAircraftAudioBinding{
          .sound = LegacyAircraftEngineSound::engineStop,
          .clip = smokeAudioClip,
          .voice = airfix::audio::AudioVoiceId{5U},
      },
      LegacyAircraftAudioBinding{
          .sound = LegacyAircraftEngineSound::engineDive,
          .clip = smokeAudioClip,
          .voice = airfix::audio::AudioVoiceId{6U},
          .looping = true,
      },
  }};
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

  const auto bindings = makeSmokeAudioBindings();
  airfix::simulation::LegacyAircraftAudioCoordinatorState state{};
  std::uint64_t sequence = 0U;
  bool outputAvailable = false;

  const auto advanceFive = [&](const float deltaSeconds, const float thrust,
                               const float health, const float velocityY) {
    for (std::uint32_t index = 0U; index < 5U; ++index) {
      ++sequence;
      const auto step = airfix::simulation::legacyAircraftAdvanceAudio(
          state, bindings,
          {
              .sequence = sequence,
              .engine =
                  {
                      .deltaSeconds = deltaSeconds,
                      .smoothedThrust = thrust,
                      .speedMagnitude = 2.0F,
                      .smoothedOrientationM01 = -0.25F,
                  },
              .destroyedDive =
                  {
                      .health = health,
                      .velocityY = velocityY,
                  },
          });
      if (!step.has_value()) {
        throw std::runtime_error(
            "reconstructed aircraft audio composition failed");
      }

      const auto result = audio.submit(step->audio);
      if (!result.accepted ||
          result.appliedCommandCount != step->audio.commandCount) {
        throw std::runtime_error(
            "XAudio2 rejected reconstructed aircraft audio");
      }
      outputAvailable = outputAvailable || result.outputAvailable;
      state = step->state;
    }
  };

  advanceFive(0.012F, 0.5F, 1.0F, 0.0F);
  if (!state.engine.engineStartTransitionActive) {
    throw std::runtime_error("aircraft engine-start audio was not entered");
  }
  advanceFive(1.01F, 0.5F, 1.0F, 0.0F);
  if (!state.engine.engineRunning) {
    throw std::runtime_error("aircraft running audio was not entered");
  }
  advanceFive(0.012F, 0.0005F, 0.0F, -8.0F);
  if (state.engine.engineRunning || !state.destroyedDive.soundActive) {
    throw std::runtime_error("aircraft shutdown/dive audio was not entered");
  }
  advanceFive(0.012F, 0.0005F, 1.0F, 0.0F);
  if (state.destroyedDive.soundActive) {
    throw std::runtime_error("aircraft dive audio was not stopped");
  }

  std::cout << (outputAvailable
                    ? "XAudio2 reconstructed aircraft audio smoke passed\n"
                    : "XAudio2 reconstructed aircraft audio accepted without "
                      "an output device\n");
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
