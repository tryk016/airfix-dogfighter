#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace airfix::io {

enum class DurableFileErrorKind {
    invalidArgument,
    alreadyExists,
    notFound,
    wrongType,
    sizeLimitExceeded,
    ioFailure,
};

enum class DurableFileOperation {
    validate,
    inspect,
    open,
    read,
    write,
    flush,
    close,
    link,
    rename,
    remove,
};

class DurableFileError final : public std::runtime_error {
public:
    DurableFileError(
        const DurableFileErrorKind kind,
        const DurableFileOperation operation,
        std::error_code systemError,
        std::filesystem::path primaryPath,
        std::filesystem::path secondaryPath,
        std::string message)
        : std::runtime_error(std::move(message)),
          kind_(kind),
          operation_(operation),
          systemError_(systemError),
          primaryPath_(std::move(primaryPath)),
          secondaryPath_(std::move(secondaryPath)) {}

    [[nodiscard]] DurableFileErrorKind kind() const noexcept { return kind_; }
    [[nodiscard]] DurableFileOperation operation() const noexcept { return operation_; }
    [[nodiscard]] const std::error_code& systemError() const noexcept {
        return systemError_;
    }
    [[nodiscard]] const std::filesystem::path& primaryPath() const noexcept {
        return primaryPath_;
    }
    [[nodiscard]] const std::filesystem::path& secondaryPath() const noexcept {
        return secondaryPath_;
    }

private:
    DurableFileErrorKind kind_;
    DurableFileOperation operation_;
    std::error_code systemError_;
    std::filesystem::path primaryPath_;
    std::filesystem::path secondaryPath_;
};

// Reads a single-link regular file through one pinned operating-system handle.
// Symbolic links/reparse points and files with any additional hard links are
// rejected. The size is checked before allocation and revalidated after the
// exact read; growth, truncation, or identity changes fail closed.
[[nodiscard]] std::vector<std::uint8_t> readBoundedRegularFile(
    const std::filesystem::path& path,
    std::size_t maxBytes);

// Creates path without replacing an existing directory entry. The file and its
// parent directory are synchronized before the function returns. A failure
// removes only the file created by this call, when it can still be identified.
void writeFileExclusiveDurable(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> bytes);

// Publish operations require exclusive control of preparedPath and both parent
// directories from verification through return. Cross-platform path-based
// rename APIs cannot pin a directory entry; identity checks fail closed if an
// accidental substitution is observed, but are not a security boundary for a
// directory writable by an untrusted process.
//
// preparedPath and targetPath must be distinct paths on the same volume.
// Failures reported after the atomic rename may still leave targetPath updated;
// callers must inspect/recover their transaction state before retrying.
void replaceFileDurable(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath);

// Publishes preparedPath without ever replacing finalPath. The POSIX fallback
// uses link/unlink, so a crash may leave both names for the same regular file.
void renameFileNoReplaceDurable(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& finalPath);

void syncFile(const std::filesystem::path& path);

// On Windows there is no supported FlushFileBuffers equivalent for directory
// handles. That implementation validates the directory; durable rename calls
// additionally use MOVEFILE_WRITE_THROUGH. POSIX implementations call fsync
// and treat EINVAL/ENOTSUP as an explicitly unsupported directory flush.
void syncDirectory(const std::filesystem::path& path);

namespace testing {

// Deterministic substitution hook for contract tests. Production callers must
// use the non-testing functions above. The hook runs after the prepared file is
// synchronized and identified, immediately before the final identity check.
using AfterPreparedSyncHook = void (*)(void* context);

void replaceFileDurableWithHook(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& targetPath,
    AfterPreparedSyncHook hook,
    void* context);

void renameFileNoReplaceDurableWithHook(
    const std::filesystem::path& preparedPath,
    const std::filesystem::path& finalPath,
    AfterPreparedSyncHook hook,
    void* context);

} // namespace testing

} // namespace airfix::io
