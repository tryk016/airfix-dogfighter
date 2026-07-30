#include "AirfixD3D11Renderer.hpp"
#include "AirfixSdlInputAdapter.hpp"
#include "AirfixWindowsCommandLine.hpp"
#include "AirfixWindowsSettingsRoot.hpp"
#include "AirfixXAudio2Backend.hpp"

#include "airfix/audio/AudioCommand.hpp"
#include "airfix/content/LegacyAircraftAudioClipSet.hpp"
#include "airfix/content/MissionLoadManifest.hpp"
#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/package/AfPackRecovery.hpp"
#include "airfix/runtime/AppSession.hpp"
#include "airfix/runtime/PlayerAircraftPresentationCoordinator.hpp"
#include "airfix/settings/RenderPresentationSettingsStore.hpp"
#include "airfix/simulation/LegacyAircraftAudioCoordinator.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t inputStepNanoseconds = 1'000'000'000ULL / 60ULL;
constexpr std::uint64_t maximumFrameNanoseconds = inputStepNanoseconds * 8ULL;
constexpr airfix::audio::AudioClipId smokeAudioClip{1U};

[[nodiscard]] constexpr std::string_view renderSettingsIssueCategory(
    const airfix::windows::RenderPresentationSettingsApplyIssueKind
        issue) noexcept {
  using Issue = airfix::windows::RenderPresentationSettingsApplyIssueKind;
  switch (issue) {
  case Issue::invalidSettings:
    return "invalid-settings";
  case Issue::surfaceUnavailable:
    return "surface-unavailable";
  case Issue::invalidLayout:
    return "invalid-layout";
  case Issue::unsupportedTargetExtent:
    return "unsupported-target-extent";
  case Issue::targetPreparationFailed:
    return "target-preparation-failed";
  case Issue::publicationGateRejected:
    return "publication-gate-rejected";
  }
  return "unknown";
}

void reportSettingsLoad(
    const airfix::settings::RenderSettingsLoadResult &load) {
  using Source = airfix::settings::RenderSettingsLoadSource;
  using Status = airfix::settings::RenderSettingsFileStatus;
  if (load.source == Source::backup) {
    std::cerr << "Render settings recovery: backup-used\n";
  }
  if (load.current.status == Status::futureSchema) {
    std::cerr << "Render settings: future-schema-preserved";
    if (load.current.schemaVersion.has_value()) {
      std::cerr << " (schema " << *load.current.schemaVersion << ')';
    }
    std::cerr << '\n';
  } else if (load.current.status == Status::malformed ||
             load.current.status == Status::oversized ||
             load.current.status == Status::wrongTypeOrLinked) {
    std::cerr << "Render settings: current-invalid\n";
  } else if (load.current.status == Status::ioUnavailable) {
    std::cerr << "Render settings: storage-unavailable\n";
  }
  if (load.persistenceBlocked && load.current.status != Status::futureSchema &&
      load.current.status != Status::ioUnavailable) {
    std::cerr << "Render settings: persistence-disabled\n";
  }
}

