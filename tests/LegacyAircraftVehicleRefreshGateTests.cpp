#include "airfix/simulation/LegacyAircraftVehicleRefreshGate.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(noexcept(legacyAircraftAdvanceVehicleRefreshGate({}, {})));
static_assert(
    std::is_trivially_copyable_v<LegacyAircraftVehicleRefreshGateInput>);
static_assert(
    std::is_trivially_copyable_v<LegacyAircraftVehicleRefreshGateResult>);

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] LegacyAircraftVehicleRefreshGateInput
groundRestingInput(const std::int64_t deltaMilliseconds = 12) noexcept {
  return {
      .linearVelocitySquared = 0.0F,
      .onGround = true,
      .waterUnit = false,
      .refreshDeltaMilliseconds = deltaMilliseconds,
  };
}

[[nodiscard]] bool
sameStateBits(const LegacyAircraftControlCommandStepState &left,
              const LegacyAircraftControlCommandStepState &right) noexcept {
  const auto &leftControl = left.controlEventState;
  const auto &rightControl = right.controlEventState;
  return left.commandState == right.commandState &&
         std::bit_cast<std::uint32_t>(leftControl.thrustControl.thrustApply) ==
             std::bit_cast<std::uint32_t>(
                 rightControl.thrustControl.thrustApply) &&
         std::bit_cast<std::uint32_t>(leftControl.thrustControl.targetThrust) ==
             std::bit_cast<std::uint32_t>(
                 rightControl.thrustControl.targetThrust) &&
         std::bit_cast<std::uint32_t>(
             leftControl.thrustControl.smoothedThrust) ==
             std::bit_cast<std::uint32_t>(
                 rightControl.thrustControl.smoothedThrust) &&
         leftControl.turnBits == rightControl.turnBits &&
         leftControl.pitchBits == rightControl.pitchBits &&
         leftControl.bankBits == rightControl.bankBits &&
         leftControl.restDurationMilliseconds ==
             rightControl.restDurationMilliseconds;
}

[[nodiscard]] LegacyAircraftControlCommandStepResult
command(const LegacyAircraftControlCommandStepState state,
        const LegacyAircraftControlCommand selected,
        const bool active) noexcept {
  return legacyAircraftAdvanceControlCommandStep(
      state, {
                 .command = selected,
                 .active = active,
                 .vehicleInactive = false,
                 .angularNumericPolicy = LegacyAircraftAngularSetNumericPolicy::
                     startupPc53RoundToNearestEven,
             });
}

void testCommittedCommandWakesRefreshGate() {
  LegacyAircraftControlCommandStepState initial{};
  initial.controlEventState.restDurationMilliseconds = 1'500;

  const auto commanded =
      command(initial, LegacyAircraftControlCommand::pitchUp, true);
  require(commanded.committed(), "pitch-up command did not commit");
  require(commanded.state.controlEventState.restDurationMilliseconds == 0,
          "nonzero command did not clear the shared rest duration");

  const auto refreshed = legacyAircraftAdvanceVehicleRefreshGate(
      commanded.state, groundRestingInput());
  require(refreshed.advanced(), "commanded refresh gate was rejected");
  require(refreshed.sleepStep ==
              LegacyVehicleSleepStepResult{
                  .restDurationMilliseconds = 0,
                  .integratePhysics = true,
                  .clearDynamics = false,
              },
          "committed wake control did not keep the vehicle active");
  require(refreshed.state.commandState.active(
              LegacyAircraftControlCommand::pitchUp) &&
              refreshed.state.controlEventState.pitchBits == 0x3FB020C5U,
          "refresh gate changed committed command or pitch state");
}

void testActiveZeroDoesNotWake() {
  LegacyAircraftControlCommandStepState initial{};
  initial.controlEventState.pitchBits = 0x3F000000U;
  initial.controlEventState.restDurationMilliseconds = 1'500;

  const auto zero =
      command(initial, LegacyAircraftControlCommand::pitchUp, false);
  require(zero.committed(), "active zero command did not commit");
  require(zero.state.controlEventState.pitchBits == 0U &&
              zero.state.controlEventState.restDurationMilliseconds == 1'500,
          "zero command changed rest duration instead of only writing zero");

  const auto refreshed =
      legacyAircraftAdvanceVehicleRefreshGate(zero.state, groundRestingInput());
  require(refreshed.advanced() &&
              refreshed.state.controlEventState.restDurationMilliseconds ==
                  1'512 &&
              refreshed.sleepStep->integratePhysics &&
              !refreshed.sleepStep->clearDynamics,
          "zero controls did not permit rest accumulation");
}

void testThresholdCrossingClearsDynamicsOnce() {
  LegacyAircraftControlCommandStepState state{};
  state.controlEventState.restDurationMilliseconds = 1'999;

  const auto crossed =
      legacyAircraftAdvanceVehicleRefreshGate(state, groundRestingInput(1));
  require(crossed.advanced() && crossed.sleepStep ==
                                    LegacyVehicleSleepStepResult{
                                        .restDurationMilliseconds = 2'000,
                                        .integratePhysics = true,
                                        .clearDynamics = true,
                                    },
          "1999-to-2000 refresh did not request one dynamics clear");

  const auto sleeping = legacyAircraftAdvanceVehicleRefreshGate(
      crossed.state, groundRestingInput(1));
  require(sleeping.advanced() && sleeping.sleepStep ==
                                     LegacyVehicleSleepStepResult{
                                         .restDurationMilliseconds = 2'000,
                                         .integratePhysics = false,
                                         .clearDynamics = false,
                                     },
          "sleeping refresh repeated integration or dynamics clearing");
}

