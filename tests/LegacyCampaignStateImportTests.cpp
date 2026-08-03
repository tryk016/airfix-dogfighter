#include "airfix/campaign/CampaignStateCodec.hpp"
#include "airfix/campaign/LegacyCampaignStateImport.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {

using airfix::assets::LegacyRosterImportResult;
using airfix::assets::LegacyRosterImportStatus;
using airfix::campaign::CampaignSide;
using airfix::campaign::CampaignStateRecord;
using airfix::campaign::LegacyCampaignStateImportRemainder;
using airfix::campaign::LegacyCampaignStateImportStatus;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] LegacyRosterImportResult importedSource() {
  return {.status = LegacyRosterImportStatus::imported};
}

void testRejectedSourcePublishesNothing() {
  LegacyRosterImportResult source;
  source.status = LegacyRosterImportStatus::invalidContainer;
  source.roster.thread = 1;
  source.roster.score = 99;
  source.roster.name = "must-not-be-observed";

  const auto result = airfix::campaign::importLegacyCampaignState(source);
  require(result.status == LegacyCampaignStateImportStatus::sourceNotImported &&
              !result.imported() && result.state == CampaignStateRecord{} &&
              result.omitted == LegacyCampaignStateImportRemainder{} &&
              result.requiresLegacySourceRetention(),
          "rejected roster import published unvalidated state");
}

void testEmptyImportPreservesAbsence() {
  const auto result =
      airfix::campaign::importLegacyCampaignState(importedSource());
  require(result.imported() && result.state == CampaignStateRecord{} &&
              !result.omitted.any() &&
              !result.requiresLegacySourceRetention() &&
              airfix::campaign::validCampaignStateRecord(result.state),
          "empty validated roster did not become canonical empty AFCS state");
}

void testAllNumericFieldsMapExactly() {
  auto source = importedSource();
  source.roster.thread = 0;
  source.roster.axisMaximum = std::numeric_limits<std::int32_t>::min();
  source.roster.alliedMaximum = std::numeric_limits<std::int32_t>::max();
  source.roster.score = 0;
  source.roster.acki = -1;
  source.roster.guki = 1;
  source.roster.wuki = -2;
  source.roster.fook = 2;
  source.roster.frik = -3;
  source.roster.deat = 3;
  source.roster.pkil = -4;
  source.roster.pdea = 4;

  const CampaignStateRecord expected{
      .storedThread = CampaignSide::axis,
      .axisMaximum = std::numeric_limits<std::int32_t>::min(),
      .alliedMaximum = std::numeric_limits<std::int32_t>::max(),
      .cumulativeScore = 0,
      .acki = -1,
      .guki = 1,
      .wuki = -2,
      .fook = 2,
      .frik = -3,
      .deat = 3,
      .pkil = -4,
      .pdea = 4,
  };
  const auto result = airfix::campaign::importLegacyCampaignState(source);
  require(result.imported() && result.state == expected &&
              !result.requiresLegacySourceRetention(),
          "validated numeric legacy fields did not map exactly");

  const auto encoded =
      airfix::campaign::encodeCampaignStateDocument(result.state);
  const auto decoded = airfix::campaign::decodeCampaignStateDocument(encoded);
  require(std::holds_alternative<CampaignStateRecord>(decoded) &&
              std::get<CampaignStateRecord>(decoded) == expected,
          "imported numeric state did not survive canonical AFCS round trip");
}

void testBothTypedSides() {
  auto axis = importedSource();
  axis.roster.thread = 0;
  auto allied = importedSource();
  allied.roster.thread = 1;

  require(
      airfix::campaign::importLegacyCampaignState(axis).state.storedThread ==
              CampaignSide::axis &&
          airfix::campaign::importLegacyCampaignState(allied)
                  .state.storedThread == CampaignSide::allied,
      "legacy THRD did not map to the exact typed side");
}

void testUnsupportedThreadFailsClosed() {
  constexpr std::array invalidValues{std::numeric_limits<std::int32_t>::min(),
                                     -1, 2,
                                     std::numeric_limits<std::int32_t>::max()};
  for (const auto value : invalidValues) {
    auto source = importedSource();
    source.roster.thread = value;
    source.roster.score = 123;
    const auto result = airfix::campaign::importLegacyCampaignState(source);
    require(result.status ==
                    LegacyCampaignStateImportStatus::unsupportedThreadValue &&
                !result.imported() && result.state == CampaignStateRecord{} &&
                result.requiresLegacySourceRetention(),
            "unsupported legacy THRD was normalized or partially published");
  }
}

void testSupplementalRecordsRequireRetention() {
  auto source = importedSource();
  source.roster.thread = 1;
  source.roster.score = 77;
  source.roster.name = "synthetic-pilot";
  source.roster.portrait = "synthetic-portrait";
  source.roster.medals.push_back({"synthetic-medal", 1});
  source.roster.unknownRecords.push_back({0x12345678U, 17U});

  const auto result = airfix::campaign::importLegacyCampaignState(source);
  require(result.imported() &&
              result.state.storedThread == CampaignSide::allied &&
              result.state.cumulativeScore == 77 &&
              result.omitted ==
                  LegacyCampaignStateImportRemainder{
                      .profileName = true,
                      .portrait = true,
                      .medals = true,
                      .unknownRecords = true,
                  } &&
              result.requiresLegacySourceRetention(),
          "supplemental legacy records were not reported as omitted");
}

void testRemainderCategoriesAreIndependent() {
  const auto check = [](LegacyRosterImportResult source,
                        const LegacyCampaignStateImportRemainder expected) {
    const auto result = airfix::campaign::importLegacyCampaignState(source);
    require(result.imported() && result.omitted == expected &&
                result.requiresLegacySourceRetention(),
            "legacy remainder category was not independently reported");
  };

  auto named = importedSource();
  named.roster.name = "name";
  check(std::move(named), {.profileName = true});

  auto portrait = importedSource();
  portrait.roster.portrait = "portrait";
  check(std::move(portrait), {.portrait = true});

  auto medals = importedSource();
  medals.roster.medals.push_back({"medal", -1});
  check(std::move(medals), {.medals = true});

  auto unknown = importedSource();
  unknown.roster.unknownRecords.push_back({1U, 0U});
  check(std::move(unknown), {.unknownRecords = true});
}

static_assert(noexcept(airfix::campaign::importLegacyCampaignState(
    std::declval<const LegacyRosterImportResult &>())));

} // namespace

int main() {
  try {
    testRejectedSourcePublishesNothing();
    testEmptyImportPreservesAbsence();
    testAllNumericFieldsMapExactly();
    testBothTypedSides();
    testUnsupportedThreadFailsClosed();
    testSupplementalRecordsRequireRetention();
    testRemainderCategoriesAreIndependent();
    std::cout << "Legacy campaign-state import tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Legacy campaign-state import tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
