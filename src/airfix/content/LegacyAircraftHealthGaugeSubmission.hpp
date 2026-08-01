#pragma once

#include "airfix/content/LegacyAircraftHealthGaugeTextureSet.hpp"
#include "airfix/render/LegacyAircraftHealthGaugePlan.hpp"
#include "airfix/render/NativeRenderLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

enum class LegacyAircraftHealthGaugeBlendMode : std::uint8_t {
  opaque,
  sourceAlphaOneMinusSourceAlpha,
};

enum class LegacyAircraftHealthGaugeDepthMode : std::uint8_t {
  alwaysWrite,
};

enum class LegacyAircraftHealthGaugeSamplingMode : std::uint8_t {
  linearClamp,
};

struct LegacyAircraftHealthGaugeUvRect final {
  float minimumU{};
  float minimumV{};
  float maximumU{1.0F};
  float maximumV{1.0F};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHealthGaugeUvRect &,
             const LegacyAircraftHealthGaugeUvRect &) noexcept = default;
};

struct LegacyAircraftHealthGaugeSubmissionCommand final {
  render::LegacyAircraftHealthGaugeCommandKind kind{
      render::LegacyAircraftHealthGaugeCommandKind::damageMaskQuad};
  LegacyAircraftHealthGaugeTextureRole textureRole{
      LegacyAircraftHealthGaugeTextureRole::background};
  render::TextureAssetId textureId{};
  std::array<render::OutputPixelPoint, 4U> outputQuad{};
  LegacyAircraftHealthGaugeUvRect uv{};
  std::uint32_t colourArgb{};
  LegacyAircraftHealthGaugeBlendMode blendMode{
      LegacyAircraftHealthGaugeBlendMode::opaque};
  LegacyAircraftHealthGaugeDepthMode depthMode{
      LegacyAircraftHealthGaugeDepthMode::alwaysWrite};
  LegacyAircraftHealthGaugeSamplingMode samplingMode{
      LegacyAircraftHealthGaugeSamplingMode::linearClamp};

  [[nodiscard]] constexpr bool textured() const noexcept {
    return kind == render::LegacyAircraftHealthGaugeCommandKind::
                       armourMeterTexture ||
           kind == render::LegacyAircraftHealthGaugeCommandKind::armourTexture;
  }
};

struct LegacyAircraftHealthGaugeSubmission final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  std::array<LegacyAircraftHealthGaugeSubmissionCommand,
             render::recovered_legacy_aircraft_health_gauge::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};
  float uiScalePercent{};

  [[nodiscard]] const LegacyAircraftHealthGaugeSubmissionCommand *
  command(std::size_t index) const noexcept;

  [[nodiscard]] bool belongsTo(
      const LoadedLegacyAircraftHealthGaugeTextureSet &textures) const noexcept;
};

enum class LegacyAircraftHealthGaugeSubmissionStatus : std::uint8_t {
  ready,
  planNotReady,
  invalidTextureSet,
  incompatibleLegacyScreenExtent,
  incompatibleUiDesignExtent,
  uiScaleOutOfRange,
  invalidPlanCommand,
  outputMappingFailed,
};

struct LegacyAircraftHealthGaugeSubmissionResult final {
  LegacyAircraftHealthGaugeSubmissionStatus status{
      LegacyAircraftHealthGaugeSubmissionStatus::planNotReady};
  std::optional<LegacyAircraftHealthGaugeSubmission> submission;

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyAircraftHealthGaugeSubmissionStatus::ready &&
           submission.has_value();
  }
};

// Maps the recovered 640x480 health-gauge plan into physical output pixels.
// The root UI canvas remains aspect-fitted inside the safe area. uiScalePercent
// scales this bottom-left-anchored widget within that stable canvas, as
// required by the independent UI-scale policy. The packet preserves native
// background -> black mask -> foreground order and the recovered blend/depth
// contract. A backend must revalidate belongsTo() before indexing GPU state.
[[nodiscard]] LegacyAircraftHealthGaugeSubmissionResult
buildLegacyAircraftHealthGaugeSubmission(
    const render::LegacyAircraftHealthGaugePlan &plan,
    const LoadedLegacyAircraftHealthGaugeTextureSet &textures,
    const render::NativeRenderLayout &layout, float uiScalePercent) noexcept;

} // namespace airfix::content
