#include "airfix/texture/PrivateTextureFileStorePlatform.hpp"

#ifndef _WIN32

#include "airfix/texture/PrivateTextureFileStoreInternal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

#if !defined(O_CLOEXEC) || !defined(O_DIRECTORY) || !defined(O_NOFOLLOW) ||    \
    !defined(O_NONBLOCK)
#error "PrivateTextureFileStore requires openat no-follow directory support"
#endif

namespace airfix::texture {
namespace {

[[nodiscard]] int closeOnExecFlag() noexcept { return O_CLOEXEC; }

[[nodiscard]] int noFollowFlag() noexcept { return O_NOFOLLOW; }

[[nodiscard]] int directoryFlag() noexcept { return O_DIRECTORY; }

[[nodiscard]] int nonBlockingFlag() noexcept { return O_NONBLOCK; }

class ScopedDescriptor final {
public:
  explicit ScopedDescriptor(const int descriptor = -1) noexcept
      : descriptor_(descriptor) {}

  ~ScopedDescriptor() {
    if (descriptor_ >= 0) {
      (void)::close(descriptor_);
    }
  }

  ScopedDescriptor(const ScopedDescriptor &) = delete;
  ScopedDescriptor &operator=(const ScopedDescriptor &) = delete;

  ScopedDescriptor(ScopedDescriptor &&other) noexcept
      : descriptor_(std::exchange(other.descriptor_, -1)) {}

