#pragma once
#include "airfix/render/LegacyGameplayCameraStepCoordinator.hpp"
#include "airfix/simulation/ClassicFlightPreview.hpp"
#include "airfix/simulation/PlayerAircraftSimulation.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

namespace airfix::runtime {
struct ClassicFlightPreviewBootstrap final {
  simulation::PlayerSpawnPose spawn;
  render::BasisTransform basis;
  simulation::LegacyAircraftTypeId aircraftType{};
};
struct ClassicFlightPreviewFrame final {
  render::ConvertedNodeTransform actorWorld;
  render::LegacyGameplayCameraStepCoordinatorInput camera;
};

// Opt-in source-world free-air producer. Does not own endpoints, input
// sampling, elapsed time, or collision. Recreate on mission/texture reload;
// counters start from the new scene's step zero, with an explicit input counter
// baseline.
class ClassicFlightPreviewSession final {
public:
  [[nodiscard]] static std::optional<ClassicFlightPreviewSession>
  create(const ClassicFlightPreviewBootstrap &bootstrap,
         std::uint64_t cameraCycleBaseline = 0) noexcept;
  [[nodiscard]] std::optional<ClassicFlightPreviewFrame>
  tryStep(const simulation::PlayerAircraftState &input) noexcept;
  [[nodiscard]] const simulation::ClassicFlightState &state() const noexcept {
    return flight_.state();
  }

private:
  explicit ClassicFlightPreviewSession(
      simulation::ClassicFlightPreview flight) noexcept
      : flight_(flight) {}
  simulation::ClassicFlightPreview flight_;
  render::BasisTransform basis_;
  render::Mat3 inverseBasis_;
  std::uint64_t cameraCycleBaseline_{};
};
} // namespace airfix::runtime
