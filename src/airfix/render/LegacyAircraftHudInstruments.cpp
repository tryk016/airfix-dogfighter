#include "airfix/render/LegacyAircraftHudInstruments.hpp"

#include <cmath>

namespace airfix::render {
namespace {

using namespace recovered_legacy_aircraft_hud_instruments;

[[nodiscard]] LegacyAircraftHudInstrumentsPlan
failure(const LegacyAircraftHudInstrumentsPlanStatus status) noexcept {
  return {.status = status};
}

[[nodiscard]] bool finite(const LegacyCanvasPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool
append(LegacyAircraftHudInstrumentsPlan &plan,
       const LegacyAircraftHudInstrumentCommand &command) noexcept {
  if (plan.commandCount >= plan.orderedCommands.size()) {
    return false;
  }
  plan.orderedCommands[plan.commandCount++] = command;
  return true;
}

[[nodiscard]] LegacyAircraftHudInstrumentCommand
faceCommand(const LegacyAircraftHudInstrumentSide side, const float centreX,
            const float centreY, const std::uint32_t tintArgb) noexcept {
  const float left = centreX - faceHalfExtent;
  const float top = centreY - faceHalfExtent;
  return {
      .side = side,
      .kind = LegacyAircraftHudInstrumentCommandKind::face,
      .destinationQuad = {{{left, top},
                           {left + faceExtent, top},
                           {left + faceExtent, top + faceExtent},
                           {left, top + faceExtent}}},
      .sourceRect = {0.0F, 0.0F, faceExtent, faceExtent},
      .tintArgb = tintArgb,
  };
}

[[nodiscard]] LegacyAircraftHudInstrumentCommand
indicatorCommand(const LegacyAircraftHudInstrumentSide side,
                 const float centreX, const float centreY,
                 const float normalizedValue,
                 const std::uint32_t tintArgb) noexcept {
  // The native x87 helper computes the angle in extended precision, then
  // spills sin and cos independently to binary32 before deriving the quad.
  // Double staging preserves that operation boundary without depending on a
  // process-wide x87 control word on modern targets.
  const double angle = (0.5 - static_cast<double>(normalizedValue)) *
                       static_cast<double>(indicatorAngleSpanRadians);
  const float sine = static_cast<float>(std::sin(angle));
  const float cosine = static_cast<float>(std::cos(angle));
  const float directionX = cosine;
  const float directionY = -sine;

  const double originXWide =
      (static_cast<double>(centreX) + indicatorCentrePixelOffset) -
      static_cast<double>(sine) * indicatorPivotY -
      static_cast<double>(cosine) * indicatorPivotX;
  const double originYWide = static_cast<double>(centreY) -
                             static_cast<double>(cosine) * indicatorPivotY -
                             static_cast<double>(directionY) * indicatorPivotX;
  const LegacyCanvasPoint origin{static_cast<float>(originXWide),
                                 static_cast<float>(originYWide)};
  const LegacyCanvasPoint widthEnd{origin.x + directionX * indicatorSourceWidth,
                                   origin.y +
                                       directionY * indicatorSourceWidth};
  const LegacyCanvasPoint farCorner{
      widthEnd.x - directionY * indicatorSourceHeight,
      widthEnd.y + directionX * indicatorSourceHeight};
  const LegacyCanvasPoint heightEnd{
      origin.x - directionY * indicatorSourceHeight,
      origin.y + directionX * indicatorSourceHeight};
  return {
      .side = side,
      .kind = LegacyAircraftHudInstrumentCommandKind::indicator,
      .destinationQuad = {{origin, widthEnd, farCorner, heightEnd}},
      .sourceRect = {0.0F, 0.0F, indicatorSourceWidth, indicatorSourceHeight},
      .tintArgb = tintArgb,
  };
}

[[nodiscard]] bool
valid(const LegacyAircraftHudInstrumentCommand &command) noexcept {
  if (!std::isfinite(command.sourceRect.x) ||
      !std::isfinite(command.sourceRect.y) ||
      !std::isfinite(command.sourceRect.width) ||
      !std::isfinite(command.sourceRect.height) ||
      command.sourceRect.width <= 0.0F || command.sourceRect.height <= 0.0F) {
    return false;
  }
  for (const auto point : command.destinationQuad) {
    if (!finite(point)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool appendInstrument(LegacyAircraftHudInstrumentsPlan &plan,
                                    const LegacyAircraftHudInstrumentSide side,
                                    const float centreX, const float centreY,
                                    const float normalizedValue,
                                    const std::uint32_t tintArgb,
                                    const bool faceAvailable,
                                    const bool indicatorAvailable) noexcept {
  if (faceAvailable) {
    const auto command = faceCommand(side, centreX, centreY, tintArgb);
    if (!valid(command) || !append(plan, command)) {
      return false;
    }
  }
  if (indicatorAvailable) {
    const auto command =
        indicatorCommand(side, centreX, centreY, normalizedValue, tintArgb);
    if (!valid(command) || !append(plan, command)) {
      return false;
    }
  }
  return true;
}

} // namespace

const LegacyAircraftHudInstrumentCommand *
LegacyAircraftHudInstrumentsPlan::command(
    const std::size_t index) const noexcept {
  if (index >= commandCount || index >= orderedCommands.size()) {
    return nullptr;
  }
  return &orderedCommands[index];
}

LegacyAircraftHudInstrumentsPlan buildLegacyAircraftHudInstrumentsPlan(
    const LegacyAircraftHudInstrumentsInput &input) noexcept {
  if (input.activeWindowPresent) {
    return failure(LegacyAircraftHudInstrumentsPlanStatus::activeWindowPresent);
  }
  if (!input.cameraAttachedAtEntry) {
    return failure(
        LegacyAircraftHudInstrumentsPlanStatus::cameraNotAttachedAtEntry);
  }
  if (!input.typeHudEnabled) {
    return failure(LegacyAircraftHudInstrumentsPlanStatus::typeHudDisabled);
  }
  if (!input.cameraAttachedAfterLayout) {
    return failure(
        LegacyAircraftHudInstrumentsPlanStatus::cameraDetachedBeforeDraw);
  }
  if (input.screenWidth == 0U || input.screenHeight == 0U) {
    return failure(LegacyAircraftHudInstrumentsPlanStatus::invalidScreenExtent);
  }
  if (!std::isfinite(input.rightNormalizedValue) ||
      !std::isfinite(input.leftNormalizedValue)) {
    return failure(
        LegacyAircraftHudInstrumentsPlanStatus::normalizedValueNotFinite);
  }

  const float width = static_cast<float>(input.screenWidth);
  const float height = static_cast<float>(input.screenHeight);
  if (!std::isfinite(width) || !std::isfinite(height)) {
    return failure(LegacyAircraftHudInstrumentsPlanStatus::invalidScreenExtent);
  }
  const float centreY = height - bottomInset;
  const float halfWidth = width * 0.5F;
  const float rightCentreX = halfWidth + horizontalCentreOffset;
  const float leftCentreX = halfWidth - horizontalCentreOffset;
  if (!std::isfinite(centreY) || !std::isfinite(rightCentreX) ||
      !std::isfinite(leftCentreX)) {
    return failure(
        LegacyAircraftHudInstrumentsPlanStatus::derivedGeometryNotFinite);
  }

  LegacyAircraftHudInstrumentsPlan plan{
      .status = LegacyAircraftHudInstrumentsPlanStatus::ready,
      .sourceScreenWidth = input.screenWidth,
      .sourceScreenHeight = input.screenHeight,
  };
  if (!appendInstrument(
          plan, LegacyAircraftHudInstrumentSide::right, rightCentreX, centreY,
          input.rightNormalizedValue, rightInstrumentTintArgb,
          input.rightFaceTextureAvailable, input.indicatorTextureAvailable) ||
      !appendInstrument(plan, LegacyAircraftHudInstrumentSide::left,
                        leftCentreX, centreY, input.leftNormalizedValue,
                        input.leftTintArgb, input.leftFaceTextureAvailable,
                        input.indicatorTextureAvailable)) {
    return failure(
        LegacyAircraftHudInstrumentsPlanStatus::derivedGeometryNotFinite);
  }
  return plan;
}

} // namespace airfix::render
