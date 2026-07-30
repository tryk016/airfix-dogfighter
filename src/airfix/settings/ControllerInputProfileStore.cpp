#include "airfix/settings/ControllerInputProfileStore.hpp"

#include "airfix/io/DurableFile.hpp"

#include <algorithm>
#include <span>
#include <system_error>
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

struct DocumentRead final {
  ControllerInputProfileFileDiagnostic diagnostic;
  std::optional<input::ResolvedControllerInputProfile> profile;
  std::vector<std::uint8_t> exactBytes;
};

enum class SettingsDirectoryStatus : std::uint8_t {
  ready,
  missing,
  wrongTypeOrLinked,
  unavailable,
};

[[noreturn]] void storeFailure(
    const ControllerInputProfileStoreErrorKind kind,
    const std::optional<input::ControllerInputProfileRecord> &requestedRecord,
    const char *const message) {
  throw ControllerInputProfileStoreError(kind, requestedRecord, message);
}

[[nodiscard]] ControllerInputProfileFileStatus
statusForIoError(const io::DurableFileErrorKind kind) noexcept {
  switch (kind) {
  case io::DurableFileErrorKind::notFound:
    return ControllerInputProfileFileStatus::missing;
  case io::DurableFileErrorKind::wrongType:
    return ControllerInputProfileFileStatus::wrongTypeOrLinked;
  case io::DurableFileErrorKind::sizeLimitExceeded:
    return ControllerInputProfileFileStatus::oversized;
  case io::DurableFileErrorKind::invalidArgument:
  case io::DurableFileErrorKind::alreadyExists:
  case io::DurableFileErrorKind::ioFailure:
    return ControllerInputProfileFileStatus::ioUnavailable;
  }
  return ControllerInputProfileFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(const std::filesystem::path &path) {
  DocumentRead result;
  try {
    result.exactBytes = io::readBoundedRegularFile(
        path, maximumControllerInputProfileDocumentBytes);
  } catch (const io::DurableFileError &error) {
    result.diagnostic.status = statusForIoError(error.kind());
    return result;
  } catch (...) {
    result.diagnostic.status = ControllerInputProfileFileStatus::ioUnavailable;
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

[[nodiscard]] SettingsDirectoryStatus
inspectSettingsDirectory(const std::filesystem::path &directory) noexcept {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  if ((!error && status.type() == std::filesystem::file_type::not_found) ||
      error == std::errc::no_such_file_or_directory) {
    return SettingsDirectoryStatus::missing;
  }
  if (error) {
    return SettingsDirectoryStatus::unavailable;
  }
  if (!std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    return SettingsDirectoryStatus::wrongTypeOrLinked;
  }
  return SettingsDirectoryStatus::ready;
}

void requireSettingsDirectory(const std::filesystem::path &directory) {
  if (directory.empty() || !directory.is_absolute()) {
    storeFailure(ControllerInputProfileStoreErrorKind::invalidDirectory,
                 std::nullopt, "controller profile directory is invalid");
  }

  std::error_code error;
  auto status = std::filesystem::symlink_status(directory, error);
  if ((!error && status.type() == std::filesystem::file_type::not_found) ||
      error == std::errc::no_such_file_or_directory) {
    error.clear();
    if (!std::filesystem::create_directory(directory, error) && error) {
      storeFailure(ControllerInputProfileStoreErrorKind::invalidDirectory,
                   std::nullopt,
                   "controller profile directory cannot be created");
    }
    try {
      io::syncDirectory(directory.parent_path());
    } catch (...) {
      storeFailure(ControllerInputProfileStoreErrorKind::invalidDirectory,
                   std::nullopt,
                   "controller profile parent cannot be synchronized");
    }
    status = std::filesystem::symlink_status(directory, error);
  }
  if (error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    storeFailure(ControllerInputProfileStoreErrorKind::invalidDirectory,
                 std::nullopt,
                 "controller profile path is not an exact directory");
  }
}

void removeOwnedPreparedIfPresent(const std::filesystem::path &path,
                                  const std::filesystem::path &directory) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if ((!error && status.type() == std::filesystem::file_type::not_found) ||
      error == std::errc::no_such_file_or_directory) {
    return;
  }
  if (error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status)) {
    storeFailure(ControllerInputProfileStoreErrorKind::saveFailed, std::nullopt,
                 "controller profile prepared entry has an unsafe type");
  }
  const auto links = std::filesystem::hard_link_count(path, error);
  if (error || links != 1U) {
    storeFailure(ControllerInputProfileStoreErrorKind::saveFailed, std::nullopt,
                 "controller profile prepared entry is linked");
  }
  if (!std::filesystem::remove(path, error) || error) {
    storeFailure(ControllerInputProfileStoreErrorKind::saveFailed, std::nullopt,
                 "controller profile prepared entry cannot be removed");
  }
  try {
    io::syncDirectory(directory);
  } catch (...) {
    storeFailure(ControllerInputProfileStoreErrorKind::saveFailed, std::nullopt,
                 "controller profile prepared cleanup is not durable");
  }
}

void cleanupOwnedPreparedNoexcept(const std::filesystem::path &path,
                                  const bool owned) noexcept {
  if (!owned) {
    return;
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status)) {
    return;
  }
  const auto links = std::filesystem::hard_link_count(path, error);
  if (error || links != 1U) {
    return;
  }
  (void)std::filesystem::remove(path, error);
}

[[nodiscard]] bool
exactTargetBytes(const std::filesystem::path &target,
                 const std::span<const std::uint8_t> expected) noexcept {
  try {
    const auto bytes = io::readBoundedRegularFile(
        target, maximumControllerInputProfileDocumentBytes);
    return std::ranges::equal(bytes, expected);
  } catch (...) {
    return false;
  }
}

