#include "AirfixD3D11Renderer.hpp"
#include "AirfixSdlInputAdapter.hpp"
#include "AirfixWindowsCommandLine.hpp"
#include "AirfixWindowsContentBootstrap.hpp"
#include "AirfixWindowsContentImport.hpp"
#include "AirfixWindowsControllerProfileCoordinator.hpp"
#include "AirfixWindowsRenderSettingsCoordinator.hpp"
#include "AirfixWindowsRenderSettingsPanel.hpp"
#include "AirfixWindowsSettingsRoot.hpp"
#include "AirfixWindowsUiAutomation.hpp"
#include "AirfixWindowsUiRasterizer.hpp"
#include "AirfixXAudio2Backend.hpp"

#include "airfix/audio/AudioCommand.hpp"
#include "airfix/content/LegacyAircraftAudioClipSet.hpp"
#include "airfix/content/LegacyAircraftHealthGaugeTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudIdentityStatusTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudInstrumentsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelTextureSet.hpp"
#include "airfix/content/LegacyWeaponCrosshairTextureSet.hpp"
#include "airfix/content/MissionLoadManifest.hpp"
#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/package/AfPackRecovery.hpp"
#include "airfix/runtime/AppSession.hpp"
#include "airfix/runtime/PlayerAircraftPresentationCoordinator.hpp"
#include "airfix/settings/ControllerInputProfileStore.hpp"
#include "airfix/settings/RenderPresentationSettingsStore.hpp"
#include "airfix/simulation/LegacyAircraftAudioCoordinator.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr std::uint64_t controllerPreviewRefreshNanoseconds =
    1'000'000'000ULL / 15ULL;
constexpr airfix::audio::AudioClipId smokeAudioClip{1U};
constexpr int minimumInteractiveWindowWidth = 640;
constexpr int minimumInteractiveWindowHeight = 360;

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

struct WindowsRenderSettingsPersistenceContext final {
  std::optional<std::filesystem::path> settingsDirectory;
};

[[nodiscard]] airfix::windows::RenderPresentationSettingsApplyResult
applyWindowsRenderSettings(
    void *const context,
    const airfix::render::RenderPresentationSettings &candidate,
    const airfix::windows::RenderPresentationSettingsPublicationGate
        publicationGate) noexcept {
  if (context == nullptr) {
    return {
        .changed = false,
        .issue = airfix::windows::RenderPresentationSettingsApplyIssueKind::
            surfaceUnavailable,
    };
  }
  return static_cast<airfix::windows::AirfixD3D11Renderer *>(context)
      ->applyRenderPresentationSettings(candidate, publicationGate);
}

[[nodiscard]] airfix::windows::AirfixWindowsRenderSettingsStoreResult
storeWindowsRenderSettings(
    void *const context,
    const airfix::render::RenderPresentationSettings &candidate) noexcept {
  using Result = airfix::windows::AirfixWindowsRenderSettingsStoreResult;
  using Status = airfix::windows::AirfixWindowsRenderSettingsStoreStatus;
  using ErrorKind = airfix::settings::RenderSettingsStoreErrorKind;
  const auto *persistence =
      static_cast<const WindowsRenderSettingsPersistenceContext *>(context);
  if (persistence == nullptr || !persistence->settingsDirectory.has_value()) {
    return Result{Status::unavailable};
  }
  try {
    (void)airfix::settings::saveRenderPresentationSettings(
        *persistence->settingsDirectory, candidate);
    return Result{Status::committed};
  } catch (const airfix::settings::RenderSettingsStoreError &error) {
    switch (error.kind()) {
    case ErrorKind::commitUnknown:
      return Result{Status::commitUnknown};
    case ErrorKind::persistenceBlocked:
      return Result{Status::blocked};
    case ErrorKind::invalidDirectory:
      return Result{Status::unavailable};
    case ErrorKind::invalidSettings:
    case ErrorKind::saveFailed:
      return Result{Status::failed};
    }
  } catch (...) {
  }
  return Result{Status::failed};
}

[[nodiscard]] airfix::windows::AirfixWindowsRenderSettingsReloadResult
reloadWindowsRenderSettings(void *const context) noexcept {
  using Result = airfix::windows::AirfixWindowsRenderSettingsReloadResult;
  using Status = airfix::windows::AirfixWindowsRenderSettingsReloadStatus;
  const auto *persistence =
      static_cast<const WindowsRenderSettingsPersistenceContext *>(context);
  if (persistence == nullptr || !persistence->settingsDirectory.has_value()) {
    return Result{.status = Status::unavailable};
  }
  try {
    const auto loaded = airfix::settings::loadRenderPresentationSettings(
        *persistence->settingsDirectory);
    return airfix::windows::classifyWindowsRenderSettingsCommitReadback(loaded);
  } catch (...) {
    return Result{.status = Status::failed};
  }
}

