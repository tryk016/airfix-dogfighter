#include "airfix/settings/TouchControlsPreferencesCodec.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>

namespace airfix::settings {
namespace {

constexpr std::array<std::uint8_t, 4U> magic{'A', 'F', 'T', 'C'};
constexpr std::uint16_t envelopeVersion = 1U;
constexpr std::size_t prefixBytes = 16U;
constexpr std::size_t digestBytes = 32U;
constexpr std::size_t headerBytes = prefixBytes + digestBytes;
constexpr std::size_t fieldHeaderBytes = 4U;
constexpr std::uint16_t handednessField = 1U;
constexpr std::uint16_t densityField = 2U;
constexpr std::uint16_t restingOpacityField = 3U;
constexpr std::uint16_t visibilityModeField = 4U;
constexpr std::uint16_t hapticsModeField = 5U;
constexpr std::size_t canonicalDocumentBytes =
    headerBytes + (fieldHeaderBytes + 1U) * 5U;

static_assert(canonicalDocumentBytes == 73U);

[[noreturn]] void fail(const TouchControlsPreferencesCodecErrorKind kind,
                       const std::optional<std::uint32_t> schemaVersion,
                       const char *const message) {
  throw TouchControlsPreferencesCodecError(kind, schemaVersion, message);
}

void appendU16(std::vector<std::uint8_t> &output, const std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(std::vector<std::uint8_t> &output, const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    output.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
  }
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
  for (std::size_t index = 0U; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
}

void appendField(std::vector<std::uint8_t> &output, const std::uint16_t field,
                 const std::uint8_t value) {
  appendU16(output, field);
  appendU16(output, 1U);
  output.push_back(value);
}

[[nodiscard]] crypto::Sha256Digest
documentDigest(const std::span<const std::uint8_t> bytes) {
  crypto::Sha256 hash;
  hash.update(bytes.first(prefixBytes));
  hash.update(bytes.subspan(headerBytes));
  return hash.finish();
}

} // namespace

TouchControlsPreferencesCodecError::TouchControlsPreferencesCodecError(
    const TouchControlsPreferencesCodecErrorKind kind,
    const std::optional<std::uint32_t> schemaVersion, const char *const message)
    : std::runtime_error(message), kind_(kind), schemaVersion_(schemaVersion) {}

std::vector<std::uint8_t> encodeTouchControlsPreferencesDocument(
    const input::TouchControlsPreferencesRecord &record) {
  if (record.schemaVersion !=
      input::touchControlsPreferencesRecordSchemaVersion) {
    fail(TouchControlsPreferencesCodecErrorKind::invalidSchemaVersion,
         record.schemaVersion,
         "AFTC encoder accepts only the current semantic schema");
  }
  if (!input::touchControlsPreferencesFromRecord(record).complete()) {
    fail(TouchControlsPreferencesCodecErrorKind::invalidSemanticRecord,
         record.schemaVersion, "AFTC semantic record is invalid");
  }

  std::vector<std::uint8_t> output;
  output.reserve(canonicalDocumentBytes);
  output.insert(output.end(), magic.begin(), magic.end());
  appendU16(output, envelopeVersion);
  appendU16(output, 0U);
  appendU32(output, record.schemaVersion);
  appendU32(output, static_cast<std::uint32_t>(canonicalDocumentBytes));
  output.resize(headerBytes, 0U);
  appendField(output, handednessField, record.handedness);
  appendField(output, densityField, record.density);
  appendField(output, restingOpacityField, record.restingOpacityPercent);
  appendField(output, visibilityModeField, record.visibilityMode);
  appendField(output, hapticsModeField, record.hapticsMode);

  if (output.size() != canonicalDocumentBytes) {
    throw std::logic_error("AFTC encoder produced a noncanonical size");
  }
  const auto digest = documentDigest(output);
  std::copy(digest.begin(), digest.end(),
            output.begin() + static_cast<std::ptrdiff_t>(prefixBytes));
  return output;
}

DecodedTouchControlsPreferencesDocument decodeTouchControlsPreferencesDocument(
    const std::span<const std::uint8_t> bytes, const std::size_t maximumBytes) {
  if (maximumBytes < headerBytes ||
      maximumBytes > maximumTouchControlsPreferencesDocumentBytes) {
    throw std::invalid_argument("AFTC maximum byte limit is invalid");
  }
  if (bytes.size() > maximumBytes) {
    fail(TouchControlsPreferencesCodecErrorKind::tooLarge, std::nullopt,
         "AFTC document exceeds its byte limit");
  }
  if (bytes.size() < headerBytes) {
    fail(TouchControlsPreferencesCodecErrorKind::tooSmall, std::nullopt,
         "AFTC document is shorter than its envelope");
  }
  if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
    fail(TouchControlsPreferencesCodecErrorKind::badMagic, std::nullopt,
         "AFTC magic is invalid");
  }
  if (readU16(bytes, 4U) != envelopeVersion) {
    fail(TouchControlsPreferencesCodecErrorKind::unsupportedEnvelopeVersion,
         std::nullopt, "AFTC envelope version is unsupported");
  }
  if (readU16(bytes, 6U) != 0U) {
    fail(TouchControlsPreferencesCodecErrorKind::unsupportedFlags, std::nullopt,
         "AFTC envelope flags are unsupported");
  }

