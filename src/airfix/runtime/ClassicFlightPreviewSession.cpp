#include "airfix/runtime/ClassicFlightPreviewSession.hpp"
#include <cmath>

namespace airfix::runtime {
namespace {
render::Vec3 vector(const simulation::FlightVector &v) noexcept {
  return {v[0], v[1], v[2]};
}
simulation::FlightVector vector(const render::Vec3 &v) noexcept {
  return {v.x, v.y, v.z};
}
bool finite(const render::Vec3 &v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
std::int32_t payload(input::Q15 q, std::int32_t maximum) noexcept {
  // Explicit port quantizer: symmetric nearest, half away from zero.
  const auto scaled = std::int32_t(q) * maximum;
  return scaled < 0 ? -((-scaled + 16383) / 32767) : (scaled + 16383) / 32767;
}
} // namespace
std::optional<ClassicFlightPreviewSession>
ClassicFlightPreviewSession::create(const ClassicFlightPreviewBootstrap &b,
                                    std::uint64_t baseline) noexcept {
  const auto source = render::tryConvertLegacyAxisRotationWorldPose(
      vector(b.spawn.legacyWorldPosition), b.spawn.legacyAxisRotationRadians);
  const auto converted = render::tryConvertLegacyAxisRotationWorldPose(
      vector(b.spawn.legacyWorldPosition), b.spawn.legacyAxisRotationRadians,
      b.basis);
  const auto inverse = render::inverse(b.basis.sourceToRuntime);
  if (!source || !converted || !inverse)
    return std::nullopt;
  // Reject mismatched source/runtime bootstrap, rather than jumping on resume.
  for (unsigned i = 0; i < 3; ++i) {
    if (!std::isfinite(b.spawn.runtimeWorldPosition[i]))
      return std::nullopt;
    const auto col = vector(converted->linear.columns[i]);
    const auto pos = vector(converted->translation);
    if (std::abs(double(pos[i]) - b.spawn.runtimeWorldPosition[i]) > 1e-4)
      return std::nullopt;
    for (unsigned j = 0; j < 3; ++j)
      if (!std::isfinite(b.spawn.runtimeWorldRotationColumns[i][j]) ||
          std::abs(double(col[j]) - b.spawn.runtimeWorldRotationColumns[i][j]) >
              1e-4)
        return std::nullopt;
  }
  auto f = simulation::ClassicFlightPreview::create(
      b.aircraftType, b.spawn.legacyWorldPosition,
      {vector(source->linear.columns[0]), vector(source->linear.columns[1]),
       vector(source->linear.columns[2])});
  if (!f)
    return std::nullopt;
  ClassicFlightPreviewSession session{*f};
  session.basis_ = b.basis;
  session.inverseBasis_ = *inverse;
  session.cameraCycleBaseline_ = baseline;
  return session;
}
std::optional<ClassicFlightPreviewFrame> ClassicFlightPreviewSession::tryStep(
    const simulation::PlayerAircraftState &input) noexcept {
  if (input.pitchIntentQ15 < input::q15Min ||
      input.bankIntentQ15 < input::q15Min ||
      input.throttleDeltaIntentQ15 < input::q15Min ||
      input.cameraCyclePressCount < cameraCycleBaseline_)
    return std::nullopt;
  auto candidate = flight_;
  if (!candidate.tryStep({payload(input.pitchIntentQ15, 32),
                          payload(input.bankIntentQ15, 32),
                          payload(input.throttleDeltaIntentQ15, 255)}))
    return std::nullopt;
  const auto r = candidate.rotation();
  const render::Mat3 source{{vector(r[0]), vector(r[1]), vector(r[2])}};
  const auto rotation = render::multiply(
      render::multiply(basis_.sourceToRuntime, source), inverseBasis_);
  auto position = render::applyRuntimeColumn(
      basis_.sourceToRuntime, vector(candidate.state().position));
  position.x *= basis_.runtimeUnitsPerSourceUnit;
  position.y *= basis_.runtimeUnitsPerSourceUnit;
  position.z *= basis_.runtimeUnitsPerSourceUnit;
  if (!finite(position))
    return std::nullopt;
  for (const auto &col : rotation.columns)
    if (!finite(col))
      return std::nullopt;
  const ClassicFlightPreviewFrame frame{
      .actorWorld = {.linear = rotation,
                     .translation = position,
                     .rawScalar = 1},
      .camera = {.vehicleChaseWorldPosition = position,
                 .vehicleWorldAnchor = position,
                 .vehicleWorldRotation = rotation,
                 .refreshDeltaSeconds = simulation::classicFlightStepSeconds,
                 .vehicleHealth = candidate.health(),
                 .vehicleInactive = false,
                 .cameraCyclePressCount =
                     input.cameraCyclePressCount - cameraCycleBaseline_,
                 .rearViewHeld = input.rearViewHeld,
                 .simulationStep = candidate.state().completedSteps}};
  flight_ = candidate;
  return frame;
}
} // namespace airfix::runtime
