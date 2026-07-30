#include "airfix/crypto/Sha256.hpp"
#include "airfix/input/ControllerInputProfile.hpp"
#include "airfix/io/DurableFile.hpp"
#include "airfix/settings/ControllerInputProfileCodec.hpp"
#include "airfix/settings/ControllerInputProfileStore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view currentName = "controller-input.afip";
constexpr std::string_view backupName = "controller-input.afip.backup";
constexpr std::string_view currentPartialName = "controller-input.afip.partial";

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TempDirectory final {
public:
  TempDirectory() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
      root_ = std::filesystem::temp_directory_path() /
              ("airfix-controller-profile-store-" + std::to_string(stamp) +
               "-" + std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(root_, error)) {
        settings_ = root_ / "settings";
        return;
      }
      if (error && error != std::errc::file_exists) {
        throw std::runtime_error("cannot create controller profile test root");
      }
    }
    throw std::runtime_error("cannot allocate controller profile test root");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  [[nodiscard]] const std::filesystem::path &settings() const {
    return settings_;
  }

  [[nodiscard]] std::filesystem::path current() const {
    return settings_ / currentName;
  }

  [[nodiscard]] std::filesystem::path backup() const {
    return settings_ / backupName;
  }

  [[nodiscard]] std::filesystem::path currentPartial() const {
    return settings_ / currentPartialName;
  }

private:
  std::filesystem::path root_;
  std::filesystem::path settings_;
};

[[nodiscard]] airfix::input::ControllerInputProfileRecord
profile(const std::uint16_t sensitivity, const bool inverted = false) {
  auto record = airfix::input::makeDefaultControllerInputProfileRecord();
  record.axes[0].sensitivityPermille = sensitivity;
  record.axes[0].inverted = static_cast<std::uint8_t>(inverted ? 1U : 0U);
  require(airfix::input::resolveControllerInputProfile(record).complete(),
          "synthetic controller profile is invalid");
  return record;
}

[[nodiscard]] std::vector<std::uint8_t>
readFile(const std::filesystem::path &path) {
  return airfix::io::readBoundedRegularFile(
      path, airfix::settings::maximumControllerInputProfileDocumentBytes);
}

void writeRaw(const std::filesystem::path &path,
              const std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output || (!bytes.empty() &&
                  !output.write(reinterpret_cast<const char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size())))) {
    throw std::runtime_error("cannot write controller profile fixture");
  }
}

void repairFutureEnvelope(std::vector<std::uint8_t> &bytes) {
  bytes[8U] = 2U;
  bytes[9U] = 0U;
  bytes[10U] = 0U;
  bytes[11U] = 0U;
  airfix::crypto::Sha256 hash;
  hash.update(std::span<const std::uint8_t>(bytes).first(16U));
  hash.update(std::span<const std::uint8_t>(bytes).subspan(48U));
  const auto digest = hash.finish();
  std::copy(digest.begin(), digest.end(), bytes.begin() + 16);
}

void requireLoadedRecord(
    const airfix::settings::ControllerInputProfileLoadResult &loaded,
    const airfix::input::ControllerInputProfileRecord &expected,
    const char *const message) {
  require(loaded.profile.record() == expected, message);
}

void requireStoreError(
    const std::function<void()> &action,
    const airfix::settings::ControllerInputProfileStoreErrorKind kind,
    const std::filesystem::path &privatePath,
    const std::optional<airfix::input::ControllerInputProfileRecord>
        &expectedRequested = std::nullopt) {
  try {
    action();
  } catch (const airfix::settings::ControllerInputProfileStoreError &error) {
    require(error.kind() == kind,
            "controller profile store error kind mismatch");
    require(std::string(error.what()).find(privatePath.string()) ==
                std::string::npos,
            "controller profile store error leaked a host path");
    require(error.requestedRecord() == expectedRequested,
            "controller profile store error retained the wrong request");
    return;
  }
  throw std::runtime_error("expected controller profile store error");
}

