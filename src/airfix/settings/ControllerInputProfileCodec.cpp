#include "airfix/settings/ControllerInputProfileCodec.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace airfix::settings {
namespace {

constexpr std::array<std::uint8_t, 4U> magic{'A', 'F', 'I', 'P'};
constexpr std::uint16_t envelopeVersion = 1U;
constexpr std::size_t prefixBytes = 16U;
constexpr std::size_t digestBytes = 32U;
constexpr std::size_t headerBytes = prefixBytes + digestBytes;
constexpr std::size_t fieldHeaderBytes = 4U;
constexpr std::uint16_t axesField = 1U;
constexpr std::uint16_t bindingsField = 2U;
constexpr std::size_t axisRecordBytes = 8U;
constexpr std::size_t bindingRecordBytes = 12U;
constexpr std::size_t axesPayloadBytes =
    input::controllerProfileAxisCount * axisRecordBytes;
constexpr std::size_t minimumDocumentBytes =
    headerBytes + fieldHeaderBytes + axesPayloadBytes + fieldHeaderBytes + 1U;
constexpr std::size_t maximumCanonicalDocumentBytes =
    minimumDocumentBytes +
    (input::controllerProfileBindingCapacity * bindingRecordBytes);

static_assert(input::controllerProfileAxisCount == 4U);
static_assert(input::controllerProfileBindingCapacity <=
              std::numeric_limits<std::uint8_t>::max());
static_assert(maximumCanonicalDocumentBytes <=
              maximumControllerInputProfileDocumentBytes);

[[noreturn]] void fail(const ControllerInputProfileCodecErrorKind kind,
                       const std::optional<std::uint32_t> schemaVersion,
                       const char *const message) {
  throw ControllerInputProfileCodecError(kind, schemaVersion, message);
}

void appendU16(std::vector<std::uint8_t> &output, const std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendI16(std::vector<std::uint8_t> &output, const std::int16_t value) {
  appendU16(output, std::bit_cast<std::uint16_t>(value));
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

[[nodiscard]] std::int16_t readI16(const std::span<const std::uint8_t> bytes,
                                   const std::size_t offset) noexcept {
  return std::bit_cast<std::int16_t>(readU16(bytes, offset));
}

[[nodiscard]] std::uint32_t readU32(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) noexcept {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
  }
  return value;
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

void appendAxisRecord(std::vector<std::uint8_t> &output,
                      const input::ControllerAxisCalibrationRecord &record) {
  appendU16(output, record.innerDeadzoneQ15);
  appendU16(output, record.outerSaturationQ15);
  appendU16(output, record.sensitivityPermille);
  output.push_back(static_cast<std::uint8_t>(record.responseCurve));
  output.push_back(record.inverted);
}

void appendBindingRecord(std::vector<std::uint8_t> &output,
                         const input::ControllerBindingRecord &record) {
  output.push_back(static_cast<std::uint8_t>(record.sourceKind));
  appendU16(output, record.control.value);
  output.push_back(static_cast<std::uint8_t>(record.physicalKind));
  output.push_back(static_cast<std::uint8_t>(record.targetKind));
  output.push_back(record.target);
  output.push_back(record.contexts);
  appendI16(output, record.scale);
  appendI16(output, record.meaningfulThreshold);
  output.push_back(record.blocksNeutralGate);
}

[[nodiscard]] input::ControllerAxisCalibrationRecord
readAxisRecord(const std::span<const std::uint8_t> bytes,
               const std::size_t offset) noexcept {
  return {
      .innerDeadzoneQ15 = readU16(bytes, offset),
      .outerSaturationQ15 = readU16(bytes, offset + 2U),
      .sensitivityPermille = readU16(bytes, offset + 4U),
      .responseCurve =
          static_cast<input::ControllerResponseCurve>(bytes[offset + 6U]),
      .inverted = bytes[offset + 7U],
  };
}

[[nodiscard]] input::ControllerBindingRecord
readBindingRecord(const std::span<const std::uint8_t> bytes,
                  const std::size_t offset) noexcept {
  return {
      .sourceKind = static_cast<input::SourceKind>(bytes[offset]),
      .control = input::ControlId{readU16(bytes, offset + 1U)},
      .physicalKind = static_cast<input::PhysicalEventKind>(bytes[offset + 3U]),
      .targetKind = static_cast<input::BindingTargetKind>(bytes[offset + 4U]),
      .target = bytes[offset + 5U],
      .contexts = bytes[offset + 6U],
      .scale = readI16(bytes, offset + 7U),
      .meaningfulThreshold = readI16(bytes, offset + 9U),
      .blocksNeutralGate = bytes[offset + 11U],
  };
}

} // namespace

ControllerInputProfileCodecError::ControllerInputProfileCodecError(
    const ControllerInputProfileCodecErrorKind kind,
    const std::optional<std::uint32_t> schemaVersion, const char *const message)
    : std::runtime_error(message), kind_(kind), schemaVersion_(schemaVersion) {}

std::vector<std::uint8_t> encodeControllerInputProfileDocument(
    const input::ControllerInputProfileRecord &record) {
  if (record.schemaVersion !=
      input::controllerInputProfileRecordSchemaVersion) {
    fail(ControllerInputProfileCodecErrorKind::invalidSchemaVersion,
         record.schemaVersion,
         "AFIP encoder accepts only the current semantic schema");
  }
  const auto resolved = input::resolveControllerInputProfile(record);
  if (!resolved.complete()) {
    fail(ControllerInputProfileCodecErrorKind::invalidSemanticRecord,
         record.schemaVersion, "AFIP semantic record is invalid");
  }

  const auto bindingPayloadBytes =
      1U + (static_cast<std::size_t>(record.bindingCount) * bindingRecordBytes);
  if (bindingPayloadBytes > std::numeric_limits<std::uint16_t>::max()) {
    throw std::logic_error("AFIP binding payload is not representable");
  }
  const auto documentBytes = headerBytes + fieldHeaderBytes + axesPayloadBytes +
                             fieldHeaderBytes + bindingPayloadBytes;

  std::vector<std::uint8_t> output;
  output.reserve(documentBytes);
  output.insert(output.end(), magic.begin(), magic.end());
  appendU16(output, envelopeVersion);
  appendU16(output, 0U);
  appendU32(output, record.schemaVersion);
  appendU32(output, static_cast<std::uint32_t>(documentBytes));
  output.resize(headerBytes, 0U);

  appendFieldHeader(output, axesField,
                    static_cast<std::uint16_t>(axesPayloadBytes));
  for (const auto &axis : record.axes) {
    appendAxisRecord(output, axis);
  }

  appendFieldHeader(output, bindingsField,
                    static_cast<std::uint16_t>(bindingPayloadBytes));
  output.push_back(record.bindingCount);
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    appendBindingRecord(output, record.bindings[index]);
  }

  if (output.size() != documentBytes) {
    throw std::logic_error("AFIP encoder produced a noncanonical size");
  }
  const auto digest = documentDigest(output);
  std::copy(digest.begin(), digest.end(),
            output.begin() + static_cast<std::ptrdiff_t>(prefixBytes));
  return output;
}

DecodedControllerInputProfileDocument
decodeControllerInputProfileDocument(const std::span<const std::uint8_t> bytes,
                                     const std::size_t maximumBytes) {
  if (maximumBytes < headerBytes ||
      maximumBytes > maximumControllerInputProfileDocumentBytes) {
    throw std::invalid_argument("AFIP maximum byte limit is invalid");
  }
  if (bytes.size() > maximumBytes) {
    fail(ControllerInputProfileCodecErrorKind::tooLarge, std::nullopt,
         "AFIP document exceeds its byte limit");
  }
  if (bytes.size() < headerBytes) {
    fail(ControllerInputProfileCodecErrorKind::tooSmall, std::nullopt,
         "AFIP document is shorter than its envelope");
  }
  if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
    fail(ControllerInputProfileCodecErrorKind::badMagic, std::nullopt,
         "AFIP magic is invalid");
  }
  if (readU16(bytes, 4U) != envelopeVersion) {
    fail(ControllerInputProfileCodecErrorKind::unsupportedEnvelopeVersion,
         std::nullopt, "AFIP envelope version is unsupported");
  }
  if (readU16(bytes, 6U) != 0U) {
    fail(ControllerInputProfileCodecErrorKind::unsupportedFlags, std::nullopt,
         "AFIP envelope flags are unsupported");
  }

