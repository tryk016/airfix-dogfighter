#pragma once

#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/render/TextureRuntimeData.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace airfix::content {

enum class LegacyAircraftHudIdentityStatusTextureKind : std::uint8_t {
  technologyStar,
  technologyCross,
  aircraftIcon,
};

enum class LegacyAircraftHudIdentityStatusTextureArchive : std::uint8_t {
  source,
  localization,
};

inline constexpr std::size_t legacyAircraftHudIdentityStatusFixedTextureCount =
    2U;
inline constexpr std::uint32_t legacyAircraftHudIdentityStatusTextureFormat =
    8U;
inline constexpr std::uint32_t legacyAircraftHudIdentityStatusTextureWidth =
    64U;
inline constexpr std::uint32_t legacyAircraftHudIdentityStatusTextureHeight =
    64U;

struct LoadedLegacyAircraftHudIdentityStatusTexture final {
  LegacyAircraftHudIdentityStatusTextureKind kind{
      LegacyAircraftHudIdentityStatusTextureKind::technologyStar};
  LegacyAircraftHudIdentityStatusTextureArchive archive{
      LegacyAircraftHudIdentityStatusTextureArchive::localization};
  // Dense only inside this dedicated set. Aircraft-icon indices accepted by
  // the HUD plan never alias this owner-local TextureAssetId namespace.
  render::TextureAssetId textureId{};
  std::uint32_t iconIndex{};
  std::size_t sourceFileIndex{};
  std::uint64_t sourceFootprintBytes{};
  std::string logicalPath;
  // Empty for fixed team badges. Dynamic Ac*.gti entries store the complete
  // archive-derived filename stem used by the native type-name comparison.
  std::string matchKey;
  render::GtiUploadPlan upload;
  std::vector<assets::RgbaImage> uploadLevels;
};

struct LoadedLegacyAircraftHudIdentityStatusTextureSet final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  // Exact dense order: team star, team cross, then the authenticated dynamic
  // aircraft-icon catalog in source-archive enumeration order.
  std::vector<LoadedLegacyAircraftHudIdentityStatusTexture> textures;
  std::size_t iconCount{};
  std::uint64_t sourceFootprintBytes{};
  std::uint64_t decodedRgbaBytes{};
  std::uint64_t uploadRgbaBytes{};
  std::uint64_t residentRgbaBytes{};

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;
  [[nodiscard]] const LoadedLegacyAircraftHudIdentityStatusTexture *
  teamBadge(LegacyAircraftHudIdentityStatusTextureKind kind) const noexcept;
  [[nodiscard]] const LoadedLegacyAircraftHudIdentityStatusTexture *
  icon(std::uint32_t iconIndex) const noexcept;

  // Mirrors the native catalog lookup: the complete aircraft type name is
  // compared case-insensitively with the stem derived from Ac*.gti. No prefix
  // is stripped and the search is allocation-free.
  [[nodiscard]] const LoadedLegacyAircraftHudIdentityStatusTexture *
  findIconForAircraftType(std::string_view aircraftTypeName) const noexcept;
};

struct LegacyAircraftHudIdentityStatusTextureLoadLimits final {
  render::GtiUploadDataLimits gti{};
  std::size_t maximumIconCount{64U};
  std::size_t maximumMatchKeyBytes{128U};
  std::size_t maximumSourceBytes{2U * 1024U * 1024U};
  std::uint64_t maximumSingleSourceFootprintBytes{4U * 1024U * 1024U};
  std::uint64_t maximumTotalSourceFootprintBytes{32U * 1024U * 1024U};
  std::uint64_t maximumDecodedRgbaBytes{16U * 1024U * 1024U};
  std::uint64_t maximumUploadRgbaBytes{16U * 1024U * 1024U};
  std::uint64_t maximumResidentRgbaBytes{16U * 1024U * 1024U};
};

enum class LegacyAircraftHudIdentityStatusTextureLoadPhase : std::uint8_t {
  validatingInput,
  discoveringCatalog,
  readingTextures,
  preparingTextures,
  complete,
};

struct LegacyAircraftHudIdentityStatusTextureLoadProgress final {
  LegacyAircraftHudIdentityStatusTextureLoadPhase phase{};
  std::size_t completedItems{};
  std::size_t totalItems{};
};

using LegacyAircraftHudIdentityStatusTextureLoadProgressCallback =
    std::function<void(
        const LegacyAircraftHudIdentityStatusTextureLoadProgress &)>;

enum class LegacyAircraftHudIdentityStatusTextureLoadIssueKind : std::uint8_t {
  cancelled,
  invalidLimits,
  sessionIdentityChanged,
  textureNotFound,
  textureAmbiguous,
  catalogDirectoryNotFound,
  catalogDirectoryAmbiguous,
  catalogEntryInvalid,
  catalogLimitExceeded,
  duplicateCatalogKey,
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

struct LegacyAircraftHudIdentityStatusTextureLoadIssue final {
  LegacyAircraftHudIdentityStatusTextureLoadIssueKind kind{
      LegacyAircraftHudIdentityStatusTextureLoadIssueKind::internalFailure};
  std::optional<LegacyAircraftHudIdentityStatusTextureKind> textureKind;
  std::optional<std::size_t> archiveFileIndex;
  std::optional<render::GtiUploadDataIssueKind> preparationIssue;
};

struct LegacyAircraftHudIdentityStatusTextureLoadResult final {
  // No partial catalog is ever published. Both localization team badges and
  // every dynamic source-archive icon must pass the same transaction,
  // source, GTI, format, dimension and aggregate-budget checks.
  std::optional<LoadedLegacyAircraftHudIdentityStatusTextureSet> textures;
  std::vector<LegacyAircraftHudIdentityStatusTextureLoadIssue> issues;

  [[nodiscard]] bool success() const noexcept {
    return textures.has_value() && issues.empty();
  }
};

// Loads techstar.gti and techcross.gti from the selected localization archive,
// then discovers Ac*.gti in the authenticated source archive. Individual
// aircraft names are never hard-coded and host paths are never opened.
[[nodiscard]] LegacyAircraftHudIdentityStatusTextureLoadResult
loadLegacyAircraftHudIdentityStatusTextures(
    VerifiedContentSession &session,
    const LegacyAircraftHudIdentityStatusTextureLoadLimits &limits = {},
    std::stop_token stopToken = {},
    LegacyAircraftHudIdentityStatusTextureLoadProgressCallback progress = {});

} // namespace airfix::content