void testMissingSaveUnchangedRotationAndFallback() {
  TempDirectory temporary;
  const auto defaults =
      airfix::input::makeDefaultControllerInputProfileRecord();
  const auto missing =
      airfix::settings::loadControllerInputProfile(temporary.settings());
  require(
      missing.source ==
              airfix::settings::ControllerInputProfileLoadSource::defaults &&
          !missing.persistenceBlocked &&
          missing.current.status ==
              airfix::settings::ControllerInputProfileFileStatus::missing &&
          missing.backup.status ==
              airfix::settings::ControllerInputProfileFileStatus::missing,
      "missing store did not select safe defaults");
  requireLoadedRecord(missing, defaults,
                      "missing store did not resolve canonical defaults");

  const auto first = profile(750U);
  const auto firstSave =
      airfix::settings::saveControllerInputProfile(temporary.settings(), first);
  require(
      firstSave.status ==
              airfix::settings::ControllerInputProfileSaveStatus::committed &&
          !firstSave.backupRotated,
      "first controller profile save had the wrong result");
  const auto firstReload =
      airfix::settings::loadControllerInputProfile(temporary.settings());
  require(firstReload.source ==
              airfix::settings::ControllerInputProfileLoadSource::current,
          "saved controller profile was not loaded as current");
  requireLoadedRecord(firstReload, first,
                      "saved controller profile did not survive restart");

  const auto unchanged =
      airfix::settings::saveControllerInputProfile(temporary.settings(), first);
  require(
      unchanged.status ==
              airfix::settings::ControllerInputProfileSaveStatus::unchanged &&
          !unchanged.backupRotated,
      "idempotent profile save rewrote storage");

  const auto second = profile(1500U, true);
  const auto secondSave = airfix::settings::saveControllerInputProfile(
      temporary.settings(), second);
  require(secondSave.backupRotated,
          "changed profile did not rotate valid current");
  requireLoadedRecord(
      airfix::settings::loadControllerInputProfile(temporary.settings()),
      second, "rotated profile did not publish the candidate");

  auto corrupt = readFile(temporary.current());
  corrupt.back() ^= 1U;
  writeRaw(temporary.current(), corrupt);
  const auto recovered =
      airfix::settings::loadControllerInputProfile(temporary.settings());
  require(recovered.source ==
                  airfix::settings::ControllerInputProfileLoadSource::backup &&
              recovered.recoveredFromBackup(),
          "corrupt current did not select the validated backup");
  requireLoadedRecord(recovered, first,
                      "corrupt current recovered the wrong profile");
}

void testMalformedCurrentNeverRotates() {
  TempDirectory temporary;
  const auto first = profile(800U);
  const auto second = profile(1200U);
  (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                     first);
  (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                     second);
  const auto backupBefore = readFile(temporary.backup());

  writeRaw(temporary.current(),
           std::array<std::uint8_t, 4U>{'B', 'A', 'D', '!'});
  const auto replacement = profile(1750U, true);
  const auto saved = airfix::settings::saveControllerInputProfile(
      temporary.settings(), replacement);
  require(!saved.backupRotated, "malformed current was promoted to backup");
  require(readFile(temporary.backup()) == backupBefore,
          "malformed current changed the retained backup");
  requireLoadedRecord(
      airfix::settings::loadControllerInputProfile(temporary.settings()),
      replacement, "malformed current was not safely replaced");
}

