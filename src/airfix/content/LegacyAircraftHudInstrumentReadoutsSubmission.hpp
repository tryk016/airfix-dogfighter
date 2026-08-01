#pragma once

#include "airfix/content/LegacyAircraftHudRollingDigitsSubmission.hpp"
#include "airfix/render/LegacyAircraftHudInstrumentReadouts.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::content {

struct LegacyAircraftHudInstrumentReadoutSubmission final {
  render::LegacyAircraftHudInstrumentReadoutSide side{
      render::LegacyAircraftHudInstrumentReadoutSide::right};
  std::optional<LegacyAircraftHudRollingDigitsSubmission> digits;
};

struct LegacyAircraftHudInstrumentReadoutsSubmission final {
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;
  float uiScalePercent{100.0F};
  std::array<LegacyAircraftHudInstrumentReadoutSubmission,
             render::recovered_legacy_aircraft_hud_instrument_readouts::
                 maximumReadouts>
      orderedReadouts{};
  std::size_t readoutCount{};

  [[nodiscard]] const LegacyAircraftHudInstrumentReadoutSubmission *
  readout(std::size_t index) const noexcept;

  [[nodiscard]] bool
  belongsTo(const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures)
      const noexcept;
};

enum class LegacyAircraftHudInstrumentReadoutsSubmissionStatus : std::uint8_t {
  ready,
  planNotReady,
  invalidTextureSet,
  incompatibleUiDesignExtent,
  uiScaleOutOfRange,
  invalidReadoutPlan,
  readoutSubmissionRejected,
};

struct LegacyAircraftHudInstrumentReadoutsSubmissionResult final {
  LegacyAircraftHudInstrumentReadoutsSubmissionStatus status{
      LegacyAircraftHudInstrumentReadoutsSubmissionStatus::planNotReady};
  std::optional<LegacyAircraftHudInstrumentReadoutsSubmission> submission;
  std::optional<render::LegacyAircraftHudInstrumentReadoutSide> rejectedSide;
  std::optional<LegacyAircraftHudRollingDigitsSubmissionStatus> readoutStatus;

  [[nodiscard]] bool ready() const noexcept {
    return status ==
               LegacyAircraftHudInstrumentReadoutsSubmissionStatus::ready &&
           submission.has_value() && !rejectedSide.has_value() &&
           !readoutStatus.has_value();
  }
};

// Atomically maps the recovered right-then-left readout plans through the
// existing authenticated digit-atlas packet. Backends consume each retained
// digits submission with their already implemented generic HUD-digit path;
// this wrapper adds no shader, texture, gameplay producer, or render-event
// wiring.
[[nodiscard]] LegacyAircraftHudInstrumentReadoutsSubmissionResult
buildLegacyAircraftHudInstrumentReadoutsSubmission(
    const render::LegacyAircraftHudInstrumentReadoutsPlan &plan,
    const LoadedLegacyAircraftHudRollingDigitsTextureSet &textures,
    const render::NativeRenderLayout &layout, float uiScalePercent) noexcept;

} // namespace airfix::content
