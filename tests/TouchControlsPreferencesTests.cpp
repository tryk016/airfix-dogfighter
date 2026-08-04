#include "airfix/input/TouchControlsPreferences.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testDefaultsAndCustomRoundTrip() {
  const airfix::input::TouchControlsPreferences defaults;
  require(
      !airfix::input::validateTouchControlsPreferences(defaults).has_value(),
      "default touch preferences are invalid");
  const auto defaultRecord =
      airfix::input::makeTouchControlsPreferencesRecord(defaults);
  require(defaultRecord.complete(), "default record was not produced");
  const auto decoded =
      airfix::input::touchControlsPreferencesFromRecord(*defaultRecord.record);
  require(decoded.complete() && *decoded.preferences == defaults,
          "default touch preferences did not round-trip");

  const airfix::input::TouchControlsPreferences custom{
      .layout =
          {
              .handedness = airfix::input::TouchControlsHandedness::leftHanded,
              .density = airfix::input::TouchControlsDensity::compact,
          },
      .restingOpacityPercent = 50U,
      .visibilityMode =
          airfix::input::TouchControlsVisibilityMode::alwaysVisible,
  };
  const auto customRecord =
      airfix::input::makeTouchControlsPreferencesRecord(custom);
  require(customRecord.complete() && customRecord.record->handedness == 1U &&
              customRecord.record->density == 1U &&
              customRecord.record->restingOpacityPercent == 50U &&
              customRecord.record->visibilityMode == 1U,
          "custom record mapping is incorrect");
  const auto customDecoded =
      airfix::input::touchControlsPreferencesFromRecord(*customRecord.record);
  require(customDecoded.complete() && *customDecoded.preferences == custom,
          "custom touch preferences did not round-trip");
}

void testInvalidValuesFailClosed() {
  auto preferences = airfix::input::TouchControlsPreferences{};
  preferences.layout.schemaVersion = 2U;
  require(airfix::input::validateTouchControlsPreferences(preferences)->kind ==
              airfix::input::TouchControlsPreferencesIssueKind::
                  invalidLayoutProfile,
          "future layout schema was accepted");

  for (const auto opacity : {49U, 101U}) {
    preferences = {};
    preferences.restingOpacityPercent = static_cast<std::uint8_t>(opacity);
    const auto issue =
        airfix::input::validateTouchControlsPreferences(preferences);
    require(issue.has_value() &&
                issue->kind ==
                    airfix::input::TouchControlsPreferencesIssueKind::
                        restingOpacityOutOfRange,
            "unsafe resting opacity was accepted");
  }

  auto record = airfix::input::TouchControlsPreferencesRecord{};
  record.schemaVersion = 3U;
  require(
      airfix::input::touchControlsPreferencesFromRecord(record).issue->kind ==
          airfix::input::TouchControlsPreferencesIssueKind::unsupportedSchema,
      "future semantic record was interpreted");
  record = {};
  record.handedness = 0xFFU;
  require(!airfix::input::touchControlsPreferencesFromRecord(record).complete(),
          "forged handedness record was accepted");
  record = {};
  record.density = 0xFFU;
  require(!airfix::input::touchControlsPreferencesFromRecord(record).complete(),
          "forged density record was accepted");
  record = {};
  record.visibilityMode = 0xFFU;
  require(
      airfix::input::touchControlsPreferencesFromRecord(record).issue->kind ==
          airfix::input::TouchControlsPreferencesIssueKind::
              invalidVisibilityMode,
      "forged visibility mode was accepted");

  preferences = {};
  preferences.visibilityMode =
      static_cast<airfix::input::TouchControlsVisibilityMode>(0xFFU);
  require(airfix::input::validateTouchControlsPreferences(preferences)->kind ==
              airfix::input::TouchControlsPreferencesIssueKind::
                  invalidVisibilityMode,
          "forged visibility preference was accepted");
}

void testVisibilityPolicy() {
  using airfix::input::TouchControlsVisibilityDecision;
  airfix::input::TouchControlsPreferences preferences;
  const auto resolve = [&](const bool active, const bool connected) {
    return airfix::input::resolveTouchControlsVisibility(
        preferences,
        {.gameplayActive = active, .controllerConnected = connected});
  };
  require(resolve(false, false) == TouchControlsVisibilityDecision::hidden &&
              resolve(false, true) == TouchControlsVisibilityDecision::hidden,
          "inactive gameplay exposed touch controls");
  require(resolve(true, false) == TouchControlsVisibilityDecision::visible &&
              resolve(true, true) == TouchControlsVisibilityDecision::hidden,
          "automatic controller-hide policy changed");

  preferences.visibilityMode =
      airfix::input::TouchControlsVisibilityMode::alwaysVisible;
  require(resolve(true, false) == TouchControlsVisibilityDecision::visible &&
              resolve(true, true) == TouchControlsVisibilityDecision::visible,
          "always-visible policy changed");

  preferences.visibilityMode =
      static_cast<airfix::input::TouchControlsVisibilityMode>(0xFFU);
  require(!resolve(true, false).has_value(),
          "invalid visibility policy did not fail closed");
}

} // namespace

int main() {
  try {
    testDefaultsAndCustomRoundTrip();
    testInvalidValuesFailClosed();
    testVisibilityPolicy();
    std::cout << "Touch controls preferences tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Touch controls preferences tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
