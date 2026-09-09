#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"
#include "airfix/render/PlayerActorPoseRuntime.hpp"
#include "airfix/runtime/ClassicFlightPreviewSession.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace airfix;
void require(bool v, const char *text) {
  if (!v)
    throw std::runtime_error(text);
}
runtime::ClassicFlightPreviewBootstrap
bootstrap(render::BasisTransform basis = {}) {
  runtime::ClassicFlightPreviewBootstrap b;
  b.basis = basis;
  b.aircraftType = simulation::LegacyAircraftTypeId::mustang;
  b.spawn.legacyWorldPosition = {1, 4, 3};
  b.spawn.legacyAxisRotationRadians = {0.2F, 0.7F, -0.3F};
  auto pose = render::tryConvertLegacyAxisRotationWorldPose(
      {1, 4, 3}, b.spawn.legacyAxisRotationRadians, basis);
  require(pose.has_value(), "fixture basis");
  b.spawn.runtimeWorldPosition = {pose->translation.x, pose->translation.y,
                                  pose->translation.z};
  for (unsigned i = 0; i < 3; ++i) {
    const auto &c = pose->linear.columns[i];
    b.spawn.runtimeWorldRotationColumns[i] = {c.x, c.y, c.z};
  }
  return b;
}
void conversionAndRejects() {
  const render::BasisTransform basis{
      .sourceToRuntime = {.columns = {render::Vec3{0, 0, 1},
                                      render::Vec3{0, 1, 0},
                                      render::Vec3{1, 0, 0}}},
      .runtimeUnitsPerSourceUnit = 10};
  const auto b = bootstrap(basis);
  auto session = runtime::ClassicFlightPreviewSession::create(b, 15);
  require(session.has_value(),
          "reflected/scaled basis supported outside physics");
  simulation::PlayerAircraftState input;
  input.cameraCyclePressCount = 15;
  input.rearViewHeld = true;
  auto frame = session->tryStep(input);
  require(frame.has_value(), "first frame");
  require(frame->actorWorld.translation == render::Vec3{30, 40, 10},
          "native units convert exactly once");
  require(frame->camera.vehicleWorldAnchor == frame->actorWorld.translation &&
              frame->camera.vehicleChaseWorldPosition ==
                  frame->actorWorld.translation &&
              frame->camera.vehicleWorldRotation == frame->actorWorld.linear &&
              frame->camera.refreshDeltaSeconds ==
                  simulation::classicFlightStepSeconds &&
              frame->camera.simulationStep == 1 &&
              frame->camera.cameraCyclePressCount == 0 &&
              frame->camera.rearViewHeld,
          "coherent frame and explicit port camera anchor policy");
  input.cameraCyclePressCount = 16;
  require(session->tryStep(input)->camera.cameraCyclePressCount == 1,
          "camera counter offset");
  auto bad = b;
  bad.spawn.runtimeWorldPosition[0] = NAN;
  require(!runtime::ClassicFlightPreviewSession::create(bad),
          "nonfinite published spawn");
  bad = b;
  bad.spawn.runtimeWorldPosition[0] += 1;
  require(!runtime::ClassicFlightPreviewSession::create(bad),
          "mismatched bootstrap");
  const auto before = session->state();
  input.pitchIntentQ15 = std::numeric_limits<input::Q15>::min();
  require(!session->tryStep(input) &&
              session->state().completedSteps == before.completedSteps,
          "invalid input atomic rejection");
  input.pitchIntentQ15 = 0;
  input.cameraCyclePressCount = 14;
  require(!session->tryStep(input), "camera baseline regression");
  auto fresh = runtime::ClassicFlightPreviewSession::create(b, 16);
  require(fresh && fresh->state().completedSteps == 0 &&
              fresh->state().position == b.spawn.legacyWorldPosition,
          "reload starts fresh");
}
void endToEndSyntheticPublication() {
  const auto b = bootstrap();
  auto flight = runtime::ClassicFlightPreviewSession::create(b);
  require(flight.has_value(), "flight fixture");
  auto initial = render::tryConvertLegacyAxisRotationWorldPose(
      {1, 4, 3}, b.spawn.legacyAxisRotationRadians);
  assets::MissionWorldSpatialArena arena;
  arena.rooms.resize(1);
  auto camera = render::LegacyGameplayCameraMissionRuntime::create(
      std::move(arena), {},
      {.vehicleWorldPosition = initial->translation,
       .vehicleWorldRotation = initial->linear,
       .worldRoomIndex = 0});
  require(camera.complete(), "synthetic camera fixture");
  const render::PlayerActorSceneBinding binding{.firstMeshSlot = 0,
                                                .meshCount = 1,
                                                .firstInstanceIndex = 0,
                                                .instanceCount = 1};
  const std::array provenance{render::PlayerActorSceneInstanceProvenance{
      .actor = {.legacySkinSlot = 0,
                .blueprintIndex = 0,
                .blueprintReference = 1,
                .physicalMeshIndex = 0},
      .finalInstanceIndex = 0,
      .actorLocal = {.linear = {}, .translation = {}, .rawScalar = 1}}};
  auto pose = render::PlayerActorPoseRuntime::create(
      binding, provenance, 1, *initial, 0,
      {.maximumInstances = 1,
       .maximumOverrides = 1,
       .maximumFrameBytes = sizeof(render::DynamicInstancePoseOverride)});
  require(pose.complete(), "synthetic pose fixture");
  simulation::PlayerAircraftState input;
  input.pitchIntentQ15 = 5000;
  input.bankIntentQ15 = -6000;
  input.throttleDeltaIntentQ15 = input::q15One;
  for (unsigned step = 1; step <= 300; ++step) {
    if (step > 80)
      input.throttleDeltaIntentQ15 = 0;
    auto frame = flight->tryStep(input);
    require(frame.has_value(), "physics frame");
    require(pose.runtime->tryPublish(frame->actorWorld, step).result ==
                render::PlayerActorPoseRuntimePublishResult::published,
            "pose publication");
    auto cameraResult = camera.runtime->tryAdvance(frame->camera);
    require(cameraResult.published(), "live chase camera publication");
    auto actorLease = pose.runtime->tryAcquire();
    auto cameraLease = camera.runtime->tryAcquire();
    require(actorLease.has_value() && cameraLease.has_value(),
            "render consumer leases");
    require(actorLease->simulationStep() == step &&
                actorLease->resolve(0, {}, {}).modelTranslation ==
                    frame->actorWorld.translation,
            "render consumer resolves the current physical pose");
    require(camera.runtime->currentSnapshot()->simulationStep == step,
            "camera follows physics clock");
  }
  require(flight->state().position != b.spawn.legacyWorldPosition,
          "input to motion to consumer, not frozen pose");
}
} // namespace
int main() {
  try {
    conversionAndRejects();
    endToEndSyntheticPublication();
    std::cout << "Classic flight preview session tests passed.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
