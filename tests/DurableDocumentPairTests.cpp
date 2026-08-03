#include "airfix/io/DurableDocumentPair.hpp"
#include "airfix/io/DurableFile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr airfix::io::DurableDocumentPairNames names{
    .current = "current.afd",
    .backup = "backup.afd",
    .currentPrepared = "current.afd.partial",
    .backupPrepared = "backup.afd.partial",
};

[[nodiscard]] airfix::io::DurableDocumentDisposition
inspect(const std::span<const std::uint8_t> bytes, void *) noexcept {
  if (!bytes.empty() && bytes.front() == 1U) {
    return airfix::io::DurableDocumentDisposition::valid;
  }
  if (!bytes.empty() && bytes.front() == 2U) {
    return airfix::io::DurableDocumentDisposition::futurePreserve;
  }
  return airfix::io::DurableDocumentDisposition::replaceableInvalid;
}

constexpr airfix::io::DurableDocumentInspection inspection{
    .maximumBytes = 16U,
    .inspect = inspect,
};

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TempDirectory final {
public:
  TempDirectory() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
      root_ = std::filesystem::temp_directory_path() /
              ("airfix-document-pair-" + std::to_string(stamp) + "-" +
               std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(root_, error)) {
        directory_ = root_ / "documents";
        return;
      }
      if (error && error != std::errc::file_exists) {
        throw std::runtime_error("cannot create document-pair test root");
      }
    }
    throw std::runtime_error("cannot allocate document-pair test root");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  [[nodiscard]] const std::filesystem::path &directory() const noexcept {
    return directory_;
  }

  [[nodiscard]] std::filesystem::path file(const std::string_view name) const {
    return directory_ / name;
  }

private:
  std::filesystem::path root_;
  std::filesystem::path directory_;
};

[[nodiscard]] std::vector<std::uint8_t>
read(const std::filesystem::path &path) {
  return airfix::io::readBoundedRegularFile(path, inspection.maximumBytes);
}

void writeRaw(const std::filesystem::path &path,
              const std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output || (!bytes.empty() &&
                  !output.write(reinterpret_cast<const char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size())))) {
    throw std::runtime_error("cannot write synthetic document fixture");
  }
}

void requireError(const std::function<void()> &action,
                  const airfix::io::DurableDocumentPairErrorKind kind,
                  const std::filesystem::path &privatePath) {
  try {
    action();
  } catch (const airfix::io::DurableDocumentPairError &error) {
    require(error.kind() == kind, "document-pair error kind mismatch");
    require(std::string(error.what()).find(privatePath.string()) ==
                std::string::npos,
            "document-pair error leaked a host path");
    return;
  }
  throw std::runtime_error("expected DurableDocumentPairError");
}

void testInspectionAndDirectoryStates() {
  TempDirectory temporary;
  require(airfix::io::inspectDurableDocumentDirectory(temporary.directory()) ==
              airfix::io::DurableDocumentDirectoryStatus::missing,
          "missing document directory was not identified");
  const auto missing = airfix::io::readDurableDocument(
      temporary.file(names.current), inspection);
  require(missing.status == airfix::io::DurableDocumentFileStatus::missing &&
              missing.exactBytes.empty(),
          "missing document read was not exact");

  std::filesystem::create_directory(temporary.directory());
  writeRaw(temporary.file(names.current), std::array<std::uint8_t, 2U>{1U, 9U});
  writeRaw(temporary.file(names.backup), std::array<std::uint8_t, 2U>{2U, 8U});
  writeRaw(temporary.file(names.currentPrepared),
           std::array<std::uint8_t, 2U>{0U, 7U});
  require(
      airfix::io::readDurableDocument(temporary.file(names.current), inspection)
                  .status == airfix::io::DurableDocumentFileStatus::valid &&
          airfix::io::readDurableDocument(temporary.file(names.backup),
                                          inspection)
                  .status ==
              airfix::io::DurableDocumentFileStatus::futurePreserve &&
          airfix::io::readDurableDocument(temporary.file(names.currentPrepared),
                                          inspection)
                  .status ==
              airfix::io::DurableDocumentFileStatus::replaceableInvalid,
      "inspection callback classifications were not preserved");

  const std::vector<std::uint8_t> oversized(17U, 1U);
  writeRaw(temporary.file("oversized.afd"), oversized);
  require(airfix::io::readDurableDocument(temporary.file("oversized.afd"),
                                          inspection)
                  .status == airfix::io::DurableDocumentFileStatus::oversized,
          "oversized document was not bounded before inspection");
}

