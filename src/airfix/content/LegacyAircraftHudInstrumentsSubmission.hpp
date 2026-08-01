#pragma once

#include "airfix/content/LegacyAircraftHudInstrumentsTextureSet.hpp"
#include "airfix/render/LegacyAircraftHudInstruments.hpp"
#include "airfix/render/NativeRenderLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

enum class LegacyAircraftHudInstrumentBlendMode : std::uint8_t {
  sourceAlphaOneMinusSourceAlpha,
};

enum class LegacyAircraftHudInstrumentDepthMode : std::uint8_t {
  alwaysWrite,
};

enum class LegacyAircraftHudInstrumentSamplingMode : std::uint8_t {
  linearClamp,
};

struct LegacyAircraftHudInstrumentUvRect final {
  float minimumU{};
  float minimumV{};
  float maximumU{};
  float maximumV{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudInstrumentUvRect &,
             const LegacyAircraftHudInstrumentUvRect &) noexcept = default;
};

struct LegacyAircraftHudInstrumentSubmissionCommand final {
  render::LegacyAircraftHudInstrumentSide side{
      render::LegacyAircraftHudInstrumentSide::right};
  render::LegacyAircraftHudInstrumentCommandKind kind{
      render::LegacyAircraftHudInstrumentCommandKind::face};
  LegacyAircraftHudInstrumentTextureRole textureRole{
      LegacyAircraftHudInstrumentTextureRole::rightFace};
  render::TextureAssetId textureId{};
  std::array<render::OutputPixelPoint, 4U> outputQuad{};
  LegacyAircraftHudInstrumentUvRect uv{};
  std::uint32_t tintArgb{};
  LegacyAircraftHudInstrumentBlendMode blendMode{
      LegacyAircraftHudInstrumentBlendMode::sourceAlphaOneMinusSourceAlpha};
  LegacyAircraftHudInstrumentDepthMode depthMode{
      LegacyAircraftHudInstrumentDepthMode::alwaysWrite};
  LegacyAircraftHudInstrumentSamplingMode samplingMode{
      LegacyAircraftHudInstrumentSamplingMode::linearClamp};
};

struct LegacyAircraftHudInstrumentsSubmission final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  float uiScalePercent{100.0F};
  std::array<LegacyAircraftHudInstrumentSubmissionCommand,
             render::recovered_legacy_aircraft_hud_instruments::maximumCommands>
      orderedCommands{};
  std::size_t commandCount{};

  [[nodiscard]] const LegacyAircraftHudInstrumentSubmissionCommand *
  command(std::size_t index) const noexcept;
  [[nodiscard]] bool belongsTo(const LoadedLegacyAircraftHudInstrumentTextureSet
                                   &textures) const noexcept;
};

enum class LegacyAircraftHudInstrumentsSubmissionStatus : std::uint8_t {
  ready,
  planNotReady,
  invalidTextureSet,
  incompatibleLegacyScreenExtent,
  incompatibleUiDesignExtent,
  uiScaleOutOfRange,
  invalidPlanCommand,
  outputMappingFailed,
};

struct LegacyAircraftHudInstrumentsSubmissionResult final {
  LegacyAircraftHudInstrumentsSubmissionStatus status{
      LegacyAircraftHudInstrumentsSubmissionStatus::planNotReady};
  std::optional<LegacyAircraftHudInstrumentsSubmission> submission;

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyAircraftHudInstrumentsSubmissionStatus::ready &&
           submission.has_value();
  }
};

// Binds the recovered two-clock plan to its immutable authenticated textures
// and maps the logical 640x480 quad geometry into native output pixels. UI
// scale is applied around the lower centre of the design canvas, keeping the
// pair symmetric. The packet retains native ordering, tint, alpha blend,
// ALWAYS/write depth and the indicator's 7x30 sub-rectangle.
[[nodiscard]] LegacyAircraftHudInstrumentsSubmissionResult
buildLegacyAircraftHudInstrumentsSubmission(
    const render::LegacyAircraftHudInstrumentsPlan &plan,
    const LoadedLegacyAircraftHudInstrumentTextureSet &textures,
    const render::NativeRenderLayout &layout, float uiScalePercent) noexcept;

} // namespace airfix::content
