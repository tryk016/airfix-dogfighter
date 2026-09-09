#pragma once

#include "airfix/simulation/LegacyAircraftEngineAudioState.hpp"
#include "airfix/simulation/LegacyAircraftThrustState.hpp"
#include "airfix/simulation/LegacyAircraftTypeCatalog.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

using FlightVector = std::array<float, 3>;
// Mathematical columns (X, Y, Z), in source world coordinates.
using FlightRotation = std::array<FlightVector, 3>;
inline constexpr FlightRotation flightIdentity{
    {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
inline constexpr std::uint64_t classicFlightStepNanoseconds = 12'000'000;
inline constexpr float classicFlightStepSeconds = 0.012F;

struct ClassicFlightControls final {
  std::int32_t pitchPayload{}; // [-32, 32], already ordered/sampled by the port
  std::int32_t bankPayload{};  // [-32, 32]
  std::int32_t thrustApplyPayload{}; // [-255, 255]
};

struct ClassicFlightState final {
  FlightVector position{};
  std::array<float, 4> quaternion{1, 0, 0, 0}; // wxyz
  FlightVector linearMomentum{};
  FlightVector angularMomentum{};
  FlightVector pendingForce{};
  FlightVector pendingTorque{};
  LegacyAircraftThrustControlState thrust{};
  LegacyAircraftEngineAudioState engine{};
  float smoothedOrientationM01{};
  std::uint64_t completedSteps{};
};

// Explicit port policy, not a native-parity kernel. Recovered free-air force
// equations + Euler(old F/T), normalize, rebuild auxiliaries, next F/T,
// damping. binary64 expressions / binary32 state stores, no contraction,
// nearest rounding. No BSP/contact/water/ceiling, damage, AI, weapons, or
// engine audio playback. Finite-domain rejection is atomic. No original/private
// assets are required.
class ClassicFlightPreview final {
public:
  [[nodiscard]] static std::optional<ClassicFlightPreview>
  create(LegacyAircraftTypeId type, const FlightVector &sourcePosition,
         const FlightRotation &sourceRotation = flightIdentity) noexcept;
  [[nodiscard]] bool tryStep(ClassicFlightControls controls) noexcept;
  [[nodiscard]] const ClassicFlightState &state() const noexcept {
    return state_;
  }
  [[nodiscard]] FlightRotation rotation() const noexcept;
  [[nodiscard]] float health() const noexcept { return health_; }
  [[nodiscard]] double mass() const noexcept { return mass_; }
  [[nodiscard]] FlightVector bodyInverseInertia() const noexcept {
    return inverseInertia_;
  }

private:
  ClassicFlightState state_{};
  double mass_{};
  FlightVector inverseInertia_{};
  float massScale_{};
  float thrustFactor_{};
  float dampingFactor_{};
  float health_{};
};

// Port host clock. At most eight 12-ms steps per call. Whole excess steps are
// explicitly discarded after a stall; fractional debt is retained. Pausing
// clears debt, so resume never replays time spent in menus/background.
struct ClassicFlightClockBatch final {
  std::uint32_t steps{};
  std::uint64_t droppedSteps{};
};
class ClassicFlightClock final {
public:
  [[nodiscard]] ClassicFlightClockBatch
  consume(std::uint64_t elapsedNanoseconds, bool running) noexcept;
  void reset() noexcept { remainder_ = 0; }

private:
  std::uint64_t remainder_{};
};

} // namespace airfix::simulation
