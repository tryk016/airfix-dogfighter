#include "airfix/settings/RenderPresentationSettingsCodec.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace airfix::settings {
namespace {

constexpr std::array<std::uint8_t, 4U> magic{'A', 'F', 'R', 'S'};
constexpr std::uint16_t envelopeVersion = 1U;
constexpr std::size_t prefixBytes = 16U;
constexpr std::size_t digestBytes = 32U;
constexpr std::size_t headerBytes = prefixBytes + digestBytes;
constexpr std::uint16_t renderScaleField = 1U;
constexpr std::uint16_t scenePresentationField = 2U;
constexpr std::uint16_t visualProfileField = 3U;
constexpr std::uint16_t diagnosticsField = 4U;
constexpr std::uint16_t verticalFovAdjustmentField = 5U;
constexpr std::uint16_t uiScaleField = 6U;
constexpr std::uint32_t legacySchemaVersion = 1U;
constexpr std::uint32_t safeFovSchemaVersion = 2U;
constexpr std::size_t fieldHeaderBytes = 4U;
constexpr std::size_t canonicalDocumentBytes =
    headerBytes + (fieldHeaderBytes + 4U) * 3U +
    (fieldHeaderBytes + 1U) * 3U;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(canonicalDocumentBytes == 87U);

[[noreturn]] void fail(const RenderSettingsCodecErrorKind kind,
                       const std::optional<std::uint32_t> schemaVersion,
                       const char* const message) {
    throw RenderSettingsCodecError(kind, schemaVersion, message);
}

void appendU16(std::vector<std::uint8_t>& output, const std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        output.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

[[nodiscard]] std::uint16_t readU16(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
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

void appendFieldHeader(std::vector<std::uint8_t>& output, const std::uint16_t field,
                       const std::uint16_t size) {
    appendU16(output, field);
    appendU16(output, size);
}

[[nodiscard]] crypto::Sha256Digest documentDigest(const std::span<const std::uint8_t> bytes) {
    crypto::Sha256 hash;
    hash.update(bytes.first(prefixBytes));
    hash.update(bytes.subspan(headerBytes));
    return hash.finish();
}

[[nodiscard]] std::optional<render::RenderPresentationSettingsIssue>
semanticIssue(const render::RenderPresentationSettingsRecord& record) noexcept {
    const auto decoded = render::renderPresentationSettingsFromRecord(record);
    return decoded.issue;
}

} // namespace

RenderSettingsCodecError::RenderSettingsCodecError(const RenderSettingsCodecErrorKind kind,
                                                   const std::optional<std::uint32_t> schemaVersion,
                                                   const char* const message)
    : std::runtime_error(message), kind_(kind), schemaVersion_(schemaVersion) {}

std::vector<std::uint8_t>
encodeRenderSettingsDocument(const render::RenderPresentationSettingsRecord& record) {
    if (record.schemaVersion != render::renderPresentationSettingsRecordSchemaVersion) {
        fail(RenderSettingsCodecErrorKind::invalidSchemaVersion, record.schemaVersion,
             "AFRS encoder accepts only the current semantic schema");
    }
    if (semanticIssue(record).has_value()) {
        fail(RenderSettingsCodecErrorKind::invalidSemanticRecord, record.schemaVersion,
             "AFRS semantic record is invalid");
    }

    std::vector<std::uint8_t> output;
    output.reserve(canonicalDocumentBytes);
    output.insert(output.end(), magic.begin(), magic.end());
    appendU16(output, envelopeVersion);
    appendU16(output, 0U);
    appendU32(output, record.schemaVersion);
    appendU32(output, static_cast<std::uint32_t>(canonicalDocumentBytes));
    output.resize(headerBytes, 0U);

    appendFieldHeader(output, renderScaleField, 4U);
    appendU32(output, std::bit_cast<std::uint32_t>(record.renderScalePercent));
    appendFieldHeader(output, scenePresentationField, 1U);
    output.push_back(record.scenePresentation);
    appendFieldHeader(output, visualProfileField, 1U);
    output.push_back(record.visualProfile);
    appendFieldHeader(output, diagnosticsField, 1U);
    output.push_back(record.diagnosticsOverlayEnabled);
    appendFieldHeader(output, verticalFovAdjustmentField, 4U);
    appendU32(
        output,
        std::bit_cast<std::uint32_t>(
            record.verticalFovAdjustmentDegrees));
    appendFieldHeader(output, uiScaleField, 4U);
    appendU32(output, std::bit_cast<std::uint32_t>(record.uiScalePercent));

    if (output.size() != canonicalDocumentBytes) {
        throw std::logic_error("AFRS encoder produced a noncanonical size");
    }
    const auto digest = documentDigest(output);
    std::copy(digest.begin(), digest.end(),
              output.begin() + static_cast<std::ptrdiff_t>(prefixBytes));
    return output;
}

DecodedRenderSettingsDocument
decodeRenderSettingsDocument(const std::span<const std::uint8_t> bytes,
                             const std::size_t maximumBytes) {
    if (maximumBytes < headerBytes || maximumBytes > maximumRenderSettingsDocumentBytes) {
        throw std::invalid_argument("AFRS maximum byte limit is invalid");
    }
    if (bytes.size() > maximumBytes) {
        fail(RenderSettingsCodecErrorKind::tooLarge, std::nullopt,
             "AFRS document exceeds its byte limit");
    }
    if (bytes.size() < headerBytes) {
        fail(RenderSettingsCodecErrorKind::tooSmall, std::nullopt,
             "AFRS document is shorter than its envelope");
    }
    if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
        fail(RenderSettingsCodecErrorKind::badMagic, std::nullopt, "AFRS magic is invalid");
    }
    const auto version = readU16(bytes, 4U);
    if (version != envelopeVersion) {
        fail(RenderSettingsCodecErrorKind::unsupportedEnvelopeVersion, std::nullopt,
             "AFRS envelope version is unsupported");
    }
    if (readU16(bytes, 6U) != 0U) {
        fail(RenderSettingsCodecErrorKind::unsupportedFlags, std::nullopt,
             "AFRS envelope flags are unsupported");
    }

    const auto schemaVersion = readU32(bytes, 8U);
    if (schemaVersion == 0U) {
        fail(RenderSettingsCodecErrorKind::invalidSchemaVersion, schemaVersion,
             "AFRS semantic schema version is invalid");
    }
    if (readU32(bytes, 12U) != bytes.size()) {
        fail(RenderSettingsCodecErrorKind::declaredSizeMismatch, schemaVersion,
             "AFRS declared size does not match the exact input");
    }
    const auto digest = documentDigest(bytes);
    if (!std::equal(digest.begin(), digest.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(prefixBytes))) {
        fail(RenderSettingsCodecErrorKind::integrityMismatch, schemaVersion,
             "AFRS integrity check failed");
    }

    if (schemaVersion > render::renderPresentationSettingsRecordSchemaVersion) {
        return OpaqueFutureRenderSettingsRecord{
            .schemaVersion = schemaVersion,
            .exactBytes = {bytes.begin(), bytes.end()},
        };
    }
    if (schemaVersion != legacySchemaVersion &&
        schemaVersion != safeFovSchemaVersion &&
        schemaVersion !=
            render::renderPresentationSettingsRecordSchemaVersion) {
        fail(RenderSettingsCodecErrorKind::invalidSchemaVersion, schemaVersion,
             "AFRS semantic schema version is unsupported");
    }

    render::RenderPresentationSettingsRecord record{
        .schemaVersion =
            render::renderPresentationSettingsRecordSchemaVersion,
    };
    const std::uint16_t maximumField =
        schemaVersion == legacySchemaVersion
            ? diagnosticsField
            : schemaVersion == safeFovSchemaVersion
                  ? verticalFovAdjustmentField
                  : uiScaleField;
    std::array<bool, uiScaleField + 1U> seen{};
    std::uint16_t previousField = 0U;
    std::size_t offset = headerBytes;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < fieldHeaderBytes) {
            fail(RenderSettingsCodecErrorKind::trailingData, schemaVersion,
                 "AFRS has bytes after its last complete field");
        }
        const auto field = readU16(bytes, offset);
        const auto size = readU16(bytes, offset + 2U);
        offset += fieldHeaderBytes;
        if (field == previousField) {
            fail(RenderSettingsCodecErrorKind::duplicateField, schemaVersion,
                 "AFRS contains a duplicate field");
        }
        if (field < previousField) {
            fail(RenderSettingsCodecErrorKind::nonCanonicalFieldOrder, schemaVersion,
                 "AFRS fields are not in canonical order");
        }
        if (field == 0U || field > maximumField) {
            fail(RenderSettingsCodecErrorKind::unknownField, schemaVersion,
                 "AFRS contains an unknown current-schema field");
        }
        previousField = field;
        if (seen[field]) {
            fail(RenderSettingsCodecErrorKind::duplicateField, schemaVersion,
                 "AFRS contains a duplicate field");
        }
        seen[field] = true;
        if (size > bytes.size() - offset) {
            fail(RenderSettingsCodecErrorKind::truncatedField, schemaVersion,
                 "AFRS field payload is truncated");
        }

