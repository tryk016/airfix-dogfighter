#include "airfix/simulation/LegacyAircraftControlCommandStep.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using namespace airfix::simulation;

static_assert(noexcept(legacyAircraftAdvanceControlCommandStep({}, {})));
static_assert(
    std::is_trivially_copyable_v<LegacyAircraftControlCommandStepState>);
static_assert(
    std::is_trivially_copyable_v<LegacyAircraftControlCommandStepResult>);

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] LegacyAircraftControlEventState sentinelControlState() {
  return {
      .thrustControl =
          {
              .thrustApply = std::bit_cast<float>(0x3E800000U),
              .targetThrust = std::bit_cast<float>(0x3F000000U),
              .smoothedThrust = std::bit_cast<float>(0x3F400000U),
          },
      .turnBits = 0xBE800000U,
      .pitchBits = 0xBF000000U,
      .bankBits = 0x3F400000U,
      .restDurationMilliseconds = 2011,
  };
}

struct CommandVector final {
  LegacyAircraftControlCommand command;
  LegacyAircraftControlCommand opposite;
  std::uint8_t event;
  std::int32_t activePayload;
};

constexpr std::array<CommandVector, 8U> commandVectors{{
    {
        LegacyAircraftControlCommand::turnLeft,
        LegacyAircraftControlCommand::turnRight,
        0x5DU,
        -32,
    },
    {
        LegacyAircraftControlCommand::turnRight,
        LegacyAircraftControlCommand::turnLeft,
        0x5DU,
        32,
    },
    {
        LegacyAircraftControlCommand::thrustIncrease,
        LegacyAircraftControlCommand::thrustDecrease,
        0x64U,
        255,
    },
    {
        LegacyAircraftControlCommand::thrustDecrease,
        LegacyAircraftControlCommand::thrustIncrease,
        0x64U,
        -255,
    },
    {
        LegacyAircraftControlCommand::pitchUp,
        LegacyAircraftControlCommand::pitchDown,
        0x5FU,
        32,
    },
    {
        LegacyAircraftControlCommand::pitchDown,
        LegacyAircraftControlCommand::pitchUp,
        0x5FU,
        -32,
    },
    {
        LegacyAircraftControlCommand::bankLeft,
        LegacyAircraftControlCommand::bankRight,
        0x65U,
        -32,
    },
    {
        LegacyAircraftControlCommand::bankRight,
        LegacyAircraftControlCommand::bankLeft,
        0x65U,
        32,
    },
}};

struct EventView final {
  std::uint8_t event{};
  std::int32_t payload{};
  bool vehicleInactive{};
};

[[nodiscard]] EventView
viewEvent(const LegacyAircraftControlCommandStepResult &result) {
  require(result.event.has_value(), "step did not retain its typed event");
  return std::visit(
      [](const auto &event) {
        return EventView{
            static_cast<std::uint8_t>(event.event),
            event.payload,
            event.vehicleInactive,
        };
      },
      *result.event);
}

struct WriteView final {
  std::uint8_t event{};
  std::uint32_t valueBits{};
  bool clearRestDuration{};
};

[[nodiscard]] WriteView
viewWrite(const LegacyAircraftControlCommandStepResult &result) {
  require(result.write.has_value(), "committed step did not retain its write");
  return std::visit(
      [](const auto &write) -> WriteView {
        using Write = std::remove_cvref_t<decltype(write)>;
        if constexpr (std::is_same_v<Write, LegacyAircraftTurnWrite>) {
          return {0x5DU, write.valueBits, write.clearRestDuration};
        } else if constexpr (std::is_same_v<Write,
                                            LegacyAircraftPitchBankWrite>) {
          const std::uint8_t event =
              write.field == LegacyAircraftPitchBankWriteField::pitch ? 0x5FU
                                                                      : 0x65U;
          return {event, write.valueBits, write.clearRestDuration};
        } else {
          require(write.field == LegacyAircraftThrustWriteField::thrustApply,
                  "command step produced a THRUST_SET write");
          return {
              0x64U,
              std::bit_cast<std::uint32_t>(write.value),
              write.clearRestDuration,
          };
        }
      },
      *result.write);
}

[[nodiscard]] constexpr std::uint32_t
expectedValueBits(const std::uint8_t event,
                  const std::int32_t payload) noexcept {
  if (payload == 0) {
    return 0U;
  }
  if (event == 0x64U) {
    return payload < 0 ? 0xBCA3D70BU : 0x3CA3D70BU;
  }
  return payload < 0 ? 0xBFB020C5U : 0x3FB020C5U;
}

void setSelectedValue(LegacyAircraftControlEventState &state,
                      const std::uint8_t event, const std::uint32_t bits) {
  switch (event) {
  case 0x5DU:
    state.turnBits = bits;
    return;
  case 0x5FU:
    state.pitchBits = bits;
    return;
  case 0x64U:
    state.thrustControl.thrustApply = std::bit_cast<float>(bits);
    return;
  case 0x65U:
    state.bankBits = bits;
    return;
  default:
    fail("test selected an unknown event");
  }
}

