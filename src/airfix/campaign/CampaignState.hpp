#pragma once

#include <cstdint>
#include <optional>

namespace airfix::campaign {

enum class CampaignSide : std::uint8_t {
  axis = 0,
  allied = 1,
};

inline constexpr std::uint32_t campaignStateSchemaVersion = 1U;

struct CampaignStateRecord final {
  std::uint32_t schemaVersion{campaignStateSchemaVersion};

  std::optional<CampaignSide> storedThread;
  std::optional<std::int32_t> axisMaximum;
  std::optional<std::int32_t> alliedMaximum;
  std::optional<std::int32_t> cumulativeScore;

  // Neutral FourCC-derived names are retained until their gameplay semantics
  // and producer policies are proven.
  std::optional<std::int32_t> acki;
  std::optional<std::int32_t> guki;
  std::optional<std::int32_t> wuki;
  std::optional<std::int32_t> fook;
  std::optional<std::int32_t> frik;
  std::optional<std::int32_t> deat;
  std::optional<std::int32_t> pkil;
  std::optional<std::int32_t> pdea;

  [[nodiscard]] friend constexpr bool
  operator==(const CampaignStateRecord &,
             const CampaignStateRecord &) noexcept = default;
};

[[nodiscard]] constexpr bool
validCampaignSide(const CampaignSide side) noexcept {
  return side == CampaignSide::axis || side == CampaignSide::allied;
}

[[nodiscard]] constexpr bool
validCampaignStateRecord(const CampaignStateRecord &record) noexcept {
  return record.schemaVersion == campaignStateSchemaVersion &&
         (!record.storedThread.has_value() ||
          validCampaignSide(*record.storedThread));
}

} // namespace airfix::campaign
