#include "airfix/io/DurableFile.hpp"

#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <limits>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace airfix::io {
namespace {

[[nodiscard]] std::string_view operationName(const DurableFileOperation operation) {
    switch (operation) {
    case DurableFileOperation::validate: return "validate";
    case DurableFileOperation::inspect: return "inspect";
    case DurableFileOperation::open: return "open";
    case DurableFileOperation::write: return "write";
    case DurableFileOperation::flush: return "flush";
    case DurableFileOperation::close: return "close";
    case DurableFileOperation::link: return "link";
    case DurableFileOperation::rename: return "rename";
    case DurableFileOperation::remove: return "remove";
    }
    return "unknown";
}

[[nodiscard]] DurableFileErrorKind kindForErrno(const int value) {
    switch (value) {
    case EEXIST: return DurableFileErrorKind::alreadyExists;
    case ENOENT: return DurableFileErrorKind::notFound;
    case EISDIR:
    case ENOTDIR:
        return DurableFileErrorKind::wrongType;
    default: return DurableFileErrorKind::ioFailure;
    }
}

[[nodiscard]] std::string makeMessage(
    const DurableFileOperation operation,
    const std::filesystem::path& primary,
    const std::filesystem::path& secondary,
    const std::error_code& error,
    const std::string_view detail = {}) {
    std::string message("durable file ");
    message += operationName(operation);
    message += " failed for '";
    message += primary.string();
    message += "'";
    if (!secondary.empty()) {
        message += " -> '";
        message += secondary.string();
        message += "'";
    }
    if (!detail.empty()) {
        message += ": ";
        message += detail;
    }
    if (error) {
        message += ": ";
        message += error.message();
    }
    return message;
}

[[noreturn]] void throwSystemError(
    const DurableFileOperation operation,
    const std::filesystem::path& primary,
    const std::filesystem::path& secondary,
    const int value) {
    const std::error_code error(value, std::generic_category());
    throw DurableFileError(
        kindForErrno(value),
        operation,
        error,
        primary,
        secondary,
        makeMessage(operation, primary, secondary, error));
}

[[noreturn]] void throwTypedError(
    const DurableFileErrorKind kind,
    const DurableFileOperation operation,
    const std::filesystem::path& primary,
    const std::filesystem::path& secondary,
    const std::errc errorValue,
    const std::string_view detail) {
    const auto error = std::make_error_code(errorValue);
    throw DurableFileError(
        kind,
        operation,
        error,
        primary,
        secondary,
        makeMessage(operation, primary, secondary, error, detail));
}

void requirePath(const std::filesystem::path& path) {
    if (path.empty()) {
        throwTypedError(
            DurableFileErrorKind::invalidArgument,
            DurableFileOperation::validate,
            path,
            {},
            std::errc::invalid_argument,
            "path must not be empty");
    }
}

void requireDistinctPaths(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    requirePath(source);
    requirePath(destination);
    if (source.lexically_normal() == destination.lexically_normal()) {
        throwTypedError(
            DurableFileErrorKind::invalidArgument,
            DurableFileOperation::validate,
            source,
            destination,
            std::errc::invalid_argument,
            "source and destination must be distinct");
    }
}

[[nodiscard]] std::filesystem::path parentDirectory(
    const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

[[nodiscard]] int closeOnExecFlag() noexcept {
#ifdef O_CLOEXEC
    return O_CLOEXEC;
#else
    return 0;
#endif
}

[[nodiscard]] int noFollowFlag() noexcept {
#ifdef O_NOFOLLOW
    return O_NOFOLLOW;
#else
    return 0;
#endif
}

void closeIgnoringErrors(const int descriptor) noexcept {
    if (descriptor >= 0) {
        (void)::close(descriptor);
    }
}

void closeChecked(
    int& descriptor,
    const std::filesystem::path& path) {
    const int closing = descriptor;
    descriptor = -1;
    if (::close(closing) != 0) {
        throwSystemError(DurableFileOperation::close, path, {}, errno);
    }
}

[[nodiscard]] struct stat inspectPath(
    const std::filesystem::path& path,
    const bool allowMissing) {
    struct stat status {};
    if (::lstat(path.c_str(), &status) == 0) {
        return status;
    }
    const int error = errno;
    if (allowMissing && error == ENOENT) {
        status.st_mode = 0;
        return status;
    }
    throwSystemError(DurableFileOperation::inspect, path, {}, error);
}

[[nodiscard]] struct stat requireRegularPath(
    const std::filesystem::path& path) {
    const auto status = inspectPath(path, false);
    if (!S_ISREG(status.st_mode)) {
        throwTypedError(
            DurableFileErrorKind::wrongType,
            DurableFileOperation::inspect,
            path,
            {},
            std::errc::invalid_argument,
            "path is not a regular file");
    }
    return status;
}

void requireSameIdentity(
    const struct stat& expected,
    const struct stat& actual,
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath) {
    if (expected.st_dev != actual.st_dev || expected.st_ino != actual.st_ino) {
        throwTypedError(
            DurableFileErrorKind::ioFailure,
            DurableFileOperation::inspect,
            preparedPath,
            targetPath,
            std::errc::io_error,
            "prepared file identity changed before publication");
    }
}

[[nodiscard]] struct stat requireRegularTargetOrMissing(
    const std::filesystem::path& path) {
    const auto status = inspectPath(path, true);
    if (status.st_mode != 0 && !S_ISREG(status.st_mode)) {
        throwTypedError(
            DurableFileErrorKind::wrongType,
            DurableFileOperation::inspect,
            path,
            {},
            std::errc::invalid_argument,
            "replacement target is not a regular file");
    }
    return status;
}

void syncDirectoryIgnoringErrors(const std::filesystem::path& path) noexcept {
    const int descriptor = ::open(
        path.c_str(), O_RDONLY | closeOnExecFlag() | noFollowFlag());
    if (descriptor < 0) {
        return;
    }
    (void)::fsync(descriptor);
    closeIgnoringErrors(descriptor);
}

void cleanupOwnedFile(
    const std::filesystem::path& path,
    const struct stat& identity) noexcept {
    struct stat current {};
    if (::lstat(path.c_str(), &current) != 0 ||
        current.st_dev != identity.st_dev || current.st_ino != identity.st_ino) {
        return;
    }
    if (::unlink(path.c_str()) == 0) {
        syncDirectoryIgnoringErrors(parentDirectory(path));
    }
}

void rollbackRenamedPublication(
    const std::filesystem::path& publishedPath,
    const std::filesystem::path& preparedPath,
    const struct stat& identity) noexcept {
    struct stat current {};
    if (::lstat(publishedPath.c_str(), &current) != 0 ||
        current.st_dev != identity.st_dev || current.st_ino != identity.st_ino) {
        return;
    }
    // link() is the portable Apple-compatible no-replace primitive. Only
    // remove the published name after the unexpected inode has another name.
    if (::link(publishedPath.c_str(), preparedPath.c_str()) != 0) {
        return;
    }
    syncDirectoryIgnoringErrors(parentDirectory(preparedPath));
    cleanupOwnedFile(publishedPath, identity);
}

void rollbackLinkedPublication(
    const std::filesystem::path& publishedPath,
    const std::filesystem::path& preparedPath,
    const struct stat& identity) noexcept {
    struct stat preparedIdentity {};
    if (::lstat(preparedPath.c_str(), &preparedIdentity) != 0 ||
        preparedIdentity.st_dev != identity.st_dev ||
        preparedIdentity.st_ino != identity.st_ino) {
        return;
    }
    // The unexpected inode still has its prepared name, so removing the link
    // created by this call cannot delete its last name.
    cleanupOwnedFile(publishedPath, identity);
}

} // namespace

namespace {

void replaceFileDurableImpl(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath,
    const testing::AfterPreparedSyncHook hook,
    void* const context) {
    requireDistinctPaths(preparedPath, targetPath);
    const auto preparedIdentity = requireRegularPath(preparedPath);
    const auto targetIdentity = requireRegularTargetOrMissing(targetPath);
    if (targetIdentity.st_mode != 0 &&
        targetIdentity.st_dev == preparedIdentity.st_dev &&
        targetIdentity.st_ino == preparedIdentity.st_ino) {
        throwTypedError(
            DurableFileErrorKind::invalidArgument,
            DurableFileOperation::validate,
            preparedPath,
            targetPath,
            std::errc::invalid_argument,
            "source and destination identify the same file");
    }
    syncFile(preparedPath);
    requireSameIdentity(
        preparedIdentity,
        requireRegularPath(preparedPath),
        preparedPath,
        targetPath);
    if (hook != nullptr) {
        hook(context);
    }
    requireSameIdentity(
        preparedIdentity,
        requireRegularPath(preparedPath),
        preparedPath,
        targetPath);
    if (::rename(preparedPath.c_str(), targetPath.c_str()) != 0) {
        throwSystemError(
            DurableFileOperation::rename, preparedPath, targetPath, errno);
    }
    const auto publishedIdentity = requireRegularPath(targetPath);
    if (publishedIdentity.st_dev != preparedIdentity.st_dev ||
        publishedIdentity.st_ino != preparedIdentity.st_ino) {
        rollbackRenamedPublication(targetPath, preparedPath, publishedIdentity);
        throwTypedError(
            DurableFileErrorKind::ioFailure,
            DurableFileOperation::inspect,
            preparedPath,
            targetPath,
            std::errc::io_error,
            "published file identity differs from the verified prepared file");
    }
    syncFile(targetPath);
    syncDirectory(parentDirectory(targetPath));
    syncDirectory(parentDirectory(preparedPath));
}

void renameFileNoReplaceDurableImpl(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& finalPath,
    const testing::AfterPreparedSyncHook hook,
    void* const context) {
    requireDistinctPaths(preparedPath, finalPath);
    const auto sourceIdentity = requireRegularPath(preparedPath);
    syncFile(preparedPath);
    requireSameIdentity(
        sourceIdentity,
        requireRegularPath(preparedPath),
        preparedPath,
        finalPath);
    if (hook != nullptr) {
        hook(context);
    }
    requireSameIdentity(
        sourceIdentity,
        requireRegularPath(preparedPath),
        preparedPath,
        finalPath);
    if (::link(preparedPath.c_str(), finalPath.c_str()) != 0) {
        throwSystemError(
            DurableFileOperation::link, preparedPath, finalPath, errno);
    }

    try {
        const auto finalIdentity = inspectPath(finalPath, false);
        if (!S_ISREG(finalIdentity.st_mode) ||
            finalIdentity.st_dev != sourceIdentity.st_dev ||
            finalIdentity.st_ino != sourceIdentity.st_ino) {
            rollbackLinkedPublication(finalPath, preparedPath, finalIdentity);
            throwTypedError(
                DurableFileErrorKind::ioFailure,
                DurableFileOperation::inspect,
                preparedPath,
                finalPath,
                std::errc::io_error,
                "published path does not identify the prepared regular file");
        }
        syncDirectory(parentDirectory(finalPath));
        if (::unlink(preparedPath.c_str()) != 0) {
            throwSystemError(
                DurableFileOperation::remove, preparedPath, finalPath, errno);
        }
        syncDirectory(parentDirectory(preparedPath));
    }
    catch (...) {
        // Roll back only the hard link this call created. If the source unlink
        // already succeeded, the final name is now the sole durable name and
        // must remain for recovery.
        struct stat sourceStatus {};
        if (::lstat(preparedPath.c_str(), &sourceStatus) == 0 &&
            sourceStatus.st_dev == sourceIdentity.st_dev &&
            sourceStatus.st_ino == sourceIdentity.st_ino) {
            cleanupOwnedFile(finalPath, sourceIdentity);
        }
        throw;
    }
}

} // namespace

void syncFile(const std::filesystem::path& path) {
    requirePath(path);
    (void)requireRegularPath(path);
    int descriptor = ::open(
        path.c_str(), O_RDONLY | closeOnExecFlag() | noFollowFlag());
    if (descriptor < 0) {
        throwSystemError(DurableFileOperation::open, path, {}, errno);
    }
    try {
        struct stat status {};
        if (::fstat(descriptor, &status) != 0) {
            throwSystemError(DurableFileOperation::inspect, path, {}, errno);
        }
        if (!S_ISREG(status.st_mode)) {
            throwTypedError(
                DurableFileErrorKind::wrongType,
                DurableFileOperation::inspect,
                path,
                {},
                std::errc::invalid_argument,
                "path is not a regular file");
        }
        if (::fsync(descriptor) != 0) {
            throwSystemError(DurableFileOperation::flush, path, {}, errno);
        }
        closeChecked(descriptor, path);
    }
    catch (...) {
        closeIgnoringErrors(descriptor);
        throw;
    }
}

void syncDirectory(const std::filesystem::path& path) {
    requirePath(path);
    const auto inspected = inspectPath(path, false);
    if (!S_ISDIR(inspected.st_mode)) {
        throwTypedError(
            DurableFileErrorKind::wrongType,
            DurableFileOperation::inspect,
            path,
            {},
            std::errc::not_a_directory,
            "path is not a directory");
    }
    int flags = O_RDONLY | closeOnExecFlag() | noFollowFlag();
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    int descriptor = ::open(path.c_str(), flags);
    if (descriptor < 0) {
        throwSystemError(DurableFileOperation::open, path, {}, errno);
    }
    try {
        struct stat status {};
        if (::fstat(descriptor, &status) != 0) {
            throwSystemError(DurableFileOperation::inspect, path, {}, errno);
        }
        if (!S_ISDIR(status.st_mode)) {
            throwTypedError(
                DurableFileErrorKind::wrongType,
                DurableFileOperation::inspect,
                path,
                {},
                std::errc::not_a_directory,
                "path is not a directory");
        }
        if (::fsync(descriptor) != 0) {
            const int error = errno;
            if (error != EINVAL
#ifdef ENOTSUP
                && error != ENOTSUP
#endif
            ) {
                throwSystemError(DurableFileOperation::flush, path, {}, error);
            }
        }
        closeChecked(descriptor, path);
    }
    catch (...) {
        closeIgnoringErrors(descriptor);
        throw;
    }
}

void writeFileExclusiveDurable(
    const std::filesystem::path& path,
    const std::span<const std::uint8_t> bytes) {
    requirePath(path);
    int descriptor = ::open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | closeOnExecFlag(),
        static_cast<mode_t>(0600));
    if (descriptor < 0) {
        throwSystemError(DurableFileOperation::open, path, {}, errno);
    }

