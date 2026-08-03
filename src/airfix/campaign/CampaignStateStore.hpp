#pragma once

#include "airfix/campaign/CampaignStateCodec.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace airfix::campaign {

enum class CampaignStateFileStatus : std::uint8_t {
  notInspected = 0,
  missing,
  valid,
  futureSchema,
  malformed,
  oversized,
  wrongTypeOrLinked,
  ioUnavailable,
};

struct CampaignStateFileDiagnostic final {
  CampaignStateFileStatus status{CampaignStateFileStatus::notInspected};
  std::optional<std::uint32_t> schemaVersion;

  [[nodiscard]] friend constexpr bool
  operator==(const CampaignStateFileDiagnostic &,
             const CampaignStateFileDiagnostic &) noexcept = default;
};

enum class CampaignStateLoadSource : std::uint8_t {
  defaults = 0,
  current,
  backup,
};

struct CampaignStateLoadResult final {
  CampaignStateRecord state;
  CampaignStateLoadSource source{CampaignStateLoadSource::defaults};
  CampaignStateFileDiagnostic current;
  CampaignStateFileDiagnostic backup;
  bool persistenceBlocked{};

  [[nodiscard]] constexpr bool recoveredFromBackup() const noexcept {
    return source == CampaignStateLoadSource::backup;
  }
};

// A non-blocked backup/default recovery after retained invalid content should
// be repaired by an explicitly serialized host save. Clean first-run absence,
// a valid current, and future/unsafe blocked stores never request repair.
[[nodiscard]] constexpr bool
campaignStateNeedsRepair(const CampaignStateLoadResult &load) noexcept {
  if (load.persistenceBlocked ||
      load.source == CampaignStateLoadSource::current) {
    return false;
  }
  if (load.source == CampaignStateLoadSource::backup) {
    return true;
  }
  return load.current.status != CampaignStateFileStatus::missing ||
         load.backup.status != CampaignStateFileStatus::missing;
}

enum class CampaignStateSaveStatus : std::uint8_t {
  unchanged = 0,
  committed,
  committedAfterReadback,
};

struct CampaignStateSaveResult final {
  CampaignStateSaveStatus status{CampaignStateSaveStatus::unchanged};
  bool backupRotated{};
};

enum class CampaignStateStoreErrorKind : std::uint8_t {
  invalidDirectory = 0,
  invalidState,
  persistenceBlocked,
  saveFailed,
  commitUnknown,
};

class CampaignStateStoreError final : public std::runtime_error {
public:
  CampaignStateStoreError(CampaignStateStoreErrorKind kind,
                          std::optional<CampaignStateRecord> requestedState,
                          const char *message);

  [[nodiscard]] CampaignStateStoreErrorKind kind() const noexcept {
    return kind_;
  }

  // A commit-unknown caller can perform a fresh load and compare semantic
  // state without receiving host paths or encoded bytes from the error.
  [[nodiscard]] const std::optional<CampaignStateRecord> &
  requestedState() const noexcept {
    return requestedState_;
  }

private:
  CampaignStateStoreErrorKind kind_;
  std::optional<CampaignStateRecord> requestedState_;
};

// campaignDirectory is an injected absolute application-private leaf already
// partitioned by the host for the selected profile. The store deliberately
// knows no callsign, profile identifier, platform root, or legacy filename.
// Missing content selects the canonical empty schema-1 state. Diagnostics and
// exceptions contain no resolved path or record bytes.
[[nodiscard]] CampaignStateLoadResult
loadCampaignState(const std::filesystem::path &campaignDirectory);

// The caller serializes access to one injected leaf. A valid current is
// rotated before the new candidate is atomically published. Invalid current
// content is never promoted. Valid future schemas and unsafe/unavailable
// entries remain untouched and block downgrade writes.
[[nodiscard]] CampaignStateSaveResult
saveCampaignState(const std::filesystem::path &campaignDirectory,
                  const CampaignStateRecord &candidate);

namespace testing {

struct CampaignStateStoreHooks final {
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

[[nodiscard]] CampaignStateSaveResult
saveCampaignStateWithHooks(const std::filesystem::path &campaignDirectory,
                           const CampaignStateRecord &candidate,
                           const CampaignStateStoreHooks &hooks);

} // namespace testing

} // namespace airfix::campaign
