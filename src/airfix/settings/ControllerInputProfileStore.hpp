#pragma once

#include "airfix/input/ControllerInputProfile.hpp"
#include "airfix/settings/ControllerInputProfileCodec.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace airfix::settings {

enum class ControllerInputProfileFileStatus : std::uint8_t {
  notInspected,
  missing,
  valid,
  futureSchema,
  malformed,
  oversized,
  wrongTypeOrLinked,
  ioUnavailable,
};

struct ControllerInputProfileFileDiagnostic final {
  ControllerInputProfileFileStatus status{
      ControllerInputProfileFileStatus::notInspected};
  std::optional<std::uint32_t> schemaVersion;

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerInputProfileFileDiagnostic &,
             const ControllerInputProfileFileDiagnostic &) noexcept = default;
};

enum class ControllerInputProfileLoadSource : std::uint8_t {
  defaults,
  current,
  backup,
};

struct ControllerInputProfileLoadResult final {
  input::ResolvedControllerInputProfile profile;
  ControllerInputProfileLoadSource source{
      ControllerInputProfileLoadSource::defaults};
  ControllerInputProfileFileDiagnostic current;
  ControllerInputProfileFileDiagnostic backup;
  bool persistenceBlocked{};

  [[nodiscard]] constexpr bool recoveredFromBackup() const noexcept {
    return source == ControllerInputProfileLoadSource::backup;
  }
};

// True when a valid backup or canonical default was selected after a
// non-missing current/backup document. A read-only future-schema boundary
// cannot be repaired by this process and a valid current needs no repair.
[[nodiscard]] constexpr bool controllerInputProfileNeedsRepair(
    const ControllerInputProfileLoadResult &load) noexcept {
  if (load.persistenceBlocked ||
      load.source == ControllerInputProfileLoadSource::current) {
    return false;
  }
  if (load.source == ControllerInputProfileLoadSource::backup) {
    return true;
  }
  return load.current.status != ControllerInputProfileFileStatus::missing ||
         load.backup.status != ControllerInputProfileFileStatus::missing;
}

enum class ControllerInputProfileSaveStatus : std::uint8_t {
  unchanged,
  committed,
  committedAfterReadback,
};

struct ControllerInputProfileSaveResult final {
  ControllerInputProfileSaveStatus status{
      ControllerInputProfileSaveStatus::unchanged};
  bool backupRotated{};
};

enum class ControllerInputProfileStoreErrorKind : std::uint8_t {
  invalidDirectory,
  invalidProfile,
  persistenceBlocked,
  saveFailed,
  commitUnknown,
};

class ControllerInputProfileStoreError final : public std::runtime_error {
public:
  ControllerInputProfileStoreError(
      ControllerInputProfileStoreErrorKind kind,
      std::optional<input::ControllerInputProfileRecord> requestedRecord,
      const char *message);

  [[nodiscard]] ControllerInputProfileStoreErrorKind kind() const noexcept {
    return kind_;
  }

  // A commit-unknown caller can perform a fresh load and compare semantic
  // state without receiving any host path or encoded record bytes from the
  // error.
  [[nodiscard]] const std::optional<input::ControllerInputProfileRecord> &
  requestedRecord() const noexcept {
    return requestedRecord_;
  }

private:
  ControllerInputProfileStoreErrorKind kind_;
  std::optional<input::ControllerInputProfileRecord> requestedRecord_;
};

// settingsDirectory is an injected absolute application-private leaf.
// Production adapters derive it from SDL's preference directory or iOS
// Application Support. Missing, malformed, or oversized current documents
// select a validated backup and then the canonical resolved default. No
// diagnostic or exception from this API contains a resolved host path.
[[nodiscard]] ControllerInputProfileLoadResult
loadControllerInputProfile(const std::filesystem::path &settingsDirectory);

// The caller serializes access to one directory. A valid current document is
// rotated to backup before the new candidate is atomically published. A
// malformed current is never promoted. Bounded, envelope-integrity-valid
// future-schema documents remain byte-exact on disk and block downgrade
// writes.
[[nodiscard]] ControllerInputProfileSaveResult saveControllerInputProfile(
    const std::filesystem::path &settingsDirectory,
    const input::ControllerInputProfileRecord &candidate);

namespace testing {

struct ControllerInputProfileStoreHooks final {
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

[[nodiscard]] ControllerInputProfileSaveResult
saveControllerInputProfileWithHooks(
    const std::filesystem::path &settingsDirectory,
    const input::ControllerInputProfileRecord &candidate,
    const ControllerInputProfileStoreHooks &hooks);

} // namespace testing

} // namespace airfix::settings
