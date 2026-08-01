#include "airfix/render/LegacyAircraftHealthGaugePlan.hpp"

#include <cmath>

namespace airfix::render {
namespace {

using namespace recovered_legacy_aircraft_health_gauge;

struct GaugeEdge final {
  LegacyCanvasPoint outer;
  LegacyCanvasPoint inner;
};

[[nodiscard]] LegacyAircraftHealthGaugePlan
failure(const LegacyAircraftHealthGaugePlanStatus status) noexcept {
  return {.status = status};
}

[[nodiscard]] bool finite(const LegacyCanvasPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] GaugeEdge gaugeEdge(const float centreY,
                                  const double angle) noexcept {
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  const double centreYWide = static_cast<double>(centreY);
  return {
      .outer =
          {
              .x =
                  static_cast<float>(static_cast<double>(centreX) -
                                     cosine * static_cast<double>(outerRadius)),
              .y = static_cast<float>(centreYWide +
                                      sine * static_cast<double>(outerRadius)),
          },
      .inner =
          {
              .x =
                  static_cast<float>(static_cast<double>(centreX) -
                                     cosine * static_cast<double>(innerRadius)),
              .y = static_cast<float>(centreYWide +
                                      sine * static_cast<double>(innerRadius)),
          },
  };
}

[[nodiscard]] bool
appendTexture(LegacyAircraftHealthGaugePlan &plan,
              const LegacyAircraftHealthGaugeCommandKind kind) noexcept {
  if (plan.commandCount >= plan.orderedCommands.size()) {
    return false;
  }
  plan.orderedCommands[plan.commandCount++] = {
      .kind = kind,
      .textureOrigin = plan.textureOrigin,
  };
  return true;
}

[[nodiscard]] bool appendMaskQuad(LegacyAircraftHealthGaugePlan &plan,
                                  const GaugeEdge &start,
                                  const GaugeEdge &end) noexcept {
  if (plan.commandCount >= plan.orderedCommands.size()) {
    return false;
  }
  plan.orderedCommands[plan.commandCount++] = {
      .kind = LegacyAircraftHealthGaugeCommandKind::damageMaskQuad,
      .quad = {start.outer, end.outer, end.inner, start.inner},
      .colourArgb = damageMaskArgb,
  };
  return true;
}

} // namespace

const LegacyAircraftHealthGaugeCommand *
LegacyAircraftHealthGaugePlan::command(const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

LegacyAircraftHealthGaugePlan buildLegacyAircraftHealthGaugePlan(
    const LegacyAircraftHealthGaugeInput &input) noexcept {
  if (input.activeWindowPresent) {
    return failure(LegacyAircraftHealthGaugePlanStatus::activeWindowPresent);
  }
  if (!input.cameraAttachedAtEntry) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::cameraNotAttachedAtEntry);
  }
  if (!input.typeHudEnabled) {
    return failure(LegacyAircraftHealthGaugePlanStatus::typeHudDisabled);
  }
  if (!input.cameraAttachedAfterLayout) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::cameraDetachedBeforeDraw);
  }
  if (input.screenWidth == 0U || input.screenHeight == 0U) {
    return failure(LegacyAircraftHealthGaugePlanStatus::invalidScreenExtent);
  }
  if (!std::isfinite(input.displayedHealth) ||
      !std::isfinite(input.maximumHealth)) {
    return failure(LegacyAircraftHealthGaugePlanStatus::nonFiniteHealth);
  }
  if (!(input.maximumHealth > 0.0F)) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::maximumHealthNotPositive);
  }
  if (input.displayedHealth < 0.0F ||
      input.displayedHealth > input.maximumHealth) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::displayedHealthOutOfRange);
  }

  const float screenWidth = static_cast<float>(input.screenWidth);
  const float screenHeight = static_cast<float>(input.screenHeight);
  if (!std::isfinite(screenWidth) || !std::isfinite(screenHeight)) {
    return failure(LegacyAircraftHealthGaugePlanStatus::invalidScreenExtent);
  }

  const float top = screenWidth < narrowScreenThreshold
                        ? narrowScreenTop
                        : screenHeight - normalScreenBottomInset;
  const float centreY = top + centreYOffset;

  // The native x87 chain computes
  // (1 - ((displayedHealth * 100) / maximumHealth) * 0.01) * pi
  // and spills once to binary32. Double staging retains that operation order
  // without making this visual-only plan depend on a process x87 policy.
  const double healthPercent =
      (static_cast<double>(input.displayedHealth) * 100.0) /
      static_cast<double>(input.maximumHealth);
  const float damageSweep = static_cast<float>((1.0 - healthPercent * 0.01) *
                                               static_cast<double>(piRadians));
  const LegacyCanvasPoint origin{textureLeft, top};
  if (!std::isfinite(top) || !std::isfinite(centreY) ||
      !std::isfinite(damageSweep) || !finite(origin)) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::derivedGeometryNotFinite);
  }

  LegacyAircraftHealthGaugePlan plan{
      .status = LegacyAircraftHealthGaugePlanStatus::ready,
      .damageSweepRadians = damageSweep,
      .textureOrigin = origin,
  };
  if (input.armourMeterTextureAvailable &&
      !appendTexture(
          plan, LegacyAircraftHealthGaugeCommandKind::armourMeterTexture)) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::commandCapacityExceeded);
  }

  GaugeEdge start = gaugeEdge(centreY, startAngleRadians);
  const float loopLimit = damageSweep - segmentStepRadians;
  float cursor = 0.0F;
  if (0.0F < loopLimit) {
    do {
      const double cursorWide =
          static_cast<double>(cursor) + static_cast<double>(segmentStepRadians);
      cursor = static_cast<float>(cursorWide);
      const GaugeEdge end = gaugeEdge(centreY, cursorWide + startAngleRadians);
      if (!finite(start.outer) || !finite(start.inner) || !finite(end.outer) ||
          !finite(end.inner)) {
        return failure(
            LegacyAircraftHealthGaugePlanStatus::derivedGeometryNotFinite);
      }
      if (!appendMaskQuad(plan, start, end)) {
        return failure(
            LegacyAircraftHealthGaugePlanStatus::commandCapacityExceeded);
      }
      start = end;
    } while (cursor < loopLimit);
  }

  const GaugeEdge finalEdge =
      gaugeEdge(centreY, static_cast<double>(damageSweep) + startAngleRadians);
  if (!finite(start.outer) || !finite(start.inner) ||
      !finite(finalEdge.outer) || !finite(finalEdge.inner)) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::derivedGeometryNotFinite);
  }
  if (!appendMaskQuad(plan, start, finalEdge)) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::commandCapacityExceeded);
  }

  if (input.armourTextureAvailable &&
      !appendTexture(plan,
                     LegacyAircraftHealthGaugeCommandKind::armourTexture)) {
    return failure(
        LegacyAircraftHealthGaugePlanStatus::commandCapacityExceeded);
  }
  return plan;
}

} // namespace airfix::render
