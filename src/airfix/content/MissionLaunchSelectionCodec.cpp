#include "airfix/content/MissionLaunchSelectionCodec.hpp"

#include "airfix/archive/UdspArchive.hpp"
#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace airfix::content {
namespace {

constexpr std::array<std::uint8_t, 4U> magic{'A', 'F', 'M', 'S'};
constexpr std::uint16_t envelopeVersion = 1U;
constexpr std::uint32_t schemaVersion = 1U;
constexpr std::size_t prefixBytes = 16U;
constexpr std::size_t digestBytes = 32U;
constexpr std::size_t headerBytes = prefixBytes + digestBytes;
constexpr std::size_t fieldHeaderBytes = 4U;
constexpr std::uint16_t setupPathField = 1U;
constexpr std::uint16_t levelPathField = 2U;
constexpr std::uint16_t playerObjectPathField = 3U;
constexpr std::uint16_t requestedStartIndexField = 4U;
constexpr std::size_t maximumLogicalPathBytes = 4096U;

[[noreturn]] void fail(const MissionSelectionCodecErrorKind kind,
                       const char *const message) {
  throw MissionSelectionCodecError(kind, message);
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

[[nodiscard]] bool isAsciiLogicalPath(const std::string_view path) noexcept {
  return std::all_of(path.begin(), path.end(), [](const char character) {
    const auto value = static_cast<std::uint8_t>(character);
    return value >= 0x20U && value <= 0x7EU;
  });
}

[[nodiscard]] std::string canonicalLogicalPath(const std::string_view path) {
  if (!isAsciiLogicalPath(path)) {
    fail(MissionSelectionCodecErrorKind::invalidLogicalPath,
         "AFMS logical path is not bounded ASCII");
  }
  try {
    return udsp::normalizeLogicalPath(path, maximumLogicalPathBytes);
  } catch (...) {
    fail(MissionSelectionCodecErrorKind::invalidLogicalPath,
         "AFMS logical path is invalid");
  }
}

void appendPathField(std::vector<std::uint8_t> &output,
                     const std::uint16_t field, const std::string_view path) {
  if (path.size() > std::numeric_limits<std::uint16_t>::max()) {
    fail(MissionSelectionCodecErrorKind::invalidFieldSize,
         "AFMS logical path field is too large");
  }
  appendFieldHeader(output, field, static_cast<std::uint16_t>(path.size()));
  output.insert(output.end(), path.begin(), path.end());
}

[[nodiscard]] std::string decodePath(const std::span<const std::uint8_t> bytes,
                                     const std::size_t offset,
                                     const std::size_t size,
                                     const bool optional) {
  if (size == 0U) {
    if (optional) {
      return {};
    }
    fail(MissionSelectionCodecErrorKind::invalidLogicalPath,
         "AFMS required logical path is empty");
  }
  if (size > maximumLogicalPathBytes) {
    fail(MissionSelectionCodecErrorKind::invalidFieldSize,
         "AFMS logical path field exceeds its limit");
  }
  const std::string decoded(
      reinterpret_cast<const char *>(bytes.data() + offset), size);
  const auto canonical = canonicalLogicalPath(decoded);
  if (canonical != decoded) {
    fail(MissionSelectionCodecErrorKind::nonCanonicalLogicalPath,
         "AFMS logical path is not canonical");
  }
  return decoded;
}

} // namespace

MissionSelectionCodecError::MissionSelectionCodecError(
    const MissionSelectionCodecErrorKind kind, const char *const message)
    : std::runtime_error(message), kind_(kind) {}

std::vector<std::uint8_t>
encodeMissionLaunchSelection(const MissionLaunchSelection &selection) {
  const auto setup = canonicalLogicalPath(selection.setupLogicalPath);
  const auto level = canonicalLogicalPath(selection.levelLogicalPath);
  std::optional<std::string> player;
  if (selection.playerObjectLogicalPath.has_value()) {
    player = canonicalLogicalPath(*selection.playerObjectLogicalPath);
  }

  const std::size_t totalBytes =
      headerBytes + fieldHeaderBytes * 4U + setup.size() + level.size() +
      (player.has_value() ? player->size() : 0U) + sizeof(std::uint32_t);
  if (totalBytes > maximumMissionSelectionDocumentBytes ||
      totalBytes > std::numeric_limits<std::uint32_t>::max()) {
    fail(MissionSelectionCodecErrorKind::tooLarge,
         "AFMS document exceeds its byte limit");
  }

  std::vector<std::uint8_t> output;
  output.reserve(totalBytes);
  output.insert(output.end(), magic.begin(), magic.end());
  appendU16(output, envelopeVersion);
  appendU16(output, 0U);
  appendU32(output, schemaVersion);
  appendU32(output, static_cast<std::uint32_t>(totalBytes));
  output.resize(headerBytes, 0U);

  appendPathField(output, setupPathField, setup);
  appendPathField(output, levelPathField, level);
  appendPathField(output, playerObjectPathField,
                  player.has_value() ? std::string_view(*player)
                                     : std::string_view{});
  appendFieldHeader(output, requestedStartIndexField, sizeof(std::uint32_t));
  appendU32(output, selection.requestedStartIndex);

  if (output.size() != totalBytes) {
    throw std::logic_error("AFMS encoder produced a noncanonical size");
  }
  const auto digest = documentDigest(output);
  std::copy(digest.begin(), digest.end(),
            output.begin() + static_cast<std::ptrdiff_t>(prefixBytes));
  return output;
}

MissionLaunchSelection
decodeMissionLaunchSelection(const std::span<const std::uint8_t> bytes,
                             const std::size_t maximumBytes) {
  if (maximumBytes < headerBytes ||
      maximumBytes > maximumMissionSelectionDocumentBytes) {
    throw std::invalid_argument("AFMS maximum byte limit is invalid");
  }
  if (bytes.size() > maximumBytes) {
    fail(MissionSelectionCodecErrorKind::tooLarge,
         "AFMS document exceeds its byte limit");
  }
  if (bytes.size() < headerBytes) {
    fail(MissionSelectionCodecErrorKind::tooSmall,
         "AFMS document is shorter than its envelope");
  }
  if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
    fail(MissionSelectionCodecErrorKind::badMagic, "AFMS magic is invalid");
  }
  if (readU16(bytes, 4U) != envelopeVersion) {
    fail(MissionSelectionCodecErrorKind::unsupportedEnvelopeVersion,
         "AFMS envelope version is unsupported");
  }
  if (readU16(bytes, 6U) != 0U) {
    fail(MissionSelectionCodecErrorKind::unsupportedFlags,
         "AFMS envelope flags are unsupported");
  }
  if (readU32(bytes, 12U) != bytes.size()) {
    fail(MissionSelectionCodecErrorKind::declaredSizeMismatch,
         "AFMS declared size does not match the exact input");
  }
  const auto digest = documentDigest(bytes);
  if (!std::equal(digest.begin(), digest.end(),
                  bytes.begin() + static_cast<std::ptrdiff_t>(prefixBytes))) {
    fail(MissionSelectionCodecErrorKind::integrityMismatch,
         "AFMS integrity check failed");
  }
  if (readU32(bytes, 8U) != schemaVersion) {
    fail(MissionSelectionCodecErrorKind::unsupportedSchemaVersion,
         "AFMS schema version is unsupported");
  }