void testFutureCurrentAndBackupPreservation() {
  {
    TempDirectory temporary;
    const auto older = profile(900U);
    const auto current = profile(1300U);
    (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                       older);
    (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                       current);

    auto future = readFile(temporary.current());
    repairFutureEnvelope(future);
    writeRaw(temporary.current(), future);
    const auto loaded =
        airfix::settings::loadControllerInputProfile(temporary.settings());
    require(
        loaded.source ==
                airfix::settings::ControllerInputProfileLoadSource::defaults &&
            loaded.persistenceBlocked &&
            loaded.current.status ==
                airfix::settings::ControllerInputProfileFileStatus::
                    futureSchema,
        "future current was interpreted or fell back");
    requireLoadedRecord(
        loaded, airfix::input::makeDefaultControllerInputProfileRecord(),
        "future current did not return resolved defaults");

    const auto before = readFile(temporary.current());
    const auto requested = profile(1100U);
    requireStoreError(
        [&] {
          (void)airfix::settings::saveControllerInputProfile(
              temporary.settings(), requested);
        },
        airfix::settings::ControllerInputProfileStoreErrorKind::
            persistenceBlocked,
        temporary.settings(), requested);
    require(readFile(temporary.current()) == before,
            "downgrade save rewrote future current bytes");
  }

  {
    TempDirectory temporary;
    const auto current = profile(1000U);
    (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                       current);
    auto future =
        airfix::settings::encodeControllerInputProfileDocument(profile(1250U));
    repairFutureEnvelope(future);
    writeRaw(temporary.backup(), future);

    const auto currentBefore = readFile(temporary.current());
    const auto backupBefore = readFile(temporary.backup());
    const auto loaded =
        airfix::settings::loadControllerInputProfile(temporary.settings());
    requireLoadedRecord(loaded, current,
                        "future backup displaced the valid current profile");
    require(
        loaded.source ==
                airfix::settings::ControllerInputProfileLoadSource::current &&
            loaded.persistenceBlocked &&
            loaded.backup.status ==
                airfix::settings::ControllerInputProfileFileStatus::
                    futureSchema,
        "future backup did not block persistence during load");

    const auto requested = profile(1600U);
    requireStoreError(
        [&] {
          (void)airfix::settings::saveControllerInputProfile(
              temporary.settings(), requested);
        },
        airfix::settings::ControllerInputProfileStoreErrorKind::
            persistenceBlocked,
        temporary.settings(), requested);
    require(readFile(temporary.current()) == currentBefore &&
                readFile(temporary.backup()) == backupBefore,
            "blocked save changed retained future backup");
  }
}

struct ReplaceHookContext final {
  enum class Mode {
    throwBefore,
    replaceThenThrow,
  };

  Mode mode{Mode::throwBefore};
  bool throwOnCurrentOnly{};
};

void replaceHook(const std::filesystem::path &prepared,
                 const std::filesystem::path &target, void *const opaque) {
  auto &context = *static_cast<ReplaceHookContext *>(opaque);
  const bool current = target.filename() == currentName;
  if (context.throwOnCurrentOnly && !current) {
    airfix::io::replaceFileDurable(prepared, target);
    return;
  }
  if (context.mode == ReplaceHookContext::Mode::replaceThenThrow) {
    airfix::io::replaceFileDurable(prepared, target);
  }
  throw std::runtime_error("synthetic controller profile replacement failure");
}

void throwRetry(const std::filesystem::path &, const std::filesystem::path &,
                void *) {
  throw std::runtime_error("synthetic controller profile durability failure");
}

void testFailureAndAmbiguousReadback() {
  TempDirectory temporary;
  const auto first = profile(1000U);
  (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                     first);

  ReplaceHookContext before{
      .mode = ReplaceHookContext::Mode::throwBefore,
      .throwOnCurrentOnly = true,
  };
  const airfix::settings::testing::ControllerInputProfileStoreHooks beforeHooks{
      .replace = replaceHook,
      .context = &before,
  };
  const auto beforeCandidate = profile(1200U);
  requireStoreError(
      [&] {
        (void)airfix::settings::testing::saveControllerInputProfileWithHooks(
            temporary.settings(), beforeCandidate, beforeHooks);
      },
      airfix::settings::ControllerInputProfileStoreErrorKind::saveFailed,
      temporary.settings(), beforeCandidate);
  requireLoadedRecord(
      airfix::settings::loadControllerInputProfile(temporary.settings()), first,
      "pre-publication failure changed active profile");

  ReplaceHookContext after{
      .mode = ReplaceHookContext::Mode::replaceThenThrow,
      .throwOnCurrentOnly = true,
  };
  const airfix::settings::testing::ControllerInputProfileStoreHooks afterHooks{
      .replace = replaceHook,
      .context = &after,
  };
  const auto afterCandidate = profile(1400U, true);
  const auto confirmed =
      airfix::settings::testing::saveControllerInputProfileWithHooks(
          temporary.settings(), afterCandidate, afterHooks);
  require(confirmed.status ==
              airfix::settings::ControllerInputProfileSaveStatus::
                  committedAfterReadback,
          "post-publication failure was not confirmed by readback");
  requireLoadedRecord(
      airfix::settings::loadControllerInputProfile(temporary.settings()),
      afterCandidate, "confirmed post-publication profile was not retained");

  const airfix::settings::testing::ControllerInputProfileStoreHooks
      unknownHooks{
          .replace = replaceHook,
          .retryDurability = throwRetry,
          .context = &after,
      };
  const auto unknownCandidate = profile(1800U);
  requireStoreError(
      [&] {
        (void)airfix::settings::testing::saveControllerInputProfileWithHooks(
            temporary.settings(), unknownCandidate, unknownHooks);
      },
      airfix::settings::ControllerInputProfileStoreErrorKind::commitUnknown,
      temporary.settings(), unknownCandidate);
  requireLoadedRecord(
      airfix::settings::loadControllerInputProfile(temporary.settings()),
      unknownCandidate,
      "commit-unknown readback cannot identify published state");
}