  const auto schemaVersion = readU32(bytes, 8U);
  if (schemaVersion == 0U) {
    fail(ControllerInputProfileCodecErrorKind::invalidSchemaVersion,
         schemaVersion, "AFIP semantic schema version is invalid");
  }
  if (readU32(bytes, 12U) != bytes.size()) {
    fail(ControllerInputProfileCodecErrorKind::declaredSizeMismatch,
         schemaVersion, "AFIP declared size does not match the exact input");
  }
  const auto digest = documentDigest(bytes);
  if (!std::equal(digest.begin(), digest.end(),
                  bytes.begin() + static_cast<std::ptrdiff_t>(prefixBytes))) {
    fail(ControllerInputProfileCodecErrorKind::integrityMismatch, schemaVersion,
         "AFIP integrity check failed");
  }

  if (schemaVersion > input::controllerInputProfileRecordSchemaVersion) {
    return OpaqueFutureControllerInputProfileRecord{
        .schemaVersion = schemaVersion,
        .exactBytes = {bytes.begin(), bytes.end()},
    };
  }
  if (schemaVersion != input::controllerInputProfileRecordSchemaVersion) {
    fail(ControllerInputProfileCodecErrorKind::invalidSchemaVersion,
         schemaVersion, "AFIP semantic schema version is unsupported");
  }

