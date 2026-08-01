#include "airfix/settings/ControllerInputProfileStore.hpp"

#include "airfix/io/DurableDocumentPair.hpp"

#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace airfix::settings {
namespace {

constexpr const char *currentFileName = "controller-input.afip";
constexpr const char *backupFileName = "controller-input.afip.backup";
constexpr const char *currentPreparedFileName = "controller-input.afip.partial";
constexpr const char *backupPreparedFileName =
    "controller-input.afip.backup.partial";

constexpr io::DurableDocumentPairNames documentNames{
    .current = currentFileName,
    .backup = backupFileName,
    .currentPrepared = currentPreparedFileName,
    .backupPrepared = backupPreparedFileName,
};

struct DocumentRead final {
  ControllerInputProfileFileDiagnostic diagnostic;
  std::optional<input::ResolvedControllerInputProfile> profile;
  std::vector<std::uint8_t> exactBytes;
};

[[noreturn]] void storeFailure(
    const ControllerInputProfileStoreErrorKind kind,
    const std::optional<input::ControllerInputProfileRecord> &requestedRecord,
    const char *const message) {
  throw ControllerInputProfileStoreError(kind, requestedRecord, message);
}

[[nodiscard]] io::DurableDocumentDisposition
inspectDocument(const std::span<const std::uint8_t> bytes, void *) noexcept {
  try {
    const auto decoded = decodeControllerInputProfileDocument(bytes);
    if (std::holds_alternative<OpaqueFutureControllerInputProfileRecord>(
            decoded)) {
      return io::DurableDocumentDisposition::futurePreserve;
    }
    const auto &record = std::get<input::ControllerInputProfileRecord>(decoded);
    if (input::resolveControllerInputProfile(record).complete()) {
      return io::DurableDocumentDisposition::valid;
    }
  } catch (...) {
  }
  return io::DurableDocumentDisposition::replaceableInvalid;
}

constexpr io::DurableDocumentInspection documentInspection{
    .maximumBytes = maximumControllerInputProfileDocumentBytes,
    .inspect = inspectDocument,
};

[[nodiscard]] ControllerInputProfileFileStatus
statusForDocumentRead(const io::DurableDocumentFileStatus status) noexcept {
  switch (status) {
  case io::DurableDocumentFileStatus::missing:
    return ControllerInputProfileFileStatus::missing;
  case io::DurableDocumentFileStatus::oversized:
    return ControllerInputProfileFileStatus::oversized;
  case io::DurableDocumentFileStatus::wrongTypeOrLinked:
    return ControllerInputProfileFileStatus::wrongTypeOrLinked;
  case io::DurableDocumentFileStatus::ioUnavailable:
    return ControllerInputProfileFileStatus::ioUnavailable;
  case io::DurableDocumentFileStatus::valid:
    return ControllerInputProfileFileStatus::valid;
  case io::DurableDocumentFileStatus::futurePreserve:
    return ControllerInputProfileFileStatus::futureSchema;
  case io::DurableDocumentFileStatus::replaceableInvalid:
    return ControllerInputProfileFileStatus::malformed;
  }
  return ControllerInputProfileFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(const std::filesystem::path &path) {
  DocumentRead result;
  try {
    auto read = io::readDurableDocument(path, documentInspection);
    result.diagnostic.status = statusForDocumentRead(read.status);
    result.exactBytes = std::move(read.exactBytes);
  } catch (...) {
    result.diagnostic.status = ControllerInputProfileFileStatus::ioUnavailable;
    return result;
  }
  if (result.exactBytes.empty() &&
      result.diagnostic.status != ControllerInputProfileFileStatus::malformed) {
    return result;
  }

  try {
    auto decoded = decodeControllerInputProfileDocument(result.exactBytes);
    if (const auto *future =
            std::get_if<OpaqueFutureControllerInputProfileRecord>(&decoded)) {
      result.diagnostic = {
          .status = ControllerInputProfileFileStatus::futureSchema,
          .schemaVersion = future->schemaVersion,
      };
      return result;
    }

    const auto &record = std::get<input::ControllerInputProfileRecord>(decoded);
    const auto resolved = input::resolveControllerInputProfile(record);
    if (!resolved.complete()) {
      result.diagnostic = {
          .status = ControllerInputProfileFileStatus::malformed,
          .schemaVersion = record.schemaVersion,
      };
      return result;
    }
    result.diagnostic = {
        .status = ControllerInputProfileFileStatus::valid,
        .schemaVersion = record.schemaVersion,
    };
    result.profile = *resolved.profile;
    return result;
  } catch (const ControllerInputProfileCodecError &error) {
    result.diagnostic = {
        .status = error.kind() == ControllerInputProfileCodecErrorKind::tooLarge
                      ? ControllerInputProfileFileStatus::oversized
                      : ControllerInputProfileFileStatus::malformed,
        .schemaVersion = error.schemaVersion(),
    };
    return result;
  } catch (...) {
    result.diagnostic.status = ControllerInputProfileFileStatus::malformed;
    return result;
  }
}

[[nodiscard]] bool
blocksPersistence(const ControllerInputProfileFileStatus status) noexcept {
  return status == ControllerInputProfileFileStatus::futureSchema ||
         status == ControllerInputProfileFileStatus::wrongTypeOrLinked ||
         status == ControllerInputProfileFileStatus::ioUnavailable;
}

[[noreturn]] void mapPairFailure(
    const io::DurableDocumentPairError &error,
    const std::optional<input::ControllerInputProfileRecord> &requestedRecord) {
  switch (error.kind()) {
  case io::DurableDocumentPairErrorKind::invalidDirectory:
    storeFailure(ControllerInputProfileStoreErrorKind::invalidDirectory,
                 std::nullopt, "controller profile directory is invalid");
  case io::DurableDocumentPairErrorKind::persistenceBlocked:
    storeFailure(ControllerInputProfileStoreErrorKind::persistenceBlocked,
                 requestedRecord,
                 "controller profile persistence is blocked by retained state");
  case io::DurableDocumentPairErrorKind::commitUnknown:
    storeFailure(ControllerInputProfileStoreErrorKind::commitUnknown,
                 requestedRecord,
                 "controller profile commit outcome is unknown");
  case io::DurableDocumentPairErrorKind::invalidArgument:
  case io::DurableDocumentPairErrorKind::saveFailed:
    storeFailure(ControllerInputProfileStoreErrorKind::saveFailed,
                 error.candidateRelevant() ? requestedRecord : std::nullopt,
                 "controller profile durable I/O failed");
  }
  storeFailure(ControllerInputProfileStoreErrorKind::saveFailed,
               requestedRecord, "controller profile durable I/O failed");
}

[[nodiscard]] input::ResolvedControllerInputProfile resolvedDefaultProfile() {
  const auto resolved = input::resolveControllerInputProfile(
      input::makeDefaultControllerInputProfileRecord());
  if (!resolved.complete()) {
    throw std::logic_error("controller profile canonical default is invalid");
  }
  return *resolved.profile;
}

[[nodiscard]] ControllerInputProfileSaveResult
saveImpl(const std::filesystem::path &settingsDirectory,
         const input::ControllerInputProfileRecord &candidate,
         const testing::ControllerInputProfileStoreHooks *const hooks) {
  const auto resolved = input::resolveControllerInputProfile(candidate);
  if (!resolved.complete()) {
    storeFailure(ControllerInputProfileStoreErrorKind::invalidProfile,
                 std::nullopt, "controller profile candidate is invalid");
  }
  const auto bytes = encodeControllerInputProfileDocument(candidate);

  try {
    io::DurableDocumentCommitResult result;
    if (hooks == nullptr) {
      result = io::commitDurableDocumentPair(settingsDirectory, documentNames,
                                             documentInspection, bytes);
    } else {
      const io::testing::DurableDocumentPairHooks pairHooks{
          .replace = hooks->replace,
          .retryDurability = hooks->retryDurability,
          .context = hooks->context,
      };
      result = io::testing::commitDurableDocumentPairWithHooks(
          settingsDirectory, documentNames, documentInspection, bytes,
          pairHooks);
    }
    switch (result.status) {
    case io::DurableDocumentCommitStatus::unchanged:
      return {.status = ControllerInputProfileSaveStatus::unchanged,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committed:
      return {.status = ControllerInputProfileSaveStatus::committed,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committedAfterReadback:
      return {.status =
                  ControllerInputProfileSaveStatus::committedAfterReadback,
              .backupRotated = result.backupRotated};
    }
  } catch (const io::DurableDocumentPairError &error) {
    mapPairFailure(error, candidate);
  }
  storeFailure(ControllerInputProfileStoreErrorKind::saveFailed, candidate,
               "controller profile durable I/O failed");
}

} // namespace

ControllerInputProfileStoreError::ControllerInputProfileStoreError(
    const ControllerInputProfileStoreErrorKind kind,
    std::optional<input::ControllerInputProfileRecord> requestedRecord,
    const char *const message)
    : std::runtime_error(message), kind_(kind),
      requestedRecord_(std::move(requestedRecord)) {}

ControllerInputProfileLoadResult
loadControllerInputProfile(const std::filesystem::path &settingsDirectory) {
  ControllerInputProfileLoadResult result{
      .profile = resolvedDefaultProfile(),
      .source = ControllerInputProfileLoadSource::defaults,
      .current = {},
      .backup = {},
      .persistenceBlocked = false,
  };
  if (settingsDirectory.empty() || !settingsDirectory.is_absolute()) {
    result.current.status = ControllerInputProfileFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }

  // The private leaf has one serialized owner. Reject the leaf itself when
  // it is a symlink or reparse point. Native adapters must also keep its
  // parent tree outside directories writable by untrusted processes.
  switch (io::inspectDurableDocumentDirectory(settingsDirectory)) {
  case io::DurableDocumentDirectoryStatus::ready:
    break;
  case io::DurableDocumentDirectoryStatus::missing:
    result.current.status = ControllerInputProfileFileStatus::missing;
    result.backup.status = ControllerInputProfileFileStatus::missing;
    return result;
  case io::DurableDocumentDirectoryStatus::wrongTypeOrLinked:
    result.current.status = ControllerInputProfileFileStatus::wrongTypeOrLinked;
    result.persistenceBlocked = true;
    return result;
  case io::DurableDocumentDirectoryStatus::unavailable:
    result.current.status = ControllerInputProfileFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }

  const auto current = readDocument(settingsDirectory / currentFileName);
  result.current = current.diagnostic;
  if (current.diagnostic.status ==
          ControllerInputProfileFileStatus::futureSchema ||
      current.diagnostic.status ==
          ControllerInputProfileFileStatus::ioUnavailable) {
    result.persistenceBlocked = true;
    return result;
  }

  const auto backup = readDocument(settingsDirectory / backupFileName);
  result.backup = backup.diagnostic;
  if (current.diagnostic.status == ControllerInputProfileFileStatus::valid &&
      current.profile.has_value()) {
    result.profile = *current.profile;
    result.source = ControllerInputProfileLoadSource::current;
    result.persistenceBlocked = blocksPersistence(backup.diagnostic.status);
    return result;
  }
  if (backup.diagnostic.status == ControllerInputProfileFileStatus::valid &&
      backup.profile.has_value()) {
    result.profile = *backup.profile;
    result.source = ControllerInputProfileLoadSource::backup;
  }
  if (blocksPersistence(current.diagnostic.status) ||
      blocksPersistence(backup.diagnostic.status)) {
    result.persistenceBlocked = true;
  }
  return result;
}

ControllerInputProfileSaveResult saveControllerInputProfile(
    const std::filesystem::path &settingsDirectory,
    const input::ControllerInputProfileRecord &candidate) {
  return saveImpl(settingsDirectory, candidate, nullptr);
}

namespace testing {

ControllerInputProfileSaveResult saveControllerInputProfileWithHooks(
    const std::filesystem::path &settingsDirectory,
    const input::ControllerInputProfileRecord &candidate,
    const ControllerInputProfileStoreHooks &hooks) {
  return saveImpl(settingsDirectory, candidate, &hooks);
}

} // namespace testing

} // namespace airfix::settings
