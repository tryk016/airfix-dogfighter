#pragma once

#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/render/TextureRuntimeData.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace airfix::content {

enum class LegacyAircraftHudInstrumentTextureRole : std::uint8_t {
  leftFace,
  rightFace,
  indicator,
};

enum class LegacyAircraftHudInstrumentTextureArchive : std::uint8_t {
  source,
  localization,
};

inline constexpr std::size_t legacyAircraftHudInstrumentTextureCount = 3U;
inline constexpr std::uint32_t legacyAircraftHudInstrumentTextureFormat = 8U;
inline constexpr std::uint32_t legacyAircraftHudInstrumentFaceWidth = 64U;
inline constexpr std::uint32_t legacyAircraftHudInstrumentFaceHeight = 64U;
inline constexpr std::uint32_t legacyAircraftHudIndicatorWidth = 16U;
inline constexpr std::uint32_t legacyAircraftHudIndicatorHeight = 32U;

struct LoadedLegacyAircraftHudInstrumentTexture final {
  LegacyAircraftHudInstrumentTextureRole role{
      LegacyAircraftHudInstrumentTextureRole::leftFace};
  LegacyAircraftHudInstrumentTextureArchive archive{
      LegacyAircraftHudInstrumentTextureArchive::source};
  // Dense only inside this dedicated HUD texture set.
  render::TextureAssetId textureId{};
  std::size_t sourceFileIndex{};
  std::uint64_t sourceFootprintBytes{};
  std::string logicalPath;
  render::GtiUploadPlan upload;
  std::vector<assets::RgbaImage> uploadLevels;
};

using LegacyAircraftHudInstrumentTextures =
    std::array<LoadedLegacyAircraftHudInstrumentTexture,
               legacyAircraftHudInstrumentTextureCount>;

struct LoadedLegacyAircraftHudInstrumentTextureSet final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  LegacyAircraftHudInstrumentTextures textures;
  std::uint64_t sourceFootprintBytes{};
  std::uint64_t decodedRgbaBytes{};
  std::uint64_t uploadRgbaBytes{};
  std::uint64_t residentRgbaBytes{};

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;
  [[nodiscard]] const LoadedLegacyAircraftHudInstrumentTexture *
  texture(LegacyAircraftHudInstrumentTextureRole role) const noexcept;
};

struct LegacyAircraftHudInstrumentTextureLoadLimits final {
  render::GtiUploadDataLimits gti{};
  std::size_t maximumSourceBytes{2U * 1024U * 1024U};
  std::uint64_t maximumSingleSourceFootprintBytes{4U * 1024U * 1024U};
  std::uint64_t maximumTotalSourceFootprintBytes{8U * 1024U * 1024U};
  std::uint64_t maximumDecodedRgbaBytes{4U * 1024U * 1024U};
  std::uint64_t maximumUploadRgbaBytes{4U * 1024U * 1024U};
  std::uint64_t maximumResidentRgbaBytes{4U * 1024U * 1024U};
};

enum class LegacyAircraftHudInstrumentTextureLoadPhase : std::uint8_t {
  validatingInput,
  lookingUpTextures,
  readingTextures,
  preparingTextures,
  complete,
};

struct LegacyAircraftHudInstrumentTextureLoadProgress final {
  LegacyAircraftHudInstrumentTextureLoadPhase phase{};
  std::size_t completedItems{};
  std::size_t totalItems{};
};

using LegacyAircraftHudInstrumentTextureLoadProgressCallback =
    std::function<void(const LegacyAircraftHudInstrumentTextureLoadProgress &)>;

enum class LegacyAircraftHudInstrumentTextureLoadIssueKind : std::uint8_t {
  cancelled,
  invalidLimits,
  sessionIdentityChanged,
  textureNotFound,
  textureAmbiguous,
  duplicateSource,
  sourceLimitExceeded,
  sourceFootprintLimitExceeded,
  aggregateSourceFootprintLimitExceeded,
  sourceReadFailure,
  texturePreparationFailure,
  unexpectedFormat,
  unexpectedDimensions,
  decodedRgbaLimitExceeded,
  uploadRgbaLimitExceeded,
  residentRgbaLimitExceeded,
  integerOverflow,
  allocationFailure,
  progressCallbackFailure,
  internalFailure,
};

struct LegacyAircraftHudInstrumentTextureLoadIssue final {
  LegacyAircraftHudInstrumentTextureLoadIssueKind kind{
      LegacyAircraftHudInstrumentTextureLoadIssueKind::internalFailure};
  std::optional<LegacyAircraftHudInstrumentTextureRole> role;
  std::optional<LegacyAircraftHudInstrumentTextureArchive> archive;
  std::optional<std::size_t> archiveFileIndex;
  std::optional<render::GtiUploadDataIssueKind> preparationIssue;
};

struct LegacyAircraftHudInstrumentTextureLoadResult final {
  // Published only when all three recovered textures pass exact archive,
  // format, dimension, aggregate-budget and session-identity validation.
  std::optional<LoadedLegacyAircraftHudInstrumentTextureSet> textures;
  std::vector<LegacyAircraftHudInstrumentTextureLoadIssue> issues;

  [[nodiscard]] bool success() const noexcept {
    return textures.has_value() && issues.empty();
  }
};

// Loads clock1/clock2 from the selected localization archive and the shared
// indicator from Resource.up through the already-authenticated session handle.
// Host paths are never opened and partial sets are never published.
[[nodiscard]] LegacyAircraftHudInstrumentTextureLoadResult
loadLegacyAircraftHudInstrumentTextures(
    VerifiedContentSession &session,
    const LegacyAircraftHudInstrumentTextureLoadLimits &limits = {},
    std::stop_token stopToken = {},
    LegacyAircraftHudInstrumentTextureLoadProgressCallback progress = {});

} // namespace airfix::content
