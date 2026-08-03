#include "airfix/settings/RenderPresentationSettingsStore.hpp"

#include "airfix/io/DurableDocumentPair.hpp"

#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace airfix::settings {
namespace {

constexpr const char *currentFileName = "render-presentation.afrs";
constexpr const char *backupFileName = "render-presentation.afrs.backup";
constexpr const char *currentPreparedFileName =
    "render-presentation.afrs.partial";
constexpr const char *backupPreparedFileName =
    "render-presentation.afrs.backup.partial";

constexpr io::DurableDocumentPairNames documentNames{
    .current = currentFileName,
    .backup = backupFileName,
    .currentPrepared = currentPreparedFileName,
    .backupPrepared = backupPreparedFileName,
};

struct DocumentRead final {
  RenderSettingsFileDiagnostic diagnostic;
  std::optional<render::RenderPresentationSettings> settings;
  std::vector<std::uint8_t> exactBytes;
};

[[noreturn]] void
storeFailure(const RenderSettingsStoreErrorKind kind,
             const std::optional<render::RenderPresentationSettingsRecord>
                 &requestedRecord,
             const char *const message) {
  throw RenderSettingsStoreError(kind, requestedRecord, message);
}

[[nodiscard]] io::DurableDocumentDisposition
inspectDocument(const std::span<const std::uint8_t> bytes, void *) noexcept {
  try {
    const auto decoded = decodeRenderSettingsDocument(bytes);
    if (std::holds_alternative<OpaqueFutureRenderSettingsRecord>(decoded)) {
      return io::DurableDocumentDisposition::futurePreserve;
    }
    const auto &record =
        std::get<render::RenderPresentationSettingsRecord>(decoded);
    if (render::renderPresentationSettingsFromRecord(record).complete()) {
      return io::DurableDocumentDisposition::valid;
    }
  } catch (...) {
  }
  return io::DurableDocumentDisposition::replaceableInvalid;
}

constexpr io::DurableDocumentInspection documentInspection{
    .maximumBytes = maximumRenderSettingsDocumentBytes,
    .inspect = inspectDocument,
};

[[nodiscard]] RenderSettingsFileStatus
statusForDocumentRead(const io::DurableDocumentFileStatus status) noexcept {
  switch (status) {
  case io::DurableDocumentFileStatus::missing:
    return RenderSettingsFileStatus::missing;
  case io::DurableDocumentFileStatus::oversized:
    return RenderSettingsFileStatus::oversized;
  case io::DurableDocumentFileStatus::wrongTypeOrLinked:
    return RenderSettingsFileStatus::wrongTypeOrLinked;
  case io::DurableDocumentFileStatus::ioUnavailable:
    return RenderSettingsFileStatus::ioUnavailable;
  case io::DurableDocumentFileStatus::valid:
    return RenderSettingsFileStatus::valid;
  case io::DurableDocumentFileStatus::futurePreserve:
    return RenderSettingsFileStatus::futureSchema;
  case io::DurableDocumentFileStatus::replaceableInvalid:
    return RenderSettingsFileStatus::malformed;
  }
  return RenderSettingsFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(const std::filesystem::path &path) {
  DocumentRead result;
  try {
    auto read = io::readDurableDocument(path, documentInspection);
    result.diagnostic.status = statusForDocumentRead(read.status);
    result.exactBytes = std::move(read.exactBytes);
  } catch (...) {
    result.diagnostic.status = RenderSettingsFileStatus::ioUnavailable;
    return result;
  }
  if (result.exactBytes.empty() &&
      result.diagnostic.status != RenderSettingsFileStatus::malformed) {
    return result;
  }

  try {
    auto decoded = decodeRenderSettingsDocument(result.exactBytes);
    if (const auto *future =
            std::get_if<OpaqueFutureRenderSettingsRecord>(&decoded)) {
      result.diagnostic = {
          .status = RenderSettingsFileStatus::futureSchema,
          .schemaVersion = future->schemaVersion,
      };
      return result;
    }
    const auto &record =
        std::get<render::RenderPresentationSettingsRecord>(decoded);
    const auto semantic = render::renderPresentationSettingsFromRecord(record);
    if (!semantic.complete()) {
      result.diagnostic = {
          .status = RenderSettingsFileStatus::malformed,
          .schemaVersion = record.schemaVersion,
      };
      return result;
    }
    result.diagnostic = {
        .status = RenderSettingsFileStatus::valid,
        .schemaVersion = record.schemaVersion,
    };
    result.settings = *semantic.settings;
    return result;
  } catch (const RenderSettingsCodecError &error) {
    result.diagnostic = {
        .status = error.kind() == RenderSettingsCodecErrorKind::tooLarge
                      ? RenderSettingsFileStatus::oversized
                      : RenderSettingsFileStatus::malformed,
        .schemaVersion = error.schemaVersion(),
    };
    return result;
  } catch (...) {
    result.diagnostic.status = RenderSettingsFileStatus::malformed;
    return result;
  }
}

[[nodiscard]] bool
blocksPersistence(const RenderSettingsFileStatus status) noexcept {
  return status == RenderSettingsFileStatus::futureSchema ||
         status == RenderSettingsFileStatus::wrongTypeOrLinked ||
         status == RenderSettingsFileStatus::ioUnavailable;
}

[[noreturn]] void
mapPairFailure(const io::DurableDocumentPairError &error,
               const std::optional<render::RenderPresentationSettingsRecord>
                   &requestedRecord) {
  switch (error.kind()) {
  case io::DurableDocumentPairErrorKind::invalidDirectory:
    storeFailure(RenderSettingsStoreErrorKind::invalidDirectory, std::nullopt,
                 "render settings directory is invalid");
  case io::DurableDocumentPairErrorKind::persistenceBlocked:
    storeFailure(RenderSettingsStoreErrorKind::persistenceBlocked,
                 requestedRecord,
                 "render settings persistence is blocked by retained state");
  case io::DurableDocumentPairErrorKind::commitUnknown:
    storeFailure(RenderSettingsStoreErrorKind::commitUnknown, requestedRecord,
                 "render settings commit outcome is unknown");
  case io::DurableDocumentPairErrorKind::invalidArgument:
  case io::DurableDocumentPairErrorKind::saveFailed:
    storeFailure(RenderSettingsStoreErrorKind::saveFailed,
                 error.candidateRelevant() ? requestedRecord : std::nullopt,
                 "render settings durable I/O failed");
  }
  storeFailure(RenderSettingsStoreErrorKind::saveFailed, requestedRecord,
               "render settings durable I/O failed");
}

[[nodiscard]] RenderSettingsSaveResult
saveImpl(const std::filesystem::path &settingsDirectory,
         const render::RenderPresentationSettings &candidate,
         const testing::RenderSettingsStoreHooks *const hooks) {
  const auto recordResult =
      render::makeRenderPresentationSettingsRecord(candidate);
  if (!recordResult.complete()) {
    storeFailure(RenderSettingsStoreErrorKind::invalidSettings, std::nullopt,
                 "render settings candidate is invalid");
  }
  const auto record = *recordResult.record;
  const auto bytes = encodeRenderSettingsDocument(record);

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
      return {.status = RenderSettingsSaveStatus::unchanged,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committed:
      return {.status = RenderSettingsSaveStatus::committed,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committedAfterReadback:
      return {.status = RenderSettingsSaveStatus::committedAfterReadback,
              .backupRotated = result.backupRotated};
    }
  } catch (const io::DurableDocumentPairError &error) {
    mapPairFailure(error, record);
  }
  storeFailure(RenderSettingsStoreErrorKind::saveFailed, record,
               "render settings durable I/O failed");
}

} // namespace

RenderSettingsStoreError::RenderSettingsStoreError(
    const RenderSettingsStoreErrorKind kind,
    std::optional<render::RenderPresentationSettingsRecord> requestedRecord,
    const char *const message)
    : std::runtime_error(message), kind_(kind),
      requestedRecord_(std::move(requestedRecord)) {}

RenderSettingsLoadResult
loadRenderPresentationSettings(const std::filesystem::path &settingsDirectory) {
  RenderSettingsLoadResult result;
  if (settingsDirectory.empty() || !settingsDirectory.is_absolute()) {
    result.current.status = RenderSettingsFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }
  // The application-private directory has one serialized owner. Reject the
  // leaf itself when it is a symlink/reparse point; path-based standard
  // library APIs cannot defend against a concurrently hostile parent-tree
  // substitution, so native adapters must not place this store in a
  // directory writable by an untrusted process.
  switch (io::inspectDurableDocumentDirectory(settingsDirectory)) {
  case io::DurableDocumentDirectoryStatus::ready:
    break;
  case io::DurableDocumentDirectoryStatus::missing:
    result.current.status = RenderSettingsFileStatus::missing;
    result.backup.status = RenderSettingsFileStatus::missing;
    return result;
  case io::DurableDocumentDirectoryStatus::wrongTypeOrLinked:
    result.current.status = RenderSettingsFileStatus::wrongTypeOrLinked;
    result.persistenceBlocked = true;
    return result;
  case io::DurableDocumentDirectoryStatus::unavailable:
    result.current.status = RenderSettingsFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }
  const auto current = readDocument(settingsDirectory / currentFileName);
  result.current = current.diagnostic;
  if (current.diagnostic.status == RenderSettingsFileStatus::valid &&
      current.settings.has_value()) {
    result.settings = *current.settings;
    result.source = RenderSettingsLoadSource::current;
    return result;
  }
  if (current.diagnostic.status == RenderSettingsFileStatus::futureSchema) {
    result.persistenceBlocked = true;
    return result;
  }
  if (current.diagnostic.status == RenderSettingsFileStatus::ioUnavailable) {
    result.persistenceBlocked = true;
    return result;
  }

  const auto backup = readDocument(settingsDirectory / backupFileName);
  result.backup = backup.diagnostic;
  if (backup.diagnostic.status == RenderSettingsFileStatus::valid &&
      backup.settings.has_value()) {
    result.settings = *backup.settings;
    result.source = RenderSettingsLoadSource::backup;
  }
  if (blocksPersistence(current.diagnostic.status) ||
      blocksPersistence(backup.diagnostic.status)) {
    result.persistenceBlocked = true;
  }
  return result;
}

RenderSettingsSaveResult saveRenderPresentationSettings(
    const std::filesystem::path &settingsDirectory,
    const render::RenderPresentationSettings &candidate) {
  return saveImpl(settingsDirectory, candidate, nullptr);
}

namespace testing {

RenderSettingsSaveResult saveRenderPresentationSettingsWithHooks(
    const std::filesystem::path &settingsDirectory,
    const render::RenderPresentationSettings &candidate,
    const RenderSettingsStoreHooks &hooks) {
  return saveImpl(settingsDirectory, candidate, &hooks);
}

} // namespace testing

} // namespace airfix::settings
