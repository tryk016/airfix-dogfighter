#include "airfix/io/DurableDocumentPair.hpp"

#include "airfix/io/DurableFile.hpp"

#include <algorithm>
#include <array>
#include <system_error>

namespace airfix::io {
namespace {

[[noreturn]] void pairFailure(const DurableDocumentPairErrorKind kind,
                              const char *const message,
                              const bool candidateRelevant = true) {
  throw DurableDocumentPairError(kind, message, candidateRelevant);
}

[[nodiscard]] bool validLeafName(const std::string_view name) {
  if (name.empty() || name == "." || name == ".." ||
      name.find('\0') != std::string_view::npos) {
    return false;
  }
  const std::filesystem::path path{name};
  return !path.empty() && !path.is_absolute() && !path.has_root_name() &&
         !path.has_root_directory() && !path.has_parent_path() &&
         path.filename() == path;
}

void requireConfiguration(const DurableDocumentPairNames &names,
                          const DurableDocumentInspection &inspection) {
  if (inspection.maximumBytes == 0U || inspection.inspect == nullptr ||
      !validLeafName(names.current) || !validLeafName(names.backup) ||
      !validLeafName(names.currentPrepared) ||
      !validLeafName(names.backupPrepared)) {
    pairFailure(DurableDocumentPairErrorKind::invalidArgument,
                "durable document-pair configuration is invalid");
  }
  const std::array namesArray{
      names.current,
      names.backup,
      names.currentPrepared,
      names.backupPrepared,
  };
  for (std::size_t left = 0U; left < namesArray.size(); ++left) {
    for (std::size_t right = left + 1U; right < namesArray.size(); ++right) {
      if (namesArray[left] == namesArray[right]) {
        pairFailure(DurableDocumentPairErrorKind::invalidArgument,
                    "durable document-pair names are not distinct");
      }
    }
  }
}

void requireInspection(const DurableDocumentInspection &inspection) {
  if (inspection.maximumBytes == 0U || inspection.inspect == nullptr) {
    pairFailure(DurableDocumentPairErrorKind::invalidArgument,
                "durable document inspection is invalid");
  }
}

[[nodiscard]] DurableDocumentFileStatus
statusForIoError(const DurableFileErrorKind kind) noexcept {
  switch (kind) {
  case DurableFileErrorKind::notFound:
    return DurableDocumentFileStatus::missing;
  case DurableFileErrorKind::wrongType:
    return DurableDocumentFileStatus::wrongTypeOrLinked;
  case DurableFileErrorKind::sizeLimitExceeded:
    return DurableDocumentFileStatus::oversized;
  case DurableFileErrorKind::invalidArgument:
  case DurableFileErrorKind::alreadyExists:
  case DurableFileErrorKind::ioFailure:
    return DurableDocumentFileStatus::ioUnavailable;
  }
  return DurableDocumentFileStatus::ioUnavailable;
}

[[nodiscard]] DurableDocumentFileStatus
statusForDisposition(const DurableDocumentDisposition disposition) noexcept {
  switch (disposition) {
  case DurableDocumentDisposition::valid:
    return DurableDocumentFileStatus::valid;
  case DurableDocumentDisposition::futurePreserve:
    return DurableDocumentFileStatus::futurePreserve;
  case DurableDocumentDisposition::replaceableInvalid:
    return DurableDocumentFileStatus::replaceableInvalid;
  }
  return DurableDocumentFileStatus::replaceableInvalid;
}

void requireDirectory(const std::filesystem::path &directory) {
  if (directory.empty() || !directory.is_absolute()) {
    pairFailure(DurableDocumentPairErrorKind::invalidDirectory,
                "durable document directory is invalid");
  }

  std::error_code error;
  auto status = std::filesystem::symlink_status(directory, error);
  if ((!error && status.type() == std::filesystem::file_type::not_found) ||
      error == std::errc::no_such_file_or_directory) {
    error.clear();
    if (!std::filesystem::create_directory(directory, error) && error) {
      pairFailure(DurableDocumentPairErrorKind::invalidDirectory,
                  "durable document directory cannot be created");
    }
    try {
      syncDirectory(directory.parent_path());
    } catch (...) {
      pairFailure(DurableDocumentPairErrorKind::invalidDirectory,
                  "durable document parent cannot be synchronized");
    }
    status = std::filesystem::symlink_status(directory, error);
  }
  if (error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    pairFailure(DurableDocumentPairErrorKind::invalidDirectory,
                "durable document path is not an exact directory");
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
    pairFailure(DurableDocumentPairErrorKind::saveFailed,
                "durable document prepared entry has an unsafe type", false);
  }
  const auto links = std::filesystem::hard_link_count(path, error);
  if (error || links != 1U) {
    pairFailure(DurableDocumentPairErrorKind::saveFailed,
                "durable document prepared entry is linked", false);
  }
  if (!std::filesystem::remove(path, error) || error) {
    pairFailure(DurableDocumentPairErrorKind::saveFailed,
                "durable document prepared entry cannot be removed", false);
  }
  try {
    syncDirectory(directory);
  } catch (...) {
    pairFailure(DurableDocumentPairErrorKind::saveFailed,
                "durable document prepared cleanup is not durable", false);
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
                 const std::span<const std::uint8_t> expected,
                 const std::size_t maximumBytes) noexcept {
  try {
    const auto bytes = readBoundedRegularFile(target, maximumBytes);
    return std::ranges::equal(bytes, expected);
  } catch (...) {
    return false;
  }
}

struct PublishResult final {
  bool confirmedAfterFailure{};
};

[[nodiscard]] PublishResult
publishExact(const std::filesystem::path &prepared,
             const std::filesystem::path &target,
             const std::filesystem::path &directory,
             const std::span<const std::uint8_t> expected,
             const std::span<const std::uint8_t> previous,
             const DurableDocumentInspection &inspection,
             const testing::DurableDocumentPairHooks *const hooks) {
  bool replaceReturned = false;
  try {
    if (hooks != nullptr && hooks->replace != nullptr) {
      hooks->replace(prepared, target, hooks->context);
    } else {
      replaceFileDurable(prepared, target);
    }
    replaceReturned = true;
  } catch (...) {
    if (exactTargetBytes(target, expected, inspection.maximumBytes)) {
      try {
        if (hooks != nullptr && hooks->retryDurability != nullptr) {
          hooks->retryDurability(target, directory, hooks->context);
        } else {
          syncFile(target);
          syncDirectory(directory);
        }
        return {.confirmedAfterFailure = true};
      } catch (...) {
        pairFailure(DurableDocumentPairErrorKind::commitUnknown,
                    "durable document commit durability is unknown");
      }
    }
    if (!replaceReturned &&
        ((!previous.empty() &&
          exactTargetBytes(target, previous, inspection.maximumBytes)) ||
         (previous.empty() && readDurableDocument(target, inspection).status ==
                                  DurableDocumentFileStatus::missing))) {
      pairFailure(DurableDocumentPairErrorKind::saveFailed,
                  "durable document replacement failed before publication");
    }
    pairFailure(DurableDocumentPairErrorKind::commitUnknown,
                "durable document replacement outcome is unknown");
  }
  if (!exactTargetBytes(target, expected, inspection.maximumBytes)) {
    pairFailure(DurableDocumentPairErrorKind::commitUnknown,
                "durable document committed readback does not match");
  }
  return {};
}

[[nodiscard]] PublishResult
writeAndPublish(const std::filesystem::path &prepared,
                const std::filesystem::path &target,
                const std::filesystem::path &directory,
                const std::span<const std::uint8_t> expected,
                const std::span<const std::uint8_t> previous,
                const DurableDocumentInspection &inspection,
                const testing::DurableDocumentPairHooks *const hooks) {
  removeOwnedPreparedIfPresent(prepared, directory);
  bool preparedOwned = false;
  try {
    writeFileExclusiveDurable(prepared, expected);
    preparedOwned = true;
    if (!exactTargetBytes(prepared, expected, inspection.maximumBytes)) {
      pairFailure(DurableDocumentPairErrorKind::saveFailed,
                  "durable document prepared readback failed");
    }
    const auto result = publishExact(prepared, target, directory, expected,
                                     previous, inspection, hooks);
    preparedOwned = false;
    return result;
  } catch (const DurableDocumentPairError &) {
    cleanupOwnedPreparedNoexcept(prepared, preparedOwned);
    throw;
  } catch (...) {
    cleanupOwnedPreparedNoexcept(prepared, preparedOwned);
    pairFailure(DurableDocumentPairErrorKind::saveFailed,
                "durable document I/O failed");
  }
}

[[nodiscard]] DurableDocumentCommitResult
commitImpl(const std::filesystem::path &directory,
           const DurableDocumentPairNames &names,
           const DurableDocumentInspection &inspection,
           const std::span<const std::uint8_t> candidate,
           const testing::DurableDocumentPairHooks *const hooks) {
  requireConfiguration(names, inspection);
  if (candidate.empty() || candidate.size() > inspection.maximumBytes) {
    pairFailure(DurableDocumentPairErrorKind::invalidArgument,
                "durable document candidate is invalid");
  }
  const auto candidateDisposition =
      inspection.inspect(candidate, inspection.context);
  if (candidateDisposition != DurableDocumentDisposition::valid) {
    pairFailure(DurableDocumentPairErrorKind::invalidArgument,
                "durable document candidate is not current-valid");
  }

  requireDirectory(directory);
  const auto currentPath = directory / names.current;
  const auto backupPath = directory / names.backup;
  const auto current = readDurableDocument(currentPath, inspection);
  const auto backup = readDurableDocument(backupPath, inspection);
  if (durableDocumentBlocksPersistence(current.status) ||
      durableDocumentBlocksPersistence(backup.status)) {
    pairFailure(DurableDocumentPairErrorKind::persistenceBlocked,
                "durable document persistence is blocked by retained state");
  }
  if (current.status == DurableDocumentFileStatus::valid &&
      current.exactBytes.size() == candidate.size() &&
      std::ranges::equal(current.exactBytes, candidate)) {
    return {};
  }

  bool backupRotated = false;
  bool confirmedAfterFailure = false;
  if (current.status == DurableDocumentFileStatus::valid) {
    const auto backupPublished = writeAndPublish(
        directory / names.backupPrepared, backupPath, directory,
        current.exactBytes, backup.exactBytes, inspection, hooks);
    backupRotated = true;
    confirmedAfterFailure = backupPublished.confirmedAfterFailure;
  }

  const auto published =
      writeAndPublish(directory / names.currentPrepared, currentPath, directory,
                      candidate, current.exactBytes, inspection, hooks);
  return {
      .status = (published.confirmedAfterFailure || confirmedAfterFailure)
                    ? DurableDocumentCommitStatus::committedAfterReadback
                    : DurableDocumentCommitStatus::committed,
      .backupRotated = backupRotated,
  };
}

} // namespace

DurableDocumentPairError::DurableDocumentPairError(
    const DurableDocumentPairErrorKind kind, const char *const message,
    const bool candidateRelevant)
    : std::runtime_error(message), kind_(kind),
      candidateRelevant_(candidateRelevant) {}

DurableDocumentDirectoryStatus inspectDurableDocumentDirectory(
    const std::filesystem::path &directory) noexcept {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  if ((!error && status.type() == std::filesystem::file_type::not_found) ||
      error == std::errc::no_such_file_or_directory) {
    return DurableDocumentDirectoryStatus::missing;
  }
  if (error) {
    return DurableDocumentDirectoryStatus::unavailable;
  }
  if (!std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    return DurableDocumentDirectoryStatus::wrongTypeOrLinked;
  }
  return DurableDocumentDirectoryStatus::ready;
}

DurableDocumentRead
readDurableDocument(const std::filesystem::path &path,
                    const DurableDocumentInspection &inspection) {
  requireInspection(inspection);
  DurableDocumentRead result;
  try {
    result.exactBytes = readBoundedRegularFile(path, inspection.maximumBytes);
  } catch (const DurableFileError &error) {
    result.status = statusForIoError(error.kind());
    return result;
  } catch (...) {
    result.status = DurableDocumentFileStatus::ioUnavailable;
    return result;
  }
  result.status = statusForDisposition(
      inspection.inspect(result.exactBytes, inspection.context));
  return result;
}

DurableDocumentCommitResult
commitDurableDocumentPair(const std::filesystem::path &directory,
                          const DurableDocumentPairNames &names,
                          const DurableDocumentInspection &inspection,
                          const std::span<const std::uint8_t> candidate) {
  return commitImpl(directory, names, inspection, candidate, nullptr);
}

namespace testing {

DurableDocumentCommitResult commitDurableDocumentPairWithHooks(
    const std::filesystem::path &directory,
    const DurableDocumentPairNames &names,
    const DurableDocumentInspection &inspection,
    const std::span<const std::uint8_t> candidate,
    const DurableDocumentPairHooks &hooks) {
  return commitImpl(directory, names, inspection, candidate, &hooks);
}

} // namespace testing

} // namespace airfix::io