  MissionLaunchSelection result;
  std::array<bool, requestedStartIndexField + 1U> seen{};
  std::uint16_t previousField = 0U;
  std::size_t offset = headerBytes;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < fieldHeaderBytes) {
      fail(MissionSelectionCodecErrorKind::trailingData,
           "AFMS has bytes after its last complete field");
    }
    const auto field = readU16(bytes, offset);
    const auto size = readU16(bytes, offset + 2U);
    offset += fieldHeaderBytes;
    if (field == previousField || (field < seen.size() && seen[field])) {
      fail(MissionSelectionCodecErrorKind::duplicateField,
           "AFMS contains a duplicate field");
    }
    if (field < previousField) {
      fail(MissionSelectionCodecErrorKind::nonCanonicalFieldOrder,
           "AFMS fields are not in canonical order");
    }
    if (field == 0U || field > requestedStartIndexField) {
      fail(MissionSelectionCodecErrorKind::unknownField,
           "AFMS contains an unknown field");
    }
    previousField = field;
    seen[field] = true;
    if (size > bytes.size() - offset) {
      fail(MissionSelectionCodecErrorKind::truncatedField,
           "AFMS field payload is truncated");
    }

    switch (field) {
    case setupPathField:
      result.setupLogicalPath = decodePath(bytes, offset, size, false);
      break;
    case levelPathField:
      result.levelLogicalPath = decodePath(bytes, offset, size, false);
      break;
    case playerObjectPathField: {
      auto decoded = decodePath(bytes, offset, size, true);
      if (!decoded.empty()) {
        result.playerObjectLogicalPath = std::move(decoded);
      }
      break;
    }
    case requestedStartIndexField:
      if (size != sizeof(std::uint32_t)) {
        fail(MissionSelectionCodecErrorKind::invalidFieldSize,
             "AFMS start-index field size is invalid");
      }
      result.requestedStartIndex = readU32(bytes, offset);
      break;
    default:
      fail(MissionSelectionCodecErrorKind::unknownField,
           "AFMS contains an unknown field");
    }
    offset += size;
  }

  for (std::uint16_t field = setupPathField; field <= requestedStartIndexField;
       ++field) {
    if (!seen[field]) {
      fail(MissionSelectionCodecErrorKind::missingField,
           "AFMS is missing a required field");
    }
  }
  return result;
}

} // namespace airfix::content