void testCommitUnchangedRotationAndInvalidReplacement() {
  TempDirectory temporary;
  const std::array<std::uint8_t, 3U> first{1U, 10U, 11U};
  const std::array<std::uint8_t, 3U> second{1U, 20U, 21U};
  const std::array<std::uint8_t, 3U> third{1U, 30U, 31U};

  const auto initial = airfix::io::commitDurableDocumentPair(
      temporary.directory(), names, inspection, first);
  require(initial.status ==
                  airfix::io::DurableDocumentCommitStatus::committed &&
              !initial.backupRotated &&
              read(temporary.file(names.current)) ==
                  std::vector(first.begin(), first.end()),
          "initial document was not published exactly");

  const auto unchanged = airfix::io::commitDurableDocumentPair(
      temporary.directory(), names, inspection, first);
  require(unchanged.status ==
                  airfix::io::DurableDocumentCommitStatus::unchanged &&
              !unchanged.backupRotated,
          "byte-identical document was rewritten");

  const auto rotated = airfix::io::commitDurableDocumentPair(
      temporary.directory(), names, inspection, second);
  require(rotated.backupRotated &&
              read(temporary.file(names.current)) ==
                  std::vector(second.begin(), second.end()) &&
              read(temporary.file(names.backup)) ==
                  std::vector(first.begin(), first.end()),
          "valid current was not rotated byte-exactly");

  writeRaw(temporary.file(names.current),
           std::array<std::uint8_t, 2U>{0U, 99U});
  const auto replaced = airfix::io::commitDurableDocumentPair(
      temporary.directory(), names, inspection, third);
  require(!replaced.backupRotated &&
              read(temporary.file(names.current)) ==
                  std::vector(third.begin(), third.end()) &&
              read(temporary.file(names.backup)) ==
                  std::vector(first.begin(), first.end()),
          "replaceable-invalid current was promoted or not replaced");
}

void testFuturePreservationAndUnsafeEntries() {
  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.directory());
    const std::array<std::uint8_t, 3U> future{2U, 42U, 43U};
    writeRaw(temporary.file(names.current), future);
    const auto before = read(temporary.file(names.current));
    requireError(
        [&] {
          (void)airfix::io::commitDurableDocumentPair(
              temporary.directory(), names, inspection,
              std::array<std::uint8_t, 2U>{1U, 1U});
        },
        airfix::io::DurableDocumentPairErrorKind::persistenceBlocked,
        temporary.directory());
    require(read(temporary.file(names.current)) == before,
            "future current was not preserved byte-for-byte");
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.directory());
    writeRaw(temporary.file(names.current),
             std::array<std::uint8_t, 2U>{1U, 1U});
    const std::array<std::uint8_t, 3U> future{2U, 52U, 53U};
    writeRaw(temporary.file(names.backup), future);
    requireError(
        [&] {
          (void)airfix::io::commitDurableDocumentPair(
              temporary.directory(), names, inspection,
              std::array<std::uint8_t, 2U>{1U, 2U});
        },
        airfix::io::DurableDocumentPairErrorKind::persistenceBlocked,
        temporary.directory());
    require(read(temporary.file(names.backup)) ==
                std::vector(future.begin(), future.end()),
            "future backup was not preserved byte-for-byte");
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.directory());
    std::filesystem::create_directory(temporary.file(names.current));
    requireError(
        [&] {
          (void)airfix::io::commitDurableDocumentPair(
              temporary.directory(), names, inspection,
              std::array<std::uint8_t, 2U>{1U, 3U});
        },
        airfix::io::DurableDocumentPairErrorKind::persistenceBlocked,
        temporary.directory());
  }
}

void testPreparedEntryCleanupAndSafety() {
  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.directory());
    writeRaw(temporary.file(names.currentPrepared),
             std::array<std::uint8_t, 2U>{0U, 7U});
    const std::array<std::uint8_t, 2U> candidate{1U, 8U};
    const auto result = airfix::io::commitDurableDocumentPair(
        temporary.directory(), names, inspection, candidate);
    require(
        result.status == airfix::io::DurableDocumentCommitStatus::committed &&
            !std::filesystem::exists(temporary.file(names.currentPrepared)) &&
            read(temporary.file(names.current)) ==
                std::vector(candidate.begin(), candidate.end()),
        "owned stale partial was not durably replaced");
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.directory());
    std::filesystem::create_directory(temporary.file(names.currentPrepared));
    requireError(
        [&] {
          (void)airfix::io::commitDurableDocumentPair(
              temporary.directory(), names, inspection,
              std::array<std::uint8_t, 2U>{1U, 9U});
        },
        airfix::io::DurableDocumentPairErrorKind::saveFailed,
        temporary.directory());
  }
}

