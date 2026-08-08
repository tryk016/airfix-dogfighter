#pragma once

#include "airfix/input/TouchControlsLayout.hpp"

#include <cstdint>
#include <optional>

namespace airfix::input {

inline constexpr std::uint8_t minimumTouchControlsRestingOpacityPercent = 50U;
inline constexpr std::uint8_t maximumTouchControlsRestingOpacityPercent = 100U;
inline constexpr std::uint8_t defaultTouchControlsRestingOpacityPercent = 100U;

enum class TouchControlsVisibilityMode : std::uint8_t {
  automaticControllerHide,
  alwaysVisible,
};

enum class TouchControlsHapticsMode : std::uint8_t {
  disabled,
  system,
};

struct TouchControlsPreferences final {
  TouchControlsLayoutProfile layout{};
  // Scales resting background fills only. Labels, semantic accent borders,
  // capture geometry, and active-state emphasis remain fully legible.
  std::uint8_t restingOpacityPercent{defaultTouchControlsRestingOpacityPercent};
  TouchControlsVisibilityMode visibilityMode{
      TouchControlsVisibilityMode::automaticControllerHide};
  TouchControlsHapticsMode hapticsMode{TouchControlsHapticsMode::system};

  [[nodiscard]] friend constexpr bool
  operator==(const TouchControlsPreferences &,
             const TouchControlsPreferences &) noexcept = default;
};

enum class TouchControlsPreferencesIssueKind : std::uint8_t {
  unsupportedSchema,
  invalidLayoutProfile,
  restingOpacityOutOfRange,
  invalidVisibilityMode,
  invalidHapticsMode,
};

struct TouchControlsPreferencesIssue final {
  TouchControlsPreferencesIssueKind kind{
      TouchControlsPreferencesIssueKind::invalidLayoutProfile};
};

[[nodiscard]] std::optional<TouchControlsPreferencesIssue>
validateTouchControlsPreferences(
    const TouchControlsPreferences &preferences) noexcept;

inline constexpr std::uint32_t touchControlsPreferencesRecordSchemaVersion = 3U;
inline constexpr std::uint32_t
    legacyTouchControlsPreferencesRecordSchemaVersion = 1U;
inline constexpr std::uint32_t
    visibilityTouchControlsPreferencesRecordSchemaVersion = 2U;

struct TouchControlsPreferencesRecord final {
  std::uint32_t schemaVersion{touchControlsPreferencesRecordSchemaVersion};
  std::uint8_t handedness{};
  std::uint8_t density{};
  std::uint8_t restingOpacityPercent{defaultTouchControlsRestingOpacityPercent};
  std::uint8_t visibilityMode{};
  std::uint8_t hapticsMode{1U};

  [[nodiscard]] friend constexpr bool
  operator==(const TouchControlsPreferencesRecord &,
             const TouchControlsPreferencesRecord &) noexcept = default;
};

struct TouchControlsPreferencesRecordBuildResult final {
  std::optional<TouchControlsPreferencesRecord> record;
  std::optional<TouchControlsPreferencesIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return record.has_value() && !issue.has_value();
  }
};

struct TouchControlsPreferencesFromRecordResult final {
  std::optional<TouchControlsPreferences> preferences;
  std::optional<TouchControlsPreferencesIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return preferences.has_value() && !issue.has_value();
  }
};

[[nodiscard]] TouchControlsPreferencesRecordBuildResult
makeTouchControlsPreferencesRecord(
    const TouchControlsPreferences &preferences) noexcept;

[[nodiscard]] TouchControlsPreferencesFromRecordResult
touchControlsPreferencesFromRecord(
    const TouchControlsPreferencesRecord &record) noexcept;

struct TouchControlsVisibilityContext final {
  bool gameplayActive{};
  bool controllerConnected{};
};

enum class TouchControlsVisibilityDecision : std::uint8_t {
  hidden,
  visible,
};

// Resolves only presentation visibility. A hidden decision never changes the
// latched absolute throttle value; the native view neutralizes every held
// touch before applying it. Invalid preferences fail closed with nullopt.
[[nodiscard]] std::optional<TouchControlsVisibilityDecision>
resolveTouchControlsVisibility(const TouchControlsPreferences &preferences,
                               TouchControlsVisibilityContext context) noexcept;

} // namespace airfix::input
