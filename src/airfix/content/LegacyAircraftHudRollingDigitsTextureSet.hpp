#pragma once

#include "airfix/content/VerifiedContentSession.hpp"
#include "airfix/render/LegacyAircraftHudRollingDigits.hpp"
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

enum class LegacyAircraftHudRollingDigitsTextureRole : std::uint8_t {
  digits,
};

inline constexpr std::size_t legacyAircraftHudRollingDigitsTextureCount = 1U;
inline constexpr std::uint32_t legacyAircraftHudRollingDigitsTextureFormat = 8U;
inline constexpr std::uint32_t legacyAircraftHudRollingDigitsTextureWidth =
    render::recovered_legacy_aircraft_hud_rolling_digits::atlasWidth;
inline constexpr std::uint32_t legacyAircraftHudRollingDigitsTextureHeight =
    render::recovered_legacy_aircraft_hud_rolling_digits::atlasHeight;

struct LoadedLegacyAircraftHudRollingDigitsTexture final {
  LegacyAircraftHudRollingDigitsTextureRole role{
      LegacyAircraftHudRollingDigitsTextureRole::digits};
  // Dense only inside this dedicated HUD texture set. It is not a scene
  // TextureAssetId and must not be merged into another namespace by value.
  render::TextureAssetId textureId{};
  std::size_t sourceFileIndex{};
  std::uint64_t sourceFootprintBytes{};
  std::string logicalPath;
  render::GtiUploadPlan upload;
  std::vector<assets::RgbaImage> uploadLevels;
};

using LegacyAircraftHudRollingDigitsTextures =
    std::array<LoadedLegacyAircraftHudRollingDigitsTexture,
               legacyAircraftHudRollingDigitsTextureCount>;

struct LoadedLegacyAircraftHudRollingDigitsTextureSet final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  LegacyAircraftHudRollingDigitsTextures textures;
  std::uint64_t sourceFootprintBytes{};
  std::uint64_t decodedRgbaBytes{};
  std::uint64_t uploadRgbaBytes{};
  std::uint64_t residentRgbaBytes{};

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;

  [[nodiscard]] const LoadedLegacyAircraftHudRollingDigitsTexture *
  texture(LegacyAircraftHudRollingDigitsTextureRole role) const noexcept;
};

struct LegacyAircraftHudRollingDigitsTextureLoadLimits final {
  render::GtiUploadDataLimits gti{};
  std::size_t maximumSourceBytes{1U * 1024U * 1024U};
  std::uint64_t maximumSourceFootprintBytes{2U * 1024U * 1024U};
  std::uint64_t maximumDecodedRgbaBytes{1U * 1024U * 1024U};
  std::uint64_t maximumUploadRgbaBytes{1U * 1024U * 1024U};
  std::uint64_t maximumResidentRgbaBytes{1U * 1024U * 1024U};
};

enum class LegacyAircraftHudRollingDigitsTextureLoadPhase : std::uint8_t {
  validatingInput,
  lookingUpTexture,
  readingTexture,
  preparingTexture,
  complete,
};

struct LegacyAircraftHudRollingDigitsTextureLoadProgress final {
  LegacyAircraftHudRollingDigitsTextureLoadPhase phase{};
  std::size_t completedItems{};
  std::size_t totalItems{};
};

using LegacyAircraftHudRollingDigitsTextureLoadProgressCallback =
    std::function<void(
        const LegacyAircraftHudRollingDigitsTextureLoadProgress &)>;

enum class LegacyAircraftHudRollingDigitsTextureLoadIssueKind : std::uint8_t {
  cancelled,
  invalidLimits,
  sessionIdentityChanged,
  textureNotFound,
  textureAmbiguous,
  sourceLimitExceeded,
  sourceFootprintLimitExceeded,
  sourceReadFailure,
  texturePreparationFailure,
  unexpectedFormat,
  unexpectedDimensions,
  decodedRgbaLimitExceeded,
  uploadRgbaLimitExceeded,
  residentRgbaLimitExceeded,
  allocationFailure,
  progressCallbackFailure,
  internalFailure,
};

struct LegacyAircraftHudRollingDigitsTextureLoadIssue final {
  LegacyAircraftHudRollingDigitsTextureLoadIssueKind kind{
      LegacyAircraftHudRollingDigitsTextureLoadIssueKind::internalFailure};
  std::optional<std::size_t> sourceFileIndex;
  std::optional<render::GtiUploadDataIssueKind> preparationIssue;
};

struct LegacyAircraftHudRollingDigitsTextureLoadResult final {
  // Published only after the exact owner-local atlas passes lookup, source
  // budgets, GTI decoding, format/dimensions and unchanged-session checks.
  std::optional<LoadedLegacyAircraftHudRollingDigitsTextureSet> textures;
  std::vector<LegacyAircraftHudRollingDigitsTextureLoadIssue> issues;

  [[nodiscard]] bool success() const noexcept {
    return textures.has_value() && issues.empty();
  }
};

// Loads the recovered vertical digits atlas through the authenticated
// Resource.up handle owned by session. The logical name is never opened as a
// host path, and no texture payload is published on any failure.
[[nodiscard]] LegacyAircraftHudRollingDigitsTextureLoadResult
loadLegacyAircraftHudRollingDigitsTexture(
    VerifiedContentSession &session,
    const LegacyAircraftHudRollingDigitsTextureLoadLimits &limits = {},
    std::stop_token stopToken = {},
    LegacyAircraftHudRollingDigitsTextureLoadProgressCallback progress = {});

} // namespace airfix::content
