#include "airfix/campaign/CampaignStateCodec.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::campaign {
namespace {

constexpr std::array<std::uint8_t, 4U> magic{'A', 'F', 'C', 'S'};
constexpr std::uint16_t envelopeVersion = 1U;
constexpr std::size_t prefixBytes = 16U;
constexpr std::size_t digestBytes = 32U;
constexpr std::size_t headerBytes = prefixBytes + digestBytes;
constexpr std::size_t fieldHeaderBytes = 4U;
constexpr std::uint16_t progressField = 1U;
constexpr std::uint16_t countersField = 2U;
constexpr std::size_t progressSlotCount = 4U;
constexpr std::size_t counterSlotCount = 8U;
constexpr std::size_t progressPayloadBytes =
    sizeof(std::uint32_t) + progressSlotCount * sizeof(std::int32_t);
constexpr std::size_t countersPayloadBytes =
    sizeof(std::uint32_t) + counterSlotCount * sizeof(std::int32_t);
constexpr std::size_t canonicalDocumentBytes =
    headerBytes + fieldHeaderBytes + progressPayloadBytes + fieldHeaderBytes +
    countersPayloadBytes;
constexpr std::uint32_t progressPresenceMask = 0x0FU;
constexpr std::uint32_t countersPresenceMask = 0xFFU;

static_assert(canonicalDocumentBytes == 112U);
static_assert(canonicalDocumentBytes <= maximumCampaignStateDocumentBytes);
static_assert(sizeof(std::int32_t) == sizeof(std::uint32_t));

[[noreturn]] void fail(const CampaignStateCodecErrorKind kind,
                       const std::optional<std::uint32_t> schemaVersion,
                       const char *const message) {
  throw CampaignStateCodecError(kind, schemaVersion, message);
}

void appendU16(std::vector<std::uint8_t> &output, const std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(std::vector<std::uint8_t> &output, const std::uint32_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    output.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
  }
}

void appendI32(std::vector<std::uint8_t> &output, const std::int32_t value) {
  appendU32(output, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint16_t readU16(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t readU32(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) noexcept {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::int32_t readI32(const std::span<const std::uint8_t> bytes,
                                   const std::size_t offset) noexcept {
  return std::bit_cast<std::int32_t>(readU32(bytes, offset));
}

void appendFieldHeader(std::vector<std::uint8_t> &output,
                       const std::uint16_t field, const std::uint16_t size) {
  appendU16(output, field);
  appendU16(output, size);
}

[[nodiscard]] crypto::Sha256Digest
documentDigest(const std::span<const std::uint8_t> bytes) {
  crypto::Sha256 hash;
  hash.update(bytes.first(prefixBytes));
  hash.update(bytes.subspan(headerBytes));
  return hash.finish();
}

[[nodiscard]] constexpr std::uint32_t
presenceBit(const std::size_t index) noexcept {
  return std::uint32_t{1U} << index;
}

[[nodiscard]] std::uint32_t presenceMask(
    const std::span<const std::optional<std::int32_t>> values) noexcept {
  std::uint32_t mask = 0U;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (values[index].has_value()) {
      mask |= presenceBit(index);
    }
  }
  return mask;
}

void appendOptionalValues(
    std::vector<std::uint8_t> &output,
    const std::span<const std::optional<std::int32_t>> values) {
  appendU32(output, presenceMask(values));
  for (const auto &value : values) {
    appendI32(output, value.value_or(0));
  }
}

void decodeOptionalValue(std::optional<std::int32_t> &destination,
                         const std::uint32_t mask, const std::size_t index,
                         const std::int32_t value,
                         const std::uint32_t schemaVersion) {
  if ((mask & presenceBit(index)) != 0U) {
    destination = value;
    return;
  }
  if (value != 0) {
    fail(CampaignStateCodecErrorKind::nonCanonicalAbsentValue, schemaVersion,
         "AFCS absent field has a nonzero backing value");
  }
}

[[nodiscard]] std::array<std::optional<std::int32_t>, counterSlotCount>
counterValues(const CampaignStateRecord &record) {
  return {record.acki, record.guki, record.wuki, record.fook,
          record.frik, record.deat, record.pkil, record.pdea};
}

void decodeProgress(CampaignStateRecord &record,
                    const std::span<const std::uint8_t> payload,
                    const std::uint32_t schemaVersion) {
  const auto mask = readU32(payload, 0U);
  if ((mask & ~progressPresenceMask) != 0U) {
    fail(CampaignStateCodecErrorKind::invalidPresenceMask, schemaVersion,
         "AFCS progress presence mask has reserved bits");
  }

  const auto thread = readI32(payload, sizeof(std::uint32_t));
  if ((mask & presenceBit(0U)) != 0U) {
    if (thread == static_cast<std::int32_t>(CampaignSide::axis)) {
      record.storedThread = CampaignSide::axis;
    } else if (thread == static_cast<std::int32_t>(CampaignSide::allied)) {
      record.storedThread = CampaignSide::allied;
    } else {
      fail(CampaignStateCodecErrorKind::invalidSemanticRecord, schemaVersion,
           "AFCS stored campaign side is invalid");
    }
  } else if (thread != 0) {
    fail(CampaignStateCodecErrorKind::nonCanonicalAbsentValue, schemaVersion,
         "AFCS absent campaign side has a nonzero backing value");
  }

  decodeOptionalValue(record.axisMaximum, mask, 1U,
                      readI32(payload, 2U * sizeof(std::uint32_t)),
                      schemaVersion);
  decodeOptionalValue(record.alliedMaximum, mask, 2U,
                      readI32(payload, 3U * sizeof(std::uint32_t)),
                      schemaVersion);
  decodeOptionalValue(record.cumulativeScore, mask, 3U,
                      readI32(payload, 4U * sizeof(std::uint32_t)),
                      schemaVersion);
}

void decodeCounters(CampaignStateRecord &record,
                    const std::span<const std::uint8_t> payload,
                    const std::uint32_t schemaVersion) {
  const auto mask = readU32(payload, 0U);
  if ((mask & ~countersPresenceMask) != 0U) {
    fail(CampaignStateCodecErrorKind::invalidPresenceMask, schemaVersion,
         "AFCS counters presence mask has reserved bits");
  }
  const std::array<std::optional<std::int32_t> *, counterSlotCount>
      destinations{&record.acki, &record.guki, &record.wuki, &record.fook,
                   &record.frik, &record.deat, &record.pkil, &record.pdea};
  for (std::size_t index = 0U; index < destinations.size(); ++index) {
    decodeOptionalValue(*destinations[index], mask, index,
                        readI32(payload, (index + 1U) * sizeof(std::uint32_t)),
                        schemaVersion);
  }
}

} // namespace

CampaignStateCodecError::CampaignStateCodecError(
    const CampaignStateCodecErrorKind kind,
    const std::optional<std::uint32_t> schemaVersion, const char *const message)
    : std::runtime_error(message), kind_(kind), schemaVersion_(schemaVersion) {}

std::vector<std::uint8_t>
encodeCampaignStateDocument(const CampaignStateRecord &record) {
  if (record.schemaVersion != campaignStateSchemaVersion) {
    fail(CampaignStateCodecErrorKind::invalidSchemaVersion,
         record.schemaVersion,
         "AFCS encoder accepts only the current semantic schema");
  }
  if (!validCampaignStateRecord(record)) {
    fail(CampaignStateCodecErrorKind::invalidSemanticRecord,
         record.schemaVersion, "AFCS semantic record is invalid");
  }

  const std::array<std::optional<std::int32_t>, progressSlotCount> progress{
      record.storedThread.has_value()
          ? std::optional<std::int32_t>{static_cast<std::int32_t>(
                *record.storedThread)}
          : std::nullopt,
      record.axisMaximum,
      record.alliedMaximum,
      record.cumulativeScore,
  };
  const auto counters = counterValues(record);

  std::vector<std::uint8_t> output;
  output.reserve(canonicalDocumentBytes);
  output.insert(output.end(), magic.begin(), magic.end());
  appendU16(output, envelopeVersion);
  appendU16(output, 0U);
  appendU32(output, record.schemaVersion);
  appendU32(output, static_cast<std::uint32_t>(canonicalDocumentBytes));
  output.resize(headerBytes, 0U);

  appendFieldHeader(output, progressField,
                    static_cast<std::uint16_t>(progressPayloadBytes));
  appendOptionalValues(output, progress);

  appendFieldHeader(output, countersField,
                    static_cast<std::uint16_t>(countersPayloadBytes));
  appendOptionalValues(output, counters);

  if (output.size() != canonicalDocumentBytes) {
    throw std::logic_error("AFCS encoder produced a noncanonical size");
  }
  const auto digest = documentDigest(output);
  std::copy(digest.begin(), digest.end(),
            output.begin() + static_cast<std::ptrdiff_t>(prefixBytes));
  return output;
}

DecodedCampaignStateDocument
decodeCampaignStateDocument(const std::span<const std::uint8_t> bytes,
                            const std::size_t maximumBytes) {
  if (maximumBytes < headerBytes ||
      maximumBytes > maximumCampaignStateDocumentBytes) {
    throw std::invalid_argument("AFCS maximum byte limit is invalid");
  }
  if (bytes.size() > maximumBytes) {
    fail(CampaignStateCodecErrorKind::tooLarge, std::nullopt,
         "AFCS document exceeds its byte limit");
  }
  if (bytes.size() < headerBytes) {
    fail(CampaignStateCodecErrorKind::tooSmall, std::nullopt,
         "AFCS document is shorter than its envelope");
  }
  if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
    fail(CampaignStateCodecErrorKind::badMagic, std::nullopt,
         "AFCS magic is invalid");
  }
  if (readU16(bytes, 4U) != envelopeVersion) {
    fail(CampaignStateCodecErrorKind::unsupportedEnvelopeVersion, std::nullopt,
         "AFCS envelope version is unsupported");
  }
  if (readU16(bytes, 6U) != 0U) {
    fail(CampaignStateCodecErrorKind::unsupportedFlags, std::nullopt,
         "AFCS envelope flags are unsupported");
  }

  const auto schemaVersion = readU32(bytes, 8U);
  if (schemaVersion == 0U) {
    fail(CampaignStateCodecErrorKind::invalidSchemaVersion, schemaVersion,
         "AFCS semantic schema version is invalid");
  }
  if (readU32(bytes, 12U) != bytes.size()) {
    fail(CampaignStateCodecErrorKind::declaredSizeMismatch, schemaVersion,
         "AFCS declared size does not match the exact input");
  }
  const auto digest = documentDigest(bytes);
  if (!std::equal(digest.begin(), digest.end(),
                  bytes.begin() + static_cast<std::ptrdiff_t>(prefixBytes))) {
    fail(CampaignStateCodecErrorKind::integrityMismatch, schemaVersion,
         "AFCS integrity check failed");
  }

  if (schemaVersion > campaignStateSchemaVersion) {
    return OpaqueFutureCampaignStateDocument{
        .schemaVersion = schemaVersion,
        .exactBytes = {bytes.begin(), bytes.end()},
    };
  }
  if (schemaVersion != campaignStateSchemaVersion) {
    fail(CampaignStateCodecErrorKind::invalidSchemaVersion, schemaVersion,
         "AFCS semantic schema version is unsupported");
  }

  CampaignStateRecord record;
  record.schemaVersion = schemaVersion;
  std::array<bool, countersField + 1U> seen{};
  std::uint16_t previousField = 0U;
  std::size_t offset = headerBytes;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < fieldHeaderBytes) {
      fail(CampaignStateCodecErrorKind::trailingData, schemaVersion,
           "AFCS has bytes after its last complete field");
    }
    const auto field = readU16(bytes, offset);
    const auto size = readU16(bytes, offset + 2U);
    offset += fieldHeaderBytes;
    if (field == previousField) {
      fail(CampaignStateCodecErrorKind::duplicateField, schemaVersion,
           "AFCS contains a duplicate field");
    }
    if (field < previousField) {
      fail(CampaignStateCodecErrorKind::nonCanonicalFieldOrder, schemaVersion,
           "AFCS fields are not in canonical order");
    }
    if (field == 0U || field > countersField) {
      fail(CampaignStateCodecErrorKind::unknownField, schemaVersion,
           "AFCS contains an unknown current-schema field");
    }
    previousField = field;
    if (seen[field]) {
      fail(CampaignStateCodecErrorKind::duplicateField, schemaVersion,
           "AFCS contains a duplicate field");
    }
    seen[field] = true;
    if (size > bytes.size() - offset) {
      fail(CampaignStateCodecErrorKind::truncatedField, schemaVersion,
           "AFCS field payload is truncated");
    }

    const auto payload = bytes.subspan(offset, size);
    if (field == progressField) {
      if (size != progressPayloadBytes) {
        fail(CampaignStateCodecErrorKind::invalidFieldSize, schemaVersion,
             "AFCS progress field size is invalid");
      }
      decodeProgress(record, payload, schemaVersion);
    } else {
      if (size != countersPayloadBytes) {
        fail(CampaignStateCodecErrorKind::invalidFieldSize, schemaVersion,
             "AFCS counters field size is invalid");
      }
      decodeCounters(record, payload, schemaVersion);
    }
    offset += size;
  }

  for (std::uint16_t field = progressField; field <= countersField; ++field) {
    if (!seen[field]) {
      fail(CampaignStateCodecErrorKind::missingField, schemaVersion,
           "AFCS is missing a required field");
    }
  }
  if (offset != bytes.size()) {
    fail(CampaignStateCodecErrorKind::trailingData, schemaVersion,
         "AFCS has trailing data");
  }
  if (!validCampaignStateRecord(record)) {
    fail(CampaignStateCodecErrorKind::invalidSemanticRecord, schemaVersion,
         "AFCS semantic record is invalid");
  }
  return record;
}

} // namespace airfix::campaign
