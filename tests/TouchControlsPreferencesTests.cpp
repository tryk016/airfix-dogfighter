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
  };
  const auto customRecord =
      airfix::input::makeTouchControlsPreferencesRecord(custom);
  require(customRecord.complete() && customRecord.record->handedness == 1U &&
              customRecord.record->density == 1U &&
              customRecord.record->restingOpacityPercent == 50U,
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
  record.schemaVersion = 2U;
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
}

} // namespace

int main() {
  try {
    testDefaultsAndCustomRoundTrip();
    testInvalidValuesFailClosed();
    std::cout << "Touch controls preferences tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Touch controls preferences tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
