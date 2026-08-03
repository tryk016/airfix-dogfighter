#include "airfix/assets/AssetPrimitives.hpp"
#include "airfix/assets/LegacyRosterImporter.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace airfix::assets;

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

void appendU32(std::vector<std::uint8_t> &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void storeU32(std::vector<std::uint8_t> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
  bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

[[nodiscard]] std::vector<std::uint8_t>
integerPayload(const std::int32_t value) {
  std::vector<std::uint8_t> payload;
  appendU32(payload, std::bit_cast<std::uint32_t>(value));
  return payload;
}

[[nodiscard]] std::vector<std::uint8_t>
stringPayload(const std::string_view value) {
  std::vector<std::uint8_t> payload(value.begin(), value.end());
  payload.push_back(0U);
  return payload;
}

[[nodiscard]] std::vector<std::uint8_t>
medalPayload(const std::string_view identifier, const std::int32_t value) {
  auto payload = stringPayload(identifier);
  appendU32(payload, std::bit_cast<std::uint32_t>(value));
  return payload;
}

class RosterBuilder final {
public:
  explicit RosterBuilder(const std::uint32_t root = 0U) {
    appendU32(bytes_, root);
    appendU32(bytes_, 0U);
  }

  RosterBuilder &record(const std::uint32_t id,
                        const std::span<const std::uint8_t> payload) {
    appendU32(bytes_, id);
    appendU32(bytes_, static_cast<std::uint32_t>(payload.size()));
    bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    storeU32(bytes_, 4U, static_cast<std::uint32_t>(bytes_.size() - 8U));
    return *this;
  }

  [[nodiscard]] std::vector<std::uint8_t> finish() const { return bytes_; }

private:
  std::vector<std::uint8_t> bytes_;
};

void requireEmptyFailure(const LegacyRosterImportResult &result,
                         const LegacyRosterImportStatus status,
                         const char *const message) {
  require(result.status == status && !result.imported() &&
              result.roster == LegacyRoster{},
          message);
}

void testImportsRecoveredRecordsAndUnknownDescriptors() {
  const auto name = stringPayload("Ace");
  const auto portrait = stringPayload("pilot09");
  const auto medalOne = medalPayload("medal_rookie", 1);
  const auto medalTwo = medalPayload("medal_test", -7);
  const auto axis = integerPayload(0);
  const auto alliedMaximum = integerPayload(10);
  const auto score = integerPayload(std::numeric_limits<std::int32_t>::min());
  const auto stat = integerPayload(42);
  const std::vector<std::uint8_t> opaque{1U, 2U, 3U};

  const auto bytes = RosterBuilder{}
                         .record(fourCC('N', 'A', 'M', 'E'), name)
                         .record(fourCC('P', 'I', 'C', 'T'), portrait)
                         .record(fourCC('M', 'E', 'D', 'A'), medalOne)
                         .record(fourCC('M', 'E', 'D', 'A'), medalTwo)
                         .record(fourCC('T', 'H', 'R', 'D'), axis)
                         .record(fourCC('A', 'L', 'M', 'I'), alliedMaximum)
                         .record(fourCC('S', 'C', 'O', 'R'), score)
                         .record(fourCC('A', 'C', 'K', 'I'), stat)
                         .record(fourCC('G', 'U', 'K', 'I'), stat)
                         .record(fourCC('W', 'U', 'K', 'I'), stat)
                         .record(fourCC('F', 'O', 'O', 'K'), stat)
                         .record(fourCC('F', 'R', 'I', 'K'), stat)
                         .record(fourCC('D', 'E', 'A', 'T'), stat)
                         .record(fourCC('P', 'K', 'I', 'L'), stat)
                         .record(fourCC('P', 'D', 'E', 'A'), stat)
                         .record(fourCC('X', 'T', 'R', 'A'), opaque)
                         .finish();

  const auto result = importLegacyRoster(bytes);
  require(result.imported(), "valid recovered roster records were rejected");
  require(result.roster.name == "Ace" && result.roster.portrait == "pilot09" &&
              result.roster.medals.size() == 2U &&
              result.roster.medals[0] == LegacyRosterMedal{"medal_rookie", 1} &&
              result.roster.medals[1] == LegacyRosterMedal{"medal_test", -7},
          "profile strings or repeatable MEDA records changed");
  require(result.roster.thread == 0 && !result.roster.axisMaximum.has_value() &&
              result.roster.alliedMaximum == 10 &&
              result.roster.score == std::numeric_limits<std::int32_t>::min(),
          "campaign or signed score records changed");
  require(result.roster.acki == 42 && result.roster.guki == 42 &&
              result.roster.wuki == 42 && result.roster.fook == 42 &&
              result.roster.frik == 42 && result.roster.deat == 42 &&
              result.roster.pkil == 42 && result.roster.pdea == 42,
          "neutral cumulative records changed");
  require(result.roster.unknownRecords ==
              std::vector<LegacyRosterUnknownRecord>{
                  {.id = fourCC('X', 'T', 'R', 'A'), .payloadSize = 3U}},
          "unknown record payload was copied or its descriptor changed");
}

void testAbsentRecordsRemainAbsent() {
  const auto result = importLegacyRoster(RosterBuilder{}.finish());
  require(result.imported() && result.roster == LegacyRoster{},
          "empty root-zero roster did not preserve absent fields");
}

void testStructuralAndRootFailuresAreTransactional() {
  const auto unsupported = importLegacyRoster(RosterBuilder{1U}.finish());
  requireEmptyFailure(unsupported, LegacyRosterImportStatus::unsupportedRoot,
                      "unsupported root was accepted");

  auto shortHeader = std::vector<std::uint8_t>(7U, 0U);
  requireEmptyFailure(importLegacyRoster(shortHeader),
                      LegacyRosterImportStatus::invalidContainer,
                      "short root published partial state");

  auto badRootSize = RosterBuilder{}.finish();
  storeU32(badRootSize, 4U, 1U);
  requireEmptyFailure(importLegacyRoster(badRootSize),
                      LegacyRosterImportStatus::invalidContainer,
                      "root-size mismatch published partial state");

  const auto validName = stringPayload("Ace");
  auto truncatedPayload =
      RosterBuilder{}.record(fourCC('N', 'A', 'M', 'E'), validName).finish();
  truncatedPayload.pop_back();
  storeU32(truncatedPayload, 4U,
           static_cast<std::uint32_t>(truncatedPayload.size() - 8U));
  requireEmptyFailure(importLegacyRoster(truncatedPayload),
                      LegacyRosterImportStatus::invalidContainer,
                      "truncated child published a preceding record");
}

void testKnownRecordPoliciesFailClosed() {
  const auto name = stringPayload("Ace");
  const auto duplicate = RosterBuilder{}
                             .record(fourCC('N', 'A', 'M', 'E'), name)
                             .record(fourCC('N', 'A', 'M', 'E'), name)
                             .finish();
  requireEmptyFailure(importLegacyRoster(duplicate),
                      LegacyRosterImportStatus::duplicateSingleton,
                      "duplicate singleton used an unproven winner policy");

  const std::vector<std::uint8_t> unterminated{'A', 'c', 'e'};
  const auto badString =
      RosterBuilder{}.record(fourCC('N', 'A', 'M', 'E'), unterminated).finish();
  requireEmptyFailure(importLegacyRoster(badString),
                      LegacyRosterImportStatus::invalidString,
                      "unterminated string was accepted");

  const std::vector<std::uint8_t> wrongInteger{1U, 2U, 3U};
  const auto badInteger =
      RosterBuilder{}.record(fourCC('T', 'H', 'R', 'D'), wrongInteger).finish();
  requireEmptyFailure(importLegacyRoster(badInteger),
                      LegacyRosterImportStatus::invalidInteger,
                      "non-int32 campaign record was accepted");

  const auto badMedal = stringPayload("medal_without_value");
  const auto medal =
      RosterBuilder{}.record(fourCC('M', 'E', 'D', 'A'), badMedal).finish();
  requireEmptyFailure(importLegacyRoster(medal),
                      LegacyRosterImportStatus::invalidMedal,
                      "MEDA without trailing int32 was accepted");
}

void testConfiguredLimitsAreEnforced() {
  const auto name = stringPayload("Ace");
  const auto medal = medalPayload("medal", 1);
  const std::vector<std::uint8_t> unknown{1U};
  const auto bytes = RosterBuilder{}
                         .record(fourCC('N', 'A', 'M', 'E'), name)
                         .record(fourCC('M', 'E', 'D', 'A'), medal)
                         .record(fourCC('X', '0', '0', '1'), unknown)
                         .record(fourCC('X', '0', '0', '2'), unknown)
                         .finish();

  auto limits = LegacyRosterImportLimits{};
  limits.maximumInputBytes = bytes.size() - 1U;
  requireEmptyFailure(importLegacyRoster(bytes, limits),
                      LegacyRosterImportStatus::inputTooLarge,
                      "input byte limit was ignored");

  limits = {};
  limits.maximumRecords = 2U;
  requireEmptyFailure(importLegacyRoster(bytes, limits),
                      LegacyRosterImportStatus::invalidContainer,
                      "record limit was ignored");

  limits = {};
  limits.maximumStringBytes = 2U;
  requireEmptyFailure(importLegacyRoster(bytes, limits),
                      LegacyRosterImportStatus::invalidString,
                      "string limit was ignored");

  limits = {};
  limits.maximumMedals = 1U;
  const auto twoMedals = RosterBuilder{}
                             .record(fourCC('M', 'E', 'D', 'A'), medal)
                             .record(fourCC('M', 'E', 'D', 'A'), medal)
                             .finish();
  requireEmptyFailure(importLegacyRoster(twoMedals, limits),
                      LegacyRosterImportStatus::medalLimitExceeded,
                      "MEDA count limit was ignored");

  limits = {};
  limits.maximumUnknownRecords = 1U;
  requireEmptyFailure(importLegacyRoster(bytes, limits),
                      LegacyRosterImportStatus::unknownRecordLimitExceeded,
                      "unknown-record limit was ignored");

  limits = {};
  limits.maximumRecords = 0U;
  requireEmptyFailure(importLegacyRoster(bytes, limits),
                      LegacyRosterImportStatus::invalidLimits,
                      "invalid limits reached the parser");
}

} // namespace

int main() {
  testImportsRecoveredRecordsAndUnknownDescriptors();
  testAbsentRecordsRemainAbsent();
  testStructuralAndRootFailuresAreTransactional();
  testKnownRecordPoliciesFailClosed();
  testConfiguredLimitsAreEnforced();
  std::cout << "Legacy roster importer tests passed\n";
  return EXIT_SUCCESS;
}
