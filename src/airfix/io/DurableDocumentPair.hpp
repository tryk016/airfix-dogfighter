#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace airfix::io {

// A document codec classifies bounded, byte-exact input without giving the
// transaction layer any knowledge of its schema or semantic value.
enum class DurableDocumentDisposition : std::uint8_t {
  valid,
  futurePreserve,
  replaceableInvalid,
};

using DurableDocumentInspector = DurableDocumentDisposition (*)(
    std::span<const std::uint8_t> bytes, void *context) noexcept;

struct DurableDocumentInspection final {
  // The callback may run more than once per operation for candidate, current,
  // backup, and failure readback. It must be deterministic for identical
  // bytes; context must remain valid and externally serialized for the call.
  std::size_t maximumBytes{};
  DurableDocumentInspector inspect{};
  void *context{};
};

enum class DurableDocumentFileStatus : std::uint8_t {
  missing,
  valid,
  futurePreserve,
  replaceableInvalid,
  oversized,
  wrongTypeOrLinked,
  ioUnavailable,
};

struct DurableDocumentRead final {
  DurableDocumentFileStatus status{DurableDocumentFileStatus::ioUnavailable};
  std::vector<std::uint8_t> exactBytes;
};

enum class DurableDocumentDirectoryStatus : std::uint8_t {
  ready,
  missing,
  wrongTypeOrLinked,
  unavailable,
};

struct DurableDocumentPairNames final {
  std::string_view current;
  std::string_view backup;
  std::string_view currentPrepared;
  std::string_view backupPrepared;
};

enum class DurableDocumentCommitStatus : std::uint8_t {
  unchanged,
  committed,
  committedAfterReadback,
};

struct DurableDocumentCommitResult final {
  DurableDocumentCommitStatus status{DurableDocumentCommitStatus::unchanged};
  bool backupRotated{};
};

enum class DurableDocumentPairErrorKind : std::uint8_t {
  invalidArgument,
  invalidDirectory,
  persistenceBlocked,
  saveFailed,
  commitUnknown,
};

// Messages are fixed, path-free diagnostics. Callers map the error kind into
// their public domain error and retain any requested semantic record there.
class DurableDocumentPairError final : public std::runtime_error {
public:
  DurableDocumentPairError(DurableDocumentPairErrorKind kind,
                           const char *message, bool candidateRelevant = true);

  [[nodiscard]] DurableDocumentPairErrorKind kind() const noexcept {
    return kind_;
  }

  // Unsafe pre-existing prepared entries are rejected before the candidate
  // transaction starts. Domain stores use this bit to preserve their
  // existing requested-record contract without exposing a path.
  [[nodiscard]] bool candidateRelevant() const noexcept {
    return candidateRelevant_;
  }

private:
  DurableDocumentPairErrorKind kind_;
  bool candidateRelevant_;
};

[[nodiscard]] DurableDocumentDirectoryStatus inspectDurableDocumentDirectory(
    const std::filesystem::path &directory) noexcept;

[[nodiscard]] DurableDocumentRead
readDurableDocument(const std::filesystem::path &path,
                    const DurableDocumentInspection &inspection);

[[nodiscard]] constexpr bool durableDocumentBlocksPersistence(
    const DurableDocumentFileStatus status) noexcept {
  return status == DurableDocumentFileStatus::futurePreserve ||
         status == DurableDocumentFileStatus::wrongTypeOrLinked ||
         status == DurableDocumentFileStatus::ioUnavailable;
}

// The caller serializes access to this application-private leaf. A valid
// current is copied byte-for-byte through a prepared file into backup before
// the candidate is published. Future-schema documents, unsafe entry types,
// and unavailable reads block all writes. A replaceable-invalid current is
// never promoted.
[[nodiscard]] DurableDocumentCommitResult
commitDurableDocumentPair(const std::filesystem::path &directory,
                          const DurableDocumentPairNames &names,
                          const DurableDocumentInspection &inspection,
                          std::span<const std::uint8_t> candidate);

namespace testing {

struct DurableDocumentPairHooks final {
  using ReplaceCallback = void (*)(const std::filesystem::path &prepared,
                                   const std::filesystem::path &target,
                                   void *context);
  using RetryDurabilityCallback =
      void (*)(const std::filesystem::path &target,
               const std::filesystem::path &directory, void *context);

  ReplaceCallback replace{};
  RetryDurabilityCallback retryDurability{};
  void *context{};
};

[[nodiscard]] DurableDocumentCommitResult
commitDurableDocumentPairWithHooks(const std::filesystem::path &directory,
                                   const DurableDocumentPairNames &names,
                                   const DurableDocumentInspection &inspection,
                                   std::span<const std::uint8_t> candidate,
                                   const DurableDocumentPairHooks &hooks);

} // namespace testing

} // namespace airfix::io