  ScopedDescriptor &operator=(ScopedDescriptor &&other) noexcept {
    if (this != &other) {
      if (descriptor_ >= 0) {
        (void)::close(descriptor_);
      }
      descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
  int descriptor_;
};

[[nodiscard]] PrivateTextureFileStatus
statusForErrno(const int value) noexcept {
  switch (value) {
  case ENOENT:
    return PrivateTextureFileStatus::notFound;
#ifdef ELOOP
  case ELOOP:
    return PrivateTextureFileStatus::unsafeIndirection;
#endif
  case ENOTDIR:
  case EISDIR:
#ifdef ENXIO
  case ENXIO:
#endif
    return PrivateTextureFileStatus::unsafeType;
  case ENAMETOOLONG:
  case EINVAL:
    return PrivateTextureFileStatus::invalidRelativePath;
  default:
    return PrivateTextureFileStatus::ioFailure;
  }
}

[[nodiscard]] bool inspectDescriptor(const int descriptor,
                                     struct stat &status) noexcept {
  int result = -1;
  do {
    result = ::fstat(descriptor, &status);
  } while (result != 0 && errno == EINTR);
  return result == 0;
}

[[nodiscard]] bool sameSnapshot(const struct stat &left,
                                const struct stat &right) noexcept {
  if (!S_ISREG(right.st_mode) || left.st_dev != right.st_dev ||
      left.st_ino != right.st_ino || left.st_size != right.st_size ||
      left.st_nlink != right.st_nlink) {
    return false;
  }
#if defined(__APPLE__)
  return left.st_mtimespec.tv_sec == right.st_mtimespec.tv_sec &&
         left.st_mtimespec.tv_nsec == right.st_mtimespec.tv_nsec &&
         left.st_ctimespec.tv_sec == right.st_ctimespec.tv_sec &&
         left.st_ctimespec.tv_nsec == right.st_ctimespec.tv_nsec;
#elif defined(__linux__)
  return left.st_mtim.tv_sec == right.st_mtim.tv_sec &&
         left.st_mtim.tv_nsec == right.st_mtim.tv_nsec &&
         left.st_ctim.tv_sec == right.st_ctim.tv_sec &&
         left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
#else
  return left.st_mtime == right.st_mtime && left.st_ctime == right.st_ctime;
#endif
}

[[nodiscard]] PrivateTextureFileStatus
openDirectoryRelative(const int parent, const std::string &component,
                      ScopedDescriptor &opened) noexcept {
  int descriptor = -1;
  do {
    descriptor = ::openat(parent, component.c_str(),
                          O_RDONLY | closeOnExecFlag() | noFollowFlag() |
                              directoryFlag() | nonBlockingFlag());
  } while (descriptor < 0 && errno == EINTR);
  if (descriptor < 0) {
    return statusForErrno(errno);
  }
  opened = ScopedDescriptor(descriptor);
  struct stat status{};
  if (!inspectDescriptor(descriptor, status)) {
    return PrivateTextureFileStatus::ioFailure;
  }
  if (!S_ISDIR(status.st_mode)) {
    return PrivateTextureFileStatus::unsafeType;
  }
  return PrivateTextureFileStatus::ready;
}

class PosixPrivateTextureFileStore final : public PrivateTextureFileStore {
public:
  PosixPrivateTextureFileStore(ScopedDescriptor root,
                               const std::uint64_t generation,
                               PrivateTextureFileStoreLimits limits) noexcept
      : root_(std::move(root)), generation_(generation), limits_(limits) {}

  ~PosixPrivateTextureFileStore() override = default;

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
      int parent = root_.get();
      ScopedDescriptor directory;
      for (std::size_t index = 0U; index + 1U < parsed.components.size();
           ++index) {
        ScopedDescriptor child;
        const auto status =
            openDirectoryRelative(parent, parsed.components[index], child);
        if (status != PrivateTextureFileStatus::ready) {
          result.status = status;
          return result;
        }
        directory = std::move(child);
        parent = directory.get();
      }

      int descriptor = -1;
      do {
        descriptor = ::openat(parent, parsed.components.back().c_str(),
                              O_RDONLY | closeOnExecFlag() | noFollowFlag() |
                                  nonBlockingFlag());
      } while (descriptor < 0 && errno == EINTR);
      if (descriptor < 0) {
        result.status = statusForErrno(errno);
        return result;
      }
      ScopedDescriptor file(descriptor);

      struct stat initial{};
      if (!inspectDescriptor(descriptor, initial)) {
        result.status = PrivateTextureFileStatus::ioFailure;
        return result;
      }
      if (!S_ISREG(initial.st_mode)) {
        result.status = PrivateTextureFileStatus::unsafeType;
        return result;
      }
      if (initial.st_nlink != 1) {
        result.status = PrivateTextureFileStatus::multipleLinks;
        return result;
      }
      if (initial.st_size < 0) {
        result.status = PrivateTextureFileStatus::ioFailure;
        return result;
      }
      const auto size = static_cast<std::uintmax_t>(initial.st_size);
      if (size > static_cast<std::uintmax_t>(maximumBytes)) {
        result.status = PrivateTextureFileStatus::sizeLimitExceeded;
        return result;
      }

      result.bytes.resize(static_cast<std::size_t>(size));
      std::size_t offset = 0U;
      while (offset < result.bytes.size()) {
        const auto chunk = std::min(
            result.bytes.size() - offset,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto read = ::pread(descriptor, result.bytes.data() + offset,
                                  chunk, static_cast<off_t>(offset));
        if (read < 0) {
          if (errno == EINTR) {
            continue;
          }
          result.bytes.clear();
          result.status = PrivateTextureFileStatus::ioFailure;
          return result;
        }
        if (read == 0) {
          result.bytes.clear();
          result.status = PrivateTextureFileStatus::changedDuringRead;
          return result;
        }
        offset += static_cast<std::size_t>(read);
      }

      std::uint8_t extra{};
      ssize_t extraRead = -1;
      do {
        extraRead = ::pread(descriptor, &extra, 1U,
                            static_cast<off_t>(result.bytes.size()));
      } while (extraRead < 0 && errno == EINTR);
      if (extraRead < 0) {
        result.bytes.clear();
        result.status = PrivateTextureFileStatus::ioFailure;
        return result;
      }
      if (extraRead != 0) {
        result.bytes.clear();
        result.status = PrivateTextureFileStatus::changedDuringRead;
        return result;
      }

      struct stat finalStatus{};
      if (!inspectDescriptor(descriptor, finalStatus)) {
        result.bytes.clear();
        result.status = PrivateTextureFileStatus::ioFailure;
        return result;
      }
      if (!sameSnapshot(initial, finalStatus)) {
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
  ScopedDescriptor root_;
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

  int descriptor = -1;
  do {
    descriptor = ::open(configuredRoot.c_str(),
                        O_RDONLY | closeOnExecFlag() | noFollowFlag() |
                            directoryFlag() | nonBlockingFlag());
  } while (descriptor < 0 && errno == EINTR);
  if (descriptor < 0) {
    result.status = statusForErrno(errno);
    return result;
  }
  ScopedDescriptor root(descriptor);
  struct stat status{};
  if (!inspectDescriptor(descriptor, status)) {
    result.status = PrivateTextureFileStatus::ioFailure;
    return result;
  }
  if (!S_ISDIR(status.st_mode)) {
    result.status = PrivateTextureFileStatus::unsafeType;
    return result;
  }

  try {
    result.store = std::make_unique<PosixPrivateTextureFileStore>(
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
