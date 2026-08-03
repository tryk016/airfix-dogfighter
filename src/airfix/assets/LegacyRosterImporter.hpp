#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace airfix::assets {

struct LegacyRosterMedal final {
  std::string identifier;
  std::int32_t value{};

  [[nodiscard]] friend bool operator==(const LegacyRosterMedal &,
                                       const LegacyRosterMedal &) = default;
};

// Unknown records are described, not copied. This keeps the importer bounded
// and makes the current no-export policy explicit without discarding the fact
// that a valid legacy document contained data outside the recovered schema.
struct LegacyRosterUnknownRecord final {
  std::uint32_t id{};
  std::uint32_t payloadSize{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyRosterUnknownRecord &,
             const LegacyRosterUnknownRecord &) noexcept = default;
};

struct LegacyRoster final {
  std::optional<std::string> name;
  std::optional<std::string> portrait;
  std::vector<LegacyRosterMedal> medals;

  std::optional<std::int32_t> thread;
  std::optional<std::int32_t> axisMaximum;
  std::optional<std::int32_t> alliedMaximum;
  std::optional<std::int32_t> score;

  // Neutral FourCC names are retained until their gameplay semantics are
  // proven. FRIK intentionally remains separate because the native producer
  // has a confirmed gate/value mismatch.
  std::optional<std::int32_t> acki;
  std::optional<std::int32_t> guki;
  std::optional<std::int32_t> wuki;
  std::optional<std::int32_t> fook;
  std::optional<std::int32_t> frik;
  std::optional<std::int32_t> deat;
  std::optional<std::int32_t> pkil;
  std::optional<std::int32_t> pdea;

  std::vector<LegacyRosterUnknownRecord> unknownRecords;

  [[nodiscard]] friend bool operator==(const LegacyRoster &,
                                       const LegacyRoster &) = default;
};

struct LegacyRosterImportLimits final {
  std::size_t maximumInputBytes{4U * 1024U * 1024U};
  std::size_t maximumRecords{4'096U};
  std::size_t maximumStringBytes{4'096U};
  std::size_t maximumMedals{1'024U};
  std::size_t maximumUnknownRecords{1'024U};
};

enum class LegacyRosterImportStatus : std::uint8_t {
  imported = 0,
  invalidLimits,
  inputTooLarge,
  invalidContainer,
  unsupportedRoot,
  duplicateSingleton,
  invalidString,
  invalidInteger,
  invalidMedal,
  medalLimitExceeded,
  unknownRecordLimitExceeded,
};

struct LegacyRosterImportResult final {
  LegacyRosterImportStatus status{LegacyRosterImportStatus::invalidContainer};
  LegacyRoster roster;

  [[nodiscard]] constexpr bool imported() const noexcept {
    return status == LegacyRosterImportStatus::imported;
  }
};

// Imports one complete root-zero roster from caller-owned bytes. Structural or
// known-record errors fail closed and publish an empty roster. Known singleton
// duplicates are rejected; MEDA is the only confirmed repeatable record.
// Unknown records are retained as ID/size descriptors only. No filename, file
// I/O, legacy writer, durable-storage policy, or runtime state is owned here.
[[nodiscard]] LegacyRosterImportResult
importLegacyRoster(std::span<const std::uint8_t> bytes,
                   const LegacyRosterImportLimits &limits = {});

} // namespace airfix::assets