[[nodiscard]] airfix::windows::AirfixWindowsControllerProfileStoreResult
storeWindowsControllerProfile(
    void *const context,
    const airfix::input::ControllerInputProfileRecord &candidate) noexcept {
  using ErrorKind = airfix::settings::ControllerInputProfileStoreErrorKind;
  using Result = airfix::windows::AirfixWindowsControllerProfileStoreResult;
  using SaveStatus = airfix::settings::ControllerInputProfileSaveStatus;
  using Status = airfix::windows::AirfixWindowsControllerProfileStoreStatus;
  const auto *persistence =
      static_cast<const WindowsRenderSettingsPersistenceContext *>(context);
  if (persistence == nullptr || !persistence->settingsDirectory.has_value()) {
    return Result{Status::unavailable};
  }
  try {
    const auto saved = airfix::settings::saveControllerInputProfile(
        *persistence->settingsDirectory, candidate);
    return Result{saved.status == SaveStatus::unchanged ? Status::unchanged
                                                        : Status::committed};
  } catch (const airfix::settings::ControllerInputProfileStoreError &error) {
    switch (error.kind()) {
    case ErrorKind::commitUnknown:
      return Result{Status::commitUnknown};
    case ErrorKind::persistenceBlocked:
      return Result{Status::blocked};
    case ErrorKind::invalidDirectory:
      return Result{Status::unavailable};
    case ErrorKind::invalidProfile:
    case ErrorKind::saveFailed:
      return Result{Status::failed};
    }
  } catch (...) {
  }
  return Result{Status::failed};
}

[[nodiscard]] airfix::windows::AirfixWindowsControllerProfileReloadResult
reloadWindowsControllerProfile(void *const context) noexcept {
  using Result = airfix::windows::AirfixWindowsControllerProfileReloadResult;
  using Status = airfix::windows::AirfixWindowsControllerProfileReloadStatus;
  const auto *persistence =
      static_cast<const WindowsRenderSettingsPersistenceContext *>(context);
  if (persistence == nullptr || !persistence->settingsDirectory.has_value()) {
    return Result{.status = Status::unavailable};
  }
  try {
    const auto loaded = airfix::settings::loadControllerInputProfile(
        *persistence->settingsDirectory);
    return airfix::windows::classifyWindowsControllerProfileCommitReadback(
        loaded);
  } catch (...) {
    return Result{.status = Status::failed};
  }
}

[[nodiscard]] airfix::windows::AirfixWindowsUiPixelExtent
windowsUiExtent(SDL_Window &window) {
  int width{};
  int height{};
  if (!SDL_GetWindowSizeInPixels(&window, &width, &height) || width <= 0 ||
      height <= 0) {
    throw std::runtime_error("SDL3 product UI extent is unavailable");
  }
  float dpiScale = SDL_GetWindowDisplayScale(&window);
  if (!std::isfinite(dpiScale) || dpiScale <= 0.0F) {
    dpiScale = 1.0F;
  }
  return {
      .width = static_cast<std::uint32_t>(width),
      .height = static_cast<std::uint32_t>(height),
      .dpiScale = dpiScale,
  };
}

[[nodiscard]] void *windowsNativeWindow(SDL_Window &window) noexcept {
  const SDL_PropertiesID properties = SDL_GetWindowProperties(&window);
  return SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                nullptr);
}

[[nodiscard]] airfix::windows::AirfixWindowsPointerInput
windowsPointerInput(SDL_Window &window, const bool primaryPressed,
                    const std::int32_t wheelY) noexcept {
  float x{};
  float y{};
  (void)SDL_GetMouseState(&x, &y);
  float pixelDensity = SDL_GetWindowPixelDensity(&window);
  if (!std::isfinite(pixelDensity) || pixelDensity <= 0.0F) {
    pixelDensity = 1.0F;
  }
  return {
      .xPixels = x * pixelDensity,
      .yPixels = y * pixelDensity,
      .wheelY = wheelY,
      .primaryPressed = primaryPressed,
  };
}

struct LoadedPrivateContent final {
  airfix::content::ContentRevision revision;
  airfix::simulation::LegacyAircraftAudioBindings aircraftAudioBindings;
  std::optional<airfix::content::LoadedMissionWorldRoom> missionRoom;
  std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>
      weaponCrosshairTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>
      aircraftHealthGaugeTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>
      aircraftHudRollingDigitTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet>
      aircraftHudInstrumentTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet>
      aircraftHudWeaponPanelTextures;
  std::optional<
      airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet>
      aircraftHudIdentityStatusTextures;
};

