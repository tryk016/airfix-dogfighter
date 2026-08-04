#include "airfix/texture/TextureModeMissionReload.hpp"

namespace airfix::texture {

std::optional<TextureModeMissionReloadCoordinator>
TextureModeMissionReloadCoordinator::create(
    const ActiveMissionTextureState activeMission,
    const TexturePackageAvailability packageAvailability,
    const TextureModeMissionReloadCallback callback) noexcept {
  const auto validation = resolveTextureModeState(
      activeMission.requestedMode, packageAvailability, activeMission);
  if (!validation.complete() || callback.function == nullptr) {
    return std::nullopt;
  }
  return TextureModeMissionReloadCoordinator{activeMission,
                                             packageAvailability, callback};
}

TextureModeMissionReloadCoordinator::TextureModeMissionReloadCoordinator(
    const ActiveMissionTextureState activeMission,
    const TexturePackageAvailability packageAvailability,
    const TextureModeMissionReloadCallback callback) noexcept
    : activeMission_(activeMission), packageAvailability_(packageAvailability),
      callback_(callback) {}

TextureModeMissionReloadOutcome
TextureModeMissionReloadCoordinator::request(
    const TextureMode requestedMode) noexcept {
  const auto target = resolveTextureModeState(requestedMode,
                                              packageAvailability_,
                                              activeMission_);
  if (!target.complete()) {
    return {
        .status = TextureModeMissionReloadStatus::invalidState,
        .requestedState = {},
        .activeMission = activeMission_,
    };
  }
  if (!target.state->missionReloadRequired) {
    return {
        .status = TextureModeMissionReloadStatus::unchanged,
        .requestedState = *target.state,
        .activeMission = activeMission_,
    };
  }

  if (!callback_.function(callback_.context, target.state->effectiveMode)) {
    return {
        .status = TextureModeMissionReloadStatus::reloadFailed,
        .requestedState = *target.state,
        .activeMission = activeMission_,
    };
  }

  const ActiveMissionTextureState publishedActive{
      .requestedMode = requestedMode,
      .effectiveMode = target.state->effectiveMode,
  };
  const auto published = resolveTextureModeState(
      requestedMode, packageAvailability_, publishedActive);
  if (!published.complete() || published.state->missionReloadRequired) {
    return {
        .status = TextureModeMissionReloadStatus::invalidState,
        .requestedState = {},
        .activeMission = activeMission_,
    };
  }
  activeMission_ = publishedActive;
  return {
      .status = TextureModeMissionReloadStatus::reloaded,
      .requestedState = *published.state,
      .activeMission = activeMission_,
  };
}

} // namespace airfix::texture
