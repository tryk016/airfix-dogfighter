#include "airfix/texture/PrivateTextureFileStorePlatform.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#include "airfix/texture/PrivateTextureFileStoreInternal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace airfix::texture {
namespace {

class ScopedHandle final {
public:
  explicit ScopedHandle(const HANDLE handle = INVALID_HANDLE_VALUE) noexcept
      : handle_(handle) {}

  ~ScopedHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      (void)::CloseHandle(handle_);
    }
  }

  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  ScopedHandle(ScopedHandle &&other) noexcept
      : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}

  ScopedHandle &operator=(ScopedHandle &&other) noexcept {
    if (this != &other) {
      if (handle_ != INVALID_HANDLE_VALUE) {
        (void)::CloseHandle(handle_);
      }
      handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
    }
    return *this;
  }

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
  HANDLE handle_;
};

struct FileSnapshot {
  FILE_ID_INFO identity{};
  std::uint64_t size{};
  DWORD linkCount{};
};

[[nodiscard]] PrivateTextureFileStatus
statusForWindowsError(const DWORD value) noexcept {
  switch (value) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    return PrivateTextureFileStatus::notFound;
  case ERROR_DIRECTORY:
    return PrivateTextureFileStatus::unsafeType;
  case ERROR_CANT_ACCESS_FILE:
  case ERROR_REPARSE_TAG_INVALID:
  case ERROR_REPARSE_TAG_MISMATCH:
  case ERROR_INVALID_REPARSE_DATA:
    return PrivateTextureFileStatus::unsafeIndirection;
  case ERROR_INVALID_NAME:
  case ERROR_BAD_PATHNAME:
  case ERROR_FILENAME_EXCED_RANGE:
    return PrivateTextureFileStatus::invalidRelativePath;
  default:
    return PrivateTextureFileStatus::ioFailure;
  }
}

[[nodiscard]] PrivateTextureFileStatus
statusForNtStatus(const NTSTATUS value) noexcept {
  return statusForWindowsError(::RtlNtStatusToDosError(value));
}

[[nodiscard]] bool isNtSuccess(const NTSTATUS status) noexcept {
  return status >= 0;
}

[[nodiscard]] bool
queryAttributes(const HANDLE handle,
                FILE_ATTRIBUTE_TAG_INFO &information) noexcept {
  return ::GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
                                        &information,
                                        sizeof(information)) != FALSE;
}

[[nodiscard]] PrivateTextureFileStatus
inspectDirectory(const HANDLE handle) noexcept {
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!queryAttributes(handle, attributes) ||
      ::GetFileType(handle) != FILE_TYPE_DISK) {
    return PrivateTextureFileStatus::ioFailure;
  }
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return PrivateTextureFileStatus::unsafeIndirection;
  }
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
    return PrivateTextureFileStatus::unsafeType;
  }
  return PrivateTextureFileStatus::ready;
}

[[nodiscard]] PrivateTextureFileStatus
snapshotFile(const HANDLE handle, FileSnapshot &snapshot) noexcept {
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  FILE_STANDARD_INFO standard{};
  if (!queryAttributes(handle, attributes) ||
      !::GetFileInformationByHandleEx(handle, FileStandardInfo, &standard,
                                      sizeof(standard)) ||
      !::GetFileInformationByHandleEx(handle, FileIdInfo, &snapshot.identity,
                                      sizeof(snapshot.identity)) ||
      ::GetFileType(handle) != FILE_TYPE_DISK) {
    return PrivateTextureFileStatus::ioFailure;
  }
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
    return PrivateTextureFileStatus::unsafeIndirection;
  }
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      standard.Directory != FALSE) {
    return PrivateTextureFileStatus::unsafeType;
  }
  if (standard.NumberOfLinks != 1U) {
    return PrivateTextureFileStatus::multipleLinks;
  }
  if (standard.EndOfFile.QuadPart < 0) {
    return PrivateTextureFileStatus::ioFailure;
  }
  snapshot.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
  snapshot.linkCount = standard.NumberOfLinks;
  return PrivateTextureFileStatus::ready;
}

