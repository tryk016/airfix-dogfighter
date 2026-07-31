#pragma once

#include "airfix/content/LegacyWeaponCrosshairBinding.hpp"
#include "airfix/render/LegacyWeaponCrosshairProjection.hpp"

#include <cstdint>
#include <optional>

namespace airfix::content {

// Visibility is deliberately supplied by the caller. The recovered projection
// labels off-screen and out-of-depth results, but it does not prove whether the
// original game hid, clipped, or replaced those results with another marker.
enum class LegacyWeaponCrosshairVisibilityDecision : std::uint8_t {
  suppress,
  draw,
};

enum class LegacyWeaponCrosshairBlendMode : std::uint8_t {
  sourceAlphaOneMinusSourceAlpha,
};

enum class LegacyWeaponCrosshairDepthMode : std::uint8_t {
  alwaysWrite,
};

inline constexpr std::uint32_t legacyWeaponCrosshairTintArgb = 0x7FFFFFFFU;

struct LegacyWeaponCrosshairUvRect final {
  float minimumU{};
  float minimumV{};
  float maximumU{1.0F};
  float maximumV{1.0F};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyWeaponCrosshairUvRect &,
             const LegacyWeaponCrosshairUvRect &) noexcept = default;
};

struct LegacyWeaponCrosshairSpriteSubmission final {
  simulation::LegacyWeaponTypeId weaponType;
  LegacyWeaponCrosshairTextureRole role;
  render::TextureAssetId textureId;
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  render::OutputPixelRect outputRect{};
  LegacyWeaponCrosshairUvRect uv{};
  std::uint32_t tintArgb{legacyWeaponCrosshairTintArgb};
  LegacyWeaponCrosshairBlendMode blendMode{
      LegacyWeaponCrosshairBlendMode::sourceAlphaOneMinusSourceAlpha};
  LegacyWeaponCrosshairDepthMode depthMode{
      LegacyWeaponCrosshairDepthMode::alwaysWrite};
  bool insideSceneViewport{};
  bool withinRecoveredDepthRange{};

  [[nodiscard]] bool belongsTo(
      const LoadedLegacyWeaponCrosshairTextureSet &textures) const noexcept;
};

enum class LegacyWeaponCrosshairSpriteSubmissionStatus : std::uint8_t {
  ready,
  suppressed,
  invalidBinding,
  invalidOutputRectangle,
};

struct LegacyWeaponCrosshairSpriteSubmissionResult final {
  LegacyWeaponCrosshairSpriteSubmissionStatus status{
      LegacyWeaponCrosshairSpriteSubmissionStatus::invalidBinding};
  std::optional<LegacyWeaponCrosshairSpriteSubmission> submission;

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyWeaponCrosshairSpriteSubmissionStatus::ready &&
           submission.has_value();
  }
};

// Converts an authenticated weapon/texture binding and a projected rectangle
// into the value-only packet consumed by native backends. The caller must make
// the visibility decision explicitly; draw never silently reinterprets the two
// recovered visibility labels. Backends must revalidate belongsTo() against
// their currently installed immutable texture set before indexing GPU state.
[[nodiscard]] LegacyWeaponCrosshairSpriteSubmissionResult
buildLegacyWeaponCrosshairSpriteSubmission(
    const render::LegacyWeaponCrosshairSpritePlan &plan,
    const LegacyWeaponCrosshairBinding &binding,
    LegacyWeaponCrosshairVisibilityDecision visibility) noexcept;

} // namespace airfix::content
