#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace airfix::content {

inline constexpr std::size_t maximumMissionSelectionDocumentBytes = 16384U;

struct MissionLaunchSelection final {
  std::string setupLogicalPath;
  std::string levelLogicalPath;
  std::optional<std::string> playerObjectLogicalPath;
  std::uint32_t requestedStartIndex{};

  [[nodiscard]] friend bool
  operator==(const MissionLaunchSelection &,
             const MissionLaunchSelection &) = default;
};

enum class MissionSelectionCodecErrorKind : std::uint8_t {
  tooSmall,
  tooLarge,
  badMagic,
  unsupportedEnvelopeVersion,
  unsupportedFlags,
  unsupportedSchemaVersion,
  declaredSizeMismatch,
  integrityMismatch,
  truncatedField,
  trailingData,
  unknownField,
  duplicateField,
  nonCanonicalFieldOrder,
  invalidFieldSize,
  missingField,
  invalidLogicalPath,
  nonCanonicalLogicalPath,
};

class MissionSelectionCodecError final : public std::runtime_error {
public:
  MissionSelectionCodecError(MissionSelectionCodecErrorKind kind,
                             const char *message);

  [[nodiscard]] MissionSelectionCodecErrorKind kind() const noexcept {
    return kind_;
  }

private:
  MissionSelectionCodecErrorKind kind_;
};

// AFMS is a bounded, canonical little-endian owner-private document. It stores
// only logical paths into an independently authenticated AFPACK plus the
// requested start ordinal. The SHA-256 envelope detects accidental corruption;
// it is not a substitute for AFPACK authentication.
[[nodiscard]] std::vector<std::uint8_t>
encodeMissionLaunchSelection(const MissionLaunchSelection &selection);

[[nodiscard]] MissionLaunchSelection decodeMissionLaunchSelection(
    std::span<const std::uint8_t> bytes,
    std::size_t maximumBytes = maximumMissionSelectionDocumentBytes);

} // namespace airfix::content
