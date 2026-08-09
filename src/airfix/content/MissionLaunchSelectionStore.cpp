#include "airfix/content/MissionLaunchSelectionStore.hpp"

#include "airfix/io/DurableDocumentPair.hpp"

#include <span>
#include <utility>
#include <vector>

namespace airfix::content {
namespace {

constexpr const char *currentFileName = "mission-selection.afmission";
constexpr const char *backupFileName = "mission-selection.afmission.backup";
constexpr io::DurableDocumentPairNames documentNames{
    .current = currentFileName,
    .backup = backupFileName,
    .currentPrepared = "mission-selection.afmission.partial",
    .backupPrepared = "mission-selection.afmission.backup.partial",
};

struct DocumentRead final {
  MissionSelectionFileStatus status{MissionSelectionFileStatus::ioUnavailable};
  std::optional<MissionLaunchSelection> selection;
};

[[noreturn]] void fail(const MissionSelectionStoreErrorKind kind,
                       const char *const message) {
  throw MissionSelectionStoreError(kind, message);
}

[[nodiscard]] io::DurableDocumentDisposition
inspectDocument(const std::span<const std::uint8_t> bytes, void *) noexcept {
  try {
    (void)decodeMissionLaunchSelection(bytes);
    return io::DurableDocumentDisposition::valid;
  } catch (const MissionSelectionCodecError &error) {
    if (error.kind() ==
        MissionSelectionCodecErrorKind::unsupportedSchemaVersion) {
      return io::DurableDocumentDisposition::futurePreserve;
    }
  } catch (...) {
  }
  return io::DurableDocumentDisposition::replaceableInvalid;
}

constexpr io::DurableDocumentInspection documentInspection{
    .maximumBytes = maximumMissionSelectionDocumentBytes,
    .inspect = inspectDocument,
};

[[nodiscard]] MissionSelectionFileStatus
statusForRead(const io::DurableDocumentFileStatus status) noexcept {
  switch (status) {
  case io::DurableDocumentFileStatus::missing:
    return MissionSelectionFileStatus::missing;
  case io::DurableDocumentFileStatus::valid:
    return MissionSelectionFileStatus::valid;
  case io::DurableDocumentFileStatus::futurePreserve:
    return MissionSelectionFileStatus::futureSchema;
  case io::DurableDocumentFileStatus::replaceableInvalid:
    return MissionSelectionFileStatus::malformed;
  case io::DurableDocumentFileStatus::oversized:
    return MissionSelectionFileStatus::oversized;
  case io::DurableDocumentFileStatus::wrongTypeOrLinked:
    return MissionSelectionFileStatus::wrongTypeOrLinked;
  case io::DurableDocumentFileStatus::ioUnavailable:
    return MissionSelectionFileStatus::ioUnavailable;
  }
  return MissionSelectionFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(const std::filesystem::path &path) {
  DocumentRead result;
  try {
    auto read = io::readDurableDocument(path, documentInspection);
    result.status = statusForRead(read.status);
    if (read.status == io::DurableDocumentFileStatus::valid) {
      result.selection = decodeMissionLaunchSelection(read.exactBytes);
    }
  } catch (...) {
    result.status = MissionSelectionFileStatus::ioUnavailable;
    result.selection.reset();
  }
  return result;
}

[[nodiscard]] bool
blocksPersistence(const MissionSelectionFileStatus status) noexcept {
  return status == MissionSelectionFileStatus::wrongTypeOrLinked ||
         status == MissionSelectionFileStatus::futureSchema ||
         status == MissionSelectionFileStatus::ioUnavailable;
}

[[noreturn]] void mapPairFailure(const io::DurableDocumentPairError &error) {
  switch (error.kind()) {
  case io::DurableDocumentPairErrorKind::invalidDirectory:
    fail(MissionSelectionStoreErrorKind::invalidDirectory,
         "mission selection directory is invalid");
  case io::DurableDocumentPairErrorKind::persistenceBlocked:
    fail(MissionSelectionStoreErrorKind::persistenceBlocked,
         "mission selection persistence is blocked");
  case io::DurableDocumentPairErrorKind::commitUnknown:
    fail(MissionSelectionStoreErrorKind::commitUnknown,
         "mission selection commit outcome is unknown");
  case io::DurableDocumentPairErrorKind::invalidArgument:
  case io::DurableDocumentPairErrorKind::saveFailed:
    fail(MissionSelectionStoreErrorKind::saveFailed,
         "mission selection durable I/O failed");
  }
  fail(MissionSelectionStoreErrorKind::saveFailed,
       "mission selection durable I/O failed");
}

} // namespace

MissionSelectionStoreError::MissionSelectionStoreError(
    const MissionSelectionStoreErrorKind kind, const char *const message)
    : std::runtime_error(message), kind_(kind) {}

MissionSelectionLoadResult
loadMissionLaunchSelection(const std::filesystem::path &selectionDirectory) {
  MissionSelectionLoadResult result;
  if (selectionDirectory.empty() || !selectionDirectory.is_absolute()) {
    result.current = MissionSelectionFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }
  switch (io::inspectDurableDocumentDirectory(selectionDirectory)) {
  case io::DurableDocumentDirectoryStatus::ready:
    break;
  case io::DurableDocumentDirectoryStatus::missing:
    result.current = MissionSelectionFileStatus::missing;
    result.backup = MissionSelectionFileStatus::missing;
    return result;
  case io::DurableDocumentDirectoryStatus::wrongTypeOrLinked:
    result.current = MissionSelectionFileStatus::wrongTypeOrLinked;
    result.persistenceBlocked = true;
    return result;
  case io::DurableDocumentDirectoryStatus::unavailable:
    result.current = MissionSelectionFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }

  const auto current = readDocument(selectionDirectory / currentFileName);
  result.current = current.status;
  if (current.status == MissionSelectionFileStatus::valid &&
      current.selection.has_value()) {
    result.selection = current.selection;
    result.source = MissionSelectionLoadSource::current;
    return result;
  }
  if (blocksPersistence(current.status)) {
    result.persistenceBlocked = true;
    return result;
  }

  const auto backup = readDocument(selectionDirectory / backupFileName);
  result.backup = backup.status;
  if (backup.status == MissionSelectionFileStatus::valid &&
      backup.selection.has_value()) {
    result.selection = backup.selection;
    result.source = MissionSelectionLoadSource::backup;
  }
  result.persistenceBlocked = blocksPersistence(backup.status);
  return result;
}

MissionSelectionSaveResult
saveMissionLaunchSelection(const std::filesystem::path &selectionDirectory,
                           const MissionLaunchSelection &candidate) {
  std::vector<std::uint8_t> bytes;
  try {
    bytes = encodeMissionLaunchSelection(candidate);
  } catch (...) {
    fail(MissionSelectionStoreErrorKind::invalidSelection,
         "mission selection candidate is invalid");
  }

  try {
    const auto result = io::commitDurableDocumentPair(
        selectionDirectory, documentNames, documentInspection, bytes);
    switch (result.status) {
    case io::DurableDocumentCommitStatus::unchanged:
      return {.status = MissionSelectionSaveStatus::unchanged,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committed:
      return {.status = MissionSelectionSaveStatus::committed,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committedAfterReadback:
      return {.status = MissionSelectionSaveStatus::committedAfterReadback,
              .backupRotated = result.backupRotated};
    }
  } catch (const io::DurableDocumentPairError &error) {
    mapPairFailure(error);
  }
  fail(MissionSelectionStoreErrorKind::saveFailed,
       "mission selection durable I/O failed");
}

} // namespace airfix::content
