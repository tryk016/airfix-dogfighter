#include "airfix/texture/PrivateTextureFileStorePlatform.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <sys/stat.h>
#endif

namespace {

using airfix::texture::PrivateTextureFileStatus;

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class TemporaryDirectory final {
public:
  TemporaryDirectory() {
    static std::atomic<std::uint64_t> sequence{};
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::uint64_t attempt = 0U; attempt < 100U; ++attempt) {
      path_ = std::filesystem::temp_directory_path() /
              ("airfix-private-store-tests-" + std::to_string(stamp) + "-" +
               std::to_string(sequence.fetch_add(1U)));
      std::error_code error;
      if (std::filesystem::create_directory(path_, error)) {
        return;
      }
    }
    throw std::runtime_error("cannot create temporary test directory");
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

void writeFile(const std::filesystem::path &path,
               const std::string_view bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("cannot create synthetic file fixture");
  }
}

[[nodiscard]] std::string asString(const std::vector<std::uint8_t> &bytes) {
  return {bytes.begin(), bytes.end()};
}

void requireFailedWithoutBytes(
    const airfix::texture::PrivateTextureFileReadResult &result,
    const std::string_view message) {
  require(!result.success(), message);
  require(result.bytes.empty(), "failed read retained file bytes");
}

#ifdef _WIN32
[[nodiscard]] bool
createDirectoryJunction(const std::filesystem::path &junction,
                        const std::filesystem::path &target) {
  std::error_code error;
  if (!std::filesystem::create_directory(junction, error)) {
    return false;
  }
  HANDLE handle = ::CreateFileW(
      junction.c_str(), GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    std::filesystem::remove(junction, error);
    return false;
  }

  const auto absoluteTarget = std::filesystem::absolute(target).wstring();
  const std::wstring substitute = L"\\??\\" + absoluteTarget;
  const std::wstring printName = absoluteTarget;
  const auto substituteBytes =
      static_cast<WORD>(substitute.size() * sizeof(wchar_t));
  const auto printBytes = static_cast<WORD>(printName.size() * sizeof(wchar_t));
  const std::size_t pathBytes =
      static_cast<std::size_t>(substituteBytes) + sizeof(wchar_t) +
      static_cast<std::size_t>(printBytes) + sizeof(wchar_t);

#pragma pack(push, 1)
  struct MountPointBuffer {
    DWORD tag;
    WORD dataLength;
    WORD reserved;
    WORD substituteOffset;
    WORD substituteLength;
    WORD printOffset;
    WORD printLength;
    wchar_t path[1];
  };
#pragma pack(pop)

  std::vector<std::uint8_t> storage(offsetof(MountPointBuffer, path) +
                                    pathBytes);
  auto *buffer = reinterpret_cast<MountPointBuffer *>(storage.data());
  buffer->tag = IO_REPARSE_TAG_MOUNT_POINT;
  buffer->dataLength = static_cast<WORD>(8U + pathBytes);
  buffer->reserved = 0U;
  buffer->substituteOffset = 0U;
  buffer->substituteLength = substituteBytes;
  buffer->printOffset = static_cast<WORD>(
      static_cast<std::size_t>(substituteBytes) + sizeof(wchar_t));
  buffer->printLength = printBytes;
  std::memcpy(buffer->path, substitute.data(), substituteBytes);
  auto *printDestination = reinterpret_cast<wchar_t *>(
      reinterpret_cast<std::uint8_t *>(buffer->path) + buffer->printOffset);
  std::memcpy(printDestination, printName.data(), printBytes);

  DWORD returned{};
  const bool success =
      ::DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer,
                        static_cast<DWORD>(storage.size()), nullptr, 0U,
                        &returned, nullptr) != FALSE;
  (void)::CloseHandle(handle);
  if (!success) {
    std::filesystem::remove(junction, error);
  }
  return success;
}
#endif

[[nodiscard]] bool
createDirectoryIndirection(const std::filesystem::path &link,
                           const std::filesystem::path &target) {
  std::error_code error;
  std::filesystem::create_directory_symlink(target, link, error);
  if (!error) {
    return true;
  }
#ifdef _WIN32
  return createDirectoryJunction(link, target);
#else
  return false;
#endif
}