[[nodiscard]] airfix::render::ConvertedNodeTransform
actorWorldFrom(const airfix::simulation::PlayerSpawnPose &pose) noexcept {
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

struct LoadedPrivateContent final {
  airfix::content::ContentRevision revision;
  airfix::simulation::LegacyAircraftAudioBindings aircraftAudioBindings;
  std::optional<airfix::content::LoadedMissionWorldRoom> missionRoom;
};

[[nodiscard]] LoadedPrivateContent loadPrivateContent(
    const std::filesystem::path &contentRoot,
    const std::optional<airfix::windows::AirfixWindowsMissionOptions> &mission,
    airfix::windows::AirfixXAudio2Backend &audio) {
  auto inspection = airfix::afpack::inspectActiveContent(contentRoot);
  if (inspection.status() != airfix::afpack::ActiveContentStatus::ready) {
    throw std::runtime_error(
        "the private Windows content root has no authenticated active AFPACK");
  }
  auto session = airfix::content::VerifiedContentSession::adopt(
      std::move(inspection).takeReadyLease());
  const auto revision = session.revision();
  auto audioResult = airfix::content::loadLegacyAircraftAudioClips(session);
  if (!audioResult.success() || !audioResult.clips.has_value() ||
      !audioResult.clips->belongsTo(session)) {
    throw std::runtime_error(
        "authenticated aircraft audio could not be loaded");
  }

  std::optional<airfix::content::LoadedMissionWorldRoom> missionRoom;
  if (mission.has_value()) {
    const airfix::content::MissionLoadManifestRequest manifestRequest{
        .levelLogicalPath = mission->levelLogicalPath,
        .setupLogicalPath = mission->setupLogicalPath,
        .playerObjectLogicalPath = mission->playerObjectLogicalPath,
    };
    auto manifest =
        airfix::content::buildMissionLoadManifest(session, manifestRequest);
    if (!manifest.success() || !manifest.manifest.has_value() ||
        !manifest.manifest->belongsTo(session) ||
        manifest.manifest->revision() != revision) {
      throw std::runtime_error(
          "authenticated Windows mission manifest could not be built");
    }

    const airfix::content::MissionWorldRoomLoadRequest roomRequest{
        .initialRootName = {},
        .requestedStartIndex = mission->requestedStartIndex,
        .basis = {},
        .uvPolicy = airfix::render::UvPolicy::preserveRaw,
    };
    auto loadedRoom = airfix::content::loadMissionWorldRoom(
        session, *manifest.manifest, roomRequest);
    if (!loadedRoom.success() || !loadedRoom.room.has_value() ||
        loadedRoom.room->revision != revision ||
        session.revision() != revision) {
      throw std::runtime_error(
          "authenticated Windows mission room could not be loaded");
    }
    missionRoom = std::move(*loadedRoom.room);
  }

  for (const auto &clip : audioResult.clips->clipViews()) {
    if (audio.registerPcm16Clip(clip) !=
        airfix::windows::AirfixAudioClipRegistrationResult::registered) {
      throw std::runtime_error("XAudio2 rejected authenticated aircraft PCM");
    }
  }

  constexpr std::array<airfix::audio::AudioVoiceId, 6U> voices{{
      {1U},
      {2U},
      {3U},
      {4U},
      {5U},
      {6U},
  }};
  const auto bindings = audioResult.clips->bindings(voices);
  if (!bindings.has_value()) {
    throw std::runtime_error(
        "authenticated aircraft audio bindings are invalid");
  }
  return {
      .revision = revision,
      .aircraftAudioBindings = *bindings,
      .missionRoom = std::move(missionRoom),
  };
}

int run(const int argumentCount, char *arguments[]) {
  std::vector<std::string_view> commandLineArguments;
  commandLineArguments.reserve(
      argumentCount > 1 ? static_cast<std::size_t>(argumentCount - 1) : 0U);
  for (int index = 1; index < argumentCount; ++index) {
    commandLineArguments.emplace_back(arguments[index]);
  }
  const auto options =
      airfix::windows::parseAirfixWindowsCommandLine(commandLineArguments);

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
  flags |= options.smokeTest || options.validateContentOnly ||
                   options.captureFrameOutput.has_value() ||
                   options.captureDiagnosticFrameOutput.has_value()
               ? SDL_WINDOW_HIDDEN
               : SDL_WINDOW_RESIZABLE;
  const auto initialWindowSize = options.captureSize.value_or(
      airfix::windows::AirfixWindowsCaptureSize{960U, 540U});
  SdlWindow window{SDL_CreateWindow("Airfix Dogfighter Reconstruction",
                                    static_cast<int>(initialWindowSize.width),
                                    static_cast<int>(initialWindowSize.height),
                                    flags)};
  if (!window) {
    throw std::runtime_error(SDL_GetError());
  }
  if (options.captureSize.has_value()) {
    int pixelWidth{};
    int pixelHeight{};
    if (!SDL_GetWindowSizeInPixels(window.get(), &pixelWidth, &pixelHeight)) {
      throw std::runtime_error(SDL_GetError());
    }
    if (pixelWidth != static_cast<int>(options.captureSize->width) ||
        pixelHeight != static_cast<int>(options.captureSize->height)) {
      throw std::runtime_error(
          "requested capture size does not match the physical backbuffer "
          "extent");
    }
  }

  airfix::runtime::AppSession session;
  airfix::windows::AirfixD3D11Renderer renderer{*window};
  airfix::render::RenderPresentationSettings persistentSettings;
  std::optional<std::filesystem::path> settingsDirectory;
  const bool sessionOnlyInvocation =
      options.smokeTest || options.validateContentOnly ||
      options.captureFrameOutput.has_value() ||
      options.captureDiagnosticFrameOutput.has_value();
  if (!sessionOnlyInvocation) {
    try {
      settingsDirectory =
          airfix::windows::resolveAirfixWindowsSettingsDirectory();
      const auto load =
          airfix::settings::loadRenderPresentationSettings(*settingsDirectory);
      persistentSettings = load.settings;
      reportSettingsLoad(load);
    } catch (...) {
      // Paths and platform error text are deliberately not exposed here.
      // An unavailable profile is nonfatal and cannot partially change the
      // canonical defaults.
      settingsDirectory.reset();
      std::cerr << "Render settings: storage-unavailable\n";
    }
  }
  const auto startupResolution =
      airfix::render::resolveRenderPresentationSettings(
          persistentSettings, options.renderOverrides);
  if (!startupResolution.accepted()) {
    throw std::runtime_error("resolved Windows render settings are invalid");
  }
  const auto startupSettings = startupResolution.settings;
  const auto startupSettingsResult =
      renderer.applyRenderPresentationSettings(startupSettings);
  if (!startupSettingsResult.accepted()) {
    std::cerr << "Windows render settings override rejected ("
              << renderSettingsIssueCategory(*startupSettingsResult.issue)
              << "); continuing with the active snapshot\n";
  }
  airfix::windows::AirfixXAudio2Backend audio;
  if (audio.outputState() ==
      airfix::windows::AirfixXAudio2OutputState::initializationFailed) {
    throw std::runtime_error("XAudio2 2.9 initialization failed");
  }

  std::optional<LoadedPrivateContent> privateContent;
  std::optional<airfix::simulation::PlayerSpawnPose> playerSpawnPose;
  airfix::runtime::PlayerActorPoseRuntimeEndpoint playerActorPoseRuntime;
  if (options.contentRoot.has_value()) {
    privateContent.emplace(
        loadPrivateContent(*options.contentRoot, options.mission, audio));
  }
  if (privateContent.has_value() && privateContent->missionRoom.has_value()) {
    playerSpawnPose = privateContent->missionRoom->playerSpawnPose;
    renderer.installLoadedMissionRoom(std::move(*privateContent->missionRoom),
                                      privateContent->revision);
    privateContent->missionRoom.reset();
    if (!renderer.missionWorldRoomInstalled()) {
      throw std::runtime_error(
          "authenticated Windows mission was not published");
    }
    playerActorPoseRuntime = renderer.playerActorPoseRuntimeEndpoint();
  }
  audio.setActive(false);
  if (options.captureFrameOutput.has_value()) {
    renderer.captureFrameToBmp(*options.captureFrameOutput);
    std::cout << "Authenticated private D3D11 mission frame captured\n";
    return 0;
  }
  if (options.captureDiagnosticFrameOutput.has_value()) {
    renderer.capturePublicDiagnosticFrameToBmp(
        *options.captureDiagnosticFrameOutput);
    std::cout << "Public D3D11 diagnostic frame captured\n";
    return 0;
  }
  if (options.validateContentOnly) {
    if (options.mission.has_value() && !renderer.renderFrame(true)) {
      throw std::runtime_error(
          "authenticated Windows mission produced no visible D3D11 output");
    }
    std::cout
        << (options.mission.has_value()
                ? "Authenticated private mission, rendering resources, and "
                  "aircraft audio validation passed\n"
                : "Authenticated private aircraft audio validation passed\n");
    return 0;
  }

  if (options.smokeTest) {
    if (!renderer.renderFrame(true)) {
      throw std::runtime_error(
          "D3D11 smoke frame contains no rendered geometry");
    }
    const auto diagnostics = renderer.frameDiagnostics();
    if (!diagnostics.has_value() || diagnostics->outputExtent.width == 0U ||
        diagnostics->renderTargetExtent.width == 0U ||
        diagnostics->sceneDrawCallCount == 0U ||
        diagnostics->sceneTriangleCount == 0U) {
      throw std::runtime_error("D3D11 smoke frame did not publish diagnostics");
    }

    auto transitionSettings = renderer.renderPresentationSettings();
    for (const float scale : std::array{100.0F, 50.0F, 200.0F, 100.0F}) {
      transitionSettings.renderScalePercent = scale;
      if (!renderer.applyRenderPresentationSettings(transitionSettings)
               .accepted()) {
        throw std::runtime_error(
            "D3D11 smoke settings transition was rejected");
      }
      if (!renderer.renderFrame(true)) {
        throw std::runtime_error(
            "D3D11 smoke frame failed during a settings transition");
      }
      const auto transitionDiagnostics = renderer.frameDiagnostics();
      if (!transitionDiagnostics.has_value()) {
        throw std::runtime_error(
            "D3D11 settings transition published no diagnostics");
      }
      const auto expectedLayout = airfix::render::buildNativeRenderLayout({
          .outputExtent = transitionDiagnostics->outputExtent,
          .renderScalePercent = scale,
          .scenePresentation = transitionSettings.scenePresentation,
      });
      if (!expectedLayout.complete() ||
          transitionDiagnostics->renderScalePercent != scale ||
          transitionDiagnostics->renderTargetExtent !=
              expectedLayout.layout->renderTargetExtent()) {
        throw std::runtime_error(
            "D3D11 settings transition used the wrong render target");
      }
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

  if (renderer.missionWorldRoomInstalled() && playerSpawnPose.has_value()) {
    // Windows currently commits the authenticated room, audio clips, and
    // frozen spawn pose plus its replacement-safe pose runtime as one startup
    // transaction. Changing pose/camera inputs remain a later trace-driven
    // milestone.
    session.setContentState(airfix::runtime::ContentState::ready);
  }
  airfix::windows::AirfixSdlInputAdapter input;
  airfix::runtime::PlayerAircraftPresentationCoordinator
      playerAircraftPresentation;
  bool simulationPipelineReady = true;
  bool windowFocused =
      (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0U;
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
        if (!input.resetForGameplayBoundary()) {
          throw std::runtime_error(
              "SDL3 input reset failed after controller disconnect");
        }
        audio.setActive(false);
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
        windowFocused = false;
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
        audio.setActive(false);
        session.enterForeground();
        windowFocused = true;
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
      if (inputFrame.frame.pressed(airfix::input::DigitalAction::globalPause)) {
        if (!input.resetForGameplayBoundary()) {
          throw std::runtime_error(
              "SDL3 input reset failed at a gameplay boundary");
        }
        if (session.simulationRunning()) {
          audio.setActive(false);
          session.pause();
        } else {
          const bool mayResume =
              session.lifecycleState() ==
                  airfix::runtime::LifecycleState::foregroundPaused &&
              windowFocused && simulationPipelineReady &&
              playerAircraftPresentation.healthy() &&
              renderer.missionWorldRoomInstalled() &&
              playerSpawnPose.has_value();
          const bool resumed = mayResume && session.resume();
          audio.setActive(resumed);
        }
        inputAccumulator -= inputStepNanoseconds;
        continue;
      }
      if (session.simulationRunning()) {
        const auto advanced = playerAircraftPresentation.tryAdvance(
            inputFrame.frame, actorWorldFrom(*playerSpawnPose),
            playerActorPoseRuntime);
        if (!advanced.accepted()) {
          simulationPipelineReady = false;
          if (!input.resetForGameplayBoundary()) {
            throw std::runtime_error(
                "SDL3 input reset failed after simulation halt");
          }
          audio.setActive(false);
          session.pause();
          std::cerr << "Windows deterministic input consumer halted after "
                       "an invalid state transition\n";
        }
      }
      inputAccumulator -= inputStepNanoseconds;
    }

    (void)renderer.renderFrame(false);
    SDL_Delay(1U);
  }

  std::cout << "Windows player input state: "
            << playerAircraftPresentation.state().completedSteps
            << " steps, hash " << playerAircraftPresentation.stateHash()
            << '\n';
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