[[nodiscard]] bool sameSnapshot(const FileSnapshot &left,
                                const FileSnapshot &right) noexcept {
  return left.identity.VolumeSerialNumber ==
             right.identity.VolumeSerialNumber &&
         std::memcmp(&left.identity.FileId, &right.identity.FileId,
                     sizeof(left.identity.FileId)) == 0 &&
         left.size == right.size && left.linkCount == right.linkCount;
}

[[nodiscard]] bool utf8ToWide(const std::string &text, std::wstring &wide) {
  if (text.empty() ||
      text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  const auto size = static_cast<int>(text.size());
  const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                             text.c_str(), size, nullptr, 0);
  if (required <= 0 ||
      required > static_cast<int>(std::numeric_limits<USHORT>::max() /
                                  sizeof(wchar_t))) {
    return false;
  }
  wide.resize(static_cast<std::size_t>(required));
  return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(),
                               size, wide.data(), required) == required;
}

[[nodiscard]] PrivateTextureFileStatus
openRelative(const HANDLE parent, const std::string &component,
             const bool directory, ScopedHandle &opened) {
  std::wstring wide;
  if (!utf8ToWide(component, wide)) {
    return PrivateTextureFileStatus::invalidRelativePath;
  }
  UNICODE_STRING name{
      .Length = static_cast<USHORT>(wide.size() * sizeof(wchar_t)),
      .MaximumLength = static_cast<USHORT>(wide.size() * sizeof(wchar_t)),
      .Buffer = wide.data(),
  };
  OBJECT_ATTRIBUTES attributes{};
  InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, parent,
                             nullptr);
  IO_STATUS_BLOCK ioStatus{};
  HANDLE handle = INVALID_HANDLE_VALUE;
  const ACCESS_MASK access =
      directory ? FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
                      SYNCHRONIZE
                : FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  const ULONG options = FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
                        (directory ? 0U : FILE_SEQUENTIAL_ONLY);
  const NTSTATUS status = ::NtCreateFile(
      &handle, access, &attributes, &ioStatus, nullptr, FILE_ATTRIBUTE_NORMAL,
      FILE_SHARE_READ, FILE_OPEN, options, nullptr, 0U);
  if (!isNtSuccess(status)) {
    return statusForNtStatus(status);
  }
  opened = ScopedHandle(handle);
  return directory ? inspectDirectory(handle) : PrivateTextureFileStatus::ready;
}

class Win32PrivateTextureFileStore final : public PrivateTextureFileStore {
public:
  Win32PrivateTextureFileStore(ScopedHandle root,
                               const std::uint64_t generation,
                               PrivateTextureFileStoreLimits limits) noexcept
      : root_(std::move(root)), generation_(generation), limits_(limits) {}

  ~Win32PrivateTextureFileStore() override = default;

  [[nodiscard]] std::uint64_t generation() const noexcept override {
    return generation_;
  }

