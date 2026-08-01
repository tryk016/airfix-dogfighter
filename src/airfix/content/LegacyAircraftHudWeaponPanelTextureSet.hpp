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

enum class LegacyAircraftHudWeaponPanelTextureKind : std::uint8_t {
  primaryBackground,
  secondaryBackground,
  icon,
};

enum class LegacyAircraftHudWeaponPanelTextureArchive : std::uint8_t {
  source,
  localization,
};

inline constexpr std::size_t legacyAircraftHudWeaponPanelFixedTextureCount = 2U;
inline constexpr std::uint32_t legacyAircraftHudWeaponPanelTextureFormat = 8U;
inline constexpr std::uint32_t legacyAircraftHudWeaponPanelBackgroundWidth =
    64U;
inline constexpr std::uint32_t legacyAircraftHudWeaponPanelBackgroundHeight =
    64U;
inline constexpr std::uint32_t legacyAircraftHudWeaponPanelIconWidth = 64U;
inline constexpr std::uint32_t legacyAircraftHudWeaponPanelIconHeight = 32U;

struct LoadedLegacyAircraftHudWeaponPanelTexture final {
  LegacyAircraftHudWeaponPanelTextureKind kind{
      LegacyAircraftHudWeaponPanelTextureKind::primaryBackground};
  LegacyAircraftHudWeaponPanelTextureArchive archive{
      LegacyAircraftHudWeaponPanelTextureArchive::localization};
  // Dense only inside this dedicated set. Icon indices accepted by the HUD
  // plan refer to iconIndex, never directly to this owner-local TextureAssetId.
  render::TextureAssetId textureId{};
  std::uint32_t iconIndex{};
  std::size_t sourceFileIndex{};
  std::uint64_t sourceFootprintBytes{};
  std::string logicalPath;
  // Empty for fixed backgrounds. Dynamic weapon_*.gti entries store only the
  // archive-derived suffix used by the native Wp-prefix-stripped comparison.
  std::string matchKey;
  render::GtiUploadPlan upload;
  std::vector<assets::RgbaImage> uploadLevels;
};

struct LoadedLegacyAircraftHudWeaponPanelTextureSet final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  // Exact dense order: primary background, secondary background, then the
  // authenticated dynamic icon catalog in archive enumeration order.
  std::vector<LoadedLegacyAircraftHudWeaponPanelTexture> textures;
  std::size_t iconCount{};
  std::uint64_t sourceFootprintBytes{};
  std::uint64_t decodedRgbaBytes{};
  std::uint64_t uploadRgbaBytes{};
  std::uint64_t residentRgbaBytes{};

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;
  [[nodiscard]] const LoadedLegacyAircraftHudWeaponPanelTexture *
  background(LegacyAircraftHudWeaponPanelTextureKind kind) const noexcept;
  [[nodiscard]] const LoadedLegacyAircraftHudWeaponPanelTexture *
  icon(std::uint32_t iconIndex) const noexcept;

  // Mirrors the native catalog lookup: exactly two leading type-name bytes
  // are skipped and the remainder is compared case-insensitively with the
  // suffix derived from weapon_*.gti. The search is allocation-free.
  [[nodiscard]] const LoadedLegacyAircraftHudWeaponPanelTexture *
  findIconForWeaponType(std::string_view weaponTypeName) const noexcept;
};

struct LegacyAircraftHudWeaponPanelTextureLoadLimits final {
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

enum class LegacyAircraftHudWeaponPanelTextureLoadPhase : std::uint8_t {
  validatingInput,
  discoveringCatalog,
  readingTextures,
  preparingTextures,
  complete,
};

struct LegacyAircraftHudWeaponPanelTextureLoadProgress final {
  LegacyAircraftHudWeaponPanelTextureLoadPhase phase{};
  std::size_t completedItems{};
  std::size_t totalItems{};
};

using LegacyAircraftHudWeaponPanelTextureLoadProgressCallback =
    std::function<void(
        const LegacyAircraftHudWeaponPanelTextureLoadProgress &)>;

enum class LegacyAircraftHudWeaponPanelTextureLoadIssueKind : std::uint8_t {
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

struct LegacyAircraftHudWeaponPanelTextureLoadIssue final {
  LegacyAircraftHudWeaponPanelTextureLoadIssueKind kind{
      LegacyAircraftHudWeaponPanelTextureLoadIssueKind::internalFailure};
  std::optional<LegacyAircraftHudWeaponPanelTextureKind> textureKind;
  std::optional<std::size_t> archiveFileIndex;
  std::optional<render::GtiUploadDataIssueKind> preparationIssue;
};

struct LegacyAircraftHudWeaponPanelTextureLoadResult final {
  // No partial catalog is ever published. The two localization backgrounds
  // and every dynamic source-archive icon must pass the same transaction,
  // source, GTI, format, dimension and aggregate-budget checks.
  std::optional<LoadedLegacyAircraftHudWeaponPanelTextureSet> textures;
  std::vector<LegacyAircraftHudWeaponPanelTextureLoadIssue> issues;

  [[nodiscard]] bool success() const noexcept {
    return textures.has_value() && issues.empty();
  }
};

// Loads weapon1.gti and weapon2.gti from the selected localization archive,
// then discovers weapon_*.gti in the authenticated source archive. Individual
// weapon names are never hard-coded and host paths are never opened.
[[nodiscard]] LegacyAircraftHudWeaponPanelTextureLoadResult
loadLegacyAircraftHudWeaponPanelTextures(
    VerifiedContentSession &session,
    const LegacyAircraftHudWeaponPanelTextureLoadLimits &limits = {},
    std::stop_token stopToken = {},
    LegacyAircraftHudWeaponPanelTextureLoadProgressCallback progress = {});

} // namespace airfix::content
