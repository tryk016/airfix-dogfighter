#include "airfix/crypto/Sha256.hpp"
#include "airfix/settings/TouchControlsPreferencesStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char *currentName = "touch-controls.aftc";

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TempDirectory final {
public:
  TempDirectory() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("airfix-touch-controls-store-" + std::to_string(stamp));
    std::filesystem::create_directories(root_);
  }

  ~TempDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  [[nodiscard]] std::filesystem::path settings() const {
    return root_ / "settings";
  }

  [[nodiscard]] std::filesystem::path current() const {
    return settings() / currentName;
  }

private:
  std::filesystem::path root_;
};

[[nodiscard]] airfix::input::TouchControlsPreferences
preferences(const std::uint8_t opacity,
            const airfix::input::TouchControlsHandedness handedness =
                airfix::input::TouchControlsHandedness::rightHanded) {
  return {
      .layout = {.handedness = handedness},
      .restingOpacityPercent = opacity,
  };
}

void writeRaw(const std::filesystem::path &path,
              const std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(output.is_open(), "cannot create AFTC fixture");
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  require(output.good(), "cannot write AFTC fixture");
}

void putU32(std::vector<std::uint8_t> &bytes, const std::size_t offset,
            const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void repairEnvelope(std::vector<std::uint8_t> &bytes) {
  putU32(bytes, 12U, static_cast<std::uint32_t>(bytes.size()));
  airfix::crypto::Sha256 hash;
  hash.update(std::span<const std::uint8_t>(bytes).first(16U));
  hash.update(std::span<const std::uint8_t>(bytes).subspan(48U));
  const auto digest = hash.finish();
  std::copy(digest.begin(), digest.end(), bytes.begin() + 16);
}

void requireStoreError(
    const std::function<void()> &action,
    const airfix::settings::TouchControlsPreferencesStoreErrorKind expected) {
  try {
    action();
  } catch (const airfix::settings::TouchControlsPreferencesStoreError &error) {
    require(error.kind() == expected, "AFTC store error kind mismatch");
    return;
  }
  throw std::runtime_error("expected AFTC store error");
}

void testMissingSaveRotationAndRecovery() {
  TempDirectory temporary;
  const auto missing =
      airfix::settings::loadTouchControlsPreferences(temporary.settings());
  require(
      missing.preferences == airfix::input::TouchControlsPreferences{} &&
          missing.source ==
              airfix::settings::TouchControlsPreferencesLoadSource::defaults &&
          missing.current.status ==
              airfix::settings::TouchControlsPreferencesFileStatus::missing &&
          !missing.persistenceBlocked &&
          !airfix::settings::touchControlsPreferencesNeedsRepair(missing),
      "missing AFTC store did not select clean defaults");

  const auto first = preferences(70U);
  const auto firstSave = airfix::settings::saveTouchControlsPreferences(
      temporary.settings(), first);
  require(firstSave.status ==
              airfix::settings::TouchControlsPreferencesSaveStatus::committed,
          "first AFTC save did not commit");
  const auto reloaded =
      airfix::settings::loadTouchControlsPreferences(temporary.settings());
  require(reloaded.preferences == first &&
              reloaded.source ==
                  airfix::settings::TouchControlsPreferencesLoadSource::current,
          "saved AFTC did not survive restart");

  const auto second =
      preferences(50U, airfix::input::TouchControlsHandedness::leftHanded);
  const auto secondSave = airfix::settings::saveTouchControlsPreferences(
      temporary.settings(), second);
  require(secondSave.backupRotated,
          "second AFTC save did not rotate valid current");
  const std::vector<std::uint8_t> corrupt{'b', 'a', 'd'};
  writeRaw(temporary.current(), corrupt);
  const auto recovered =
      airfix::settings::loadTouchControlsPreferences(temporary.settings());
  require(recovered.preferences == first && recovered.recoveredFromBackup() &&
              airfix::settings::touchControlsPreferencesNeedsRepair(recovered),
          "corrupt current did not recover validated AFTC backup");
}

void testFutureAndReplaceableInvalidRecords() {
  {
    TempDirectory temporary;
    std::filesystem::create_directories(temporary.settings());
    const auto record =
        airfix::input::makeTouchControlsPreferencesRecord(preferences(75U));
    require(record.complete(), "future AFTC fixture is invalid");
    auto future = airfix::settings::encodeTouchControlsPreferencesDocument(
        *record.record);
    putU32(future, 8U, 4U);
    future.push_back(0xA5U);
    repairEnvelope(future);
    writeRaw(temporary.current(), future);
    const auto loaded =
        airfix::settings::loadTouchControlsPreferences(temporary.settings());
    require(loaded.current.status ==
                    airfix::settings::TouchControlsPreferencesFileStatus::
                        futureSchema &&
                loaded.persistenceBlocked &&
                loaded.preferences == airfix::input::TouchControlsPreferences{},
            "future AFTC was interpreted or did not block writes");
    requireStoreError(
        [&] {
          (void)airfix::settings::saveTouchControlsPreferences(
              temporary.settings(), preferences(80U));
        },
        airfix::settings::TouchControlsPreferencesStoreErrorKind::
            persistenceBlocked);
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directories(temporary.settings());
    const std::vector<std::uint8_t> oversized(
        airfix::settings::maximumTouchControlsPreferencesDocumentBytes + 1U,
        0xA5U);
    writeRaw(temporary.current(), oversized);
    const auto loaded =
        airfix::settings::loadTouchControlsPreferences(temporary.settings());
    require(loaded.current.status ==
                    airfix::settings::TouchControlsPreferencesFileStatus::
                        oversized &&
                !loaded.persistenceBlocked &&
                airfix::settings::touchControlsPreferencesNeedsRepair(loaded),
            "oversized AFTC did not select replaceable defaults");
    (void)airfix::settings::saveTouchControlsPreferences(temporary.settings(),
                                                         preferences(85U));
    require(airfix::settings::loadTouchControlsPreferences(temporary.settings())
                    .preferences == preferences(85U),
            "oversized AFTC could not be replaced safely");
  }
}

void testLegacyCurrentMigratesAndRequiresRepair() {
  for (const auto schema : {
           airfix::input::legacyTouchControlsPreferencesRecordSchemaVersion,
           airfix::input::visibilityTouchControlsPreferencesRecordSchemaVersion,
       }) {
    TempDirectory temporary;
    std::filesystem::create_directories(temporary.settings());
    auto source = preferences(75U);
    source.visibilityMode =
        airfix::input::TouchControlsVisibilityMode::alwaysVisible;
    const auto record =
        airfix::input::makeTouchControlsPreferencesRecord(source);
    require(record.complete(), "legacy AFTC fixture is invalid");
    auto legacy = airfix::settings::encodeTouchControlsPreferencesDocument(
        *record.record);
    legacy.resize(
        schema ==
                airfix::input::legacyTouchControlsPreferencesRecordSchemaVersion
            ? 63U
            : 68U);
    putU32(legacy, 8U, schema);
    repairEnvelope(legacy);
    writeRaw(temporary.current(), legacy);

    auto expected = source;
    expected.hapticsMode = airfix::input::TouchControlsHapticsMode::disabled;
    if (schema ==
        airfix::input::legacyTouchControlsPreferencesRecordSchemaVersion) {
      expected.visibilityMode =
          airfix::input::TouchControlsVisibilityMode::automaticControllerHide;
    }
    const auto migrated =
        airfix::settings::loadTouchControlsPreferences(temporary.settings());
    require(
        migrated.source ==
                airfix::settings::TouchControlsPreferencesLoadSource::current &&
            migrated.preferences == expected &&
            migrated.current.schemaVersion == schema &&
            airfix::settings::touchControlsPreferencesNeedsRepair(migrated),
        "legacy current AFTC was not migrated with a repair request");

    const auto saved = airfix::settings::saveTouchControlsPreferences(
        temporary.settings(), migrated.preferences);
    require(saved.backupRotated,
            "legacy current AFTC was not retained during repair");
    const auto repaired =
        airfix::settings::loadTouchControlsPreferences(temporary.settings());
    require(
        repaired.preferences == expected &&
            repaired.current.schemaVersion ==
                airfix::input::touchControlsPreferencesRecordSchemaVersion &&
            !airfix::settings::touchControlsPreferencesNeedsRepair(repaired),
        "legacy AFTC repair did not publish canonical schema 3");
  }
}

void testInvalidCandidateAndWrongTypeBlock() {
  TempDirectory invalid;
  auto candidate = preferences(70U);
  candidate.restingOpacityPercent = 49U;
  requireStoreError(
      [&] {
        (void)airfix::settings::saveTouchControlsPreferences(invalid.settings(),
                                                             candidate);
      },
      airfix::settings::TouchControlsPreferencesStoreErrorKind::
          invalidPreferences);

  TempDirectory wrongType;
  std::filesystem::create_directories(wrongType.settings());
  std::filesystem::create_directory(wrongType.current());
  const auto loaded =
      airfix::settings::loadTouchControlsPreferences(wrongType.settings());
  require(loaded.current.status ==
                  airfix::settings::TouchControlsPreferencesFileStatus::
                      wrongTypeOrLinked &&
              loaded.persistenceBlocked,
          "wrong-type AFTC current did not block persistence");
}

} // namespace

int main() {
  try {
    testMissingSaveRotationAndRecovery();
    testFutureAndReplaceableInvalidRecords();
    testLegacyCurrentMigratesAndRequiresRepair();
    testInvalidCandidateAndWrongTypeBlock();
    std::cout << "Touch controls preferences store tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Touch controls preferences store tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
