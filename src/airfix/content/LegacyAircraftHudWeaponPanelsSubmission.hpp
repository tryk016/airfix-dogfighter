#pragma once

#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudWeaponPanelTextureSet.hpp"
#include "airfix/render/LegacyAircraftHudWeaponPanels.hpp"
#include "airfix/render/NativeRenderLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

enum class LegacyAircraftHudWeaponPanelTextureNamespace : std::uint8_t {
  none,
  weaponPanels,
  rollingDigits,
};

enum class LegacyAircraftHudWeaponPanelBlendMode : std::uint8_t {
  sourceAlphaOneMinusSourceAlpha,
  destinationMultiplySourceColour,
};

enum class LegacyAircraftHudWeaponPanelDepthMode : std::uint8_t {
  alwaysWrite,
};

enum class LegacyAircraftHudWeaponPanelSamplingMode : std::uint8_t {
  notApplicable,
  linearClamp,
};

struct LegacyAircraftHudWeaponPanelUvRect final {
  float minimumU{};
  float minimumV{};
  float maximumU{};
  float maximumV{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudWeaponPanelUvRect &,
             const LegacyAircraftHudWeaponPanelUvRect &) noexcept = default;
};

struct LegacyAircraftHudWeaponPanelSubmissionCommand final {
  render::LegacyAircraftHudWeaponPanelSlot slot{
      render::LegacyAircraftHudWeaponPanelSlot::primary};
  render::LegacyAircraftHudWeaponPanelCommandKind kind{
      render::LegacyAircraftHudWeaponPanelCommandKind::background};
  LegacyAircraftHudWeaponPanelTextureNamespace textureNamespace{
      LegacyAircraftHudWeaponPanelTextureNamespace::none};
  render::TextureAssetId textureId{};
  std::uint32_t sourceTextureIndex{};
  std::uint8_t digitSlotIndex{};
  render::OutputPixelRect outputRect{};
  LegacyAircraftHudWeaponPanelUvRect uv{};
  std::uint32_t colourArgb{};
  LegacyAircraftHudWeaponPanelBlendMode blendMode{
      LegacyAircraftHudWeaponPanelBlendMode::sourceAlphaOneMinusSourceAlpha};
  LegacyAircraftHudWeaponPanelDepthMode depthMode{
      LegacyAircraftHudWeaponPanelDepthMode::alwaysWrite};
  LegacyAircraftHudWeaponPanelSamplingMode samplingMode{
      LegacyAircraftHudWeaponPanelSamplingMode::linearClamp};

  [[nodiscard]] constexpr bool textured() const noexcept {
    return textureNamespace !=
           LegacyAircraftHudWeaponPanelTextureNamespace::none;
  }
};

struct LegacyAircraftHudWeaponPanelsSubmission final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  float uiScalePercent{100.0F};
  std::array<
      LegacyAircraftHudWeaponPanelSubmissionCommand,
      render::recovered_legacy_aircraft_hud_weapon_panels::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};

  [[nodiscard]] const LegacyAircraftHudWeaponPanelSubmissionCommand *
  command(std::size_t index) const noexcept;
  [[nodiscard]] bool
  belongsTo(const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
            const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures)
      const noexcept;
};

enum class LegacyAircraftHudWeaponPanelsSubmissionStatus : std::uint8_t {
  ready,
  planNotReady,
  invalidWeaponTextureSet,
  invalidDigitTextureSet,
  textureOwnersMismatch,
  incompatibleLegacyScreenExtent,
  incompatibleUiDesignExtent,
  uiScaleOutOfRange,
  invalidPlanCommand,
  outputMappingFailed,
};

struct LegacyAircraftHudWeaponPanelsSubmissionResult final {
  LegacyAircraftHudWeaponPanelsSubmissionStatus status{
      LegacyAircraftHudWeaponPanelsSubmissionStatus::planNotReady};
  std::optional<LegacyAircraftHudWeaponPanelsSubmission> submission;

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyAircraftHudWeaponPanelsSubmissionStatus::ready &&
           submission.has_value();
  }
};

// Resolves every recovered command against two immutable authenticated owners:
// the weapon-panel set for backgrounds/icons and the shared digit atlas for
// ammo. It maps only logical UI geometry into output pixels. The status quad
// deliberately retains native GT_ALPHA mode 2 as destination multiplied by
// source colour; it is not reinterpreted as ordinary source-alpha blending.
[[nodiscard]] LegacyAircraftHudWeaponPanelsSubmissionResult
buildLegacyAircraftHudWeaponPanelsSubmission(
    const render::LegacyAircraftHudWeaponPanelsPlan &plan,
    const LoadedLegacyAircraftHudWeaponPanelTextureSet &weaponTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
    const render::NativeRenderLayout &layout, float uiScalePercent) noexcept;

} // namespace airfix::content