struct PublishResult final {
  bool confirmedAfterFailure{};
};

[[nodiscard]] PublishResult publishExact(
    const std::filesystem::path &prepared, const std::filesystem::path &target,
    const std::filesystem::path &directory,
    const std::span<const std::uint8_t> expected,
    const std::span<const std::uint8_t> previous,
    const testing::ControllerInputProfileStoreHooks *const hooks,
    const std::optional<input::ControllerInputProfileRecord> &requestedRecord) {
  bool replaceReturned = false;
  try {
    if (hooks != nullptr && hooks->replace != nullptr) {
      hooks->replace(prepared, target, hooks->context);
    } else {
      io::replaceFileDurable(prepared, target);
    }
    replaceReturned = true;
  } catch (...) {
    if (exactTargetBytes(target, expected)) {
      try {
        if (hooks != nullptr && hooks->retryDurability != nullptr) {
          hooks->retryDurability(target, directory, hooks->context);
        } else {
          io::syncFile(target);
          io::syncDirectory(directory);
        }
        return {.confirmedAfterFailure = true};
      } catch (...) {
        storeFailure(ControllerInputProfileStoreErrorKind::commitUnknown,
                     requestedRecord,
                     "controller profile commit durability is unknown");
      }
    }
    if (!replaceReturned &&
        ((!previous.empty() && exactTargetBytes(target, previous)) ||
         (previous.empty() && readDocument(target).diagnostic.status ==
                                  ControllerInputProfileFileStatus::missing))) {
      storeFailure(ControllerInputProfileStoreErrorKind::saveFailed,
                   requestedRecord,
                   "controller profile replacement failed before publication");
    }
    storeFailure(ControllerInputProfileStoreErrorKind::commitUnknown,
                 requestedRecord,
                 "controller profile replacement outcome is unknown");
  }
  if (!exactTargetBytes(target, expected)) {
    storeFailure(ControllerInputProfileStoreErrorKind::commitUnknown,
                 requestedRecord,
                 "controller profile committed readback does not match");
  }
  return {};
}

[[nodiscard]] PublishResult writeAndPublish(
    const std::filesystem::path &prepared, const std::filesystem::path &target,
    const std::filesystem::path &directory,
    const std::span<const std::uint8_t> expected,
    const std::span<const std::uint8_t> previous,
    const testing::ControllerInputProfileStoreHooks *const hooks,
    const std::optional<input::ControllerInputProfileRecord> &requestedRecord) {
  removeOwnedPreparedIfPresent(prepared, directory);
  bool preparedOwned = false;
  try {
    io::writeFileExclusiveDurable(prepared, expected);
    preparedOwned = true;
    if (!exactTargetBytes(prepared, expected)) {
      storeFailure(ControllerInputProfileStoreErrorKind::saveFailed,
                   requestedRecord,
                   "controller profile prepared readback failed");
    }
    const auto result = publishExact(prepared, target, directory, expected,
                                     previous, hooks, requestedRecord);
    preparedOwned = false;
    return result;
  } catch (const ControllerInputProfileStoreError &) {
    cleanupOwnedPreparedNoexcept(prepared, preparedOwned);
    throw;
  } catch (...) {
    cleanupOwnedPreparedNoexcept(prepared, preparedOwned);
    storeFailure(ControllerInputProfileStoreErrorKind::saveFailed,
                 requestedRecord, "controller profile durable I/O failed");
  }
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

  requireSettingsDirectory(settingsDirectory);
  const auto currentPath = settingsDirectory / currentFileName;
  const auto backupPath = settingsDirectory / backupFileName;
  const auto current = readDocument(currentPath);
  const auto backup = readDocument(backupPath);
  if (blocksPersistence(current.diagnostic.status) ||
      blocksPersistence(backup.diagnostic.status)) {
    storeFailure(ControllerInputProfileStoreErrorKind::persistenceBlocked,
                 candidate,
                 "controller profile persistence is blocked by retained state");
  }
  if (current.diagnostic.status == ControllerInputProfileFileStatus::valid &&
      current.exactBytes == bytes) {
    return {
        .status = ControllerInputProfileSaveStatus::unchanged,
        .backupRotated = false,
    };
  }

  bool backupRotated = false;
  bool confirmedAfterFailure = false;
  if (current.diagnostic.status == ControllerInputProfileFileStatus::valid) {
    const auto backupPrepared = settingsDirectory / backupPreparedFileName;
    const auto backupPublished = writeAndPublish(
        backupPrepared, backupPath, settingsDirectory, current.exactBytes,
        backup.exactBytes, hooks, candidate);
    backupRotated = true;
    confirmedAfterFailure = backupPublished.confirmedAfterFailure;
  }

  const auto currentPrepared = settingsDirectory / currentPreparedFileName;
  const auto published =
      writeAndPublish(currentPrepared, currentPath, settingsDirectory, bytes,
                      current.exactBytes, hooks, candidate);
  return {
      .status = (published.confirmedAfterFailure || confirmedAfterFailure)
                    ? ControllerInputProfileSaveStatus::committedAfterReadback
                    : ControllerInputProfileSaveStatus::committed,
      .backupRotated = backupRotated,
  };
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
  switch (inspectSettingsDirectory(settingsDirectory)) {
  case SettingsDirectoryStatus::ready:
    break;
  case SettingsDirectoryStatus::missing:
    result.current.status = ControllerInputProfileFileStatus::missing;
    result.backup.status = ControllerInputProfileFileStatus::missing;
    return result;
  case SettingsDirectoryStatus::wrongTypeOrLinked:
    result.current.status = ControllerInputProfileFileStatus::wrongTypeOrLinked;
    result.persistenceBlocked = true;
    return result;
  case SettingsDirectoryStatus::unavailable:
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
