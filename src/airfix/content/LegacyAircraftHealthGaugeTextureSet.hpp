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

enum class LegacyAircraftHealthGaugeTextureRole : std::uint8_t {
  background,
  foreground,
};

enum class LegacyAircraftHealthGaugeTextureArchive : std::uint8_t {
  source,
  localization,
};

inline constexpr std::size_t legacyAircraftHealthGaugeTextureCount = 2U;
inline constexpr std::uint32_t legacyAircraftHealthGaugeTextureFormat = 8U;
inline constexpr std::uint32_t legacyAircraftHealthGaugeTextureWidth = 128U;
inline constexpr std::uint32_t legacyAircraftHealthGaugeTextureHeight = 128U;

struct LoadedLegacyAircraftHealthGaugeTexture final {
  LegacyAircraftHealthGaugeTextureRole role{
      LegacyAircraftHealthGaugeTextureRole::background};
  LegacyAircraftHealthGaugeTextureArchive archive{
      LegacyAircraftHealthGaugeTextureArchive::source};
  // Dense only inside this dedicated HUD texture set. It is not a scene
  // TextureAssetId and must not be merged into a room's namespace by value.
  render::TextureAssetId textureId{};
  std::size_t sourceFileIndex{};
  std::uint64_t sourceFootprintBytes{};
  std::string logicalPath;
  render::GtiUploadPlan upload;
  std::vector<assets::RgbaImage> uploadLevels;
};

using LegacyAircraftHealthGaugeTextures =
    std::array<LoadedLegacyAircraftHealthGaugeTexture,
               legacyAircraftHealthGaugeTextureCount>;

struct LoadedLegacyAircraftHealthGaugeTextureSet final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  LegacyAircraftHealthGaugeTextures textures;
  std::uint64_t sourceFootprintBytes{};
  std::uint64_t decodedRgbaBytes{};
  std::uint64_t uploadRgbaBytes{};
  std::uint64_t residentRgbaBytes{};

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;

  [[nodiscard]] const LoadedLegacyAircraftHealthGaugeTexture *
  texture(LegacyAircraftHealthGaugeTextureRole role) const noexcept;
};

struct LegacyAircraftHealthGaugeTextureLoadLimits final {
  render::GtiUploadDataLimits gti{};
  std::size_t maximumSourceBytes{2U * 1024U * 1024U};
  std::uint64_t maximumSingleSourceFootprintBytes{4U * 1024U * 1024U};
  std::uint64_t maximumTotalSourceFootprintBytes{8U * 1024U * 1024U};
  std::uint64_t maximumDecodedRgbaBytes{4U * 1024U * 1024U};
  std::uint64_t maximumUploadRgbaBytes{4U * 1024U * 1024U};
  std::uint64_t maximumResidentRgbaBytes{4U * 1024U * 1024U};
};

enum class LegacyAircraftHealthGaugeTextureLoadPhase : std::uint8_t {
  validatingInput,
  lookingUpTextures,
  readingTextures,
  preparingTextures,
  complete,
};

struct LegacyAircraftHealthGaugeTextureLoadProgress final {
  LegacyAircraftHealthGaugeTextureLoadPhase phase{};
  std::size_t completedItems{};
  std::size_t totalItems{};
};

using LegacyAircraftHealthGaugeTextureLoadProgressCallback =
    std::function<void(const LegacyAircraftHealthGaugeTextureLoadProgress &)>;

enum class LegacyAircraftHealthGaugeTextureLoadIssueKind : std::uint8_t {
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

struct LegacyAircraftHealthGaugeTextureLoadIssue final {
  LegacyAircraftHealthGaugeTextureLoadIssueKind kind{
      LegacyAircraftHealthGaugeTextureLoadIssueKind::internalFailure};
  std::optional<LegacyAircraftHealthGaugeTextureRole> role;
  std::optional<LegacyAircraftHealthGaugeTextureArchive> archive;
  std::optional<std::size_t> archiveFileIndex;
  std::optional<render::GtiUploadDataIssueKind> preparationIssue;
};

struct LegacyAircraftHealthGaugeTextureLoadResult final {
  // Published only when both recovered gauge textures pass lookup, source
  // budgets, GTI decoding, exact format/dimensions, aggregate budgets and
  // unchanged authenticated-session checks.
  std::optional<LoadedLegacyAircraftHealthGaugeTextureSet> textures;
  std::vector<LegacyAircraftHealthGaugeTextureLoadIssue> issues;

  [[nodiscard]] bool success() const noexcept {
    return textures.has_value() && issues.empty();
  }
};

// Loads the recovered armour-meter background from Resource.up and foreground
// from the selected localization archive through the exact authenticated
// handle owned by session. No logical path is opened as a host path and no
// partial set is published on failure. GPU upload and drawing remain separate
// consumers.
[[nodiscard]] LegacyAircraftHealthGaugeTextureLoadResult
loadLegacyAircraftHealthGaugeTextures(
    VerifiedContentSession &session,
    const LegacyAircraftHealthGaugeTextureLoadLimits &limits = {},
    std::stop_token stopToken = {},
    LegacyAircraftHealthGaugeTextureLoadProgressCallback progress = {});

} // namespace airfix::content