struct ReplaceContext final {
  enum class Mode {
    throwBefore,
    replaceThenThrow,
  };

  enum class Target {
    current,
    backup,
    all,
  };

  Mode mode{Mode::throwBefore};
  Target target{Target::all};
};

void replaceHook(const std::filesystem::path &prepared,
                 const std::filesystem::path &target, void *const opaque) {
  auto &context = *static_cast<ReplaceContext *>(opaque);
  const bool current = target.filename() == names.current;
  const bool selected =
      context.target == ReplaceContext::Target::all ||
      (context.target == ReplaceContext::Target::current && current) ||
      (context.target == ReplaceContext::Target::backup && !current);
  if (!selected) {
    airfix::io::replaceFileDurable(prepared, target);
    return;
  }
  if (context.mode == ReplaceContext::Mode::replaceThenThrow) {
    airfix::io::replaceFileDurable(prepared, target);
  }
  throw std::runtime_error("synthetic replacement failure");
}

void retryHook(const std::filesystem::path &, const std::filesystem::path &,
               void *) {
  throw std::runtime_error("synthetic durability retry failure");
}

void testFaultMatrixAndCommitUnknown() {
  TempDirectory temporary;
  const std::array<std::uint8_t, 2U> first{1U, 10U};
  const std::array<std::uint8_t, 2U> second{1U, 20U};
  const std::array<std::uint8_t, 2U> third{1U, 30U};
  (void)airfix::io::commitDurableDocumentPair(temporary.directory(), names,
                                              inspection, first);

  ReplaceContext before{
      .mode = ReplaceContext::Mode::throwBefore,
      .target = ReplaceContext::Target::current,
  };
  const airfix::io::testing::DurableDocumentPairHooks beforeHooks{
      .replace = replaceHook,
      .context = &before,
  };
  requireError(
      [&] {
        (void)airfix::io::testing::commitDurableDocumentPairWithHooks(
            temporary.directory(), names, inspection, second, beforeHooks);
      },
      airfix::io::DurableDocumentPairErrorKind::saveFailed,
      temporary.directory());
  require(read(temporary.file(names.current)) ==
              std::vector(first.begin(), first.end()),
          "pre-publication failure changed current bytes");

  ReplaceContext after{
      .mode = ReplaceContext::Mode::replaceThenThrow,
      .target = ReplaceContext::Target::current,
  };
  const airfix::io::testing::DurableDocumentPairHooks afterHooks{
      .replace = replaceHook,
      .context = &after,
  };
  const auto recovered =
      airfix::io::testing::commitDurableDocumentPairWithHooks(
          temporary.directory(), names, inspection, second, afterHooks);
  require(
      recovered.status ==
              airfix::io::DurableDocumentCommitStatus::committedAfterReadback &&
          recovered.backupRotated &&
          read(temporary.file(names.current)) ==
              std::vector(second.begin(), second.end()),
      "post-publication failure was not confirmed by exact readback");

  const airfix::io::testing::DurableDocumentPairHooks unknownHooks{
      .replace = replaceHook,
      .retryDurability = retryHook,
      .context = &after,
  };
  requireError(
      [&] {
        (void)airfix::io::testing::commitDurableDocumentPairWithHooks(
            temporary.directory(), names, inspection, third, unknownHooks);
      },
      airfix::io::DurableDocumentPairErrorKind::commitUnknown,
      temporary.directory());
  require(read(temporary.file(names.current)) ==
              std::vector(third.begin(), third.end()),
          "commit-unknown fixture did not publish the candidate bytes");
}

