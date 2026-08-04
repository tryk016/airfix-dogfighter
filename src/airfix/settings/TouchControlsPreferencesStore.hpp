#pragma once

#include "airfix/input/TouchControlsPreferences.hpp"
#include "airfix/settings/TouchControlsPreferencesCodec.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace airfix::settings {

enum class TouchControlsPreferencesFileStatus : std::uint8_t {
  notInspected,
  missing,
  valid,
  futureSchema,
  malformed,
  oversized,
  wrongTypeOrLinked,
  ioUnavailable,
};

struct TouchControlsPreferencesFileDiagnostic final {
  TouchControlsPreferencesFileStatus status{
      TouchControlsPreferencesFileStatus::notInspected};
  std::optional<std::uint32_t> schemaVersion;
};

enum class TouchControlsPreferencesLoadSource : std::uint8_t {
  defaults,
  current,
  backup,
};

struct TouchControlsPreferencesLoadResult final {
  input::TouchControlsPreferences preferences;
  TouchControlsPreferencesLoadSource source{
      TouchControlsPreferencesLoadSource::defaults};
  TouchControlsPreferencesFileDiagnostic current;
  TouchControlsPreferencesFileDiagnostic backup;
  bool persistenceBlocked{};

  [[nodiscard]] constexpr bool recoveredFromBackup() const noexcept {
    return source == TouchControlsPreferencesLoadSource::backup;
  }
};

[[nodiscard]] constexpr bool touchControlsPreferencesNeedsRepair(
    const TouchControlsPreferencesLoadResult &load) noexcept {
  if (load.persistenceBlocked) {
    return false;
  }
  if (load.source == TouchControlsPreferencesLoadSource::current) {
    return load.current.schemaVersion.has_value() &&
           *load.current.schemaVersion !=
               input::touchControlsPreferencesRecordSchemaVersion;
  }
  if (load.source == TouchControlsPreferencesLoadSource::backup) {
    return true;
  }
  return load.current.status != TouchControlsPreferencesFileStatus::missing ||
         load.backup.status != TouchControlsPreferencesFileStatus::missing;
}

enum class TouchControlsPreferencesSaveStatus : std::uint8_t {
  unchanged,
  committed,
  committedAfterReadback,
};

struct TouchControlsPreferencesSaveResult final {
  TouchControlsPreferencesSaveStatus status{
      TouchControlsPreferencesSaveStatus::unchanged};
  bool backupRotated{};
};

enum class TouchControlsPreferencesStoreErrorKind : std::uint8_t {
  invalidDirectory,
  invalidPreferences,
  persistenceBlocked,
  saveFailed,
  commitUnknown,
};

class TouchControlsPreferencesStoreError final : public std::runtime_error {
public:
  TouchControlsPreferencesStoreError(
      TouchControlsPreferencesStoreErrorKind kind,
      std::optional<input::TouchControlsPreferencesRecord> requestedRecord,
      const char *message);

  [[nodiscard]] TouchControlsPreferencesStoreErrorKind kind() const noexcept {
    return kind_;
  }

  [[nodiscard]] const std::optional<input::TouchControlsPreferencesRecord> &
  requestedRecord() const noexcept {
    return requestedRecord_;
  }

private:
  TouchControlsPreferencesStoreErrorKind kind_;
  std::optional<input::TouchControlsPreferencesRecord> requestedRecord_;
};

// The directory is an injected absolute application-private settings leaf.
// Diagnostics and exceptions contain no host paths or document bytes.
[[nodiscard]] TouchControlsPreferencesLoadResult
loadTouchControlsPreferences(const std::filesystem::path &settingsDirectory);

// The caller serializes directory access. Valid current bytes rotate into the
// backup before atomic publication. Malformed current bytes are never promoted;
// future schemas and unsafe filesystem types block downgrade writes.
[[nodiscard]] TouchControlsPreferencesSaveResult
saveTouchControlsPreferences(const std::filesystem::path &settingsDirectory,
                             const input::TouchControlsPreferences &candidate);

} // namespace airfix::settings
