#include "airfix/io/DurableFile.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace airfix::io {
namespace {

struct FileIdentity {
    DWORD volumeSerial{};
    DWORD indexHigh{};
    DWORD indexLow{};
};

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

[[nodiscard]] DurableFileErrorKind kindForWindowsError(const DWORD value) {
    switch (value) {
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return DurableFileErrorKind::alreadyExists;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return DurableFileErrorKind::notFound;
    case ERROR_DIRECTORY:
        return DurableFileErrorKind::wrongType;
    default: return DurableFileErrorKind::ioFailure;
    }
}

[[nodiscard]] std::string formatWindowsError(const DWORD value) {
    wchar_t* wideMessage = nullptr;
    DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        value,
        0U,
        reinterpret_cast<wchar_t*>(&wideMessage),
        0U,
        nullptr);
    if (length == 0U || wideMessage == nullptr) {
        return "Windows error " + std::to_string(value);
    }
    while (length > 0U &&
        (wideMessage[length - 1U] == L'\r' || wideMessage[length - 1U] == L'\n')) {
        wideMessage[length - 1U] = L'\0';
        --length;
    }
    const int required = ::WideCharToMultiByte(
        CP_UTF8, 0U, wideMessage, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string message;
    if (required > 0) {
        message.resize(static_cast<std::size_t>(required));
        (void)::WideCharToMultiByte(
            CP_UTF8,
            0U,
            wideMessage,
            static_cast<int>(length),
            message.data(),
            required,
            nullptr,
            nullptr);
    }
    ::LocalFree(wideMessage);
    return message.empty() ? "Windows error " + std::to_string(value) : message;
}

[[nodiscard]] std::string makeMessage(
    const DurableFileOperation operation,
    const std::filesystem::path& primary,
    const std::filesystem::path& secondary,
    const std::string_view detail) {
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
    return message;
}

[[noreturn]] void throwWindowsError(
    const DurableFileOperation operation,
    const std::filesystem::path& primary,
    const std::filesystem::path& secondary,
    const DWORD value) {
    const std::error_code error(static_cast<int>(value), std::system_category());
    throw DurableFileError(
        kindForWindowsError(value),
        operation,
        error,
        primary,
        secondary,
        makeMessage(operation, primary, secondary, formatWindowsError(value)));
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
        makeMessage(operation, primary, secondary, detail));
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
    return parent.empty() ? std::filesystem::path(L".") : parent;
}

void closeIgnoringErrors(const HANDLE handle) noexcept {
    if (handle != INVALID_HANDLE_VALUE) {
        (void)::CloseHandle(handle);
    }
}

void closeChecked(HANDLE& handle, const std::filesystem::path& path) {
    const HANDLE closing = handle;
    handle = INVALID_HANDLE_VALUE;
    if (!::CloseHandle(closing)) {
        throwWindowsError(DurableFileOperation::close, path, {}, ::GetLastError());
    }
}