void testOpenAndBoundedRead() {
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "root";
  writeFile(root / "layer" / "sample.bin", "synthetic-bytes");
  writeFile(root / "empty.bin", "");
  const std::u8string unicodeName = u8"unicode-\u2713.bin";
  writeFile(root / std::filesystem::path(unicodeName), "unicode");
  std::string unicodeRelative;
  unicodeRelative.reserve(unicodeName.size());
  for (const auto byte : unicodeName) {
    unicodeRelative.push_back(static_cast<char>(byte));
  }

  auto opened =
      airfix::texture::openPrivateTextureFileStoreLocalRoot(root, 17U);
  require(opened.success(), "valid private root was rejected");
  require(opened.store->generation() == 17U, "generation was not retained");

  const auto read = opened.store->readFile("layer/sample.bin", 64U, 17U);
  require(read.success(), "bounded regular-file read failed");
  require(asString(read.bytes) == "synthetic-bytes", "read bytes changed");

  const auto empty = opened.store->readFile("empty.bin", 1U, 17U);
  require(empty.success(), "empty regular file was rejected");
  require(empty.bytes.empty(), "empty regular file returned bytes");

  const auto unicode = opened.store->readFile(unicodeRelative, 16U, 17U);
  require(unicode.success(), "valid UTF-8 component was rejected");
  require(asString(unicode.bytes) == "unicode",
          "valid UTF-8 file returned wrong bytes");

  const auto windowsSeparators =
      opened.store->readFile("layer\\sample.bin", 64U, 17U);
  require(windowsSeparators.success(), "separator normalization failed");

  const auto oversized = opened.store->readFile("layer/sample.bin", 4U, 17U);
  requireFailedWithoutBytes(oversized, "oversized file was accepted");
  require(oversized.status == PrivateTextureFileStatus::sizeLimitExceeded,
          "oversized file returned the wrong status");

  const auto stale = opened.store->readFile("layer/sample.bin", 64U, 18U);
  requireFailedWithoutBytes(stale, "stale generation was accepted");
  require(stale.status == PrivateTextureFileStatus::staleGeneration,
          "stale generation returned the wrong status");

  const auto zeroLimit = opened.store->readFile("layer/sample.bin", 0U, 17U);
  requireFailedWithoutBytes(zeroLimit, "zero byte limit was accepted");
  require(zeroLimit.status == PrivateTextureFileStatus::invalidArgument,
          "zero byte limit returned the wrong status");
}

void testRelativePathBoundary() {
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "root";
  writeFile(root / "valid.bin", "safe");
  auto opened = airfix::texture::openPrivateTextureFileStoreLocalRoot(root, 1U);
  require(opened.success(), "path-boundary root was rejected");

  std::string invalidUtf8("bad-");
  invalidUtf8.push_back(static_cast<char>(0xC0U));
  invalidUtf8 += ".bin";
  const std::vector<std::string> invalid{
      "",
      "/absolute.bin",
      "\\absolute.bin",
      "C:/drive.bin",
      "../escape.bin",
      "safe/../escape.bin",
      "safe/./file.bin",
      "safe//file.bin",
      "safe/",
      "safe/file.bin:stream",
      "safe/question?.bin",
      "safe/trailing. ",
      "CON",
      "nul.txt",
      invalidUtf8,
  };
  for (const auto &path : invalid) {
    const auto result = opened.store->readFile(path, 64U, 1U);
    requireFailedWithoutBytes(result, "unsafe relative path was accepted");
    require(result.status == PrivateTextureFileStatus::invalidRelativePath,
            "unsafe relative path returned the wrong status");
  }

  airfix::texture::PrivateTextureFileStoreLimits limits;
  limits.maximumRelativePathBytes = 8U;
  limits.maximumComponentBytes = 4U;
  limits.maximumComponents = 1U;
  auto limited =
      airfix::texture::openPrivateTextureFileStoreLocalRoot(root, 2U, limits);
  require(limited.success(), "valid constrained root was rejected");
  requireFailedWithoutBytes(limited.store->readFile("valid.bin", 64U, 2U),
                            "component limit was not enforced");
  requireFailedWithoutBytes(limited.store->readFile("a/b", 64U, 2U),
                            "component-count limit was not enforced");
}

void testMissingTypesAndLinks() {
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "root";
  const auto target = temporary.path() / "target";
  writeFile(root / "ordinary.bin", "ordinary");
  writeFile(target / "outside.bin", "outside");
  std::filesystem::create_directories(root / "directory");

  const auto hardLink = root / "hard-link.bin";
  std::error_code error;
  std::filesystem::create_hard_link(root / "ordinary.bin", hardLink, error);
  require(!error, "cannot create synthetic hard-link fixture");

  const bool madeIndirection =
      createDirectoryIndirection(root / "indirection", target);
  require(madeIndirection, "cannot create synthetic indirection fixture");

  auto opened = airfix::texture::openPrivateTextureFileStoreLocalRoot(root, 3U);
  require(opened.success(), "link-boundary root was rejected");

  const auto missing = opened.store->readFile("missing.bin", 64U, 3U);
  requireFailedWithoutBytes(missing, "missing file was accepted");
  require(missing.status == PrivateTextureFileStatus::notFound,
          "missing file returned the wrong status");

  const auto directory = opened.store->readFile("directory", 64U, 3U);
  requireFailedWithoutBytes(directory, "directory was accepted as a file");
  require(directory.status == PrivateTextureFileStatus::unsafeType,
          "directory returned the wrong status");

  const auto linked = opened.store->readFile("ordinary.bin", 64U, 3U);
  requireFailedWithoutBytes(linked, "multiply linked file was accepted");
  require(linked.status == PrivateTextureFileStatus::multipleLinks,
          "multiply linked file returned the wrong status");

  const auto indirect =
      opened.store->readFile("indirection/outside.bin", 64U, 3U);
  requireFailedWithoutBytes(indirect, "directory indirection escaped root");
  require(indirect.status == PrivateTextureFileStatus::unsafeIndirection ||
              indirect.status == PrivateTextureFileStatus::unsafeType,
          "directory indirection returned an unrelated status");

  const auto indirectLeaf = opened.store->readFile("indirection", 64U, 3U);
  requireFailedWithoutBytes(indirectLeaf,
                            "directory indirection was accepted as a leaf");
  require(indirectLeaf.status == PrivateTextureFileStatus::unsafeIndirection ||
              indirectLeaf.status == PrivateTextureFileStatus::unsafeType,
          "leaf indirection returned an unrelated status");

#ifndef _WIN32
  const auto fifo = root / "fifo";
  require(::mkfifo(fifo.c_str(), 0600) == 0,
          "cannot create synthetic FIFO fixture");
  const auto special = opened.store->readFile("fifo", 64U, 3U);
  requireFailedWithoutBytes(special, "FIFO was accepted as a regular file");
  require(special.status == PrivateTextureFileStatus::unsafeType,
          "FIFO returned the wrong status");
#endif
}