    struct stat identity {};
    int inspectResult = -1;
    do {
        inspectResult = ::fstat(descriptor, &identity);
    } while (inspectResult != 0 && errno == EINTR);
    if (inspectResult != 0) {
        const int error = errno;
        closeIgnoringErrors(descriptor);
        throwSystemError(DurableFileOperation::inspect, path, {}, error);
    }

    try {
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const auto chunk = std::min(
                remaining,
                static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const auto written = ::write(descriptor, bytes.data() + offset, chunk);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throwSystemError(DurableFileOperation::write, path, {}, errno);
            }
            if (written == 0) {
                throwTypedError(
                    DurableFileErrorKind::ioFailure,
                    DurableFileOperation::write,
                    path,
                    {},
                    std::errc::io_error,
                    "write made no progress");
            }
            offset += static_cast<std::size_t>(written);
        }
        if (::fsync(descriptor) != 0) {
            throwSystemError(DurableFileOperation::flush, path, {}, errno);
        }
        closeChecked(descriptor, path);
        syncDirectory(parentDirectory(path));
    }
    catch (...) {
        closeIgnoringErrors(descriptor);
        cleanupOwnedFile(path, identity);
        throw;
    }
}

void replaceFileDurable(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath) {
    replaceFileDurableImpl(preparedPath, targetPath, nullptr, nullptr);
}

void renameFileNoReplaceDurable(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& finalPath) {
    renameFileNoReplaceDurableImpl(preparedPath, finalPath, nullptr, nullptr);
}

namespace testing {

void replaceFileDurableWithHook(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath,
    const AfterPreparedSyncHook hook,
    void* const context) {
    replaceFileDurableImpl(preparedPath, targetPath, hook, context);
}

void renameFileNoReplaceDurableWithHook(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& finalPath,
    const AfterPreparedSyncHook hook,
    void* const context) {
    renameFileNoReplaceDurableImpl(preparedPath, finalPath, hook, context);
}

} // namespace testing

} // namespace airfix::io

#endif