[[nodiscard]] LegacyAircraftControlCommandStepResult advance(
    const LegacyAircraftControlCommandStepState state,
    const LegacyAircraftControlCommand command, const bool active,
    const bool vehicleInactive = false,
    const LegacyAircraftAngularSetNumericPolicy policy =
        LegacyAircraftAngularSetNumericPolicy::startupPc53RoundToNearestEven) {
  return legacyAircraftAdvanceControlCommandStep(
      state, {
                 .command = command,
                 .active = active,
                 .vehicleInactive = vehicleInactive,
                 .angularNumericPolicy = policy,
             });
}

void testExhaustiveCommandStateAndControlCommit() {
  for (std::uint16_t rawBits = 0U; rawBits <= 0xFFU; ++rawBits) {
    for (const auto &vector : commandVectors) {
      for (const bool active : {false, true}) {
        const LegacyAircraftControlCommandStepState initial{
            .commandState =
                {
                    .activeCommandBits = static_cast<std::uint8_t>(rawBits),
                },
            .controlEventState = sentinelControlState(),
        };
        const auto result = advance(initial, vector.command, active);

        const auto commandBit = static_cast<std::uint8_t>(
            std::uint8_t{1U} << static_cast<std::uint8_t>(vector.command));
        const auto expectedCommandBits =
            active ? static_cast<std::uint8_t>(rawBits | commandBit)
                   : static_cast<std::uint8_t>(
                         rawBits & static_cast<std::uint8_t>(~commandBit));
        const auto expectedPayload =
            active ? vector.activePayload
                   : (initial.commandState.active(vector.opposite)
                          ? -vector.activePayload
                          : 0);
        const auto expectedBits =
            expectedValueBits(vector.event, expectedPayload);
        auto expectedControl = initial.controlEventState;
        setSelectedValue(expectedControl, vector.event, expectedBits);
        if (expectedPayload != 0) {
          expectedControl.restDurationMilliseconds = 0;
        }

        require(result.committed(), "valid command invocation did not commit");
        require(result.status ==
                    LegacyAircraftControlCommandStepStatus::committed,
                "valid command invocation reported wrong status");
        require(result.state.commandState.activeCommandBits ==
                    expectedCommandBits,
                "step changed the wrong command flag");
        require(result.state.controlEventState == expectedControl,
                "step committed the wrong control-event state");

        const auto event = viewEvent(result);
        require(event.event == vector.event &&
                    event.payload == expectedPayload && !event.vehicleInactive,
                "step retained the wrong typed event");
        const auto write = viewWrite(result);
        require(write.event == vector.event &&
                    write.valueBits == expectedBits &&
                    write.clearRestDuration == (expectedPayload != 0),
                "step retained the wrong typed write");
      }
    }
  }
}

void testInactiveCommitsOnlyCallbackFlagStage() {
  const auto unsupportedPolicy =
      static_cast<LegacyAircraftAngularSetNumericPolicy>(0xFFU);
  for (const auto &vector : commandVectors) {
    const LegacyAircraftControlCommandStepState initial{
        .commandState = {},
        .controlEventState = sentinelControlState(),
    };
    const auto result =
        advance(initial, vector.command, true, true, unsupportedPolicy);
    require(result.ignored() &&
                result.status ==
                    LegacyAircraftControlCommandStepStatus::ignoredInactive,
            "inactive command was not accepted as an ignored event");
    require(result.state.commandState.active(vector.command),
            "inactive command did not retain the callback flag store");
    require(result.state.controlEventState == initial.controlEventState,
            "inactive command mutated control-event state");
    require(viewEvent(result).vehicleInactive,
            "inactive gate was not retained in the typed event");
  }
}

void testDecodeRejectionPreservesTheEarlierFlagStore() {
  const auto unsupportedPolicy =
      static_cast<LegacyAircraftAngularSetNumericPolicy>(0xFFU);
  constexpr std::array angularCommands{
      LegacyAircraftControlCommand::turnLeft,
      LegacyAircraftControlCommand::pitchUp,
      LegacyAircraftControlCommand::bankRight,
  };

  for (const auto command : angularCommands) {
    const LegacyAircraftControlCommandStepState initial{
        .commandState = {},
        .controlEventState = sentinelControlState(),
    };
    const auto result =
        advance(initial, command, true, false, unsupportedPolicy);
    require(result.status ==
                    LegacyAircraftControlCommandStepStatus::decodeRejected &&
                result.event.has_value() && !result.write.has_value(),
            "active unsupported numeric policy did not reject at decode");
    require(result.state.commandState.active(command),
            "decode rejection rolled back the earlier callback flag store");
    require(result.state.controlEventState == initial.controlEventState,
            "decode rejection partially mutated control-event state");
  }

  const LegacyAircraftControlCommandStepState thrustInitial{
      .commandState = {},
      .controlEventState = sentinelControlState(),
  };
  const auto thrust =
      advance(thrustInitial, LegacyAircraftControlCommand::thrustIncrease, true,
              false, unsupportedPolicy);
  require(thrust.committed(),
          "angular numeric policy leaked into the thrust decoder");
}

