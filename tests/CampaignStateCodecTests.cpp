#include "airfix/campaign/CampaignStateCodec.hpp"
#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

using namespace airfix::campaign;

constexpr std::size_t kPrefixBytes = 16U;
constexpr std::size_t kHeaderBytes = 48U;
constexpr std::size_t kProgressFieldOffset = 48U;
constexpr std::size_t kProgressPayloadOffset = 52U;
constexpr std::size_t kCountersFieldOffset = 72U;
constexpr std::size_t kCountersPayloadOffset = 76U;
constexpr std::size_t kCanonicalBytes = 112U;

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

void storeU16(std::vector<std::uint8_t> &bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void storeU32(std::vector<std::uint8_t> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void storeI32(std::vector<std::uint8_t> &bytes, const std::size_t offset,
              const std::int32_t value) {
  storeU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint16_t readU16(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t readU32(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

void refreshEnvelope(std::vector<std::uint8_t> &bytes) {
  require(bytes.size() >= kHeaderBytes,
          "test mutation made AFCS shorter than its envelope");
  storeU32(bytes, 12U, static_cast<std::uint32_t>(bytes.size()));
  airfix::crypto::Sha256 hash;
  hash.update(std::span<const std::uint8_t>(bytes).first(kPrefixBytes));
  hash.update(std::span<const std::uint8_t>(bytes).subspan(kHeaderBytes));
  const auto digest = hash.finish();
  std::copy(digest.begin(), digest.end(), bytes.begin() + kPrefixBytes);
}

[[nodiscard]] CampaignStateRecord populatedRecord() {
  return {
      .schemaVersion = campaignStateSchemaVersion,
      .storedThread = CampaignSide::allied,
      .axisMaximum = std::numeric_limits<std::int32_t>::min(),
      .alliedMaximum = std::numeric_limits<std::int32_t>::max(),
      .cumulativeScore = -1,
      .acki = 0,
      .guki = 1,
      .wuki = -2,
      .fook = 3,
      .frik = -4,
      .deat = 5,
      .pkil = -6,
      .pdea = 7,
  };
}

void expectCodecError(
    const std::span<const std::uint8_t> bytes,
    const CampaignStateCodecErrorKind kind,
    const std::optional<std::uint32_t> schemaVersion, const char *const message,
    const std::size_t maximumBytes = maximumCampaignStateDocumentBytes) {
  try {
    (void)decodeCampaignStateDocument(bytes, maximumBytes);
  } catch (const CampaignStateCodecError &error) {
    require(error.kind() == kind && error.schemaVersion() == schemaVersion,
            message);
    return;
  }
  fail(message);
}

void testCanonicalRoundTripAndLayout() {
  const auto record = populatedRecord();
  const auto bytes = encodeCampaignStateDocument(record);
  require(bytes.size() == kCanonicalBytes && bytes[0U] == 'A' &&
              bytes[1U] == 'F' && bytes[2U] == 'C' && bytes[3U] == 'S' &&
              readU16(bytes, 4U) == 1U && readU16(bytes, 6U) == 0U &&
              readU32(bytes, 8U) == campaignStateSchemaVersion &&
              readU32(bytes, 12U) == kCanonicalBytes,
          "AFCS canonical envelope changed");
  require(readU16(bytes, kProgressFieldOffset) == 1U &&
              readU16(bytes, kProgressFieldOffset + 2U) == 20U &&
              readU32(bytes, kProgressPayloadOffset) == 0x0FU &&
              readU16(bytes, kCountersFieldOffset) == 2U &&
              readU16(bytes, kCountersFieldOffset + 2U) == 36U &&
              readU32(bytes, kCountersPayloadOffset) == 0xFFU,
          "AFCS canonical fields or presence masks changed");
  require(readU32(bytes, kProgressPayloadOffset + 4U) == 1U &&
              readU32(bytes, kProgressPayloadOffset + 8U) == 0x80000000U &&
              readU32(bytes, kProgressPayloadOffset + 12U) == 0x7FFFFFFFU &&
              readU32(bytes, kProgressPayloadOffset + 16U) == 0xFFFFFFFFU &&
              readU32(bytes, kCountersPayloadOffset + 4U) == 0U &&
              readU32(bytes, kCountersPayloadOffset + 8U) == 1U &&
              readU32(bytes, kCountersPayloadOffset + 12U) == 0xFFFFFFFEU &&
              readU32(bytes, kCountersPayloadOffset + 16U) == 3U &&
              readU32(bytes, kCountersPayloadOffset + 20U) == 0xFFFFFFFCU &&
              readU32(bytes, kCountersPayloadOffset + 24U) == 5U &&
              readU32(bytes, kCountersPayloadOffset + 28U) == 0xFFFFFFFAU &&
              readU32(bytes, kCountersPayloadOffset + 32U) == 7U,
          "AFCS canonical slot order or signed encoding changed");
  const auto decoded = decodeCampaignStateDocument(bytes);
  require(std::holds_alternative<CampaignStateRecord>(decoded) &&
              std::get<CampaignStateRecord>(decoded) == record,
          "AFCS populated signed record did not round-trip");
  require(encodeCampaignStateDocument(record) == bytes,
          "AFCS encoding is not deterministic");
}

void testAbsentValuesAreCanonicalAndRoundTrip() {
  const CampaignStateRecord empty{};
  const auto bytes = encodeCampaignStateDocument(empty);
  require(
      bytes.size() == kCanonicalBytes &&
          readU32(bytes, kProgressPayloadOffset) == 0U &&
          readU32(bytes, kCountersPayloadOffset) == 0U &&
          std::all_of(bytes.begin() + kProgressPayloadOffset + 4U,
                      bytes.begin() + kCountersFieldOffset,
                      [](const std::uint8_t value) { return value == 0U; }) &&
          std::all_of(bytes.begin() + kCountersPayloadOffset + 4U, bytes.end(),
                      [](const std::uint8_t value) { return value == 0U; }),
      "AFCS absent values did not use canonical zero slots");
  const auto decoded = decodeCampaignStateDocument(bytes);
  require(std::get<CampaignStateRecord>(decoded) == empty,
          "AFCS absent optional values were materialized");
}

void testFutureSchemaIsPreservedWithoutFieldInterpretation() {
  auto bytes = encodeCampaignStateDocument(populatedRecord());
  storeU32(bytes, 8U, campaignStateSchemaVersion + 1U);
  storeU16(bytes, kProgressFieldOffset, 0xFFFFU);
  refreshEnvelope(bytes);

  const auto decoded = decodeCampaignStateDocument(bytes);
  require(std::holds_alternative<OpaqueFutureCampaignStateDocument>(decoded),
          "future AFCS schema was interpreted as current");
  const auto &future = std::get<OpaqueFutureCampaignStateDocument>(decoded);
  require(future.schemaVersion == campaignStateSchemaVersion + 1U &&
              future.exactBytes == bytes,
          "future AFCS bytes were not preserved exactly");
}

void testEnvelopeAndLimitFailures() {
  const auto canonical = encodeCampaignStateDocument(populatedRecord());

  expectCodecError(std::span<const std::uint8_t>(canonical).first(47U),
                   CampaignStateCodecErrorKind::tooSmall, std::nullopt,
                   "short AFCS envelope was accepted");
  expectCodecError(canonical, CampaignStateCodecErrorKind::tooLarge,
                   std::nullopt, "AFCS caller limit was ignored",
                   canonical.size() - 1U);
  const std::vector<std::uint8_t> oversized(
      maximumCampaignStateDocumentBytes + 1U, 0U);
  expectCodecError(oversized, CampaignStateCodecErrorKind::tooLarge,
                   std::nullopt, "AFCS default byte limit was ignored");

  auto mutated = canonical;
  mutated[0U] = 'X';
  expectCodecError(mutated, CampaignStateCodecErrorKind::badMagic, std::nullopt,
                   "bad AFCS magic was accepted");

  mutated = canonical;
  storeU16(mutated, 4U, 2U);
  expectCodecError(mutated,
                   CampaignStateCodecErrorKind::unsupportedEnvelopeVersion,
                   std::nullopt, "future AFCS envelope was guessed");

  mutated = canonical;
  storeU16(mutated, 6U, 1U);
  expectCodecError(mutated, CampaignStateCodecErrorKind::unsupportedFlags,
                   std::nullopt, "AFCS flags were ignored");

  mutated = canonical;
  storeU32(mutated, 8U, 0U);
  expectCodecError(mutated, CampaignStateCodecErrorKind::invalidSchemaVersion,
                   0U, "zero AFCS schema was accepted");

  mutated = canonical;
  storeU32(mutated, 12U, static_cast<std::uint32_t>(mutated.size() - 1U));
  expectCodecError(mutated, CampaignStateCodecErrorKind::declaredSizeMismatch,
                   campaignStateSchemaVersion,
                   "AFCS declared-size mismatch was accepted");

  mutated = canonical;
  mutated.back() ^= 0x80U;
  expectCodecError(mutated, CampaignStateCodecErrorKind::integrityMismatch,
                   campaignStateSchemaVersion,
                   "AFCS digest mismatch was accepted");

  try {
    (void)decodeCampaignStateDocument(canonical, kHeaderBytes - 1U);
    fail("too-small AFCS configured maximum was accepted");
  } catch (const std::invalid_argument &) {
  }
  try {
    (void)decodeCampaignStateDocument(canonical,
                                      maximumCampaignStateDocumentBytes + 1U);
    fail("oversized AFCS configured maximum was accepted");
  } catch (const std::invalid_argument &) {
  }
}

void testCurrentSchemaFieldFailures() {
  const auto canonical = encodeCampaignStateDocument(populatedRecord());
  auto mutated = canonical;

  mutated.resize(kCountersFieldOffset);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::missingField,
                   campaignStateSchemaVersion,
                   "AFCS missing counters field was accepted");

  mutated = canonical;
  mutated.push_back(0U);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::trailingData,
                   campaignStateSchemaVersion,
                   "AFCS trailing byte was accepted");

  mutated = canonical;
  storeU16(mutated, kProgressFieldOffset, 3U);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::unknownField,
                   campaignStateSchemaVersion,
                   "AFCS unknown current field was accepted");

  mutated = canonical;
  storeU16(mutated, kCountersFieldOffset, 1U);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::duplicateField,
                   campaignStateSchemaVersion,
                   "AFCS duplicate field was accepted");

  mutated.assign(canonical.begin(),
                 canonical.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes));
  mutated.insert(mutated.end(),
                 canonical.begin() +
                     static_cast<std::ptrdiff_t>(kCountersFieldOffset),
                 canonical.end());
  mutated.insert(
      mutated.end(),
      canonical.begin() + static_cast<std::ptrdiff_t>(kProgressFieldOffset),
      canonical.begin() + static_cast<std::ptrdiff_t>(kCountersFieldOffset));
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::nonCanonicalFieldOrder,
                   campaignStateSchemaVersion,
                   "AFCS reordered fields were accepted");

  mutated = canonical;
  storeU16(mutated, kProgressFieldOffset + 2U, 19U);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::invalidFieldSize,
                   campaignStateSchemaVersion,
                   "AFCS resized progress field was accepted");

  mutated = canonical;
  storeU16(mutated, kCountersFieldOffset + 2U, 37U);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::truncatedField,
                   campaignStateSchemaVersion,
                   "AFCS truncated counters field was accepted");
}

