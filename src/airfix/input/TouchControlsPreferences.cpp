#include "airfix/input/TouchControlsPreferences.hpp"

namespace airfix::input {
namespace {

inline constexpr std::uint8_t rightHandedRecordValue = 0U;
inline constexpr std::uint8_t leftHandedRecordValue = 1U;
inline constexpr std::uint8_t automaticDensityRecordValue = 0U;
inline constexpr std::uint8_t compactDensityRecordValue = 1U;

[[nodiscard]] constexpr TouchControlsPreferencesIssue
issue(const TouchControlsPreferencesIssueKind kind) noexcept {
  return {.kind = kind};
}

[[nodiscard]] constexpr std::uint8_t
recordValue(const TouchControlsHandedness handedness) noexcept {
  switch (handedness) {
  case TouchControlsHandedness::rightHanded:
    return rightHandedRecordValue;
  case TouchControlsHandedness::leftHanded:
    return leftHandedRecordValue;
  }
  return rightHandedRecordValue;
}

[[nodiscard]] constexpr std::uint8_t
recordValue(const TouchControlsDensity density) noexcept {
  switch (density) {
  case TouchControlsDensity::automatic:
    return automaticDensityRecordValue;
  case TouchControlsDensity::compact:
    return compactDensityRecordValue;
  }
  return automaticDensityRecordValue;
}

[[nodiscard]] constexpr std::optional<TouchControlsHandedness>
handednessFromRecordValue(const std::uint8_t value) noexcept {
  switch (value) {
  case rightHandedRecordValue:
    return TouchControlsHandedness::rightHanded;
  case leftHandedRecordValue:
    return TouchControlsHandedness::leftHanded;
  default:
    return std::nullopt;
  }
}

[[nodiscard]] constexpr std::optional<TouchControlsDensity>
densityFromRecordValue(const std::uint8_t value) noexcept {
  switch (value) {
  case automaticDensityRecordValue:
    return TouchControlsDensity::automatic;
  case compactDensityRecordValue:
    return TouchControlsDensity::compact;
  default:
    return std::nullopt;
  }
}

} // namespace

std::optional<TouchControlsPreferencesIssue> validateTouchControlsPreferences(
    const TouchControlsPreferences &preferences) noexcept {
  if (!validTouchControlsLayoutProfile(preferences.layout)) {
    return issue(TouchControlsPreferencesIssueKind::invalidLayoutProfile);
  }
  if (preferences.restingOpacityPercent <
          minimumTouchControlsRestingOpacityPercent ||
      preferences.restingOpacityPercent >
          maximumTouchControlsRestingOpacityPercent) {
    return issue(TouchControlsPreferencesIssueKind::restingOpacityOutOfRange);
  }
  return std::nullopt;
}

TouchControlsPreferencesRecordBuildResult makeTouchControlsPreferencesRecord(
    const TouchControlsPreferences &preferences) noexcept {
  const auto validation = validateTouchControlsPreferences(preferences);
  if (validation.has_value()) {
    return {.record = std::nullopt, .issue = validation};
  }
  return {
      .record =
          TouchControlsPreferencesRecord{
              .schemaVersion = touchControlsPreferencesRecordSchemaVersion,
              .handedness = recordValue(preferences.layout.handedness),
              .density = recordValue(preferences.layout.density),
              .restingOpacityPercent = preferences.restingOpacityPercent,
          },
      .issue = std::nullopt,
  };
}

TouchControlsPreferencesFromRecordResult touchControlsPreferencesFromRecord(
    const TouchControlsPreferencesRecord &record) noexcept {
  if (record.schemaVersion != touchControlsPreferencesRecordSchemaVersion) {
    return {
        .preferences = std::nullopt,
        .issue = issue(TouchControlsPreferencesIssueKind::unsupportedSchema),
    };
  }
  const auto handedness = handednessFromRecordValue(record.handedness);
  const auto density = densityFromRecordValue(record.density);
  if (!handedness.has_value() || !density.has_value()) {
    return {
        .preferences = std::nullopt,
        .issue = issue(TouchControlsPreferencesIssueKind::invalidLayoutProfile),
    };
  }
  const TouchControlsPreferences preferences{
      .layout =
          {
              .schemaVersion = touchControlsLayoutProfileSchemaVersion,
              .handedness = *handedness,
              .density = *density,
          },
      .restingOpacityPercent = record.restingOpacityPercent,
  };
  const auto validation = validateTouchControlsPreferences(preferences);
  if (validation.has_value()) {
    return {.preferences = std::nullopt, .issue = validation};
  }
  return {.preferences = preferences, .issue = std::nullopt};
}

} // namespace airfix::input
