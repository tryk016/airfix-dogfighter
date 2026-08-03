#include "airfix/assets/LegacyRosterImporter.hpp"

#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/AssetPrimitives.hpp"

#include <algorithm>
#include <bit>
#include <optional>
#include <string>
#include <utility>

namespace airfix::assets {
namespace {

constexpr auto kName = fourCC('N', 'A', 'M', 'E');
constexpr auto kPicture = fourCC('P', 'I', 'C', 'T');
constexpr auto kMedal = fourCC('M', 'E', 'D', 'A');
constexpr auto kThread = fourCC('T', 'H', 'R', 'D');
constexpr auto kAxisMaximum = fourCC('A', 'X', 'M', 'I');
constexpr auto kAlliedMaximum = fourCC('A', 'L', 'M', 'I');
constexpr auto kScore = fourCC('S', 'C', 'O', 'R');
constexpr auto kAcki = fourCC('A', 'C', 'K', 'I');
constexpr auto kGuki = fourCC('G', 'U', 'K', 'I');
constexpr auto kWuki = fourCC('W', 'U', 'K', 'I');
constexpr auto kFook = fourCC('F', 'O', 'O', 'K');
constexpr auto kFrik = fourCC('F', 'R', 'I', 'K');
constexpr auto kDeat = fourCC('D', 'E', 'A', 'T');
constexpr auto kPkil = fourCC('P', 'K', 'I', 'L');
constexpr auto kPdea = fourCC('P', 'D', 'E', 'A');

[[nodiscard]] LegacyRosterImportResult
failure(const LegacyRosterImportStatus status) {
  return {.status = status, .roster = {}};
}

[[nodiscard]] bool validLimits(const LegacyRosterImportLimits &limits) {
  return limits.maximumInputBytes >= 8U && limits.maximumRecords > 0U &&
         limits.maximumStringBytes > 0U && limits.maximumMedals > 0U &&
         limits.maximumUnknownRecords > 0U;
}

[[nodiscard]] std::optional<std::string>
readExactString(const std::span<const std::uint8_t> payload,
                const std::size_t maximumStringBytes) {
  if (payload.empty() || payload.size() - 1U > maximumStringBytes) {
    return std::nullopt;
  }
  const auto terminator = std::find(payload.begin(), payload.end(), 0U);
  if (terminator == payload.end() || terminator + 1U != payload.end()) {
    return std::nullopt;
  }
  return std::string(reinterpret_cast<const char *>(payload.data()),
                     static_cast<std::size_t>(terminator - payload.begin()));
}

[[nodiscard]] std::optional<std::int32_t>
readExactInteger(const std::span<const std::uint8_t> payload) {
  if (payload.size() != sizeof(std::uint32_t)) {
    return std::nullopt;
  }
  const auto bits = static_cast<std::uint32_t>(payload[0U]) |
                    (static_cast<std::uint32_t>(payload[1U]) << 8U) |
                    (static_cast<std::uint32_t>(payload[2U]) << 16U) |
                    (static_cast<std::uint32_t>(payload[3U]) << 24U);
  return std::bit_cast<std::int32_t>(bits);
}

[[nodiscard]] std::optional<LegacyRosterMedal>
readMedal(const std::span<const std::uint8_t> payload,
          const std::size_t maximumStringBytes) {
  constexpr auto kValueBytes = sizeof(std::uint32_t);
  if (payload.size() < 1U + kValueBytes) {
    return std::nullopt;
  }
  const auto stringBytes = payload.first(payload.size() - kValueBytes);
  const auto identifier = readExactString(stringBytes, maximumStringBytes);
  const auto value = readExactInteger(payload.last(kValueBytes));
  if (!identifier.has_value() || !value.has_value()) {
    return std::nullopt;
  }
  return LegacyRosterMedal{.identifier = *identifier, .value = *value};
}

[[nodiscard]] LegacyRosterImportStatus
storeString(std::optional<std::string> &destination,
            const std::span<const std::uint8_t> payload,
            const std::size_t maximumStringBytes) {
  if (destination.has_value()) {
    return LegacyRosterImportStatus::duplicateSingleton;
  }
  auto value = readExactString(payload, maximumStringBytes);
  if (!value.has_value()) {
    return LegacyRosterImportStatus::invalidString;
  }
  destination = std::move(*value);
  return LegacyRosterImportStatus::imported;
}

[[nodiscard]] LegacyRosterImportStatus
storeInteger(std::optional<std::int32_t> &destination,
             const std::span<const std::uint8_t> payload) {
  if (destination.has_value()) {
    return LegacyRosterImportStatus::duplicateSingleton;
  }
  const auto value = readExactInteger(payload);
  if (!value.has_value()) {
    return LegacyRosterImportStatus::invalidInteger;
  }
  destination = *value;
  return LegacyRosterImportStatus::imported;
}

} // namespace

LegacyRosterImportResult
importLegacyRoster(const std::span<const std::uint8_t> bytes,
                   const LegacyRosterImportLimits &limits) {
  if (!validLimits(limits)) {
    return failure(LegacyRosterImportStatus::invalidLimits);
  }
  if (bytes.size() > limits.maximumInputBytes) {
    return failure(LegacyRosterImportStatus::inputTooLarge);
  }

  AfChunkContainer container;
  try {
    container = parseAfChunkContainer(bytes, limits.maximumRecords);
  } catch (const ParseError &) {
    return failure(LegacyRosterImportStatus::invalidContainer);
  }
  if (container.rootId != 0U) {
    return failure(LegacyRosterImportStatus::unsupportedRoot);
  }

  LegacyRoster roster;
  for (const auto &chunk : container.chunks) {
    const auto payload = afChunkPayload(bytes, chunk);
    auto status = LegacyRosterImportStatus::imported;
    switch (chunk.id) {
    case kName:
      status = storeString(roster.name, payload, limits.maximumStringBytes);
      break;
    case kPicture:
      status = storeString(roster.portrait, payload, limits.maximumStringBytes);
      break;
    case kMedal: {
      if (roster.medals.size() >= limits.maximumMedals) {
        return failure(LegacyRosterImportStatus::medalLimitExceeded);
      }
      auto medal = readMedal(payload, limits.maximumStringBytes);
      if (!medal.has_value()) {
        return failure(LegacyRosterImportStatus::invalidMedal);
      }
      roster.medals.push_back(std::move(*medal));
      break;
    }
    case kThread:
      status = storeInteger(roster.thread, payload);
      break;
    case kAxisMaximum:
      status = storeInteger(roster.axisMaximum, payload);
      break;
    case kAlliedMaximum:
      status = storeInteger(roster.alliedMaximum, payload);
      break;
    case kScore:
      status = storeInteger(roster.score, payload);
      break;
    case kAcki:
      status = storeInteger(roster.acki, payload);
      break;
    case kGuki:
      status = storeInteger(roster.guki, payload);
      break;
    case kWuki:
      status = storeInteger(roster.wuki, payload);
      break;
    case kFook:
      status = storeInteger(roster.fook, payload);
      break;
    case kFrik:
      status = storeInteger(roster.frik, payload);
      break;
    case kDeat:
      status = storeInteger(roster.deat, payload);
      break;
    case kPkil:
      status = storeInteger(roster.pkil, payload);
      break;
    case kPdea:
      status = storeInteger(roster.pdea, payload);
      break;
    default:
      if (roster.unknownRecords.size() >= limits.maximumUnknownRecords) {
        return failure(LegacyRosterImportStatus::unknownRecordLimitExceeded);
      }
      roster.unknownRecords.push_back(
          {.id = chunk.id, .payloadSize = chunk.payloadSize});
      break;
    }
    if (status != LegacyRosterImportStatus::imported) {
      return failure(status);
    }
  }

  return {.status = LegacyRosterImportStatus::imported,
          .roster = std::move(roster)};
}

} // namespace airfix::assets
