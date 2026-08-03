#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace airfix::texture {

enum class PrivateTextureFileStatus : std::uint8_t {
  ready,
  invalidArgument,
  invalidRelativePath,
  staleGeneration,
  notFound,
  unsafeIndirection,
  unsafeType,
  multipleLinks,
  sizeLimitExceeded,
  changedDuringRead,
  ioFailure,
};

struct PrivateTextureFileStoreLimits {
  std::size_t maximumRelativePathBytes{4'096U};
  std::size_t maximumComponentBytes{255U};
  std::size_t maximumComponents{128U};
};

struct PrivateTextureFileReadResult {
  PrivateTextureFileStatus status{PrivateTextureFileStatus::ioFailure};
  std::vector<std::uint8_t> bytes;

  [[nodiscard]] bool success() const noexcept {
    return status == PrivateTextureFileStatus::ready;
  }
};

// A read-only capability pinned to one owner-configured private directory.
// Implementations retain only native handles/descriptors, never the configured
// root spelling. Relative paths are validated again at this boundary and every
// component is opened relative to the previously pinned directory without
// following symbolic links, junctions, or other reparse points.
class PrivateTextureFileStore {
public:
  virtual ~PrivateTextureFileStore() = default;

  PrivateTextureFileStore(const PrivateTextureFileStore &) = delete;
  PrivateTextureFileStore &operator=(const PrivateTextureFileStore &) = delete;
  PrivateTextureFileStore(PrivateTextureFileStore &&) = delete;
  PrivateTextureFileStore &operator=(PrivateTextureFileStore &&) = delete;

  [[nodiscard]] virtual std::uint64_t generation() const noexcept = 0;

  // Failure results contain only a fixed status and an empty byte vector.
  // They never contain an input path, checksum, operating-system message, or
  // local root. expectedGeneration must match the pinned capability.
  [[nodiscard]] virtual PrivateTextureFileReadResult
  readFile(std::string_view relativePath, std::size_t maximumBytes,
           std::uint64_t expectedGeneration) const noexcept = 0;

protected:
  PrivateTextureFileStore() = default;
};

struct PrivateTextureFileStoreOpenResult {
  PrivateTextureFileStatus status{PrivateTextureFileStatus::ioFailure};
  std::unique_ptr<PrivateTextureFileStore> store;

  [[nodiscard]] bool success() const noexcept {
    return status == PrivateTextureFileStatus::ready && store != nullptr;
  }
};

} // namespace airfix::texture
