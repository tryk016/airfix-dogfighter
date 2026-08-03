#pragma once

#include "airfix/assets/LegacyRosterImporter.hpp"
#include "airfix/campaign/CampaignState.hpp"

#include <cstdint>

namespace airfix::campaign {

enum class LegacyCampaignStateImportStatus : std::uint8_t {
  imported = 0,
  sourceNotImported,
  unsupportedThreadValue,
};

// AFCS intentionally contains only the recovered numeric campaign subset.
// These flags report validated legacy data that this adapter did not copy.
// They contain no profile strings, medal identifiers, record IDs, or payloads.
struct LegacyCampaignStateImportRemainder final {
  bool profileName{};
  bool portrait{};
  bool medals{};
  bool unknownRecords{};

  [[nodiscard]] constexpr bool any() const noexcept {
    return profileName || portrait || medals || unknownRecords;
  }

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyCampaignStateImportRemainder &,
             const LegacyCampaignStateImportRemainder &) noexcept = default;
};

struct LegacyCampaignStateImportResult final {
  LegacyCampaignStateImportStatus status{
      LegacyCampaignStateImportStatus::sourceNotImported};
  CampaignStateRecord state;
  LegacyCampaignStateImportRemainder omitted;

  [[nodiscard]] constexpr bool imported() const noexcept {
    return status == LegacyCampaignStateImportStatus::imported;
  }

  // A failed conversion or any omitted supplemental record requires the host
  // to retain the legacy source. The adapter never deletes or rewrites it.
  [[nodiscard]] constexpr bool requiresLegacySourceRetention() const noexcept {
    return !imported() || omitted.any();
  }
};

// Converts only an already validated root-zero roster import. Presence and the
// full signed int32 domain are copied exactly for the recovered numeric fields.
// THRD is accepted only when absent, Axis (0), or Allied (1). Profile identity,
// medals, and unknown records are reported as omitted and never interpreted.
// No filesystem, persistence, migration-completion, or runtime policy lives at
// this boundary.
[[nodiscard]] LegacyCampaignStateImportResult importLegacyCampaignState(
    const assets::LegacyRosterImportResult &source) noexcept;

} // namespace airfix::campaign