  input::ControllerInputProfileRecord record{
      .schemaVersion = schemaVersion,
  };
  std::array<bool, bindingsField + 1U> seen{};
  std::uint16_t previousField = 0U;
  std::size_t offset = headerBytes;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < fieldHeaderBytes) {
      fail(ControllerInputProfileCodecErrorKind::trailingData, schemaVersion,
           "AFIP has bytes after its last complete field");
    }
    const auto field = readU16(bytes, offset);
    const auto size = readU16(bytes, offset + 2U);
    offset += fieldHeaderBytes;
    if (field == previousField) {
      fail(ControllerInputProfileCodecErrorKind::duplicateField, schemaVersion,
           "AFIP contains a duplicate field");
    }
    if (field < previousField) {
      fail(ControllerInputProfileCodecErrorKind::nonCanonicalFieldOrder,
           schemaVersion, "AFIP fields are not in canonical order");
    }
    if (field == 0U || field > bindingsField) {
      fail(ControllerInputProfileCodecErrorKind::unknownField, schemaVersion,
           "AFIP contains an unknown current-schema field");
    }
    previousField = field;
    if (seen[field]) {
      fail(ControllerInputProfileCodecErrorKind::duplicateField, schemaVersion,
           "AFIP contains a duplicate field");
    }
    seen[field] = true;
    if (size > bytes.size() - offset) {
      fail(ControllerInputProfileCodecErrorKind::truncatedField, schemaVersion,
           "AFIP field payload is truncated");
    }

    if (field == axesField) {
      if (size != axesPayloadBytes) {
        fail(ControllerInputProfileCodecErrorKind::invalidFieldSize,
             schemaVersion, "AFIP axis field size is invalid");
      }
      for (std::size_t index = 0U; index < input::controllerProfileAxisCount;
           ++index) {
        record.axes[index] =
            readAxisRecord(bytes, offset + index * axisRecordBytes);
      }
    } else {
      if (size < 1U ||
          (static_cast<std::size_t>(size) - 1U) % bindingRecordBytes != 0U) {
        fail(ControllerInputProfileCodecErrorKind::invalidFieldSize,
             schemaVersion, "AFIP binding field size is invalid");
      }
      const auto encodedCount =
          (static_cast<std::size_t>(size) - 1U) / bindingRecordBytes;
      const auto declaredCount = static_cast<std::size_t>(bytes[offset]);
      if (encodedCount != declaredCount ||
          declaredCount > input::controllerProfileBindingCapacity) {
        fail(ControllerInputProfileCodecErrorKind::invalidFieldSize,
             schemaVersion, "AFIP binding count is inconsistent");
      }
      record.bindingCount = static_cast<std::uint8_t>(declaredCount);
      for (std::size_t index = 0U; index < declaredCount; ++index) {
        record.bindings[index] =
            readBindingRecord(bytes, offset + 1U + index * bindingRecordBytes);
      }
    }
    offset += size;
  }

  for (std::uint16_t field = axesField; field <= bindingsField; ++field) {
    if (!seen[field]) {
      fail(ControllerInputProfileCodecErrorKind::missingField, schemaVersion,
           "AFIP is missing a required field");
    }
  }
  if (offset != bytes.size()) {
    fail(ControllerInputProfileCodecErrorKind::trailingData, schemaVersion,
         "AFIP has trailing data");
  }
  if (!input::resolveControllerInputProfile(record).complete()) {
    fail(ControllerInputProfileCodecErrorKind::invalidSemanticRecord,
         schemaVersion, "AFIP semantic record is invalid");
  }
  return record;
}

} // namespace airfix::settings
