#include "airfix/settings/RenderPresentationSettingsStore.hpp"

#include "airfix/io/DurableFile.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <span>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace airfix::settings {
namespace {

constexpr const char* currentFileName = "render-presentation.afrs";
constexpr const char* backupFileName = "render-presentation.afrs.backup";
constexpr const char* currentPreparedFileName = "render-presentation.afrs.partial";
constexpr const char* backupPreparedFileName = "render-presentation.afrs.backup.partial";

struct DocumentRead final {
    RenderSettingsFileDiagnostic diagnostic;
    std::optional<render::RenderPresentationSettings> settings;
    std::vector<std::uint8_t> exactBytes;
};

enum class SettingsDirectoryStatus : std::uint8_t {
    ready,
    missing,
    wrongTypeOrLinked,
    unavailable,
};

[[noreturn]] void
storeFailure(const RenderSettingsStoreErrorKind kind,
             const std::optional<render::RenderPresentationSettingsRecord>& requestedRecord,
             const char* const message) {
    throw RenderSettingsStoreError(kind, requestedRecord, message);
}

[[nodiscard]] RenderSettingsFileStatus
statusForIoError(const io::DurableFileErrorKind kind) noexcept {
    switch (kind) {
    case io::DurableFileErrorKind::notFound: return RenderSettingsFileStatus::missing;
    case io::DurableFileErrorKind::wrongType: return RenderSettingsFileStatus::wrongTypeOrLinked;
    case io::DurableFileErrorKind::sizeLimitExceeded: return RenderSettingsFileStatus::oversized;
    case io::DurableFileErrorKind::invalidArgument:
    case io::DurableFileErrorKind::alreadyExists:
    case io::DurableFileErrorKind::ioFailure: return RenderSettingsFileStatus::ioUnavailable;
    }
    return RenderSettingsFileStatus::ioUnavailable;
}

[[nodiscard]] DocumentRead readDocument(const std::filesystem::path& path) {
    DocumentRead result;
    try {
        result.exactBytes = io::readBoundedRegularFile(path, maximumRenderSettingsDocumentBytes);
    } catch (const io::DurableFileError& error) {
        result.diagnostic.status = statusForIoError(error.kind());
        return result;
    } catch (...) {
        result.diagnostic.status = RenderSettingsFileStatus::ioUnavailable;
        return result;
    }

    try {
        auto decoded = decodeRenderSettingsDocument(result.exactBytes);
        if (const auto* future = std::get_if<OpaqueFutureRenderSettingsRecord>(&decoded)) {
            result.diagnostic = {
                .status = RenderSettingsFileStatus::futureSchema,
                .schemaVersion = future->schemaVersion,
            };
            return result;
        }
        const auto& record = std::get<render::RenderPresentationSettingsRecord>(decoded);
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
    } catch (const RenderSettingsCodecError& error) {
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

[[nodiscard]] bool blocksPersistence(const RenderSettingsFileStatus status) noexcept {
    return status == RenderSettingsFileStatus::futureSchema ||
           status == RenderSettingsFileStatus::wrongTypeOrLinked ||
           status == RenderSettingsFileStatus::ioUnavailable;
}

[[nodiscard]] SettingsDirectoryStatus
inspectSettingsDirectory(const std::filesystem::path& directory) noexcept {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(directory, error);
    if ((!error && status.type() == std::filesystem::file_type::not_found) ||
        error == std::errc::no_such_file_or_directory) {
        return SettingsDirectoryStatus::missing;
    }
    if (error) {
        return SettingsDirectoryStatus::unavailable;
    }
    if (!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        return SettingsDirectoryStatus::wrongTypeOrLinked;
    }
    return SettingsDirectoryStatus::ready;
}

void requireSettingsDirectory(const std::filesystem::path& directory) {
    if (directory.empty() || !directory.is_absolute()) {
        storeFailure(RenderSettingsStoreErrorKind::invalidDirectory, std::nullopt,
                     "render settings directory is invalid");
    }

    std::error_code error;
    auto status = std::filesystem::symlink_status(directory, error);
    if ((!error && status.type() == std::filesystem::file_type::not_found) ||
        error == std::errc::no_such_file_or_directory) {
        error.clear();
        if (!std::filesystem::create_directory(directory, error)) {
            if (error) {
                storeFailure(RenderSettingsStoreErrorKind::invalidDirectory, std::nullopt,
                             "render settings directory cannot be created");
            }
        }
        try {
            io::syncDirectory(directory.parent_path());
        } catch (...) {
            storeFailure(RenderSettingsStoreErrorKind::invalidDirectory, std::nullopt,
                         "render settings parent directory cannot be synchronized");
        }
        status = std::filesystem::symlink_status(directory, error);
    }
    if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        storeFailure(RenderSettingsStoreErrorKind::invalidDirectory, std::nullopt,
                     "render settings path is not an exact directory");
    }
}

void removeOwnedPreparedIfPresent(const std::filesystem::path& path,
                                  const std::filesystem::path& directory) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if ((!error && status.type() == std::filesystem::file_type::not_found) ||
        error == std::errc::no_such_file_or_directory) {
        return;
    }
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        storeFailure(RenderSettingsStoreErrorKind::saveFailed, std::nullopt,
                     "render settings prepared entry has an unsafe type");
    }
    const auto links = std::filesystem::hard_link_count(path, error);
    if (error || links != 1U) {
        storeFailure(RenderSettingsStoreErrorKind::saveFailed, std::nullopt,
                     "render settings prepared entry is linked");
    }
    if (!std::filesystem::remove(path, error) || error) {
        storeFailure(RenderSettingsStoreErrorKind::saveFailed, std::nullopt,
                     "render settings prepared entry cannot be removed");
    }
    try {
        io::syncDirectory(directory);
    } catch (...) {
        storeFailure(RenderSettingsStoreErrorKind::saveFailed, std::nullopt,
                     "render settings prepared cleanup cannot be synchronized");
    }
}

