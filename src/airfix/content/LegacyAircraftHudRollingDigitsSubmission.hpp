#pragma once

#include "airfix/content/LegacyAircraftHudRollingDigitsTextureSet.hpp"
#include "airfix/render/NativeRenderLayout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

enum class LegacyAircraftHudRollingDigitsBlendMode : std::uint8_t {
  sourceAlphaOneMinusSourceAlpha,
};

enum class LegacyAircraftHudRollingDigitsDepthMode : std::uint8_t {
  alwaysWrite,
};

enum class LegacyAircraftHudRollingDigitsSamplingMode : std::uint8_t {
  linearClamp,
};

struct LegacyAircraftHudRollingDigitsUvRect final {
  float minimumU{};
  float minimumV{};
  float maximumU{};
  float maximumV{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftHudRollingDigitsUvRect &,
             const LegacyAircraftHudRollingDigitsUvRect &) noexcept = default;
};

struct LegacyAircraftHudRollingDigitsSubmissionCommand final {
  std::uint8_t slotIndex{};
  LegacyAircraftHudRollingDigitsTextureRole textureRole{
      LegacyAircraftHudRollingDigitsTextureRole::digits};
  render::TextureAssetId textureId{};
  render::OutputPixelRect outputRect{};
  LegacyAircraftHudRollingDigitsUvRect uv{};
  std::uint32_t tintArgb{};
  LegacyAircraftHudRollingDigitsBlendMode blendMode{
      LegacyAircraftHudRollingDigitsBlendMode::sourceAlphaOneMinusSourceAlpha};
  LegacyAircraftHudRollingDigitsDepthMode depthMode{
      LegacyAircraftHudRollingDigitsDepthMode::alwaysWrite};
  LegacyAircraftHudRollingDigitsSamplingMode samplingMode{
      LegacyAircraftHudRollingDigitsSamplingMode::linearClamp};
};

struct LegacyAircraftHudRollingDigitsSubmission final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  float uiScalePercent{100.0F};
  std::array<LegacyAircraftHudRollingDigitsSubmissionCommand,
             render::recovered_legacy_aircraft_hud_rolling_digits::digitCount>
      orderedCommands{};
  std::size_t commandCount{};

  [[nodiscard]] const LegacyAircraftHudRollingDigitsSubmissionCommand *
  command(std::size_t index) const noexcept;

  [[nodiscard]] bool
  belongsTo(const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures)
      const noexcept;
};

enum class LegacyAircraftHudRollingDigitsSubmissionStatus : std::uint8_t {
  ready,
  planNotReady,
  invalidTextureSet,
  incompatibleUiDesignExtent,
  uiScaleOutOfRange,
  invalidPlanCommand,
  outputMappingFailed,
};

struct LegacyAircraftHudRollingDigitsSubmissionResult final {
  LegacyAircraftHudRollingDigitsSubmissionStatus status{
      LegacyAircraftHudRollingDigitsSubmissionStatus::planNotReady};
  std::optional<LegacyAircraftHudRollingDigitsSubmission> submission;

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyAircraftHudRollingDigitsSubmissionStatus::ready &&
           submission.has_value();
  }
};

// Binds one recovered value-only atlas plan to the immutable authenticated
// texture owner and maps the legacy 640x480 widget into native output pixels.
// Independent UI scale is applied around the caller-supplied widget origin,
// reconstructed from each native slot command. No live gameplay value, clock,
// visibility gate or render-event order is inferred here.
[[nodiscard]] LegacyAircraftHudRollingDigitsSubmissionResult
buildLegacyAircraftHudRollingDigitsSubmission(
    const render::LegacyAircraftHudRollingDigitsPlan &plan,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures,
    const render::NativeRenderLayout &layout, float uiScalePercent) noexcept;

} // namespace airfix::content
