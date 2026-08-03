#include "airfix/campaign/CampaignStateStore.hpp"

#include "airfix/io/DurableDocumentPair.hpp"

#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace airfix::campaign {
namespace {

constexpr const char *currentFileName = "campaign-state.afcs";
constexpr const char *backupFileName = "campaign-state.afcs.backup";
constexpr const char *currentPreparedFileName = "campaign-state.afcs.partial";
constexpr const char *backupPreparedFileName =
    "campaign-state.afcs.backup.partial";

constexpr io::DurableDocumentPairNames documentNames{
    .current = currentFileName,
    .backup = backupFileName,
    .currentPrepared = currentPreparedFileName,
    .backupPrepared = backupPreparedFileName,
};

struct DocumentRead final {
  CampaignStateFileDiagnostic diagnostic;
  std::optional<CampaignStateRecord> state;
  std::vector<std::uint8_t> exactBytes;
};

[[noreturn]] void
storeFailure(const CampaignStateStoreErrorKind kind,
             const std::optional<CampaignStateRecord> &requestedState,
             const char *const message) {
  throw CampaignStateStoreError(kind, requestedState, message);
}

[[nodiscard]] io::DurableDocumentDisposition
inspectDocument(const std::span<const std::uint8_t> bytes, void *) noexcept {
  try {
    const auto decoded = decodeCampaignStateDocument(bytes);
    if (std::holds_alternative<OpaqueFutureCampaignStateDocument>(decoded)) {
      return io::DurableDocumentDisposition::futurePreserve;
    }
    if (validCampaignStateRecord(std::get<CampaignStateRecord>(decoded))) {
      return io::DurableDocumentDisposition::valid;
    }
  } catch (...) {
  }
  return io::DurableDocumentDisposition::replaceableInvalid;
}

constexpr io::DurableDocumentInspection documentInspection{
    .maximumBytes = maximumCampaignStateDocumentBytes,
    .inspect = inspectDocument,
};

[[nodiscard]] CampaignStateFileStatus
statusForDocumentRead(const io::DurableDocumentFileStatus status) noexcept {
  switch (status) {
  case io::DurableDocumentFileStatus::missing:
    return CampaignStateFileStatus::missing;
  case io::DurableDocumentFileStatus::valid:
    return CampaignStateFileStatus::valid;
  case io::DurableDocumentFileStatus::futurePreserve:
    return CampaignStateFileStatus::futureSchema;
  case io::DurableDocumentFileStatus::replaceableInvalid:
    return CampaignStateFileStatus::malformed;
  case io::DurableDocumentFileStatus::oversized:
    return CampaignStateFileStatus::oversized;
  case io::DurableDocumentFileStatus::wrongTypeOrLinked:
    return CampaignStateFileStatus::wrongTypeOrLinked;
  case io::DurableDocumentFileStatus::ioUnavailable:
    return CampaignStateFileStatus::ioUnavailable;
  }
  return CampaignStateFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(const std::filesystem::path &path) {
  DocumentRead result;
  try {
    auto read = io::readDurableDocument(path, documentInspection);
    result.diagnostic.status = statusForDocumentRead(read.status);
    result.exactBytes = std::move(read.exactBytes);
  } catch (...) {
    result.diagnostic.status = CampaignStateFileStatus::ioUnavailable;
    return result;
  }
  if (result.exactBytes.empty() &&
      result.diagnostic.status != CampaignStateFileStatus::malformed) {
    return result;
  }

  try {
    auto decoded = decodeCampaignStateDocument(result.exactBytes);
    if (const auto *future =
            std::get_if<OpaqueFutureCampaignStateDocument>(&decoded)) {
      result.diagnostic = {
          .status = CampaignStateFileStatus::futureSchema,
          .schemaVersion = future->schemaVersion,
      };
      return result;
    }
    const auto &state = std::get<CampaignStateRecord>(decoded);
    if (!validCampaignStateRecord(state)) {
      result.diagnostic = {
          .status = CampaignStateFileStatus::malformed,
          .schemaVersion = state.schemaVersion,
      };
      return result;
    }
    result.diagnostic = {
        .status = CampaignStateFileStatus::valid,
        .schemaVersion = state.schemaVersion,
    };
    result.state = state;
    return result;
  } catch (const CampaignStateCodecError &error) {
    result.diagnostic = {
        .status = error.kind() == CampaignStateCodecErrorKind::tooLarge
                      ? CampaignStateFileStatus::oversized
                      : CampaignStateFileStatus::malformed,
        .schemaVersion = error.schemaVersion(),
    };
    return result;
  } catch (...) {
    result.diagnostic.status = CampaignStateFileStatus::malformed;
    return result;
  }
}

[[nodiscard]] bool
blocksPersistence(const CampaignStateFileStatus status) noexcept {
  return status == CampaignStateFileStatus::futureSchema ||
         status == CampaignStateFileStatus::wrongTypeOrLinked ||
         status == CampaignStateFileStatus::ioUnavailable;
}

[[noreturn]] void
mapPairFailure(const io::DurableDocumentPairError &error,
               const std::optional<CampaignStateRecord> &requestedState) {
  switch (error.kind()) {
  case io::DurableDocumentPairErrorKind::invalidDirectory:
    storeFailure(CampaignStateStoreErrorKind::invalidDirectory, std::nullopt,
                 "campaign state directory is invalid");
  case io::DurableDocumentPairErrorKind::persistenceBlocked:
    storeFailure(CampaignStateStoreErrorKind::persistenceBlocked,
                 requestedState,
                 "campaign state persistence is blocked by retained state");
  case io::DurableDocumentPairErrorKind::commitUnknown:
    storeFailure(CampaignStateStoreErrorKind::commitUnknown, requestedState,
                 "campaign state commit outcome is unknown");
  case io::DurableDocumentPairErrorKind::invalidArgument:
  case io::DurableDocumentPairErrorKind::saveFailed:
    storeFailure(CampaignStateStoreErrorKind::saveFailed,
                 error.candidateRelevant() ? requestedState : std::nullopt,
                 "campaign state durable I/O failed");
  }
  storeFailure(CampaignStateStoreErrorKind::saveFailed, requestedState,
               "campaign state durable I/O failed");
}

[[nodiscard]] CampaignStateSaveResult
saveImpl(const std::filesystem::path &campaignDirectory,
         const CampaignStateRecord &candidate,
         const testing::CampaignStateStoreHooks *const hooks) {
  if (!validCampaignStateRecord(candidate)) {
    storeFailure(CampaignStateStoreErrorKind::invalidState, std::nullopt,
                 "campaign state candidate is invalid");
  }
  const auto bytes = encodeCampaignStateDocument(candidate);

  try {
    io::DurableDocumentCommitResult result;
    if (hooks == nullptr) {
      result = io::commitDurableDocumentPair(campaignDirectory, documentNames,
                                             documentInspection, bytes);
    } else {
      const io::testing::DurableDocumentPairHooks pairHooks{
          .replace = hooks->replace,
          .retryDurability = hooks->retryDurability,
          .context = hooks->context,
      };
      result = io::testing::commitDurableDocumentPairWithHooks(
          campaignDirectory, documentNames, documentInspection, bytes,
          pairHooks);
    }
    switch (result.status) {
    case io::DurableDocumentCommitStatus::unchanged:
      return {.status = CampaignStateSaveStatus::unchanged,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committed:
      return {.status = CampaignStateSaveStatus::committed,
              .backupRotated = result.backupRotated};
    case io::DurableDocumentCommitStatus::committedAfterReadback:
      return {.status = CampaignStateSaveStatus::committedAfterReadback,
              .backupRotated = result.backupRotated};
    }
  } catch (const io::DurableDocumentPairError &error) {
    mapPairFailure(error, candidate);
  }
  storeFailure(CampaignStateStoreErrorKind::saveFailed, candidate,
               "campaign state durable I/O failed");
}

} // namespace

CampaignStateStoreError::CampaignStateStoreError(
    const CampaignStateStoreErrorKind kind,
    std::optional<CampaignStateRecord> requestedState,
    const char *const message)
    : std::runtime_error(message), kind_(kind),
      requestedState_(std::move(requestedState)) {}

CampaignStateLoadResult
loadCampaignState(const std::filesystem::path &campaignDirectory) {
  CampaignStateLoadResult result;
  if (campaignDirectory.empty() || !campaignDirectory.is_absolute()) {
    result.current.status = CampaignStateFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }

  // Reject a linked/reparse private leaf. Native adapters must also keep its
  // parent tree outside directories writable by untrusted processes.
  switch (io::inspectDurableDocumentDirectory(campaignDirectory)) {
  case io::DurableDocumentDirectoryStatus::ready:
    break;
  case io::DurableDocumentDirectoryStatus::missing:
    result.current.status = CampaignStateFileStatus::missing;
    result.backup.status = CampaignStateFileStatus::missing;
    return result;
  case io::DurableDocumentDirectoryStatus::wrongTypeOrLinked:
    result.current.status = CampaignStateFileStatus::wrongTypeOrLinked;
    result.persistenceBlocked = true;
    return result;
  case io::DurableDocumentDirectoryStatus::unavailable:
    result.current.status = CampaignStateFileStatus::ioUnavailable;
    result.persistenceBlocked = true;
    return result;
  }

  const auto current = readDocument(campaignDirectory / currentFileName);
  result.current = current.diagnostic;
  if (current.diagnostic.status == CampaignStateFileStatus::futureSchema ||
      current.diagnostic.status == CampaignStateFileStatus::ioUnavailable) {
    result.persistenceBlocked = true;
    return result;
  }

  const auto backup = readDocument(campaignDirectory / backupFileName);
  result.backup = backup.diagnostic;
  if (current.diagnostic.status == CampaignStateFileStatus::valid &&
      current.state.has_value()) {
    result.state = *current.state;
    result.source = CampaignStateLoadSource::current;
    result.persistenceBlocked = blocksPersistence(backup.diagnostic.status);
    return result;
  }
  if (backup.diagnostic.status == CampaignStateFileStatus::valid &&
      backup.state.has_value()) {
    result.state = *backup.state;
    result.source = CampaignStateLoadSource::backup;
  }
  if (blocksPersistence(current.diagnostic.status) ||
      blocksPersistence(backup.diagnostic.status)) {
    result.persistenceBlocked = true;
  }
  return result;
}

CampaignStateSaveResult
saveCampaignState(const std::filesystem::path &campaignDirectory,
                  const CampaignStateRecord &candidate) {
  return saveImpl(campaignDirectory, candidate, nullptr);
}

namespace testing {

CampaignStateSaveResult
saveCampaignStateWithHooks(const std::filesystem::path &campaignDirectory,
                           const CampaignStateRecord &candidate,
                           const CampaignStateStoreHooks &hooks) {
  return saveImpl(campaignDirectory, candidate, &hooks);
}

} // namespace testing

} // namespace airfix::campaign