void testUnsupportedCommandRollsBackBothStages() {
  const LegacyAircraftControlCommandStepState initial{
      .commandState = {.activeCommandBits = 0xA5U},
      .controlEventState = sentinelControlState(),
  };
  const auto result =
      advance(initial, static_cast<LegacyAircraftControlCommand>(0xFFU), true);
  require(result.status ==
                  LegacyAircraftControlCommandStepStatus::unsupportedCommand &&
              !result.event.has_value() && !result.write.has_value(),
          "unsupported command did not fail before event production");
  require(result.state == initial,
          "unsupported command changed command or control state");
}

[[nodiscard]] LegacyAircraftControlCommandStepState
replay(LegacyAircraftControlCommandStepState state,
       const std::span<const LegacyAircraftControlCommandInput> inputs) {
  for (const auto input : inputs) {
    const auto result = legacyAircraftAdvanceControlCommandStep(state, input);
    require(
        result.status !=
                LegacyAircraftControlCommandStepStatus::unsupportedCommand &&
            result.status !=
                LegacyAircraftControlCommandStepStatus::decodeRejected &&
            result.status !=
                LegacyAircraftControlCommandStepStatus::commitRejected,
        "synthetic replay contained a rejected invocation");
    state = result.state;
  }
  return state;
}

void testOrderedReplayIsDeterministicAndComposable() {
  constexpr std::array inputs{
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::turnLeft,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::turnRight,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::turnLeft,
          .active = false,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::pitchUp,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::pitchDown,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::pitchUp,
          .active = false,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::thrustIncrease,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::thrustDecrease,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::thrustIncrease,
          .active = false,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::bankLeft,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::bankLeft,
          .active = true,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::bankLeft,
          .active = false,
      },
      LegacyAircraftControlCommandInput{
          .command = LegacyAircraftControlCommand::bankRight,
          .active = false,
      },
  };
  const LegacyAircraftControlCommandStepState initial{
      .commandState = {},
      .controlEventState = sentinelControlState(),
  };

  const auto whole = replay(initial, inputs);
  const auto repeated = replay(initial, inputs);
  require(whole == repeated,
          "identical explicit replay changed exact final state");

  constexpr std::size_t split = 6U;
  const auto prefix = replay(initial, std::span{inputs}.first(split));
  const auto composed = replay(prefix, std::span{inputs}.subspan(split));
  require(composed == whole,
          "prefix and suffix replay differed from the whole sequence");

  require(
      whole.commandState.active(LegacyAircraftControlCommand::turnRight) &&
          whole.commandState.active(LegacyAircraftControlCommand::pitchDown) &&
          whole.commandState.active(
              LegacyAircraftControlCommand::thrustDecrease) &&
          whole.commandState.activeCommandBits == 0x2AU,
      "ordered replay lost opposing-command flag state");
  require(whole.controlEventState.turnBits == 0x3FB020C5U &&
              whole.controlEventState.pitchBits == 0xBFB020C5U &&
              std::bit_cast<std::uint32_t>(
                  whole.controlEventState.thrustControl.thrustApply) ==
                  0xBCA3D70BU &&
              whole.controlEventState.bankBits == 0U &&
              whole.controlEventState.restDurationMilliseconds == 0,
          "ordered replay produced wrong exact control state");
}

void testZeroReleasePreservesRestAndRepeatedNonzeroClearsIt() {
  LegacyAircraftControlCommandStepState state{
      .commandState = {},
      .controlEventState = sentinelControlState(),
  };

  auto zero = advance(state, LegacyAircraftControlCommand::pitchUp, false);
  require(zero.committed() && viewWrite(zero).valueBits == 0U &&
              !viewWrite(zero).clearRestDuration &&
              zero.state.controlEventState.restDurationMilliseconds ==
                  state.controlEventState.restDurationMilliseconds,
          "zero release changed the retained rest duration");

  state = zero.state;
  auto nonzero = advance(state, LegacyAircraftControlCommand::pitchUp, true);
  require(nonzero.committed() &&
              nonzero.state.controlEventState.restDurationMilliseconds == 0,
          "nonzero press did not clear retained rest");

  nonzero.state.controlEventState.restDurationMilliseconds = 1777;
  const auto repeated =
      advance(nonzero.state, LegacyAircraftControlCommand::pitchUp, true);
  require(repeated.committed() &&
              repeated.state.controlEventState.pitchBits ==
                  nonzero.state.controlEventState.pitchBits &&
              repeated.state.controlEventState.restDurationMilliseconds == 0,
          "repeated equal nonzero write was suppressed");
}

} // namespace

int main() {
  testExhaustiveCommandStateAndControlCommit();
  testInactiveCommitsOnlyCallbackFlagStage();
  testDecodeRejectionPreservesTheEarlierFlagStore();
  testUnsupportedCommandRollsBackBothStages();
  testOrderedReplayIsDeterministicAndComposable();
  testZeroReleasePreservesRestAndRepeatedNonzeroClearsIt();
  std::cout << "Legacy aircraft control-command step tests passed.\n";
  return EXIT_SUCCESS;
}
