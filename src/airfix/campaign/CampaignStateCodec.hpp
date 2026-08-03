#pragma once

#include "airfix/campaign/CampaignState.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace airfix::campaign {

inline constexpr std::size_t maximumCampaignStateDocumentBytes = 4'096U;

enum class CampaignStateCodecErrorKind : std::uint8_t {
  tooSmall = 0,
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
  invalidPresenceMask,
  nonCanonicalAbsentValue,
  invalidSemanticRecord,
};

class CampaignStateCodecError final : public std::runtime_error {
public:
  CampaignStateCodecError(CampaignStateCodecErrorKind kind,
                          std::optional<std::uint32_t> schemaVersion,
                          const char *message);

  [[nodiscard]] CampaignStateCodecErrorKind kind() const noexcept {
    return kind_;
  }

  [[nodiscard]] std::optional<std::uint32_t> schemaVersion() const noexcept {
    return schemaVersion_;
  }

private:
  CampaignStateCodecErrorKind kind_;
  std::optional<std::uint32_t> schemaVersion_;
};

struct OpaqueFutureCampaignStateDocument final {
  std::uint32_t schemaVersion{};
  std::vector<std::uint8_t> exactBytes;

  [[nodiscard]] friend bool
  operator==(const OpaqueFutureCampaignStateDocument &,
             const OpaqueFutureCampaignStateDocument &) = default;
};

using DecodedCampaignStateDocument =
    std::variant<CampaignStateRecord, OpaqueFutureCampaignStateDocument>;

// AFCS is a bounded, canonical little-endian envelope. Schema 1 stores two
// fixed ordered fields: optional numeric progress/score and optional neutral
// cumulative counters. Envelope-valid future semantic schemas are retained as
// exact opaque bytes so older builds can preserve them without downgrade.
[[nodiscard]] DecodedCampaignStateDocument decodeCampaignStateDocument(
    std::span<const std::uint8_t> bytes,
    std::size_t maximumBytes = maximumCampaignStateDocumentBytes);

[[nodiscard]] std::vector<std::uint8_t>
encodeCampaignStateDocument(const CampaignStateRecord &record);

} // namespace airfix::campaign