void cleanupOwnedPreparedNoexcept(const std::filesystem::path& path, const bool owned) noexcept {
    if (!owned) {
        return;
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        return;
    }
    const auto links = std::filesystem::hard_link_count(path, error);
    if (error || links != 1U) {
        return;
    }
    (void)std::filesystem::remove(path, error);
}

[[nodiscard]] bool exactTargetBytes(const std::filesystem::path& target,
                                    const std::span<const std::uint8_t> expected) noexcept {
    try {
        const auto bytes = io::readBoundedRegularFile(target, maximumRenderSettingsDocumentBytes);
        return std::ranges::equal(bytes, expected);
    } catch (...) {
        return false;
    }
}

struct PublishResult final {
    bool confirmedAfterFailure{};
};

[[nodiscard]] PublishResult
publishExact(const std::filesystem::path& prepared, const std::filesystem::path& target,
             const std::filesystem::path& directory, const std::span<const std::uint8_t> expected,
             const std::span<const std::uint8_t> previous,
             const testing::RenderSettingsStoreHooks* const hooks,
             const std::optional<render::RenderPresentationSettingsRecord>& requestedRecord) {
    bool replaceReturned = false;
    try {
        if (hooks != nullptr && hooks->replace != nullptr) {
            hooks->replace(prepared, target, hooks->context);
        } else {
            io::replaceFileDurable(prepared, target);
        }
        replaceReturned = true;
    } catch (...) {
        const bool candidatePublished = exactTargetBytes(target, expected);
        if (candidatePublished) {
            try {
                if (hooks != nullptr && hooks->retryDurability != nullptr) {
                    hooks->retryDurability(target, directory, hooks->context);
                } else {
                    io::syncFile(target);
                    io::syncDirectory(directory);
                }
                return {.confirmedAfterFailure = true};
            } catch (...) {
                storeFailure(RenderSettingsStoreErrorKind::commitUnknown, requestedRecord,
                             "render settings commit durability is unknown");
            }
        }
        if (!replaceReturned && ((!previous.empty() && exactTargetBytes(target, previous)) ||
                                 (previous.empty() && readDocument(target).diagnostic.status ==
                                                          RenderSettingsFileStatus::missing))) {
            storeFailure(RenderSettingsStoreErrorKind::saveFailed, requestedRecord,
                         "render settings replacement failed before publication");
        }
        storeFailure(RenderSettingsStoreErrorKind::commitUnknown, requestedRecord,
                     "render settings replacement outcome is unknown");
    }
    if (!exactTargetBytes(target, expected)) {
        storeFailure(RenderSettingsStoreErrorKind::commitUnknown, requestedRecord,
                     "render settings readback does not match the committed record");
    }
    return {};
}

