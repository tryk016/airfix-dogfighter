#pragma once

#include "airfix/texture/TextureModeState.hpp"

#include <cstdint>
#include <optional>

namespace airfix::texture {

enum class TextureModeMissionReloadStatus : std::uint8_t {
  unchanged,
  reloaded,
  reloadFailed,
  invalidState,
};

struct TextureModeMissionReloadOutcome final {
  TextureModeMissionReloadStatus status{
      TextureModeMissionReloadStatus::invalidState};
  TextureModeRuntimeState requestedState;
  ActiveMissionTextureState activeMission;

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status == TextureModeMissionReloadStatus::unchanged ||
           status == TextureModeMissionReloadStatus::reloaded;
  }
};

struct TextureModeMissionReloadCallback final {
  using Function = bool (*)(void *context,
                            TextureMode effectiveMode) noexcept;

  Function function{};
  void *context{};
};

// Owner-thread coordinator for a complete mission visual replacement. The
// product callback must prepare every CPU/GPU resource before its no-fail
// publication step. A failed callback leaves activeMission() byte-for-field
// unchanged; only a successful publication advances requested/effective state.
class TextureModeMissionReloadCoordinator final {
public:
  [[nodiscard]] static std::optional<TextureModeMissionReloadCoordinator>
  create(ActiveMissionTextureState activeMission,
         TexturePackageAvailability packageAvailability,
         TextureModeMissionReloadCallback callback) noexcept;

  [[nodiscard]] TextureModeMissionReloadOutcome
  request(TextureMode requestedMode) noexcept;

  [[nodiscard]] constexpr const ActiveMissionTextureState &
  activeMission() const noexcept {
    return activeMission_;
  }

  [[nodiscard]] constexpr TexturePackageAvailability
  packageAvailability() const noexcept {
    return packageAvailability_;
  }

private:
  TextureModeMissionReloadCoordinator(
      ActiveMissionTextureState activeMission,
      TexturePackageAvailability packageAvailability,
      TextureModeMissionReloadCallback callback) noexcept;

  ActiveMissionTextureState activeMission_;
  TexturePackageAvailability packageAvailability_;
  TextureModeMissionReloadCallback callback_;
};

} // namespace airfix::texture
