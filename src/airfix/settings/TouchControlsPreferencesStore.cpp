#include "airfix/settings/TouchControlsPreferencesStore.hpp"

#include "airfix/io/DurableDocumentPair.hpp"

#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace airfix::settings {
namespace {

constexpr const char *currentFileName = "touch-controls.aftc";
constexpr const char *backupFileName = "touch-controls.aftc.backup";
constexpr const char *currentPreparedFileName = "touch-controls.aftc.partial";
constexpr const char *backupPreparedFileName =
    "touch-controls.aftc.backup.partial";

constexpr io::DurableDocumentPairNames documentNames{
    .current = currentFileName,
    .backup = backupFileName,
    .currentPrepared = currentPreparedFileName,
    .backupPrepared = backupPreparedFileName,
};

struct DocumentRead final {
  TouchControlsPreferencesFileDiagnostic diagnostic;
  std::optional<input::TouchControlsPreferences> preferences;
  std::vector<std::uint8_t> exactBytes;
};

[[noreturn]] void storeFailure(
    const TouchControlsPreferencesStoreErrorKind kind,
    const std::optional<input::TouchControlsPreferencesRecord> &requestedRecord,
    const char *const message) {
  throw TouchControlsPreferencesStoreError(kind, requestedRecord, message);
}

[[nodiscard]] io::DurableDocumentDisposition
inspectDocument(const std::span<const std::uint8_t> bytes, void *) noexcept {
  try {
    const auto decoded = decodeTouchControlsPreferencesDocument(bytes);
    if (std::holds_alternative<OpaqueFutureTouchControlsPreferencesRecord>(
            decoded)) {
      return io::DurableDocumentDisposition::futurePreserve;
    }
    const auto &record =
        std::get<input::TouchControlsPreferencesRecord>(decoded);
    if (input::touchControlsPreferencesFromRecord(record).complete()) {
      return io::DurableDocumentDisposition::valid;
    }
  } catch (...) {
  }
  return io::DurableDocumentDisposition::replaceableInvalid;
}

constexpr io::DurableDocumentInspection documentInspection{
    .maximumBytes = maximumTouchControlsPreferencesDocumentBytes,
    .inspect = inspectDocument,
};

[[nodiscard]] TouchControlsPreferencesFileStatus
statusForDocumentRead(const io::DurableDocumentFileStatus status) noexcept {
  switch (status) {
  case io::DurableDocumentFileStatus::missing:
    return TouchControlsPreferencesFileStatus::missing;
  case io::DurableDocumentFileStatus::valid:
    return TouchControlsPreferencesFileStatus::valid;
  case io::DurableDocumentFileStatus::futurePreserve:
    return TouchControlsPreferencesFileStatus::futureSchema;
  case io::DurableDocumentFileStatus::replaceableInvalid:
    return TouchControlsPreferencesFileStatus::malformed;
  case io::DurableDocumentFileStatus::oversized:
    return TouchControlsPreferencesFileStatus::oversized;
  case io::DurableDocumentFileStatus::wrongTypeOrLinked:
    return TouchControlsPreferencesFileStatus::wrongTypeOrLinked;
  case io::DurableDocumentFileStatus::ioUnavailable:
    return TouchControlsPreferencesFileStatus::ioUnavailable;
  }
  return TouchControlsPreferencesFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(const std::filesystem::path &path) {
  DocumentRead result;
  try {
    auto read = io::readDurableDocument(path, documentInspection);
    result.diagnostic.status = statusForDocumentRead(read.status);
    result.exactBytes = std::move(read.exactBytes);
  } catch (...) {
    result.diagnostic.status =
        TouchControlsPreferencesFileStatus::ioUnavailable;
    return result;
  }
  if (result.exactBytes.empty() &&
      result.diagnostic.status !=
          TouchControlsPreferencesFileStatus::malformed) {
    return result;
  }

  try {
    auto decoded = decodeTouchControlsPreferencesDocument(result.exactBytes);
    if (const auto *future =
            std::get_if<OpaqueFutureTouchControlsPreferencesRecord>(&decoded)) {
      result.diagnostic = {
          .status = TouchControlsPreferencesFileStatus::futureSchema,
          .schemaVersion = future->schemaVersion,
      };
      return result;
    }
    const auto &record =
        std::get<input::TouchControlsPreferencesRecord>(decoded);
    const auto semantic = input::touchControlsPreferencesFromRecord(record);
    if (!semantic.complete()) {
      result.diagnostic = {
          .status = TouchControlsPreferencesFileStatus::malformed,
          .schemaVersion = record.schemaVersion,
      };
      return result;
    }
    result.diagnostic = {
        .status = TouchControlsPreferencesFileStatus::valid,
        .schemaVersion = record.schemaVersion,
    };
    result.preferences = *semantic.preferences;
    return result;
  } catch (const TouchControlsPreferencesCodecError &error) {
    result.diagnostic = {
        .status =
            error.kind() == TouchControlsPreferencesCodecErrorKind::tooLarge
                ? TouchControlsPreferencesFileStatus::oversized
                : TouchControlsPreferencesFileStatus::malformed,
        .schemaVersion = error.schemaVersion(),
    };
    return result;
  } catch (...) {
    result.diagnostic.status = TouchControlsPreferencesFileStatus::malformed;
    return result;
  }
}

[[nodiscard]] bool
blocksPersistence(const TouchControlsPreferencesFileStatus status) noexcept {
  return status == TouchControlsPreferencesFileStatus::futureSchema ||
         status == TouchControlsPreferencesFileStatus::wrongTypeOrLinked ||
         status == TouchControlsPreferencesFileStatus::ioUnavailable;
}

[[noreturn]] void
mapPairFailure(const io::DurableDocumentPairError &error,
               const std::optional<input::TouchControlsPreferencesRecord>
                   &requestedRecord) {
  switch (error.kind()) {
  case io::DurableDocumentPairErrorKind::invalidDirectory:
    storeFailure(TouchControlsPreferencesStoreErrorKind::invalidDirectory,
                 std::nullopt, "touch controls settings directory is invalid");
  case io::DurableDocumentPairErrorKind::persistenceBlocked:
    storeFailure(TouchControlsPreferencesStoreErrorKind::persistenceBlocked,
                 requestedRecord,
                 "touch controls persistence is blocked by retained state");
  case io::DurableDocumentPairErrorKind::commitUnknown:
    storeFailure(TouchControlsPreferencesStoreErrorKind::commitUnknown,
                 requestedRecord, "touch controls commit outcome is unknown");
  case io::DurableDocumentPairErrorKind::invalidArgument:
  case io::DurableDocumentPairErrorKind::saveFailed:
    storeFailure(TouchControlsPreferencesStoreErrorKind::saveFailed,
                 error.candidateRelevant() ? requestedRecord : std::nullopt,
                 "touch controls durable I/O failed");
  }
  storeFailure(TouchControlsPreferencesStoreErrorKind::saveFailed,
               requestedRecord, "touch controls durable I/O failed");
}

} // namespace

TouchControlsPreferencesStoreError::TouchControlsPreferencesStoreError(
    const TouchControlsPreferencesStoreErrorKind kind,
    std::optional<input::TouchControlsPreferencesRecord> requestedRecord,
    const char *const message)
    : std::runtime_error(message), kind_(kind),
      requestedRecord_(std::move(requestedRecord)) {}

TouchControlsPreferencesLoadResult
loadTouchControlsPreferences(const std::filesystem::path &settingsDirectory) {
  TouchControlsPreferencesLoadResult result;
  if (settingsDirectory.empty() || !settingsDirectory.is_absolute()) {
    result.current.status = TouchControlsPreferencesFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }
  switch (io::inspectDurableDocumentDirectory(settingsDirectory)) {
  case io::DurableDocumentDirectoryStatus::ready:
    break;
  case io::DurableDocumentDirectoryStatus::missing:
    result.current.status = TouchControlsPreferencesFileStatus::missing;
    result.backup.status = TouchControlsPreferencesFileStatus::missing;
    return result;
  case io::DurableDocumentDirectoryStatus::wrongTypeOrLinked:
    result.current.status =
        TouchControlsPreferencesFileStatus::wrongTypeOrLinked;
    result.persistenceBlocked = true;
    return result;
  case io::DurableDocumentDirectoryStatus::unavailable:
    result.current.status = TouchControlsPreferencesFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }

  const auto current = readDocument(settingsDirectory / currentFileName);
  result.current = current.diagnostic;
  if (current.diagnostic.status == TouchControlsPreferencesFileStatus::valid &&
      current.preferences.has_value()) {
    result.preferences = *current.preferences;
    result.source = TouchControlsPreferencesLoadSource::current;
    return result;
  }
  if (current.diagnostic.status ==
          TouchControlsPreferencesFileStatus::futureSchema ||
      current.diagnostic.status ==
          TouchControlsPreferencesFileStatus::ioUnavailable) {
    result.persistenceBlocked = true;
    return result;
  }

  const auto backup = readDocument(settingsDirectory / backupFileName);
  result.backup = backup.diagnostic;
  if (backup.diagnostic.status == TouchControlsPreferencesFileStatus::valid &&
      backup.preferences.has_value()) {
    result.preferences = *backup.preferences;
    result.source = TouchControlsPreferencesLoadSource::backup;
  }
  if (blocksPersistence(current.diagnostic.status) ||
      blocksPersistence(backup.diagnostic.status)) {
    result.persistenceBlocked = true;
  }
  return result;
}

TouchControlsPreferencesSaveResult
saveTouchControlsPreferences(const std::filesystem::path &settingsDirectory,
                             const input::TouchControlsPreferences &candidate) {
  const auto recordResult =
      input::makeTouchControlsPreferencesRecord(candidate);
  if (!recordResult.complete()) {
    storeFailure(TouchControlsPreferencesStoreErrorKind::invalidPreferences,
                 std::nullopt, "touch controls candidate is invalid");
  }
  const auto record = *recordResult.record;
  const auto bytes = encodeTouchControlsPreferencesDocument(record);
  try {
    const auto result = io::commitDurableDocumentPair(
        settingsDirectory, documentNames, documentInspection, bytes);
    switch (result.status) {
    case io::DurableDocumentCommitStatus::unchanged:
      return {.status = TouchControlsPreferencesSaveStatus::unchanged,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committed:
      return {.status = TouchControlsPreferencesSaveStatus::committed,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committedAfterReadback:
      return {
          .status = TouchControlsPreferencesSaveStatus::committedAfterReadback,
          .backupRotated = result.backupRotated,
      };
    }
  } catch (const io::DurableDocumentPairError &error) {
    mapPairFailure(error, record);
  }
  storeFailure(TouchControlsPreferencesStoreErrorKind::saveFailed, record,
               "touch controls durable I/O failed");
}

} // namespace airfix::settings
