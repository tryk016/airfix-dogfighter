#include "airfix/campaign/LegacyCampaignStateImport.hpp"

namespace airfix::campaign {

LegacyCampaignStateImportResult importLegacyCampaignState(
    const assets::LegacyRosterImportResult &source) noexcept {
  if (!source.imported()) {
    return {};
  }

  LegacyCampaignStateImportResult result{
      .status = LegacyCampaignStateImportStatus::imported,
      .state = {},
      .omitted =
          {
              .profileName = source.roster.name.has_value(),
              .portrait = source.roster.portrait.has_value(),
              .medals = !source.roster.medals.empty(),
              .unknownRecords = !source.roster.unknownRecords.empty(),
          },
  };

  if (source.roster.thread.has_value()) {
    switch (*source.roster.thread) {
    case 0:
      result.state.storedThread = CampaignSide::axis;
      break;
    case 1:
      result.state.storedThread = CampaignSide::allied;
      break;
    default:
      result.status = LegacyCampaignStateImportStatus::unsupportedThreadValue;
      result.state = {};
      return result;
    }
  }

  result.state.axisMaximum = source.roster.axisMaximum;
  result.state.alliedMaximum = source.roster.alliedMaximum;
  result.state.cumulativeScore = source.roster.score;
  result.state.acki = source.roster.acki;
  result.state.guki = source.roster.guki;
  result.state.wuki = source.roster.wuki;
  result.state.fook = source.roster.fook;
  result.state.frik = source.roster.frik;
  result.state.deat = source.roster.deat;
  result.state.pkil = source.roster.pkil;
  result.state.pdea = source.roster.pdea;
  return result;
}

} // namespace airfix::campaign