[[nodiscard]] PublishResult
writeAndPublish(const std::filesystem::path& prepared, const std::filesystem::path& target,
                const std::filesystem::path& directory,
                const std::span<const std::uint8_t> expected,
                const std::span<const std::uint8_t> previous,
                const testing::RenderSettingsStoreHooks* const hooks,
                const std::optional<render::RenderPresentationSettingsRecord>& requestedRecord) {
    removeOwnedPreparedIfPresent(prepared, directory);
    bool preparedOwned = false;
    try {
        io::writeFileExclusiveDurable(prepared, expected);
        preparedOwned = true;
        if (!exactTargetBytes(prepared, expected)) {
            storeFailure(RenderSettingsStoreErrorKind::saveFailed, requestedRecord,
                         "render settings prepared readback failed");
        }
        const auto result =
            publishExact(prepared, target, directory, expected, previous, hooks, requestedRecord);
        preparedOwned = false;
        return result;
    } catch (const RenderSettingsStoreError&) {
        cleanupOwnedPreparedNoexcept(prepared, preparedOwned);
        throw;
    } catch (...) {
        cleanupOwnedPreparedNoexcept(prepared, preparedOwned);
        storeFailure(RenderSettingsStoreErrorKind::saveFailed, requestedRecord,
                     "render settings durable I/O failed");
    }
}

[[nodiscard]] RenderSettingsSaveResult
saveImpl(const std::filesystem::path& settingsDirectory,
         const render::RenderPresentationSettings& candidate,
         const testing::RenderSettingsStoreHooks* const hooks) {
    const auto recordResult = render::makeRenderPresentationSettingsRecord(candidate);
    if (!recordResult.complete()) {
        storeFailure(RenderSettingsStoreErrorKind::invalidSettings, std::nullopt,
                     "render settings candidate is invalid");
    }
    const auto record = *recordResult.record;
    const auto bytes = encodeRenderSettingsDocument(record);

    requireSettingsDirectory(settingsDirectory);
    const auto currentPath = settingsDirectory / currentFileName;
    const auto backupPath = settingsDirectory / backupFileName;
    const auto current = readDocument(currentPath);
    const auto backup = readDocument(backupPath);
    if (blocksPersistence(current.diagnostic.status) ||
        blocksPersistence(backup.diagnostic.status)) {
        storeFailure(RenderSettingsStoreErrorKind::persistenceBlocked, record,
                     "render settings persistence is blocked by retained state");
    }
    if (current.diagnostic.status == RenderSettingsFileStatus::valid &&
        current.exactBytes == bytes) {
        return {
            .status = RenderSettingsSaveStatus::unchanged,
            .backupRotated = false,
        };
    }

    bool backupRotated = false;
    bool confirmedAfterFailure = false;
    if (current.diagnostic.status == RenderSettingsFileStatus::valid) {
        const auto backupPrepared = settingsDirectory / backupPreparedFileName;
        const auto backupPublished =
            writeAndPublish(backupPrepared, backupPath, settingsDirectory, current.exactBytes,
                            backup.exactBytes, hooks, record);
        backupRotated = true;
        confirmedAfterFailure = backupPublished.confirmedAfterFailure;
    }

    const auto currentPrepared = settingsDirectory / currentPreparedFileName;
    const auto published = writeAndPublish(currentPrepared, currentPath, settingsDirectory, bytes,
                                           current.exactBytes, hooks, record);
    return {
        .status = (published.confirmedAfterFailure || confirmedAfterFailure)
                      ? RenderSettingsSaveStatus::committedAfterReadback
                      : RenderSettingsSaveStatus::committed,
        .backupRotated = backupRotated,
    };
}

} // namespace

RenderSettingsStoreError::RenderSettingsStoreError(
    const RenderSettingsStoreErrorKind kind,
    std::optional<render::RenderPresentationSettingsRecord> requestedRecord,
    const char* const message)
    : std::runtime_error(message), kind_(kind), requestedRecord_(std::move(requestedRecord)) {}

RenderSettingsLoadResult
loadRenderPresentationSettings(const std::filesystem::path& settingsDirectory) {
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
    switch (inspectSettingsDirectory(settingsDirectory)) {
    case SettingsDirectoryStatus::ready: break;
    case SettingsDirectoryStatus::missing:
        result.current.status = RenderSettingsFileStatus::missing;
        result.backup.status = RenderSettingsFileStatus::missing;
        return result;
    case SettingsDirectoryStatus::wrongTypeOrLinked:
        result.current.status = RenderSettingsFileStatus::wrongTypeOrLinked;
        result.persistenceBlocked = true;
        return result;
    case SettingsDirectoryStatus::unavailable:
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

RenderSettingsSaveResult
saveRenderPresentationSettings(const std::filesystem::path& settingsDirectory,
                               const render::RenderPresentationSettings& candidate) {
    return saveImpl(settingsDirectory, candidate, nullptr);
}

namespace testing {

RenderSettingsSaveResult
saveRenderPresentationSettingsWithHooks(const std::filesystem::path& settingsDirectory,
                                        const render::RenderPresentationSettings& candidate,
                                        const RenderSettingsStoreHooks& hooks) {
    return saveImpl(settingsDirectory, candidate, &hooks);
}

} // namespace testing

} // namespace airfix::settings