void testSleepingEntrySkipsActivePathValidation() {
  LegacyAircraftControlCommandStepState state{};
  state.controlEventState.thrustControl.targetThrust =
      std::numeric_limits<float>::quiet_NaN();
  state.controlEventState.turnBits = 0x7F800000U;
  state.controlEventState.restDurationMilliseconds = 2'000;

  const auto refreshed = legacyAircraftAdvanceVehicleRefreshGate(
      state,
      {
          .linearVelocitySquared = std::numeric_limits<float>::quiet_NaN(),
          .onGround = true,
          .waterUnit = true,
          .refreshDeltaMilliseconds = std::numeric_limits<std::int64_t>::max(),
      });
  require(refreshed.advanced() && refreshed.sleepStep ==
                                      LegacyVehicleSleepStepResult{
                                          .restDurationMilliseconds = 2'000,
                                          .integratePhysics = false,
                                          .clearDynamics = false,
                                      },
          "sleeping entry inspected skipped active-path values");
  require(sameStateBits(refreshed.state, state),
          "sleeping entry changed control fields");
}

void testRejectedRefreshIsAtomic() {
  LegacyAircraftControlCommandStepState state{};
  state.commandState.activeCommandBits = 0xA5U;
  state.controlEventState = {
      .thrustControl =
          {
              .thrustApply = 0.25F,
              .targetThrust = 0.5F,
              .smoothedThrust = 0.75F,
          },
      .turnBits = 0xBE800000U,
      .pitchBits = 0xBF000000U,
      .bankBits = 0x3F400000U,
      .restDurationMilliseconds = 100,
  };

  const auto nonFinite = legacyAircraftAdvanceVehicleRefreshGate(
      state,
      {
          .linearVelocitySquared = std::numeric_limits<float>::infinity(),
          .onGround = true,
          .waterUnit = false,
          .refreshDeltaMilliseconds = 12,
      });
  require(nonFinite.rejected() && sameStateBits(nonFinite.state, state),
          "non-finite active refresh did not reject atomically");

  state.controlEventState.turnBits = 0x7FC00001U;
  const auto nonFiniteWake =
      legacyAircraftAdvanceVehicleRefreshGate(state, groundRestingInput());
  require(nonFiniteWake.rejected() && sameStateBits(nonFiniteWake.state, state),
          "non-finite wake control did not reject atomically");

  state.controlEventState.restDurationMilliseconds = 1'999;
  state.controlEventState.turnBits = 0U;
  state.controlEventState.pitchBits = 0U;
  state.controlEventState.bankBits = 0U;
  state.controlEventState.thrustControl = {};
  const auto overflow = legacyAircraftAdvanceVehicleRefreshGate(
      state, groundRestingInput(std::numeric_limits<std::int64_t>::max()));
  require(overflow.rejected() && sameStateBits(overflow.state, state),
          "rest-duration overflow did not reject atomically");
}

void testSignedNegativeDelta() {
  LegacyAircraftControlCommandStepState state{};
  state.controlEventState.restDurationMilliseconds = 100;

  const auto refreshed =
      legacyAircraftAdvanceVehicleRefreshGate(state, groundRestingInput(-12));
  require(refreshed.advanced() &&
              refreshed.state.controlEventState.restDurationMilliseconds ==
                  88 &&
              refreshed.sleepStep->integratePhysics &&
              !refreshed.sleepStep->clearDynamics,
          "signed negative refresh delta changed semantics");
}

[[nodiscard]] LegacyAircraftControlCommandStepState
replay(LegacyAircraftControlCommandStepState state,
       const std::span<const LegacyAircraftVehicleRefreshGateInput> inputs) {
  for (const auto input : inputs) {
    const auto result = legacyAircraftAdvanceVehicleRefreshGate(state, input);
    require(result.advanced(), "replay refresh was rejected");
    state = result.state;
  }
  return state;
}

void testSegmentedReplayComposition() {
  LegacyAircraftControlCommandStepState initial{};
  initial.commandState.activeCommandBits = 0x81U;
  initial.controlEventState.restDurationMilliseconds = 64;

  constexpr std::array inputs{
      LegacyAircraftVehicleRefreshGateInput{
          .linearVelocitySquared = 0.0F,
          .onGround = true,
          .waterUnit = false,
          .refreshDeltaMilliseconds = 12,
      },
      LegacyAircraftVehicleRefreshGateInput{
          .linearVelocitySquared = 0.0F,
          .onGround = true,
          .waterUnit = false,
          .refreshDeltaMilliseconds = -4,
      },
      LegacyAircraftVehicleRefreshGateInput{
          .linearVelocitySquared = 1.0F,
          .onGround = false,
          .waterUnit = false,
          .refreshDeltaMilliseconds = 999,
      },
      LegacyAircraftVehicleRefreshGateInput{
          .linearVelocitySquared = 0.01F,
          .onGround = false,
          .waterUnit = true,
          .refreshDeltaMilliseconds = 33,
      },
  };

  const auto whole = replay(initial, inputs);
  const auto prefix = replay(initial, std::span{inputs}.first(2U));
  const auto segmented = replay(prefix, std::span{inputs}.subspan(2U));
  require(sameStateBits(whole, segmented),
          "segmented refresh replay changed the final state");
  require(whole.commandState == initial.commandState &&
              whole.controlEventState.restDurationMilliseconds == 33,
          "replay changed command flags or produced the wrong rest duration");
}

} // namespace

int main() {
  testCommittedCommandWakesRefreshGate();
  testActiveZeroDoesNotWake();
  testThresholdCrossingClearsDynamicsOnce();
  testSleepingEntrySkipsActivePathValidation();
  testRejectedRefreshIsAtomic();
  testSignedNegativeDelta();
  testSegmentedReplayComposition();

  std::cout << "Legacy aircraft vehicle refresh-gate tests passed.\n";
  return EXIT_SUCCESS;
}
