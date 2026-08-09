#pragma once

#include "airfix/content/MissionLaunchSelectionCodec.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace airfix::content {

enum class MissionSelectionFileStatus : std::uint8_t {
  notInspected,
  missing,
  valid,
  futureSchema,
  malformed,
  oversized,
  wrongTypeOrLinked,
  ioUnavailable,
};

enum class MissionSelectionLoadSource : std::uint8_t {
  none,
  current,
  backup,
};

struct MissionSelectionLoadResult final {
  std::optional<MissionLaunchSelection> selection;
  MissionSelectionLoadSource source{MissionSelectionLoadSource::none};
  MissionSelectionFileStatus current{MissionSelectionFileStatus::notInspected};
  MissionSelectionFileStatus backup{MissionSelectionFileStatus::notInspected};
  bool persistenceBlocked{};

  [[nodiscard]] constexpr bool recoveredFromBackup() const noexcept {
    return source == MissionSelectionLoadSource::backup;
  }
};

enum class MissionSelectionSaveStatus : std::uint8_t {
  unchanged,
  committed,
  committedAfterReadback,
};

struct MissionSelectionSaveResult final {
  MissionSelectionSaveStatus status{MissionSelectionSaveStatus::unchanged};
  bool backupRotated{};
};

enum class MissionSelectionStoreErrorKind : std::uint8_t {
  invalidDirectory,
  invalidSelection,
  persistenceBlocked,
  saveFailed,
  commitUnknown,
};

class MissionSelectionStoreError final : public std::runtime_error {
public:
  MissionSelectionStoreError(MissionSelectionStoreErrorKind kind,
                             const char *message);

  [[nodiscard]] MissionSelectionStoreErrorKind kind() const noexcept {
    return kind_;
  }

private:
  MissionSelectionStoreErrorKind kind_;
};

// selectionDirectory is an application-private, single-owner leaf. No error
// or diagnostic from this API contains a host path or logical mission path.
[[nodiscard]] MissionSelectionLoadResult
loadMissionLaunchSelection(const std::filesystem::path &selectionDirectory);

[[nodiscard]] MissionSelectionSaveResult
saveMissionLaunchSelection(const std::filesystem::path &selectionDirectory,
                           const MissionLaunchSelection &candidate);

} // namespace airfix::content