  [[nodiscard]] PrivateTextureFileReadResult
  readFile(const std::string_view relativePath, const std::size_t maximumBytes,
           const std::uint64_t expectedGeneration) const noexcept override {
    PrivateTextureFileReadResult result;
    if (maximumBytes == 0U) {
      result.status = PrivateTextureFileStatus::invalidArgument;
      return result;
    }
    if (expectedGeneration == 0U || expectedGeneration != generation_) {
      result.status = PrivateTextureFileStatus::staleGeneration;
      return result;
    }
    const auto parsed =
        detail::parsePrivateTextureRelativePath(relativePath, limits_);
    if (!parsed.valid()) {
      result.status = parsed.status;
      return result;
    }

    try {
      HANDLE parent = root_.get();
      ScopedHandle directory;
      for (std::size_t index = 0U; index + 1U < parsed.components.size();
           ++index) {
        ScopedHandle child;
        const auto status =
            openRelative(parent, parsed.components[index], true, child);
        if (status != PrivateTextureFileStatus::ready) {
          result.status = status;
          return result;
        }
        directory = std::move(child);
        parent = directory.get();
      }

      ScopedHandle file;
      auto status = openRelative(parent, parsed.components.back(), false, file);
      if (status != PrivateTextureFileStatus::ready) {
        result.status = status;
        return result;
      }
      FileSnapshot initial;
      status = snapshotFile(file.get(), initial);
      if (status != PrivateTextureFileStatus::ready) {
        result.status = status;
        return result;
      }
      if (initial.size > static_cast<std::uint64_t>(maximumBytes) ||
          initial.size > static_cast<std::uint64_t>(
                             std::numeric_limits<std::size_t>::max())) {
        result.status = PrivateTextureFileStatus::sizeLimitExceeded;
        return result;
      }

      result.bytes.resize(static_cast<std::size_t>(initial.size));
      std::size_t offset = 0U;
      while (offset < result.bytes.size()) {
        const auto remaining = result.bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD bytesRead = 0U;
        if (!::ReadFile(file.get(), result.bytes.data() + offset, chunk,
                        &bytesRead, nullptr)) {
          result.bytes.clear();
          result.status = PrivateTextureFileStatus::ioFailure;
          return result;
        }
        if (bytesRead == 0U) {
          result.bytes.clear();
          result.status = PrivateTextureFileStatus::changedDuringRead;
          return result;
        }
        offset += bytesRead;
      }

      std::uint8_t extra{};
      DWORD extraRead{};
      if (!::ReadFile(file.get(), &extra, 1U, &extraRead, nullptr)) {
        result.bytes.clear();
        result.status = PrivateTextureFileStatus::ioFailure;
        return result;
      }
      if (extraRead != 0U) {
        result.bytes.clear();
        result.status = PrivateTextureFileStatus::changedDuringRead;
        return result;
      }

      FileSnapshot finalSnapshot;
      status = snapshotFile(file.get(), finalSnapshot);
      if (status != PrivateTextureFileStatus::ready ||
          !sameSnapshot(initial, finalSnapshot)) {
        result.bytes.clear();
        result.status = PrivateTextureFileStatus::changedDuringRead;
        return result;
      }
      result.status = PrivateTextureFileStatus::ready;
      return result;
    } catch (const std::bad_alloc &) {
      result.bytes.clear();
      result.status = PrivateTextureFileStatus::ioFailure;
      return result;
    } catch (...) {
      result.bytes.clear();
      result.status = PrivateTextureFileStatus::ioFailure;
      return result;
    }
  }

private:
  ScopedHandle root_;
  std::uint64_t generation_;
  PrivateTextureFileStoreLimits limits_;
};

} // namespace

PrivateTextureFileStoreOpenResult openPrivateTextureFileStoreLocalRoot(
    const std::filesystem::path &configuredRoot, const std::uint64_t generation,
    const PrivateTextureFileStoreLimits &limits) noexcept {
  PrivateTextureFileStoreOpenResult result;
  const auto &nativeRoot = configuredRoot.native();
  if (configuredRoot.empty() || !configuredRoot.is_absolute() ||
      std::find(nativeRoot.begin(), nativeRoot.end(),
                std::filesystem::path::value_type{}) != nativeRoot.end() ||
      generation == 0U || !detail::validPrivateTextureFileStoreLimits(limits)) {
    result.status = PrivateTextureFileStatus::invalidArgument;
    return result;
  }

  HANDLE handle = ::CreateFileW(
      configuredRoot.c_str(),
      FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    result.status = statusForWindowsError(::GetLastError());
    return result;
  }
  ScopedHandle root(handle);
  result.status = inspectDirectory(handle);
  if (result.status != PrivateTextureFileStatus::ready) {
    return result;
  }

  try {
    result.store = std::make_unique<Win32PrivateTextureFileStore>(
        std::move(root), generation, limits);
    result.status = PrivateTextureFileStatus::ready;
  } catch (...) {
    result.store.reset();
    result.status = PrivateTextureFileStatus::ioFailure;
  }
  return result;
}

} // namespace airfix::texture

#endif
