#pragma once

#include "airfix/texture/TextureMode.hpp"

#include <cstdint>
#include <optional>

namespace airfix::texture {

// Availability is intentionally coarse and safe to expose in product UI.
// It never carries a path, manifest name, checksum, or failing asset identity.
enum class TexturePackageAvailability : std::uint8_t {
  notConfigured,
  validating,
  ready,
  unavailable,
};

struct ActiveMissionTextureState final {
  TextureMode requestedMode{TextureMode::classic};
  TextureMode effectiveMode{TextureMode::classic};

  [[nodiscard]] friend constexpr bool
  operator==(const ActiveMissionTextureState &,
             const ActiveMissionTextureState &) noexcept = default;
};

struct TextureModeRuntimeState final {
  TextureMode requestedMode{TextureMode::classic};
  TexturePackageAvailability packageAvailability{
      TexturePackageAvailability::notConfigured};
  TextureMode effectiveMode{TextureMode::classic};
  bool missionReloadRequired{};

  [[nodiscard]] constexpr bool fallbackToClassic() const noexcept {
    return requestedMode == TextureMode::enhanced &&
           effectiveMode == TextureMode::classic;
  }

  [[nodiscard]] constexpr bool enhancedPackageReady() const noexcept {
    return packageAvailability == TexturePackageAvailability::ready;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const TextureModeRuntimeState &,
             const TextureModeRuntimeState &) noexcept = default;
};

enum class TextureModeStateIssue : std::uint8_t {
  unsupportedRequestedMode,
  unsupportedPackageAvailability,
  unsupportedActiveRequestedMode,
  unsupportedActiveEffectiveMode,
  inconsistentActiveMissionState,
};

struct TextureModeStateResolveResult final {
  std::optional<TextureModeRuntimeState> state;
  std::optional<TextureModeStateIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return state.has_value() && !issue.has_value();
  }
};

// Resolves a requested preference without touching package files. Enhanced is
// effective only for a completely validated ready package. A running mission
// is immutable: any requested/effective difference is reported as requiring a
// controlled mission snapshot reload instead of an in-place texture mutation.
[[nodiscard]] constexpr TextureModeStateResolveResult resolveTextureModeState(
    const TextureMode requestedMode,
    const TexturePackageAvailability packageAvailability,
    const std::optional<ActiveMissionTextureState> activeMission =
        std::nullopt) noexcept {
  const auto validMode = [](const TextureMode mode) constexpr noexcept {
    switch (mode) {
    case TextureMode::classic:
    case TextureMode::enhanced:
      return true;
    }
    return false;
  };
  const auto validAvailability =
      [](const TexturePackageAvailability availability) constexpr noexcept {
        switch (availability) {
        case TexturePackageAvailability::notConfigured:
        case TexturePackageAvailability::validating:
        case TexturePackageAvailability::ready:
        case TexturePackageAvailability::unavailable:
          return true;
        }
        return false;
      };

  if (!validMode(requestedMode)) {
    return {.issue = TextureModeStateIssue::unsupportedRequestedMode};
  }
  if (!validAvailability(packageAvailability)) {
    return {
        .issue = TextureModeStateIssue::unsupportedPackageAvailability,
    };
  }
  if (activeMission.has_value()) {
    if (!validMode(activeMission->requestedMode)) {
      return {
          .issue = TextureModeStateIssue::unsupportedActiveRequestedMode,
      };
    }
    if (!validMode(activeMission->effectiveMode)) {
      return {
          .issue = TextureModeStateIssue::unsupportedActiveEffectiveMode,
      };
    }
    if (activeMission->requestedMode == TextureMode::classic &&
        activeMission->effectiveMode != TextureMode::classic) {
      return {
          .issue = TextureModeStateIssue::inconsistentActiveMissionState,
      };
    }
  }

  const auto effectiveMode =
      requestedMode == TextureMode::enhanced &&
              packageAvailability == TexturePackageAvailability::ready
          ? TextureMode::enhanced
          : TextureMode::classic;
  const bool reloadRequired = activeMission.has_value() &&
                              (activeMission->requestedMode != requestedMode ||
                               activeMission->effectiveMode != effectiveMode);
  return {
      .state =
          TextureModeRuntimeState{
              .requestedMode = requestedMode,
              .packageAvailability = packageAvailability,
              .effectiveMode = effectiveMode,
              .missionReloadRequired = reloadRequired,
          },
      .issue = std::nullopt,
  };
}

} // namespace airfix::texture