void testBackupFaultMatrix() {
  const std::array<std::uint8_t, 2U> first{1U, 40U};
  const std::array<std::uint8_t, 2U> second{1U, 50U};

  {
    TempDirectory temporary;
    (void)airfix::io::commitDurableDocumentPair(temporary.directory(), names,
                                                inspection, first);
    ReplaceContext before{
        .mode = ReplaceContext::Mode::throwBefore,
        .target = ReplaceContext::Target::backup,
    };
    const airfix::io::testing::DurableDocumentPairHooks hooks{
        .replace = replaceHook,
        .context = &before,
    };
    requireError(
        [&] {
          (void)airfix::io::testing::commitDurableDocumentPairWithHooks(
              temporary.directory(), names, inspection, second, hooks);
        },
        airfix::io::DurableDocumentPairErrorKind::saveFailed,
        temporary.directory());
    require(read(temporary.file(names.current)) ==
                    std::vector(first.begin(), first.end()) &&
                !std::filesystem::exists(temporary.file(names.backup)),
            "pre-publication backup failure changed the document pair");
  }

  {
    TempDirectory temporary;
    (void)airfix::io::commitDurableDocumentPair(temporary.directory(), names,
                                                inspection, first);
    ReplaceContext after{
        .mode = ReplaceContext::Mode::replaceThenThrow,
        .target = ReplaceContext::Target::backup,
    };
    const airfix::io::testing::DurableDocumentPairHooks hooks{
        .replace = replaceHook,
        .context = &after,
    };
    const auto recovered =
        airfix::io::testing::commitDurableDocumentPairWithHooks(
            temporary.directory(), names, inspection, second, hooks);
    require(recovered.status == airfix::io::DurableDocumentCommitStatus::
                                    committedAfterReadback &&
                recovered.backupRotated &&
                read(temporary.file(names.current)) ==
                    std::vector(second.begin(), second.end()) &&
                read(temporary.file(names.backup)) ==
                    std::vector(first.begin(), first.end()),
            "post-publication backup failure did not complete after readback");
  }

  {
    TempDirectory temporary;
    (void)airfix::io::commitDurableDocumentPair(temporary.directory(), names,
                                                inspection, first);
    ReplaceContext after{
        .mode = ReplaceContext::Mode::replaceThenThrow,
        .target = ReplaceContext::Target::backup,
    };
    const airfix::io::testing::DurableDocumentPairHooks hooks{
        .replace = replaceHook,
        .retryDurability = retryHook,
        .context = &after,
    };
    requireError(
        [&] {
          (void)airfix::io::testing::commitDurableDocumentPairWithHooks(
              temporary.directory(), names, inspection, second, hooks);
        },
        airfix::io::DurableDocumentPairErrorKind::commitUnknown,
        temporary.directory());
    require(read(temporary.file(names.current)) ==
                    std::vector(first.begin(), first.end()) &&
                read(temporary.file(names.backup)) ==
                    std::vector(first.begin(), first.end()),
            "backup commit-unknown advanced the current document");
  }
}

void testInvalidConfigurationAndCandidate() {
  TempDirectory temporary;
  const airfix::io::DurableDocumentInspection noInspector{
      .maximumBytes = 16U,
  };
  requireError(
      [&] {
        (void)airfix::io::readDurableDocument(temporary.file("x"), noInspector);
      },
      airfix::io::DurableDocumentPairErrorKind::invalidArgument,
      temporary.directory());

  auto duplicateNames = names;
  duplicateNames.backup = duplicateNames.current;
  requireError(
      [&] {
        (void)airfix::io::commitDurableDocumentPair(
            temporary.directory(), duplicateNames, inspection,
            std::array<std::uint8_t, 2U>{1U, 1U});
      },
      airfix::io::DurableDocumentPairErrorKind::invalidArgument,
      temporary.directory());

  const std::array nulName{'b', 'a', 'd', '\0', 'x'};
  auto invalidNames = names;
  invalidNames.current = std::string_view(nulName.data(), nulName.size());
  requireError(
      [&] {
        (void)airfix::io::commitDurableDocumentPair(
            temporary.directory(), invalidNames, inspection,
            std::array<std::uint8_t, 2U>{1U, 1U});
      },
      airfix::io::DurableDocumentPairErrorKind::invalidArgument,
      temporary.directory());
  requireError(
      [&] {
        (void)airfix::io::commitDurableDocumentPair(
            temporary.directory(), names, inspection,
            std::array<std::uint8_t, 2U>{0U, 1U});
      },
      airfix::io::DurableDocumentPairErrorKind::invalidArgument,
      temporary.directory());
  requireError(
      [&] {
        (void)airfix::io::commitDurableDocumentPair(
            std::filesystem::path("relative"), names, inspection,
            std::array<std::uint8_t, 2U>{1U, 1U});
      },
      airfix::io::DurableDocumentPairErrorKind::invalidDirectory,
      temporary.directory());
}

} // namespace

int main() {
  try {
    testInspectionAndDirectoryStates();
    testCommitUnchangedRotationAndInvalidReplacement();
    testFuturePreservationAndUnsafeEntries();
    testPreparedEntryCleanupAndSafety();
    testFaultMatrixAndCommitUnknown();
    testBackupFaultMatrix();
    testInvalidConfigurationAndCandidate();
    std::cout << "Durable document-pair tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Durable document-pair tests failed: " << error.what() << '\n';
    return 1;
  }
}
