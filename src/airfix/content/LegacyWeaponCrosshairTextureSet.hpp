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

enum class LegacyWeaponCrosshairTextureRole : std::uint8_t {
  machineGun = 0U,
  rocket = 1U,
  bomb = 2U,
};

inline constexpr std::size_t legacyWeaponCrosshairTextureCount = 3U;
inline constexpr std::uint32_t legacyWeaponCrosshairTextureFormat = 8U;
inline constexpr std::uint32_t legacyWeaponCrosshairTextureWidth = 32U;
inline constexpr std::uint32_t legacyWeaponCrosshairTextureHeight = 32U;

struct LoadedLegacyWeaponCrosshairTexture final {
  LegacyWeaponCrosshairTextureRole role{
      LegacyWeaponCrosshairTextureRole::machineGun};
  // Dense only inside this dedicated HUD texture set. It is not a scene
  // TextureAssetId and must not be merged into a room's namespace by value.
  render::TextureAssetId textureId{};
  std::size_t sourceFileIndex{};
  std::uint64_t sourceFootprintBytes{};
  std::string logicalPath;
  render::GtiUploadPlan upload;
  std::vector<assets::RgbaImage> uploadLevels;
};

using LegacyWeaponCrosshairTextures =
    std::array<LoadedLegacyWeaponCrosshairTexture,
               legacyWeaponCrosshairTextureCount>;

struct LoadedLegacyWeaponCrosshairTextureSet final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  LegacyWeaponCrosshairTextures textures;
  std::uint64_t sourceFootprintBytes{};
  std::uint64_t decodedRgbaBytes{};
  std::uint64_t uploadRgbaBytes{};
  std::uint64_t residentRgbaBytes{};

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;

  [[nodiscard]] const LoadedLegacyWeaponCrosshairTexture *
  texture(LegacyWeaponCrosshairTextureRole role) const noexcept;
};

struct LegacyWeaponCrosshairTextureLoadLimits final {
  render::GtiUploadDataLimits gti{};
  std::size_t maximumSourceBytes{1U * 1024U * 1024U};
  std::uint64_t maximumSingleSourceFootprintBytes{2U * 1024U * 1024U};
  std::uint64_t maximumTotalSourceFootprintBytes{6U * 1024U * 1024U};
  std::uint64_t maximumDecodedRgbaBytes{4U * 1024U * 1024U};
  std::uint64_t maximumUploadRgbaBytes{4U * 1024U * 1024U};
  std::uint64_t maximumResidentRgbaBytes{4U * 1024U * 1024U};
};

enum class LegacyWeaponCrosshairTextureLoadPhase : std::uint8_t {
  validatingInput,
  lookingUpTextures,
  readingTextures,
  preparingTextures,
  complete,
};

struct LegacyWeaponCrosshairTextureLoadProgress final {
  LegacyWeaponCrosshairTextureLoadPhase phase{};
  std::size_t completedItems{};
  std::size_t totalItems{};
};

using LegacyWeaponCrosshairTextureLoadProgressCallback =
    std::function<void(const LegacyWeaponCrosshairTextureLoadProgress &)>;

enum class LegacyWeaponCrosshairTextureLoadIssueKind : std::uint8_t {
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

struct LegacyWeaponCrosshairTextureLoadIssue final {
  LegacyWeaponCrosshairTextureLoadIssueKind kind{
      LegacyWeaponCrosshairTextureLoadIssueKind::internalFailure};
  std::optional<LegacyWeaponCrosshairTextureRole> role;
  std::optional<std::size_t> sourceFileIndex;
  std::optional<render::GtiUploadDataIssueKind> preparationIssue;
};

struct LegacyWeaponCrosshairTextureLoadResult final {
  // Published only when all three recovered sight roles pass lookup, source
  // budgets, GTI decoding, exact format/dimensions, aggregate budgets and
  // unchanged authenticated-session checks.
  std::optional<LoadedLegacyWeaponCrosshairTextureSet> textures;
  std::vector<LegacyWeaponCrosshairTextureLoadIssue> issues;

  [[nodiscard]] bool success() const noexcept {
    return textures.has_value() && issues.empty();
  }
};

// Loads the three recovered MG/RO/BO sight GTIs through the exact authenticated
// Resource.up handle owned by session. No logical path is opened as a host path
// and no partial set is published on failure. Selected live weapon mapping and
// GPU upload remain separate consumers.
[[nodiscard]] LegacyWeaponCrosshairTextureLoadResult
loadLegacyWeaponCrosshairTextures(
    VerifiedContentSession &session,
    const LegacyWeaponCrosshairTextureLoadLimits &limits = {},
    std::stop_token stopToken = {},
    LegacyWeaponCrosshairTextureLoadProgressCallback progress = {});

} // namespace airfix::content
