#pragma once

#include "airfix/render/NativeGameplayScreenProjection.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyWeaponCrosshairSizeState final {
  float currentLogicalWidth{};
  float currentLogicalHeight{};
  float targetLogicalWidth{};
  float targetLogicalHeight{};
  bool resetCurrentSize{true};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyWeaponCrosshairSizeState &,
             const LegacyWeaponCrosshairSizeState &) noexcept = default;
};

struct LegacyWeaponCrosshairProjectionInput final {
  Vec3 targetWorldPosition{};
  std::uint32_t originalTextureWidth{};
  std::uint32_t originalTextureHeight{};
  // Native RenderCrosshair uses 1.0 when the aim line has no BSP hit and
  // otherwise uses the PhLine hit fraction.
  float collisionFraction{1.0F};
  // Local Z of the recovered aim offset after the owner-rotation round trip.
  float localAimOffsetZ{1.0F};
  float uiScalePercent{native_render_policy::defaultUiScalePercent};
};

enum class LegacyWeaponCrosshairProjectionIssueKind : std::uint8_t {
  textureExtentNotPositive,
  nonFiniteInput,
  collisionFractionOutOfRange,
  uiScaleOutOfRange,
  nonPositiveLogicalSize,
  screenProjectionFailed,
  nonFiniteOutput,
};

struct LegacyWeaponCrosshairProjectionIssue final {
  LegacyWeaponCrosshairProjectionIssueKind kind{
      LegacyWeaponCrosshairProjectionIssueKind::nonFiniteInput};
  std::optional<NativeGameplayScreenProjectionIssue> screenProjectionIssue;
};

struct LegacyWeaponCrosshairSpritePlan final {
  NativeGameplayScreenProjectedWorldPoint projectedTarget{};
  OutputPixelRect outputRect{};
  float logicalDistanceScale{};
  bool insideSceneViewport{};
  bool withinRecoveredDepthRange{};

  [[nodiscard]] constexpr bool recoveredVisibilitySatisfied() const noexcept {
    return insideSceneViewport && withinRecoveredDepthRange;
  }
};

struct LegacyWeaponCrosshairProjectionResult final {
  LegacyWeaponCrosshairSizeState state{};
  std::optional<LegacyWeaponCrosshairSpritePlan> plan;
  std::optional<LegacyWeaponCrosshairProjectionIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return plan.has_value() && !issue.has_value();
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return complete();
  }
};

// Mirrors AfWeapon::Activate's first-visible-size latch without changing the
// retained current or target dimensions. The next successful projection
// adopts its newly calculated target dimensions as the current dimensions.
[[nodiscard]] constexpr LegacyWeaponCrosshairSizeState
activateLegacyWeaponCrosshair(LegacyWeaponCrosshairSizeState state) noexcept {
  state.resetCurrentSize = true;
  return state;
}

// Composes the recovered AfWeapon::RenderCrosshair final sizing/centering
// stage with the native output-pixel projection. The recovered logical size is
// scaled only by the UI scale and never by the 3D render scale. Off-screen and
// out-of-depth results remain complete plans with explicit labels; the caller
// owns the eventual hide/indicator policy.
[[nodiscard]] LegacyWeaponCrosshairProjectionResult
projectLegacyWeaponCrosshairToOutput(
    const LegacyGameplayCameraClipPacket &camera,
    const NativeRenderLayout &layout,
    const LegacyWeaponCrosshairSizeState &state,
    const LegacyWeaponCrosshairProjectionInput &input) noexcept;

} // namespace airfix::render