[[nodiscard]] FileIdentity fileIdentity(
    const HANDLE handle,
    const std::filesystem::path& path) {
    BY_HANDLE_FILE_INFORMATION information {};
    if (!::GetFileInformationByHandle(handle, &information)) {
        throwWindowsError(DurableFileOperation::inspect, path, {}, ::GetLastError());
    }
    if ((information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U ||
        ::GetFileType(handle) != FILE_TYPE_DISK) {
        throwTypedError(
            DurableFileErrorKind::wrongType,
            DurableFileOperation::inspect,
            path,
            {},
            std::errc::invalid_argument,
            "path is not a regular file");
    }
    return {
        .volumeSerial = information.dwVolumeSerialNumber,
        .indexHigh = information.nFileIndexHigh,
        .indexLow = information.nFileIndexLow,
    };
}

[[nodiscard]] bool sameIdentity(
    const FileIdentity& left,
    const FileIdentity& right) noexcept {
    return left.volumeSerial == right.volumeSerial &&
        left.indexHigh == right.indexHigh && left.indexLow == right.indexLow;
}

void requireSameIdentity(
    const FileIdentity& expected,
    const FileIdentity& actual,
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath) {
    if (!sameIdentity(expected, actual)) {
        throwTypedError(
            DurableFileErrorKind::ioFailure,
            DurableFileOperation::inspect,
            preparedPath,
            targetPath,
            std::errc::io_error,
            "prepared file identity changed before publication");
    }
}

void markForDeletion(const HANDLE handle) noexcept {
    FILE_DISPOSITION_INFO disposition {.DeleteFile = TRUE};
    (void)::SetFileInformationByHandle(
        handle, FileDispositionInfo, &disposition, sizeof(disposition));
}

void cleanupOwnedFile(
    const std::filesystem::path& path,
    const FileIdentity& identity) noexcept {
    const HANDLE handle = ::CreateFileW(
        path.c_str(),
        DELETE | FILE_READ_ATTRIBUTES,
        0U,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    BY_HANDLE_FILE_INFORMATION information {};
    if (::GetFileInformationByHandle(handle, &information)) {
        const FileIdentity current {
            .volumeSerial = information.dwVolumeSerialNumber,
            .indexHigh = information.nFileIndexHigh,
            .indexLow = information.nFileIndexLow,
        };
        if (sameIdentity(identity, current)) {
            markForDeletion(handle);
        }
    }
    closeIgnoringErrors(handle);
}

void rollbackRenamedPublication(
    const std::filesystem::path& publishedPath,
    const std::filesystem::path& preparedPath,
    const FileIdentity& identity) noexcept {
    const HANDLE handle = ::CreateFileW(
        publishedPath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    BY_HANDLE_FILE_INFORMATION information {};
    const bool inspected = ::GetFileInformationByHandle(handle, &information) != FALSE;
    const FileIdentity current {
        .volumeSerial = information.dwVolumeSerialNumber,
        .indexHigh = information.nFileIndexHigh,
        .indexLow = information.nFileIndexLow,
    };
    closeIgnoringErrors(handle);
    if (!inspected || !sameIdentity(identity, current)) {
        return;
    }
    // No REPLACE flag: rollback never overwrites a path created by recovery or
    // another actor. MOVEFILE_WRITE_THROUGH makes a successful rollback durable.
    (void)::MoveFileExW(
        publishedPath.c_str(), preparedPath.c_str(), MOVEFILE_WRITE_THROUGH);
}

[[nodiscard]] DWORD pathAttributes(
    const std::filesystem::path& path,
    const bool allowMissing) {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return attributes;
    }
    const DWORD error = ::GetLastError();
    if (allowMissing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
        return INVALID_FILE_ATTRIBUTES;
    }
    throwWindowsError(DurableFileOperation::inspect, path, {}, error);
}

[[nodiscard]] FileIdentity inspectRegularIdentity(
    const std::filesystem::path& path) {
    HANDLE handle = ::CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throwWindowsError(DurableFileOperation::open, path, {}, ::GetLastError());
    }
    try {
        const auto identity = fileIdentity(handle, path);
        closeChecked(handle, path);
        return identity;
    }
    catch (...) {
        closeIgnoringErrors(handle);
        throw;
    }
}

[[nodiscard]] std::optional<FileIdentity> regularTargetIdentityOrMissing(
    const std::filesystem::path& path) {
    const DWORD attributes = pathAttributes(path, true);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return std::nullopt;
    }
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
        throwTypedError(
            DurableFileErrorKind::wrongType,
            DurableFileOperation::inspect,
            path,
            {},
            std::errc::invalid_argument,
            "replacement target is not a regular file");
    }
    return inspectRegularIdentity(path);
}

} // namespace