  const auto schemaVersion = readU32(bytes, 8U);
  if (schemaVersion == 0U) {
    fail(TouchControlsPreferencesCodecErrorKind::invalidSchemaVersion,
         schemaVersion, "AFTC semantic schema version is invalid");
  }
  if (readU32(bytes, 12U) != bytes.size()) {
    fail(TouchControlsPreferencesCodecErrorKind::declaredSizeMismatch,
         schemaVersion, "AFTC declared size does not match the exact input");
  }
  const auto digest = documentDigest(bytes);
  if (!std::equal(digest.begin(), digest.end(),
                  bytes.begin() + static_cast<std::ptrdiff_t>(prefixBytes))) {
    fail(TouchControlsPreferencesCodecErrorKind::integrityMismatch,
         schemaVersion, "AFTC integrity check failed");
  }
  if (schemaVersion > input::touchControlsPreferencesRecordSchemaVersion) {
    return OpaqueFutureTouchControlsPreferencesRecord{
        .schemaVersion = schemaVersion,
        .exactBytes = {bytes.begin(), bytes.end()},
    };
  }
  if (schemaVersion != input::touchControlsPreferencesRecordSchemaVersion &&
      schemaVersion !=
          input::legacyTouchControlsPreferencesRecordSchemaVersion &&
      schemaVersion !=
          input::visibilityTouchControlsPreferencesRecordSchemaVersion) {
    fail(TouchControlsPreferencesCodecErrorKind::invalidSchemaVersion,
         schemaVersion, "AFTC semantic schema version is unsupported");
  }

  std::uint16_t lastField = hapticsModeField;
  if (schemaVersion ==
      input::legacyTouchControlsPreferencesRecordSchemaVersion) {
    lastField = restingOpacityField;
  } else if (schemaVersion ==
             input::visibilityTouchControlsPreferencesRecordSchemaVersion) {
    lastField = visibilityModeField;
  }

  input::TouchControlsPreferencesRecord record;
  if (schemaVersion < input::touchControlsPreferencesRecordSchemaVersion) {
    // Preserve pre-haptics behavior during migration. New/default V3 records
    // use system haptics; V1/V2 become explicitly disabled until the user
    // chooses otherwise.
    record.hapticsMode = 0U;
  }
  std::array<bool, hapticsModeField + 1U> seen{};
  std::uint16_t previousField = 0U;
  std::size_t offset = headerBytes;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < fieldHeaderBytes) {
      fail(TouchControlsPreferencesCodecErrorKind::trailingData, schemaVersion,
           "AFTC has bytes after its last complete field");
    }
    const auto field = readU16(bytes, offset);
    const auto size = readU16(bytes, offset + 2U);
    offset += fieldHeaderBytes;
    if (field == previousField) {
      fail(TouchControlsPreferencesCodecErrorKind::duplicateField,
           schemaVersion, "AFTC contains a duplicate field");
    }
    if (field < previousField) {
      fail(TouchControlsPreferencesCodecErrorKind::nonCanonicalFieldOrder,
           schemaVersion, "AFTC fields are not in canonical order");
    }
    if (field == 0U || field > lastField) {
      fail(TouchControlsPreferencesCodecErrorKind::unknownField, schemaVersion,
           "AFTC contains an unknown current-schema field");
    }
    previousField = field;
    if (seen[field]) {
      fail(TouchControlsPreferencesCodecErrorKind::duplicateField,
           schemaVersion, "AFTC contains a duplicate field");
    }
    seen[field] = true;
    if (size > bytes.size() - offset) {
      fail(TouchControlsPreferencesCodecErrorKind::truncatedField,
           schemaVersion, "AFTC field payload is truncated");
    }
    if (size != 1U) {
      fail(TouchControlsPreferencesCodecErrorKind::invalidFieldSize,
           schemaVersion, "AFTC field size is invalid");
    }
    switch (field) {
    case handednessField:
      record.handedness = bytes[offset];
      break;
    case densityField:
      record.density = bytes[offset];
      break;
    case restingOpacityField:
      record.restingOpacityPercent = bytes[offset];
      break;
    case visibilityModeField:
      record.visibilityMode = bytes[offset];
      break;
    case hapticsModeField:
      record.hapticsMode = bytes[offset];
      break;
    default:
      throw std::logic_error("validated AFTC field is unreachable");
    }
    offset += size;
  }
  for (std::uint16_t field = handednessField; field <= lastField; ++field) {
    if (!seen[field]) {
      fail(TouchControlsPreferencesCodecErrorKind::missingField, schemaVersion,
           "AFTC is missing a required field");
    }
  }
  if (offset != bytes.size()) {
    fail(TouchControlsPreferencesCodecErrorKind::trailingData, schemaVersion,
         "AFTC has trailing data");
  }
  if (!input::touchControlsPreferencesFromRecord(record).complete()) {
    fail(TouchControlsPreferencesCodecErrorKind::invalidSemanticRecord,
         schemaVersion, "AFTC semantic record is invalid");
  }
  return DecodedCurrentTouchControlsPreferencesRecord{
      .record = record,
      .sourceSchemaVersion = schemaVersion,
  };
}

} // namespace airfix::settings
