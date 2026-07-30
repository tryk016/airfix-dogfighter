#pragma once

#include "airfix/input/ControllerInputProfile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace airfix::settings {

inline constexpr std::size_t maximumControllerInputProfileDocumentBytes = 4096U;

enum class ControllerInputProfileCodecErrorKind : std::uint8_t {
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

class ControllerInputProfileCodecError final : public std::runtime_error {
public:
  ControllerInputProfileCodecError(ControllerInputProfileCodecErrorKind kind,
                                   std::optional<std::uint32_t> schemaVersion,
                                   const char *message);

  [[nodiscard]] ControllerInputProfileCodecErrorKind kind() const noexcept {
    return kind_;
  }

  [[nodiscard]] std::optional<std::uint32_t> schemaVersion() const noexcept {
    return schemaVersion_;
  }

private:
  ControllerInputProfileCodecErrorKind kind_;
  std::optional<std::uint32_t> schemaVersion_;
};

struct OpaqueFutureControllerInputProfileRecord final {
  std::uint32_t schemaVersion{};
  std::vector<std::uint8_t> exactBytes;

  [[nodiscard]] friend bool
  operator==(const OpaqueFutureControllerInputProfileRecord &,
             const OpaqueFutureControllerInputProfileRecord &) = default;
};

using DecodedControllerInputProfileDocument =
    std::variant<input::ControllerInputProfileRecord,
                 OpaqueFutureControllerInputProfileRecord>;

// AFIP is a bounded, canonical little-endian envelope. The current schema
// stores four fixed axis-calibration records followed by exactly bindingCount
// packed binding records. The unused record tail is never serialized. A
// bounded, envelope-integrity-valid future semantic schema is retained as
// opaque exact bytes so older builds can preserve it without interpreting or
// rewriting it.
[[nodiscard]] DecodedControllerInputProfileDocument
decodeControllerInputProfileDocument(
    std::span<const std::uint8_t> bytes,
    std::size_t maximumBytes = maximumControllerInputProfileDocumentBytes);

[[nodiscard]] std::vector<std::uint8_t> encodeControllerInputProfileDocument(
    const input::ControllerInputProfileRecord &record);

} // namespace airfix::settings