void testRootIndirectionAndPinnedIdentity() {
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "root";
  const auto moved = temporary.path() / "moved-root";
  writeFile(root / "identity.bin", "original-root");

  const auto rootLink = temporary.path() / "root-link";
  require(createDirectoryIndirection(rootLink, root),
          "cannot create root-indirection fixture");
  auto indirect =
      airfix::texture::openPrivateTextureFileStoreLocalRoot(rootLink, 4U);
  require(!indirect.success(), "indirect configured root was accepted");
  require(indirect.store == nullptr, "failed root open retained a capability");

  auto opened = airfix::texture::openPrivateTextureFileStoreLocalRoot(root, 5U);
  require(opened.success(), "pinned-identity root was rejected");

  std::error_code renameError;
  std::filesystem::rename(root, moved, renameError);
  if (!renameError) {
    writeFile(root / "identity.bin", "replacement-root");
  }
  const auto read = opened.store->readFile("identity.bin", 64U, 5U);
  require(read.success(), "pinned root became unreadable after rename attempt");
  require(asString(read.bytes) == "original-root",
          "capability followed a replacement root spelling");
}

void testFactoryValidation() {
  TemporaryDirectory temporary;
  const auto root = temporary.path() / "root";
  std::filesystem::create_directories(root);

  auto zeroGeneration =
      airfix::texture::openPrivateTextureFileStoreLocalRoot(root, 0U);
  require(!zeroGeneration.success(), "zero generation was accepted");
  require(zeroGeneration.status == PrivateTextureFileStatus::invalidArgument,
          "zero generation returned the wrong status");

  auto relativeRoot =
      airfix::texture::openPrivateTextureFileStoreLocalRoot("relative", 1U);
  require(!relativeRoot.success(), "relative configured root was accepted");
  require(relativeRoot.status == PrivateTextureFileStatus::invalidArgument,
          "relative configured root returned the wrong status");

  auto embeddedNullNative = root.native();
  embeddedNullNative.push_back(std::filesystem::path::value_type{});
  embeddedNullNative.push_back(
      static_cast<std::filesystem::path::value_type>('x'));
  auto embeddedNullRoot = airfix::texture::openPrivateTextureFileStoreLocalRoot(
      std::filesystem::path(embeddedNullNative), 1U);
  require(!embeddedNullRoot.success(),
          "configured root with embedded NUL was accepted");
  require(embeddedNullRoot.status == PrivateTextureFileStatus::invalidArgument,
          "embedded-NUL root returned the wrong status");

  airfix::texture::PrivateTextureFileStoreLimits invalidLimits;
  invalidLimits.maximumComponents = 0U;
  auto invalid = airfix::texture::openPrivateTextureFileStoreLocalRoot(
      root, 1U, invalidLimits);
  require(!invalid.success(), "invalid limits were accepted");

  auto missing = airfix::texture::openPrivateTextureFileStoreLocalRoot(
      temporary.path() / "missing", 1U);
  require(!missing.success(), "missing root was accepted");
  require(missing.status == PrivateTextureFileStatus::notFound,
          "missing root returned the wrong status");

  writeFile(temporary.path() / "not-a-directory.bin", "file");
  auto fileRoot = airfix::texture::openPrivateTextureFileStoreLocalRoot(
      temporary.path() / "not-a-directory.bin", 1U);
  require(!fileRoot.success(), "regular file was accepted as a root");
}

} // namespace

int main() {
  try {
    testOpenAndBoundedRead();
    testRelativePathBoundary();
    testMissingTypesAndLinks();
    testRootIndirectionAndPinnedIdentity();
    testFactoryValidation();
    std::cout << "private texture file-store tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "private texture file-store tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
