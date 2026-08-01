#pragma once

#include "airfix/content/LegacyAircraftHudIdentityStatusTextureSet.hpp"
#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/render/LegacyAircraftHudIdentityStatus.hpp"
#include "airfix/render/NativeRenderLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

enum class LegacyAircraftHudIdentityStatusTextureNamespace : std::uint8_t {
  identityStatus,
  rollingDigits,
};

enum class LegacyAircraftHudIdentityStatusBlendMode : std::uint8_t {
  sourceAlphaOneMinusSourceAlpha,
};

enum class LegacyAircraftHudIdentityStatusDepthMode : std::uint8_t {
  alwaysWrite,
};

enum class LegacyAircraftHudIdentityStatusSamplingMode : std::uint8_t {
  linearClamp,
};

struct LegacyAircraftHudIdentityStatusUvRect final {
  float minimumU{};
  float minimumV{};
  float maximumU{};
  float maximumV{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudIdentityStatusUvRect &,
             const LegacyAircraftHudIdentityStatusUvRect &) noexcept = default;
};

struct LegacyAircraftHudIdentityStatusSubmissionCommand final {
  render::LegacyAircraftHudIdentityStatusCommandKind kind{
      render::LegacyAircraftHudIdentityStatusCommandKind::aircraftIcon};
  render::LegacyAircraftHudTeamBadge teamBadge{
      render::LegacyAircraftHudTeamBadge::technologyStar};
  LegacyAircraftHudIdentityStatusTextureNamespace textureNamespace{
      LegacyAircraftHudIdentityStatusTextureNamespace::identityStatus};
  render::TextureAssetId textureId{};
  std::uint32_t sourceTextureIndex{};
  std::uint8_t digitSlotIndex{};
  render::OutputPixelRect outputRect{};
  LegacyAircraftHudIdentityStatusUvRect uv{};
  std::uint32_t colourArgb{};
  LegacyAircraftHudIdentityStatusBlendMode blendMode{
      LegacyAircraftHudIdentityStatusBlendMode::sourceAlphaOneMinusSourceAlpha};
  LegacyAircraftHudIdentityStatusDepthMode depthMode{
      LegacyAircraftHudIdentityStatusDepthMode::alwaysWrite};
  LegacyAircraftHudIdentityStatusSamplingMode samplingMode{
      LegacyAircraftHudIdentityStatusSamplingMode::linearClamp};
};

struct LegacyAircraftHudIdentityStatusSubmission final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  float uiScalePercent{100.0F};
  std::array<
      LegacyAircraftHudIdentityStatusSubmissionCommand,
      render::recovered_legacy_aircraft_hud_identity_status::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};

  [[nodiscard]] const LegacyAircraftHudIdentityStatusSubmissionCommand *
  command(std::size_t index) const noexcept;
  [[nodiscard]] bool belongsTo(
      const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures,
      const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures)
      const noexcept;
};

enum class LegacyAircraftHudIdentityStatusSubmissionStatus : std::uint8_t {
  ready,
  planNotReady,
  invalidIdentityTextureSet,
  invalidDigitTextureSet,
  textureOwnersMismatch,
  incompatibleLegacyScreenExtent,
  incompatibleUiDesignExtent,
  uiScaleOutOfRange,
  invalidPlanCommand,
  outputMappingFailed,
};

struct LegacyAircraftHudIdentityStatusSubmissionResult final {
  LegacyAircraftHudIdentityStatusSubmissionStatus status{
      LegacyAircraftHudIdentityStatusSubmissionStatus::planNotReady};
  std::optional<LegacyAircraftHudIdentityStatusSubmission> submission;

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyAircraftHudIdentityStatusSubmissionStatus::ready &&
           submission.has_value();
  }
};

// Resolves every command against one immutable aircraft/team texture owner and
// the shared rolling-digit atlas, then maps the recovered logical geometry to
// native output pixels. Every layer retains source-alpha blending, linear
// clamp sampling, and the native always-write HUD depth state.
[[nodiscard]] LegacyAircraftHudIdentityStatusSubmissionResult
buildLegacyAircraftHudIdentityStatusSubmission(
    const render::LegacyAircraftHudIdentityStatusPlan &plan,
    const LoadedLegacyAircraftHudIdentityStatusTextureSet &identityTextures,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &digitTextures,
    const render::NativeRenderLayout &layout, float uiScalePercent) noexcept;

} // namespace airfix::content