void testWrongTypesOversizeAndLinks() {
  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.settings());
    std::filesystem::create_directory(temporary.current());
    const auto loaded =
        airfix::settings::loadControllerInputProfile(temporary.settings());
    require(loaded.current.status ==
                    airfix::settings::ControllerInputProfileFileStatus::
                        wrongTypeOrLinked &&
                loaded.persistenceBlocked,
            "wrong-type current did not fail closed");
    const auto candidate = profile(1100U);
    requireStoreError(
        [&] {
          (void)airfix::settings::saveControllerInputProfile(
              temporary.settings(), candidate);
        },
        airfix::settings::ControllerInputProfileStoreErrorKind::
            persistenceBlocked,
        temporary.settings(), candidate);
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.settings());
    const std::vector<std::uint8_t> oversized(
        airfix::settings::maximumControllerInputProfileDocumentBytes + 1U,
        0xA5U);
    writeRaw(temporary.current(), oversized);
    const auto loaded =
        airfix::settings::loadControllerInputProfile(temporary.settings());
    require(
        loaded.current.status ==
                airfix::settings::ControllerInputProfileFileStatus::oversized &&
            loaded.source ==
                airfix::settings::ControllerInputProfileLoadSource::defaults &&
            !loaded.persistenceBlocked,
        "oversized current did not select safe defaults");
    const auto replacement = profile(1150U);
    (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                       replacement);
    requireLoadedRecord(
        airfix::settings::loadControllerInputProfile(temporary.settings()),
        replacement, "oversized current could not be replaced safely");
  }

  {
    TempDirectory temporary;
    const auto current = profile(900U);
    (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                       current);
    std::error_code error;
    std::filesystem::create_hard_link(
        temporary.current(), temporary.settings() / "linked-alias.afip", error);
    require(!error, "cannot create controller profile hard-link fixture");
    const auto loaded =
        airfix::settings::loadControllerInputProfile(temporary.settings());
    require(loaded.current.status ==
                    airfix::settings::ControllerInputProfileFileStatus::
                        wrongTypeOrLinked &&
                loaded.persistenceBlocked,
            "linked current did not block persistence");
    const auto candidate = profile(1300U);
    requireStoreError(
        [&] {
          (void)airfix::settings::saveControllerInputProfile(
              temporary.settings(), candidate);
        },
        airfix::settings::ControllerInputProfileStoreErrorKind::
            persistenceBlocked,
        temporary.settings(), candidate);
  }
}

void testPreparedEntrySafety() {
  {
    TempDirectory temporary;
    const auto first = profile(1000U);
    (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                       first);
    writeRaw(temporary.currentPartial(),
             std::array<std::uint8_t, 3U>{1U, 2U, 3U});
    const auto replacement = profile(1050U);
    const auto saved = airfix::settings::saveControllerInputProfile(
        temporary.settings(), replacement);
    require(saved.status ==
                airfix::settings::ControllerInputProfileSaveStatus::committed,
            "owned stale partial was not replaced");
    require(!std::filesystem::exists(temporary.currentPartial()),
            "stale partial remained after successful commit");
  }

  {
    TempDirectory temporary;
    const auto first = profile(1000U);
    (void)airfix::settings::saveControllerInputProfile(temporary.settings(),
                                                       first);
    require(std::filesystem::create_directory(temporary.currentPartial()),
            "cannot create unsafe partial fixture");
    const auto replacement = profile(1050U);
    requireStoreError(
        [&] {
          (void)airfix::settings::saveControllerInputProfile(
              temporary.settings(), replacement);
        },
        airfix::settings::ControllerInputProfileStoreErrorKind::saveFailed,
        temporary.settings());
    require(std::filesystem::is_directory(temporary.currentPartial()),
            "unsafe partial entry was removed");
    requireLoadedRecord(
        airfix::settings::loadControllerInputProfile(temporary.settings()),
        first, "unsafe partial changed active controller profile");
  }
}