[[nodiscard]] std::filesystem::path resolveInstalledContentRoot() {
  try {
    return airfix::windows::resolveAirfixWindowsContentDirectory();
  } catch (...) {
    // SDL/platform diagnostics can disclose the host path. Keep the product
    // error stable and private.
    throw std::runtime_error("private Windows content storage is unavailable");
  }
}

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
  std::optional<airfix::content::LoadedLegacyWeaponCrosshairTextureSet>
      weaponCrosshairTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHealthGaugeTextureSet>
      aircraftHealthGaugeTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHudRollingDigitsTextureSet>
      aircraftHudRollingDigitTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHudInstrumentTextureSet>
      aircraftHudInstrumentTextures;
  std::optional<airfix::content::LoadedLegacyAircraftHudWeaponPanelTextureSet>
      aircraftHudWeaponPanelTextures;
  std::optional<
      airfix::content::LoadedLegacyAircraftHudIdentityStatusTextureSet>
      aircraftHudIdentityStatusTextures;
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

    auto loadedCrosshairs =
        airfix::content::loadLegacyWeaponCrosshairTextures(session);
    if (!loadedCrosshairs.success() || !loadedCrosshairs.textures.has_value() ||
        !loadedCrosshairs.textures->belongsTo(session) ||
        loadedCrosshairs.textures->revision != revision ||
        session.revision() != revision) {
      throw std::runtime_error(
          "authenticated weapon crosshair textures could not be loaded");
    }
    weaponCrosshairTextures = std::move(*loadedCrosshairs.textures);

    auto loadedHealthGauge =
        airfix::content::loadLegacyAircraftHealthGaugeTextures(session);
    if (!loadedHealthGauge.success() ||
        !loadedHealthGauge.textures.has_value() ||
        !loadedHealthGauge.textures->belongsTo(session) ||
        loadedHealthGauge.textures->revision != revision ||
        session.revision() != revision) {
      throw std::runtime_error(
          "authenticated aircraft health gauge textures could not be loaded");
    }
    aircraftHealthGaugeTextures = std::move(*loadedHealthGauge.textures);

    auto loadedRollingDigits =
        airfix::content::loadLegacyAircraftHudRollingDigitsTexture(session);
    if (!loadedRollingDigits.success() ||
        !loadedRollingDigits.textures.has_value() ||
        !loadedRollingDigits.textures->belongsTo(session) ||
        loadedRollingDigits.textures->revision != revision ||
        session.revision() != revision) {
      throw std::runtime_error(
          "authenticated aircraft rolling digit atlas could not be loaded");
    }
    aircraftHudRollingDigitTextures = std::move(*loadedRollingDigits.textures);

    auto loadedHudInstruments =
        airfix::content::loadLegacyAircraftHudInstrumentTextures(session);
    if (!loadedHudInstruments.success() ||
        !loadedHudInstruments.textures.has_value() ||
        !loadedHudInstruments.textures->belongsTo(session) ||
        loadedHudInstruments.textures->revision != revision ||
        session.revision() != revision) {
      throw std::runtime_error(
          "authenticated aircraft HUD instruments could not be loaded");
    }
    aircraftHudInstrumentTextures = std::move(*loadedHudInstruments.textures);

    auto loadedHudWeaponPanels =
        airfix::content::loadLegacyAircraftHudWeaponPanelTextures(session);
    if (!loadedHudWeaponPanels.success() ||
        !loadedHudWeaponPanels.textures.has_value() ||
        !loadedHudWeaponPanels.textures->belongsTo(session) ||
        loadedHudWeaponPanels.textures->revision != revision ||
        session.revision() != revision) {
      throw std::runtime_error(
          "authenticated aircraft HUD weapon panels could not be loaded");
    }
    aircraftHudWeaponPanelTextures = std::move(*loadedHudWeaponPanels.textures);

    auto loadedHudIdentityStatus =
        airfix::content::loadLegacyAircraftHudIdentityStatusTextures(session);
    if (!loadedHudIdentityStatus.success() ||
        !loadedHudIdentityStatus.textures.has_value() ||
        !loadedHudIdentityStatus.textures->belongsTo(session) ||
        loadedHudIdentityStatus.textures->revision != revision ||
        session.revision() != revision) {
      throw std::runtime_error(
          "authenticated aircraft HUD identity/status textures could not be "
          "loaded");
    }
    aircraftHudIdentityStatusTextures =
        std::move(*loadedHudIdentityStatus.textures);
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
      .weaponCrosshairTextures = std::move(weaponCrosshairTextures),
      .aircraftHealthGaugeTextures = std::move(aircraftHealthGaugeTextures),
      .aircraftHudRollingDigitTextures =
          std::move(aircraftHudRollingDigitTextures),
      .aircraftHudInstrumentTextures = std::move(aircraftHudInstrumentTextures),
      .aircraftHudWeaponPanelTextures =
          std::move(aircraftHudWeaponPanelTextures),
      .aircraftHudIdentityStatusTextures =
          std::move(aircraftHudIdentityStatusTextures),
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

  struct SdlQuitGuard {
    ~SdlQuitGuard() { SDL_Quit(); }
  };
  if (options.manageInstalledContent) {
    if (!SDL_Init(0U)) {
      throw std::runtime_error(SDL_GetError());
    }
    const SdlQuitGuard contentUiQuitGuard;
    const auto result = airfix::windows::runAirfixWindowsContentBootstrap(
        resolveInstalledContentRoot());
    switch (result) {
    case airfix::windows::AirfixWindowsContentBootstrapResult::
        closedWithoutReadyContent:
      std::cout << "Private AFPACK manager closed: content-not-ready\n";
      return 0;
    case airfix::windows::AirfixWindowsContentBootstrapResult::
        closedWithReadyContent:
      std::cout << "Private AFPACK manager closed: content-ready\n";
      return 0;
    case airfix::windows::AirfixWindowsContentBootstrapResult::failed:
      throw std::runtime_error("private AFPACK manager failed safely");
    }
    throw std::runtime_error(
        "private AFPACK manager returned an invalid state");
  }
  if (options.importAfPackSource.has_value()) {
    if (!SDL_Init(0U)) {
      throw std::runtime_error(SDL_GetError());
    }
    const SdlQuitGuard importQuitGuard;
    const auto result = airfix::windows::importAirfixWindowsContent(
        *options.importAfPackSource, resolveInstalledContentRoot());
    std::cout << "Private AFPACK import complete: generation="
              << result.generation << " bytes=" << result.size
              << " reused=" << (result.reusedExisting ? "yes" : "no")
              << " active-changed=" << (result.activeChanged ? "yes" : "no")
              << '\n';
    return 0;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    throw std::runtime_error(SDL_GetError());
  }
  const SdlQuitGuard quitGuard;

  auto activeContentRoot = options.contentRoot;
  if (options.useInstalledContent) {
    activeContentRoot = resolveInstalledContentRoot();
  }

  const bool sessionOnlyInvocation =
      options.smokeTest || options.validateContentOnly ||
      options.captureFrameOutput.has_value() ||
      options.captureOverviewFrameOutput.has_value() ||
      options.captureCrosshairValidationFrameOutput.has_value() ||
      options.captureDiagnosticFrameOutput.has_value() ||
      options.captureSettingsPanelOutput.has_value() ||
      options.captureControllerCalibrationPanelOutput.has_value() ||
      options.captureControllerBindingsPanelOutput.has_value();
  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
  flags |= sessionOnlyInvocation ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
  const auto initialWindowSize = options.captureSize.value_or(
      airfix::windows::AirfixWindowsCaptureSize{960U, 540U});
  SdlWindow window{SDL_CreateWindow("Airfix Dogfighter Reconstruction",
                                    static_cast<int>(initialWindowSize.width),
                                    static_cast<int>(initialWindowSize.height),
                                    flags)};
  if (!window) {
    throw std::runtime_error(SDL_GetError());
  }
  if (!sessionOnlyInvocation &&
      !SDL_SetWindowMinimumSize(window.get(), minimumInteractiveWindowWidth,
                                minimumInteractiveWindowHeight)) {
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
  WindowsRenderSettingsPersistenceContext persistenceContext;
  auto persistenceState =
      airfix::windows::AirfixWindowsRenderSettingsPersistenceState::unavailable;
  if (!sessionOnlyInvocation) {
    try {
      persistenceContext.settingsDirectory =
          airfix::windows::resolveAirfixWindowsSettingsDirectory();
      const auto load = airfix::settings::loadRenderPresentationSettings(
          *persistenceContext.settingsDirectory);
      persistentSettings = load.settings;
      persistenceState =
          load.persistenceBlocked
              ? airfix::windows::AirfixWindowsRenderSettingsPersistenceState::
                    blocked
              : airfix::windows::AirfixWindowsRenderSettingsPersistenceState::
                    available;
      reportSettingsLoad(load);
    } catch (...) {
      // Paths and platform error text are deliberately not exposed here.
      // An unavailable profile is nonfatal and cannot partially change the
      // canonical defaults.
      persistenceContext.settingsDirectory.reset();
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
  const airfix::windows::AirfixWindowsRenderSettingsCallbacks
      renderSettingsCallbacks{
          .applyRenderer = applyWindowsRenderSettings,
          .rendererContext = &renderer,
          .store = storeWindowsRenderSettings,
          .reload = reloadWindowsRenderSettings,
          .persistenceContext = &persistenceContext,
      };
  auto renderSettingsCoordinator =
      airfix::windows::AirfixWindowsRenderSettingsCoordinator::create(
          persistentSettings, options.renderOverrides,
          renderer.renderPresentationSettings(), persistenceState,
          renderSettingsCallbacks);
  if (!renderSettingsCoordinator.has_value()) {
    throw std::runtime_error(
        "Windows render settings coordinator could not be created");
  }

  const auto defaultControllerRecord =
      airfix::input::makeDefaultControllerInputProfileRecord();
  const auto defaultControllerProfile =
      airfix::input::resolveControllerInputProfile(defaultControllerRecord);
  if (!defaultControllerProfile.complete()) {
    throw std::runtime_error(
        "canonical controller profile could not be resolved");
  }
  auto preparedControllerInput =
      airfix::input::prepareControllerInputRuntimeConfiguration(
          *defaultControllerProfile.profile);
  if (!preparedControllerInput.complete()) {
    throw std::runtime_error(
        "canonical controller runtime configuration could not be prepared");
  }
  auto controllerInputConfiguration = *preparedControllerInput.configuration;
  auto activeControllerRecord = defaultControllerRecord;
  auto persistentControllerRecord = defaultControllerRecord;
  auto controllerPersistenceState = airfix::windows::
      AirfixWindowsControllerProfilePersistenceState::unavailable;
  bool controllerProfileRepairRequired = false;
  if (!sessionOnlyInvocation &&
      persistenceContext.settingsDirectory.has_value()) {
    try {
      const auto loaded = airfix::settings::loadControllerInputProfile(
          *persistenceContext.settingsDirectory);
      const auto prepared =
          airfix::input::prepareControllerInputRuntimeConfiguration(
              loaded.profile);
      if (!prepared.complete()) {
        throw std::runtime_error(
            "validated controller profile did not prepare");
      }
      controllerInputConfiguration = *prepared.configuration;
      activeControllerRecord = loaded.profile.record();
      persistentControllerRecord = activeControllerRecord;
      controllerPersistenceState =
          loaded.persistenceBlocked
              ? airfix::windows::
                    AirfixWindowsControllerProfilePersistenceState::blocked
              : airfix::windows::
                    AirfixWindowsControllerProfilePersistenceState::available;
      controllerProfileRepairRequired =
          airfix::settings::controllerInputProfileNeedsRepair(loaded);
      switch (loaded.source) {
      case airfix::settings::ControllerInputProfileLoadSource::current:
        std::cerr << "Controller input profile: current\n";
        break;
      case airfix::settings::ControllerInputProfileLoadSource::backup:
        std::cerr << "Controller input profile: backup\n";
        break;
      case airfix::settings::ControllerInputProfileLoadSource::defaults:
        std::cerr << "Controller input profile: defaults\n";
        break;
      }
      if (loaded.persistenceBlocked) {
        std::cerr << "Controller input profile: persistence-blocked\n";
      }
    } catch (...) {
      // The canonical in-memory profile remains active. Never reveal a
      // settings path, document bytes, checksum, or platform error here.
      controllerPersistenceState = airfix::windows::
          AirfixWindowsControllerProfilePersistenceState::unavailable;
      std::cerr << "Controller input profile: storage-unavailable\n";
    }
  }
  const airfix::windows::AirfixWindowsControllerProfileCallbacks
      controllerProfileCallbacks{
          .store = storeWindowsControllerProfile,
          .reload = reloadWindowsControllerProfile,
          .context = &persistenceContext,
      };
  auto controllerProfileCoordinator =
      airfix::windows::AirfixWindowsControllerProfileCoordinator::create(
          persistentControllerRecord, controllerPersistenceState,
          controllerProfileCallbacks);
  if (!controllerProfileCoordinator.has_value()) {
    throw std::runtime_error(
        "Windows controller profile coordinator could not be created");
  }

  airfix::windows::AirfixWindowsUiRasterizer uiRasterizer;
  if (options.captureSettingsPanelOutput.has_value()) {
    auto panel = airfix::windows::AirfixWindowsRenderSettingsPanel::create(
        startupSettings, true, windowsUiExtent(*window),
        airfix::windows::airfixWindowsRenderSettingsSessionOverrideMask(
            options.renderOverrides),
        false);
    if (!panel.has_value()) {
      throw std::runtime_error(
          "public settings-panel capture model is invalid");
    }
    const auto pause = panel->snapshot();
    const auto &displaySettings = pause.items[0];
    (void)panel->consumePointer({
        .xPixels =
            displaySettings.bounds.x + displaySettings.bounds.width * 0.5F,
        .yPixels =
            displaySettings.bounds.y + displaySettings.bounds.height * 0.5F,
        .wheelY = 0,
        .primaryPressed = true,
    });
    const auto raster = uiRasterizer.rasterize(panel->snapshot());
    if (!raster.complete() || !renderer.setProductUiRaster(*raster.raster)) {
      throw std::runtime_error(
          "public settings-panel raster could not be published");
    }
    renderer.capturePublicSettingsPanelFrameToBmp(
        *options.captureSettingsPanelOutput);
    std::cout << "Public D3D11 settings-panel frame captured\n";
    return 0;
  }
  if (options.captureControllerCalibrationPanelOutput.has_value()) {
    auto captureControllerRecord = defaultControllerRecord;
    captureControllerRecord
        .axes[static_cast<std::size_t>(
            airfix::input::ControllerAxisElement::leftStickX)]
        .sensitivityPermille = 1350U;
    auto panel = airfix::windows::AirfixWindowsRenderSettingsPanel::create(
        startupSettings, true, windowsUiExtent(*window),
        airfix::windows::airfixWindowsRenderSettingsSessionOverrideMask(
            options.renderOverrides),
        false,
        airfix::windows::AirfixWindowsControllerProfilePanelState{
            .active = captureControllerRecord,
            .persisted = captureControllerRecord,
            .capabilities =
                {
                    .persistenceAvailable = true,
                    .repairRequired = false,
                },
        });
    if (!panel.has_value()) {
      throw std::runtime_error(
          "public controller-calibration capture model is invalid");
    }
    const auto activateItem =
        [&](const airfix::windows::AirfixWindowsRenderSettingsItem item) {
          const auto snapshot = panel->snapshot();
          const auto found = std::find_if(
              snapshot.items.begin(),
              snapshot.items.begin() + snapshot.itemCount,
              [item](const auto &candidate) { return candidate.item == item; });
          if (found == snapshot.items.begin() + snapshot.itemCount) {
            throw std::runtime_error(
                "public controller-calibration capture item is unavailable");
          }
          (void)panel->consumePointer({
              .xPixels = found->bounds.x + found->bounds.width * 0.5F,
              .yPixels = found->bounds.y + found->bounds.height * 0.5F,
              .wheelY = 0,
              .primaryPressed = true,
          });
        };
    activateItem(airfix::windows::AirfixWindowsRenderSettingsItem::
                     controllerCalibration);
    activateItem(airfix::windows::AirfixWindowsRenderSettingsItem::leftStickX);
    airfix::windows::AirfixWindowsControllerAxisInputSnapshot syntheticInput{};
    syntheticInput.rawAxes[static_cast<std::size_t>(
        airfix::input::ControllerAxisElement::leftStickX)] =
        static_cast<airfix::input::Q15>(19'661);
    syntheticInput.connected = true;
    panel->setControllerAxisInput(syntheticInput);
    const auto raster = uiRasterizer.rasterize(panel->snapshot());
    if (!raster.complete() || !renderer.setProductUiRaster(*raster.raster)) {
      throw std::runtime_error(
          "public controller-calibration raster could not be published");
    }
    renderer.capturePublicSettingsPanelFrameToBmp(
        *options.captureControllerCalibrationPanelOutput);
    std::cout << "Public D3D11 controller-calibration panel frame captured\n";
    return 0;
  }
  if (options.captureControllerBindingsPanelOutput.has_value()) {
    auto panel = airfix::windows::AirfixWindowsRenderSettingsPanel::create(
        startupSettings, true, windowsUiExtent(*window),
        airfix::windows::airfixWindowsRenderSettingsSessionOverrideMask(
            options.renderOverrides),
        false,
        airfix::windows::AirfixWindowsControllerProfilePanelState{
            .active = defaultControllerRecord,
            .persisted = defaultControllerRecord,
            .capabilities =
                {
                    .persistenceAvailable = true,
                    .repairRequired = false,
                },
        });
    if (!panel.has_value()) {
      throw std::runtime_error(
          "public controller-bindings capture model is invalid");
    }
    const auto activateItem =
        [&](const airfix::windows::AirfixWindowsRenderSettingsItem item) {
          const auto snapshot = panel->snapshot();
          const auto found = std::find_if(
              snapshot.items.begin(),
              snapshot.items.begin() + snapshot.itemCount,
              [item](const auto &candidate) { return candidate.item == item; });
          if (found == snapshot.items.begin() + snapshot.itemCount) {
            throw std::runtime_error(
                "public controller-bindings capture item is unavailable");
          }
          (void)panel->consumePointer({
              .xPixels = found->bounds.x + found->bounds.width * 0.5F,
              .yPixels = found->bounds.y + found->bounds.height * 0.5F,
              .wheelY = 0,
              .primaryPressed = true,
          });
        };
    activateItem(airfix::windows::AirfixWindowsRenderSettingsItem::
                     controllerCalibration);
    activateItem(
        airfix::windows::AirfixWindowsRenderSettingsItem::buttonBindings);
    const auto raster = uiRasterizer.rasterize(panel->snapshot());
    if (!raster.complete() || !renderer.setProductUiRaster(*raster.raster)) {
      throw std::runtime_error(
          "public controller-bindings raster could not be published");
    }
    renderer.capturePublicSettingsPanelFrameToBmp(
        *options.captureControllerBindingsPanelOutput);
    std::cout << "Public D3D11 controller-bindings panel frame captured\n";
    return 0;
  }
  airfix::windows::AirfixXAudio2Backend audio;
  if (audio.outputState() ==
      airfix::windows::AirfixXAudio2OutputState::initializationFailed) {
    throw std::runtime_error("XAudio2 2.9 initialization failed");
  }

  std::optional<LoadedPrivateContent> privateContent;
  std::optional<airfix::simulation::PlayerSpawnPose> playerSpawnPose;
  airfix::runtime::PlayerActorPoseRuntimeEndpoint playerActorPoseRuntime;
  std::weak_ptr<airfix::render::LegacyGameplayCameraMissionRuntime>
      gameplayCameraMissionRuntime;
  if (activeContentRoot.has_value()) {
    privateContent.emplace(
        loadPrivateContent(*activeContentRoot, options.mission, audio));
  }
  if (privateContent.has_value() && privateContent->missionRoom.has_value()) {
    if (!privateContent->weaponCrosshairTextures.has_value()) {
      throw std::runtime_error(
          "authenticated Windows mission has no weapon crosshair textures");
    }
    if (!privateContent->aircraftHealthGaugeTextures.has_value()) {
      throw std::runtime_error("authenticated Windows mission has no aircraft "
                               "health gauge textures");
    }
    if (!privateContent->aircraftHudRollingDigitTextures.has_value()) {
      throw std::runtime_error("authenticated Windows mission has no aircraft "
                               "rolling digit atlas");
    }
    if (!privateContent->aircraftHudInstrumentTextures.has_value()) {
      throw std::runtime_error("authenticated Windows mission has no aircraft "
                               "HUD instrument textures");
    }
    if (!privateContent->aircraftHudWeaponPanelTextures.has_value()) {
      throw std::runtime_error("authenticated Windows mission has no aircraft "
                               "HUD weapon panel textures");
    }
    if (!privateContent->aircraftHudIdentityStatusTextures.has_value()) {
      throw std::runtime_error("authenticated Windows mission has no aircraft "
                               "HUD identity/status textures");
    }
    playerSpawnPose = privateContent->missionRoom->playerSpawnPose;
    renderer.installLoadedMissionRoom(
        std::move(*privateContent->missionRoom),
        std::move(*privateContent->weaponCrosshairTextures),
        std::move(*privateContent->aircraftHealthGaugeTextures),
        std::move(*privateContent->aircraftHudRollingDigitTextures),
        std::move(*privateContent->aircraftHudInstrumentTextures),
        std::move(*privateContent->aircraftHudWeaponPanelTextures),
        std::move(*privateContent->aircraftHudIdentityStatusTextures),
        privateContent->revision);
    privateContent->missionRoom.reset();
    privateContent->weaponCrosshairTextures.reset();
    privateContent->aircraftHealthGaugeTextures.reset();
    privateContent->aircraftHudRollingDigitTextures.reset();
    privateContent->aircraftHudInstrumentTextures.reset();
    privateContent->aircraftHudWeaponPanelTextures.reset();
    privateContent->aircraftHudIdentityStatusTextures.reset();
    if (!renderer.missionWorldRoomInstalled()) {
      throw std::runtime_error(
          "authenticated Windows mission was not published");
    }
    playerActorPoseRuntime = renderer.playerActorPoseRuntimeEndpoint();
    gameplayCameraMissionRuntime =
        renderer.gameplayCameraMissionRuntimeEndpoint();
    if (gameplayCameraMissionRuntime.expired()) {
      throw std::runtime_error(
          "authenticated Windows gameplay camera was not published");
    }
  }
  audio.setActive(false);
  if (options.captureFrameOutput.has_value()) {
    renderer.captureFrameToBmp(*options.captureFrameOutput);
    std::cout << "Authenticated private D3D11 mission frame captured\n";
    return 0;
  }
  if (options.captureOverviewFrameOutput.has_value()) {
    renderer.captureMissionOverviewFrameToBmp(
        *options.captureOverviewFrameOutput);
    std::cout << "Authenticated private D3D11 full-room overview captured\n";
    return 0;
  }
  if (options.captureCrosshairValidationFrameOutput.has_value()) {
    renderer.captureMissionCrosshairValidationFrameToBmp(
        *options.captureCrosshairValidationFrameOutput);
    std::cout << "Authenticated private D3D11 crosshair validation frame "
                 "captured\n";
    return 0;
  }
  if (options.captureHealthGaugeValidationFrameOutput.has_value()) {
    renderer.captureMissionHealthGaugeValidationFrameToBmp(
        *options.captureHealthGaugeValidationFrameOutput);
    std::cout << "Authenticated private D3D11 health-gauge validation frame "
                 "captured\n";
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
          .verticalFovAdjustmentDegrees =
              transitionSettings.verticalFovAdjustmentDegrees,
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

  airfix::windows::AirfixWindowsUiAutomationHost uiAutomation;
  const bool uiAutomationAvailable =
      uiAutomation.attach(windowsNativeWindow(*window));
  if (!uiAutomationAvailable) {
    std::cerr << "Windows UI Automation: unavailable\n";
  }

  if (renderer.missionWorldRoomInstalled() && playerSpawnPose.has_value() &&
      !gameplayCameraMissionRuntime.expired()) {
    // Windows commits the authenticated room, audio clips, frozen spawn pose,
    // pose runtime, and replacement-safe gameplay-camera runtime as one
    // startup transaction. Supplying changing camera inputs remains a later
    // trace-driven milestone.
    session.setContentState(airfix::runtime::ContentState::ready);
  }
  airfix::windows::AirfixSdlInputAdapter input{controllerInputConfiguration};
  airfix::runtime::PlayerAircraftPresentationCoordinator
      playerAircraftPresentation;
  std::optional<airfix::windows::AirfixWindowsRenderSettingsPanel>
      renderSettingsPanel;
  std::optional<airfix::windows::AirfixWindowsRenderSettingsViewSnapshot>
      publishedPanelSnapshot;
  std::uint64_t controllerPreviewRefreshTime{};
  bool simulationPipelineReady = true;
  bool windowFocused =
      (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0U;

  const auto mayResumeSimulation = [&]() noexcept {
    return session.lifecycleState() ==
               airfix::runtime::LifecycleState::foregroundPaused &&
           windowFocused && simulationPipelineReady &&
           playerAircraftPresentation.healthy() &&
           renderer.missionWorldRoomInstalled() &&
           playerSpawnPose.has_value() &&
           !gameplayCameraMissionRuntime.expired();
  };
  const auto refreshRenderSettingsPanel = [&]() {
    if (!renderSettingsPanel.has_value()) {
      return;
    }
    renderSettingsPanel->setResumeAvailable(mayResumeSimulation());
    const auto snapshot = renderSettingsPanel->snapshot();
    if (publishedPanelSnapshot.has_value() &&
        *publishedPanelSnapshot == snapshot) {
      return;
    }
    const auto raster = uiRasterizer.rasterize(snapshot);
    const auto semantics =
        airfix::windows::buildAirfixWindowsUiSemanticTree(snapshot);
    const bool semanticsPublished =
        semantics.complete() &&
        (!uiAutomationAvailable || uiAutomation.publish(*semantics.tree));
    if (!raster.complete() || !renderer.setProductUiRaster(*raster.raster) ||
        !semanticsPublished) {
      throw std::runtime_error(
          "Windows product UI raster and semantics could not be published");
    }
    publishedPanelSnapshot = snapshot;
  };
  const auto openRenderSettingsPanel = [&]() {
    if (renderSettingsPanel.has_value()) {
      return;
    }
    audio.setActive(false);
    session.pause();
    input.setContext(airfix::input::InputContext::menu);
    if (!input.resetForGameplayBoundary()) {
      throw std::runtime_error(
          "SDL3 input reset failed while opening the pause menu");
    }
    renderSettingsPanel =
        airfix::windows::AirfixWindowsRenderSettingsPanel::create(
            renderSettingsCoordinator->persistentBase(),
            renderSettingsCoordinator->persistenceAvailable(),
            windowsUiExtent(*window),
            airfix::windows::airfixWindowsRenderSettingsSessionOverrideMask(
                renderSettingsCoordinator->sessionOverrides()),
            mayResumeSimulation(),
            airfix::windows::AirfixWindowsControllerProfilePanelState{
                .active = activeControllerRecord,
                .persisted = controllerProfileCoordinator->persistentBase(),
                .capabilities =
                    {
                        .persistenceAvailable = controllerProfileCoordinator
                                                    ->persistenceAvailable(),
                        .repairRequired = controllerProfileRepairRequired,
                    },
            });
    if (!renderSettingsPanel.has_value()) {
      throw std::runtime_error(
          "Windows render settings panel could not be created");
    }
    const auto controllerAxes = input.controllerAxisSnapshot();
    renderSettingsPanel->setControllerAxisInput({
        .rawAxes = controllerAxes.rawAxes,
        .connected = controllerAxes.connected,
    });
    controllerPreviewRefreshTime = SDL_GetTicksNS();
    publishedPanelSnapshot.reset();
    refreshRenderSettingsPanel();
  };
  const auto consumeRenderSettingsIntent =
      [&](const airfix::windows::AirfixWindowsRenderSettingsIntent &intent) {
        if (!renderSettingsPanel.has_value()) {
          return;
        }
        if (intent.applyTicket.has_value()) {
          const auto outcome =
              renderSettingsCoordinator->applyPersistentCandidate(
                  intent.applyTicket->candidate);
          renderSettingsPanel->setPersistenceAvailable(
              renderSettingsCoordinator->persistenceAvailable());
          if (outcome.accepted()) {
            (void)renderSettingsPanel->finishApplySuccess(*intent.applyTicket);
          } else {
            (void)renderSettingsPanel->finishApplyFailure(*intent.applyTicket);
          }
        }
        if (intent.controllerProfileSaveTicket.has_value()) {
          const auto outcome =
              controllerProfileCoordinator->applyPersistentCandidate(
                  intent.controllerProfileSaveTicket->candidate);
          if (outcome.accepted()) {
            controllerProfileRepairRequired = false;
            (void)renderSettingsPanel->finishControllerProfileSaveSuccess(
                *intent.controllerProfileSaveTicket);
          } else {
            (void)renderSettingsPanel->finishControllerProfileSaveFailure(
                *intent.controllerProfileSaveTicket);
          }
          renderSettingsPanel->setControllerProfilePersistenceAvailable(
              controllerProfileCoordinator->persistenceAvailable());
        }
        if (intent.resumeRequested && mayResumeSimulation()) {
          input.setContext(airfix::input::InputContext::gameplay);
          if (!input.resetForGameplayBoundary()) {
            throw std::runtime_error(
                "SDL3 input reset failed while resuming gameplay");
          }
          const bool resumed = session.resume();
          audio.setActive(resumed);
          if (resumed) {
            if (uiAutomationAvailable) {
              (void)uiAutomation.publish(std::nullopt);
            }
            renderer.clearProductUiRaster();
            renderSettingsPanel.reset();
            publishedPanelSnapshot.reset();
            return;
          }
        }
        refreshRenderSettingsPanel();
      };

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
        refreshRenderSettingsPanel();
      }

      if (renderSettingsPanel.has_value() &&
          (event.type == SDL_EVENT_MOUSE_MOTION ||
           event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           event.type == SDL_EVENT_MOUSE_WHEEL)) {
        const bool primaryPressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                                    event.button.button == SDL_BUTTON_LEFT;
        std::int32_t wheelY = 0;
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
          wheelY = event.wheel.y > 0.0F ? 1 : (event.wheel.y < 0.0F ? -1 : 0);
        }
        const auto intent = renderSettingsPanel->consumePointer(
            windowsPointerInput(*window, primaryPressed, wheelY));
        consumeRenderSettingsIntent(intent);
      }

      switch (event.type) {
      case SDL_EVENT_QUIT:
        running = false;
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        renderer.resize();
        if (renderSettingsPanel.has_value()) {
          renderSettingsPanel->setOutput(windowsUiExtent(*window));
          publishedPanelSnapshot.reset();
          refreshRenderSettingsPanel();
        }
        break;
      case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (renderSettingsPanel.has_value()) {
          renderSettingsPanel->setOutput(windowsUiExtent(*window));
          publishedPanelSnapshot.reset();
          refreshRenderSettingsPanel();
        }
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        windowFocused = false;
        input.focusLost();
        audio.setActive(false);
        session.enterInactive();
        refreshRenderSettingsPanel();
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
        refreshRenderSettingsPanel();
        break;
      default:
        break;
      }
    }

    while (const auto request = uiAutomation.popAction()) {
      if (!renderSettingsPanel.has_value()) {
        continue;
      }
      const auto result = renderSettingsPanel->consumeAccessibilityAction(
          request->screen, request->accessibilityGeneration, request->item,
          request->action);
      if (!result.accepted()) {
        continue;
      }
      session.noteInputActivity();
      consumeRenderSettingsIntent(result.intent);
    }

    const std::uint64_t currentTime = SDL_GetTicksNS();
    const std::uint64_t elapsed =
        currentTime >= previousTime ? currentTime - previousTime : 0U;
    previousTime = currentTime;
    if (renderSettingsPanel.has_value() &&
        renderSettingsPanel->screen() ==
            airfix::windows::AirfixWindowsRenderSettingsScreen::
                controllerAxisCalibration &&
        (currentTime < controllerPreviewRefreshTime ||
         currentTime - controllerPreviewRefreshTime >=
             controllerPreviewRefreshNanoseconds)) {
      const auto controllerAxes = input.controllerAxisSnapshot();
      renderSettingsPanel->setControllerAxisInput({
          .rawAxes = controllerAxes.rawAxes,
          .connected = controllerAxes.connected,
      });
      controllerPreviewRefreshTime = currentTime;
      refreshRenderSettingsPanel();
    }
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
      if (renderSettingsPanel.has_value()) {
        const auto intent =
            renderSettingsPanel->consumeInputFrame(inputFrame.frame);
        consumeRenderSettingsIntent(intent);
        inputAccumulator -= inputStepNanoseconds;
        continue;
      }
      if (inputFrame.frame.pressed(airfix::input::DigitalAction::globalPause)) {
        openRenderSettingsPanel();
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
        // Do not call gameplayCameraMissionRuntime.tryAdvance() from this
        // 60 Hz input loop. The recovered camera consumes the complete
        // trace-driven AirCraft producer state on its independent 12 ms
        // cadence; guessing that contract here would break parity.
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