namespace {

void replaceFileDurableImpl(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath,
    const testing::AfterPreparedSyncHook hook,
    void* const context) {
    requireDistinctPaths(preparedPath, targetPath);
    const auto preparedIdentity = inspectRegularIdentity(preparedPath);
    syncFile(preparedPath);
    requireSameIdentity(
        preparedIdentity,
        inspectRegularIdentity(preparedPath),
        preparedPath,
        targetPath);
    const auto targetIdentity = regularTargetIdentityOrMissing(targetPath);
    if (targetIdentity.has_value() && sameIdentity(preparedIdentity, *targetIdentity)) {
        throwTypedError(
            DurableFileErrorKind::invalidArgument,
            DurableFileOperation::validate,
            preparedPath,
            targetPath,
            std::errc::invalid_argument,
            "source and destination identify the same file");
    }
    if (hook != nullptr) {
        hook(context);
    }
    requireSameIdentity(
        preparedIdentity,
        inspectRegularIdentity(preparedPath),
        preparedPath,
        targetPath);
    if (!::MoveFileExW(
            preparedPath.c_str(),
            targetPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throwWindowsError(
            DurableFileOperation::rename,
            preparedPath,
            targetPath,
            ::GetLastError());
    }
    const auto publishedIdentity = inspectRegularIdentity(targetPath);
    if (!sameIdentity(preparedIdentity, publishedIdentity)) {
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
    const auto preparedIdentity = inspectRegularIdentity(preparedPath);
    syncFile(preparedPath);
    requireSameIdentity(
        preparedIdentity,
        inspectRegularIdentity(preparedPath),
        preparedPath,
        finalPath);
    if (hook != nullptr) {
        hook(context);
    }
    requireSameIdentity(
        preparedIdentity,
        inspectRegularIdentity(preparedPath),
        preparedPath,
        finalPath);
    if (!::MoveFileExW(
            preparedPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
        throwWindowsError(
            DurableFileOperation::rename,
            preparedPath,
            finalPath,
            ::GetLastError());
    }
    const auto publishedIdentity = inspectRegularIdentity(finalPath);
    if (!sameIdentity(preparedIdentity, publishedIdentity)) {
        rollbackRenamedPublication(finalPath, preparedPath, publishedIdentity);
        throwTypedError(
            DurableFileErrorKind::ioFailure,
            DurableFileOperation::inspect,
            preparedPath,
            finalPath,
            std::errc::io_error,
            "published file identity differs from the verified prepared file");
    }
    syncFile(finalPath);
    syncDirectory(parentDirectory(finalPath));
    syncDirectory(parentDirectory(preparedPath));
}

} // namespace

void syncFile(const std::filesystem::path& path) {
    requirePath(path);
    const DWORD attributes = pathAttributes(path, false);
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
        throwTypedError(
            DurableFileErrorKind::wrongType,
            DurableFileOperation::inspect,
            path,
            {},
            std::errc::invalid_argument,
            "path is not a regular file");
    }
    HANDLE handle = ::CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throwWindowsError(DurableFileOperation::open, path, {}, ::GetLastError());
    }
    try {
        (void)fileIdentity(handle, path);
        if (!::FlushFileBuffers(handle)) {
            throwWindowsError(DurableFileOperation::flush, path, {}, ::GetLastError());
        }
        closeChecked(handle, path);
    }
    catch (...) {
        closeIgnoringErrors(handle);
        throw;
    }
}

void syncDirectory(const std::filesystem::path& path) {
    requirePath(path);
    const DWORD attributes = pathAttributes(path, false);
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        throwTypedError(
            DurableFileErrorKind::wrongType,
            DurableFileOperation::inspect,
            path,
            {},
            std::errc::not_a_directory,
            "path is not a directory");
    }
    HANDLE handle = ::CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throwWindowsError(DurableFileOperation::open, path, {}, ::GetLastError());
    }
    closeChecked(handle, path);
}

void writeFileExclusiveDurable(
    const std::filesystem::path& path,
    const std::span<const std::uint8_t> bytes) {
    requirePath(path);
    HANDLE handle = ::CreateFileW(
        path.c_str(),
        GENERIC_WRITE | DELETE,
        0U,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throwWindowsError(DurableFileOperation::open, path, {}, ::GetLastError());
    }

    FileIdentity identity {};
    try {
        identity = fileIdentity(handle, path);
        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const auto chunk = static_cast<DWORD>(std::min(
                remaining,
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD written = 0U;
            if (!::WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr)) {
                throwWindowsError(DurableFileOperation::write, path, {}, ::GetLastError());
            }
            if (written == 0U) {
                throwTypedError(
                    DurableFileErrorKind::ioFailure,
                    DurableFileOperation::write,
                    path,
                    {},
                    std::errc::io_error,
                    "write made no progress");
            }
            offset += written;
        }
        if (!::FlushFileBuffers(handle)) {
            throwWindowsError(DurableFileOperation::flush, path, {}, ::GetLastError());
        }
        closeChecked(handle, path);
        syncDirectory(parentDirectory(path));
    }
    catch (...) {
        if (handle != INVALID_HANDLE_VALUE) {
            markForDeletion(handle);
            closeIgnoringErrors(handle);
            handle = INVALID_HANDLE_VALUE;
        }
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