void testInvalidProfileAndDirectory() {
  {
    TempDirectory temporary;
    auto invalid = profile(1000U);
    invalid.axes[0].sensitivityPermille = 0U;
    requireStoreError(
        [&] {
          (void)airfix::settings::saveControllerInputProfile(
              temporary.settings(), invalid);
        },
        airfix::settings::ControllerInputProfileStoreErrorKind::invalidProfile,
        temporary.settings());
    require(!std::filesystem::exists(temporary.settings()),
            "invalid profile created persistent storage");
  }

  {
    const auto relative = std::filesystem::path{"relative-settings"};
    const auto loaded = airfix::settings::loadControllerInputProfile(relative);
    require(
        loaded.source ==
                airfix::settings::ControllerInputProfileLoadSource::defaults &&
            loaded.persistenceBlocked &&
            loaded.current.status ==
                airfix::settings::ControllerInputProfileFileStatus::
                    ioUnavailable,
        "relative load did not fail closed to defaults");
    requireStoreError(
        [&] {
          (void)airfix::settings::saveControllerInputProfile(relative,
                                                             profile(1000U));
        },
        airfix::settings::ControllerInputProfileStoreErrorKind::
            invalidDirectory,
        relative);
  }
}

void testLinkedSettingsDirectoryAndCurrent() {
  {
    TempDirectory temporary;
    const auto target = temporary.settings().parent_path() / "settings-target";
    require(std::filesystem::create_directory(target),
            "cannot create linked settings target");
    writeRaw(
        target / currentName,
        airfix::settings::encodeControllerInputProfileDocument(profile(1350U)));

    std::error_code error;
    std::filesystem::create_directory_symlink(target, temporary.settings(),
                                              error);
    if (!error) {
      const auto loaded =
          airfix::settings::loadControllerInputProfile(temporary.settings());
      require(loaded.source == airfix::settings::
                                   ControllerInputProfileLoadSource::defaults &&
                  loaded.current.status ==
                      airfix::settings::ControllerInputProfileFileStatus::
                          wrongTypeOrLinked &&
                  loaded.persistenceBlocked,
              "linked settings directory was followed");
      requireStoreError(
          [&] {
            (void)airfix::settings::saveControllerInputProfile(
                temporary.settings(), profile(1450U));
          },
          airfix::settings::ControllerInputProfileStoreErrorKind::
              invalidDirectory,
          temporary.settings());
    }
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.settings());
    const auto target = temporary.settings() / "symlink-target.afip";
    writeRaw(target, airfix::settings::encodeControllerInputProfileDocument(
                         profile(1250U)));
    std::error_code error;
    std::filesystem::create_symlink(target.filename(), temporary.current(),
                                    error);
    if (!error) {
      const auto loaded =
          airfix::settings::loadControllerInputProfile(temporary.settings());
      require(loaded.current.status ==
                      airfix::settings::ControllerInputProfileFileStatus::
                          wrongTypeOrLinked &&
                  loaded.persistenceBlocked,
              "linked current profile was followed");
    }
  }
}

} // namespace

int main() {
  try {
    testMissingSaveUnchangedRotationAndFallback();
    testMalformedCurrentNeverRotates();
    testFutureCurrentAndBackupPreservation();
    testFailureAndAmbiguousReadback();
    testWrongTypesOversizeAndLinks();
    testPreparedEntrySafety();
    testInvalidProfileAndDirectory();
    testLinkedSettingsDirectoryAndCurrent();
    std::cout << "Controller input profile store tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Controller input profile store tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
