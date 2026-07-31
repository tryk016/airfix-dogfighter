#pragma once

#include "airfix/render/RenderPresentationSettings.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace airfix::settings {

inline constexpr std::size_t maximumRenderSettingsDocumentBytes = 4096U;

enum class RenderSettingsCodecErrorKind : std::uint8_t {
    tooSmall,
    tooLarge,
    badMagic,
    unsupportedEnvelopeVersion,
    unsupportedFlags,
    invalidSchemaVersion,
    declaredSizeMismatch,
    integrityMismatch,
    truncatedField,
    trailingData,
    unknownField,
    duplicateField,
    missingField,
    nonCanonicalFieldOrder,
    invalidFieldSize,
    invalidSemanticRecord,
};

class RenderSettingsCodecError final : public std::runtime_error {
  public:
    RenderSettingsCodecError(RenderSettingsCodecErrorKind kind,
                             std::optional<std::uint32_t> schemaVersion, const char* message);

    [[nodiscard]] RenderSettingsCodecErrorKind kind() const noexcept { return kind_; }

    [[nodiscard]] std::optional<std::uint32_t> schemaVersion() const noexcept {
        return schemaVersion_;
    }

  private:
    RenderSettingsCodecErrorKind kind_;
    std::optional<std::uint32_t> schemaVersion_;
};

struct OpaqueFutureRenderSettingsRecord final {
    std::uint32_t schemaVersion{};
    std::vector<std::uint8_t> exactBytes;

    [[nodiscard]] friend bool operator==(const OpaqueFutureRenderSettingsRecord&,
                                         const OpaqueFutureRenderSettingsRecord&) = default;
};

using DecodedRenderSettingsDocument =
    std::variant<render::RenderPresentationSettingsRecord, OpaqueFutureRenderSettingsRecord>;

// AFRS is a bounded, canonical little-endian envelope. Its checksum covers the
// complete header except the checksum field itself and every TLV payload byte.
// Current-schema fields are required exactly once and in ascending field-ID
// order. Schema 1 migrates with safe FOV and UI scale at their exact defaults;
// schema 2 migrates with UI scale at its exact default. A structurally intact
// future schema is returned as opaque exact bytes so a downgrade can preserve
// it without interpreting or rewriting it.
[[nodiscard]] DecodedRenderSettingsDocument
decodeRenderSettingsDocument(std::span<const std::uint8_t> bytes,
                             std::size_t maximumBytes = maximumRenderSettingsDocumentBytes);

[[nodiscard]] std::vector<std::uint8_t>
encodeRenderSettingsDocument(const render::RenderPresentationSettingsRecord& record);

} // namespace airfix::settings