        const auto expectedSize =
            field == renderScaleField ||
                    field == verticalFovAdjustmentField ||
                    field == uiScaleField
                ? 4U
                : 1U;
        if (size != expectedSize) {
            fail(RenderSettingsCodecErrorKind::invalidFieldSize, schemaVersion,
                 "AFRS field size is invalid");
        }
        switch (field) {
        case renderScaleField:
            record.renderScalePercent = std::bit_cast<float>(readU32(bytes, offset));
            break;
        case scenePresentationField: record.scenePresentation = bytes[offset]; break;
        case visualProfileField: record.visualProfile = bytes[offset]; break;
        case diagnosticsField: record.diagnosticsOverlayEnabled = bytes[offset]; break;
        case verticalFovAdjustmentField:
            record.verticalFovAdjustmentDegrees =
                std::bit_cast<float>(readU32(bytes, offset));
            break;
        case uiScaleField:
            record.uiScalePercent =
                std::bit_cast<float>(readU32(bytes, offset));
            break;
        default: throw std::logic_error("validated AFRS field is unreachable");
        }
        offset += size;
    }
    for (std::uint16_t field = renderScaleField;
         field <= maximumField; ++field) {
        if (!seen[field]) {
            fail(RenderSettingsCodecErrorKind::missingField, schemaVersion,
                 "AFRS is missing a required field");
        }
    }
    if (offset != bytes.size()) {
        fail(RenderSettingsCodecErrorKind::trailingData, schemaVersion, "AFRS has trailing data");
    }
    if (semanticIssue(record).has_value()) {
        fail(RenderSettingsCodecErrorKind::invalidSemanticRecord, schemaVersion,
             "AFRS semantic record is invalid");
    }
    return record;
}

} // namespace airfix::settings