void testPresenceAndSemanticFailures() {
  auto mutated = encodeCampaignStateDocument(CampaignStateRecord{});
  storeU32(mutated, kProgressPayloadOffset, 0x10U);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::invalidPresenceMask,
                   campaignStateSchemaVersion,
                   "AFCS reserved progress mask bit was accepted");

  mutated = encodeCampaignStateDocument(CampaignStateRecord{});
  storeU32(mutated, kCountersPayloadOffset, 0x100U);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::invalidPresenceMask,
                   campaignStateSchemaVersion,
                   "AFCS reserved counters mask bit was accepted");

  mutated = encodeCampaignStateDocument(CampaignStateRecord{});
  storeI32(mutated, kProgressPayloadOffset + 8U, 1);
  refreshEnvelope(mutated);
  expectCodecError(mutated,
                   CampaignStateCodecErrorKind::nonCanonicalAbsentValue,
                   campaignStateSchemaVersion,
                   "AFCS absent maximum retained a noncanonical backing value");

  mutated = encodeCampaignStateDocument(CampaignStateRecord{});
  storeI32(mutated, kCountersPayloadOffset + 4U, -1);
  refreshEnvelope(mutated);
  expectCodecError(mutated,
                   CampaignStateCodecErrorKind::nonCanonicalAbsentValue,
                   campaignStateSchemaVersion,
                   "AFCS absent counter retained a noncanonical backing value");

  mutated = encodeCampaignStateDocument(populatedRecord());
  storeI32(mutated, kProgressPayloadOffset + 4U, 2);
  refreshEnvelope(mutated);
  expectCodecError(mutated, CampaignStateCodecErrorKind::invalidSemanticRecord,
                   campaignStateSchemaVersion,
                   "AFCS invalid present campaign side was accepted");

  auto invalid = populatedRecord();
  invalid.schemaVersion = campaignStateSchemaVersion + 1U;
  try {
    (void)encodeCampaignStateDocument(invalid);
    fail("AFCS encoder accepted a future semantic schema");
  } catch (const CampaignStateCodecError &error) {
    require(error.kind() == CampaignStateCodecErrorKind::invalidSchemaVersion,
            "AFCS encoder reported the wrong schema failure");
  }

  invalid = populatedRecord();
  invalid.storedThread = static_cast<CampaignSide>(2U);
  try {
    (void)encodeCampaignStateDocument(invalid);
    fail("AFCS encoder accepted a forged campaign side");
  } catch (const CampaignStateCodecError &error) {
    require(error.kind() == CampaignStateCodecErrorKind::invalidSemanticRecord,
            "AFCS encoder reported the wrong semantic failure");
  }
}

} // namespace

int main() {
  testCanonicalRoundTripAndLayout();
  testAbsentValuesAreCanonicalAndRoundTrip();
  testFutureSchemaIsPreservedWithoutFieldInterpretation();
  testEnvelopeAndLimitFailures();
  testCurrentSchemaFieldFailures();
  testPresenceAndSemanticFailures();
  std::cout << "Campaign state codec tests passed\n";
  return EXIT_SUCCESS;
}
