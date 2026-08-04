#pragma once

#include "airfix/input/TouchControlsPreferences.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace airfix::settings {

inline constexpr std::size_t maximumTouchControlsPreferencesDocumentBytes =
    4096U;

enum class TouchControlsPreferencesCodecErrorKind : std::uint8_t {
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

class TouchControlsPreferencesCodecError final : public std::runtime_error {
public:
  TouchControlsPreferencesCodecError(
      TouchControlsPreferencesCodecErrorKind kind,
      std::optional<std::uint32_t> schemaVersion, const char *message);

  [[nodiscard]] TouchControlsPreferencesCodecErrorKind kind() const noexcept {
    return kind_;
  }

  [[nodiscard]] std::optional<std::uint32_t> schemaVersion() const noexcept {
    return schemaVersion_;
  }

private:
  TouchControlsPreferencesCodecErrorKind kind_;
  std::optional<std::uint32_t> schemaVersion_;
};

struct OpaqueFutureTouchControlsPreferencesRecord final {
  std::uint32_t schemaVersion{};
  std::vector<std::uint8_t> exactBytes;

  [[nodiscard]] friend bool
  operator==(const OpaqueFutureTouchControlsPreferencesRecord &,
             const OpaqueFutureTouchControlsPreferencesRecord &) = default;
};

struct DecodedCurrentTouchControlsPreferencesRecord final {
  input::TouchControlsPreferencesRecord record;
  std::uint32_t sourceSchemaVersion{};

  [[nodiscard]] constexpr bool migrated() const noexcept {
    return sourceSchemaVersion !=
           input::touchControlsPreferencesRecordSchemaVersion;
  }

  [[nodiscard]] friend constexpr bool operator==(
      const DecodedCurrentTouchControlsPreferencesRecord &,
      const DecodedCurrentTouchControlsPreferencesRecord &) noexcept = default;
};

using DecodedTouchControlsPreferencesDocument =
    std::variant<DecodedCurrentTouchControlsPreferencesRecord,
                 OpaqueFutureTouchControlsPreferencesRecord>;

// AFTC is a bounded canonical little-endian envelope. The digest covers the
// prefix and every field byte, excluding only the digest slot. Current fields
// are mandatory, unique, one byte wide, and strictly ordered. Schema 1 is
// migrated in memory to schema 2 with automatic controller hiding. An intact
// future schema is retained byte-for-byte so older builds cannot downgrade it.
[[nodiscard]] DecodedTouchControlsPreferencesDocument
decodeTouchControlsPreferencesDocument(
    std::span<const std::uint8_t> bytes,
    std::size_t maximumBytes = maximumTouchControlsPreferencesDocumentBytes);

[[nodiscard]] std::vector<std::uint8_t> encodeTouchControlsPreferencesDocument(
    const input::TouchControlsPreferencesRecord &record);

} // namespace airfix::settings
