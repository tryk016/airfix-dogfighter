#include "airfix/simulation/LegacyAircraftControlCommandReducer.hpp"
#include "airfix/simulation/LegacyAircraftControlEventStateOwner.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using namespace airfix::simulation;

static_assert(noexcept(legacyAircraftReduceControlCommand({}, {})));
static_assert(std::is_trivially_copyable_v<LegacyAircraftControlCommandState>);
static_assert(std::is_trivially_copyable_v<LegacyAircraftControlCommandInput>);
static_assert(
    std::is_trivially_copyable_v<LegacyAircraftControlCommandReduceResult>);
static_assert(
    static_cast<std::uint8_t>(LegacyAircraftNativeTurnEvent::turnSet) == 0x5DU);
static_assert(static_cast<std::uint8_t>(
                  LegacyAircraftNativePitchBankEvent::pitchSet) == 0x5FU);
static_assert(static_cast<std::uint8_t>(
                  LegacyAircraftNativeThrustEvent::thrustApply) == 0x64U);
static_assert(static_cast<std::uint8_t>(
                  LegacyAircraftNativePitchBankEvent::bankSet) == 0x65U);

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

struct EventView final {
  std::uint8_t event{};
  std::int32_t payload{};
  bool vehicleInactive{};
  bool hasAngularPolicy{};
  LegacyAircraftAngularSetNumericPolicy angularNumericPolicy{
      LegacyAircraftAngularSetNumericPolicy::startupPc53RoundToNearestEven};
};

[[nodiscard]] EventView
view(const LegacyAircraftControlCommandReduceResult &result) {
  require(result.emitted(), "command did not emit exactly one event");
  return std::visit(
      [](const auto &event) -> EventView {
        using Event = std::remove_cvref_t<decltype(event)>;
        if constexpr (std::is_same_v<Event,
                                     LegacyAircraftNativeTurnEventInput>) {
          return {
              static_cast<std::uint8_t>(event.event),
              event.payload,
              event.vehicleInactive,
              true,
              event.numericPolicy,
          };
        } else if constexpr (std::is_same_v<
                                 Event,
                                 LegacyAircraftNativePitchBankEventInput>) {
          return {
              static_cast<std::uint8_t>(event.event),
              event.payload,
              event.vehicleInactive,
              true,
              event.numericPolicy,
          };
        } else {
          return {
              static_cast<std::uint8_t>(event.event),
              event.payload,
              event.vehicleInactive,
              false,
              LegacyAircraftAngularSetNumericPolicy::
                  startupPc53RoundToNearestEven,
          };
        }
      },
      *result.event);
}

[[nodiscard]] LegacyAircraftControlCommandReduceResult reduce(
    const LegacyAircraftControlCommandState state,
    const LegacyAircraftControlCommand command, const bool active,
    const bool vehicleInactive = false,
    const LegacyAircraftAngularSetNumericPolicy policy =
        LegacyAircraftAngularSetNumericPolicy::startupPc53RoundToNearestEven) {
  return legacyAircraftReduceControlCommand(
      state, {
                 .command = command,
                 .active = active,
                 .vehicleInactive = vehicleInactive,
                 .angularNumericPolicy = policy,
             });
}

struct CommandVector final {
  LegacyAircraftControlCommand command;
  std::uint8_t event;
  std::int32_t payload;
};

constexpr std::array<CommandVector, 8U> commandVectors{{
    {LegacyAircraftControlCommand::turnLeft, 0x5DU, -32},
    {LegacyAircraftControlCommand::turnRight, 0x5DU, 32},
    {LegacyAircraftControlCommand::thrustIncrease, 0x64U, 255},
    {LegacyAircraftControlCommand::thrustDecrease, 0x64U, -255},
    {LegacyAircraftControlCommand::pitchUp, 0x5FU, 32},
    {LegacyAircraftControlCommand::pitchDown, 0x5FU, -32},
    {LegacyAircraftControlCommand::bankLeft, 0x65U, -32},
    {LegacyAircraftControlCommand::bankRight, 0x65U, 32},
}};

[[nodiscard]] constexpr LegacyAircraftControlCommand
oppositeOf(const LegacyAircraftControlCommand command) noexcept {
  using Command = LegacyAircraftControlCommand;
  switch (command) {
  case Command::turnLeft:
    return Command::turnRight;
  case Command::turnRight:
    return Command::turnLeft;
  case Command::thrustIncrease:
    return Command::thrustDecrease;
  case Command::thrustDecrease:
    return Command::thrustIncrease;
  case Command::pitchUp:
    return Command::pitchDown;
  case Command::pitchDown:
    return Command::pitchUp;
  case Command::bankLeft:
    return Command::bankRight;
  case Command::bankRight:
    return Command::bankLeft;
  case Command::count:
    return Command::count;
  }
  return Command::count;
}

struct CommandPair final {
  LegacyAircraftControlCommand negative;
  LegacyAircraftControlCommand positive;
  std::uint8_t event;
  std::int32_t magnitude;
};

constexpr std::array<CommandPair, 4U> commandPairs{{
    {
        LegacyAircraftControlCommand::turnLeft,
        LegacyAircraftControlCommand::turnRight,
        0x5DU,
        32,
    },
    {
        LegacyAircraftControlCommand::thrustDecrease,
        LegacyAircraftControlCommand::thrustIncrease,
        0x64U,
        255,
    },
    {
        LegacyAircraftControlCommand::pitchDown,
        LegacyAircraftControlCommand::pitchUp,
        0x5FU,
        32,
    },
    {
        LegacyAircraftControlCommand::bankLeft,
        LegacyAircraftControlCommand::bankRight,
        0x65U,
        32,
    },
}};

void requireEvent(const LegacyAircraftControlCommandReduceResult &result,
                  const std::uint8_t event, const std::int32_t payload,
                  const char *const message) {
  const auto observed = view(result);
  require(observed.event == event && observed.payload == payload, message);
}

void testEveryActiveCommandMapping() {
  for (const auto &vector : commandVectors) {
    const auto result = reduce({}, vector.command, true);
    requireEvent(result, vector.event, vector.payload,
                 "active command mapping changed");
    require(result.state.active(vector.command),
            "active command flag was not stored");
    require(
        result.state.activeCommandBits ==
            static_cast<std::uint8_t>(
                std::uint8_t{1U} << static_cast<std::uint8_t>(vector.command)),
        "active command touched another flag");
  }
}

void testExhaustiveAllFlagSnapshots() {
  for (std::uint16_t rawBits = 0U; rawBits <= 0xFFU; ++rawBits) {
    const LegacyAircraftControlCommandState state{
        static_cast<std::uint8_t>(rawBits)};
    for (const auto &vector : commandVectors) {
      const auto commandIndex = static_cast<std::uint8_t>(vector.command);
      const auto commandBit =
          static_cast<std::uint8_t>(std::uint8_t{1U} << commandIndex);
      const auto opposite = oppositeOf(vector.command);

      for (const bool active : {false, true}) {
        const auto result = reduce(state, vector.command, active);
        const auto expectedStateBits =
            active ? static_cast<std::uint8_t>(state.activeCommandBits |
                                               commandBit)
                   : static_cast<std::uint8_t>(
                         state.activeCommandBits &
                         static_cast<std::uint8_t>(~commandBit));
        const auto expectedPayload =
            active ? vector.payload
                   : (state.active(opposite) ? -vector.payload : 0);

        require(result.state.activeCommandBits == expectedStateBits,
                "exhaustive transition changed wrong flag bits");
        requireEvent(result, vector.event, expectedPayload,
                     "exhaustive transition emitted wrong event");
      }
    }
  }
}

void testOpposingCommandSequences() {
  for (const auto &pair : commandPairs) {
    auto negative = reduce({}, pair.negative, true);
    requireEvent(negative, pair.event, -pair.magnitude,
                 "negative press emitted wrong payload");

    auto both = reduce(negative.state, pair.positive, true);
    requireEvent(both, pair.event, pair.magnitude,
                 "last positive press did not win");
    require(both.state.active(pair.negative) &&
                both.state.active(pair.positive),
            "both-held state was not preserved");

    const auto repeatedPositive = reduce(both.state, pair.positive, true);
    requireEvent(repeatedPositive, pair.event, pair.magnitude,
                 "repeated true invocation was suppressed");
    require(repeatedPositive.state == both.state,
            "repeated true invocation changed unrelated state");

    const auto releasedNegative =
        reduce(repeatedPositive.state, pair.negative, false);
    requireEvent(releasedNegative, pair.event, pair.magnitude,
                 "release did not restore opposite-held payload");
    require(!releasedNegative.state.active(pair.negative) &&
                releasedNegative.state.active(pair.positive),
            "negative release changed the wrong flag");

    const auto repeatedNegativeRelease =
        reduce(releasedNegative.state, pair.negative, false);
    requireEvent(repeatedNegativeRelease, pair.event, pair.magnitude,
                 "repeated false invocation was suppressed");

    const auto neutral =
        reduce(repeatedNegativeRelease.state, pair.positive, false);
    requireEvent(neutral, pair.event, 0, "last release did not emit zero");
    require(neutral.state.activeCommandBits == 0U,
            "last release did not return pair to neutral");

    const auto repeatedZero = reduce(neutral.state, pair.positive, false);
    requireEvent(repeatedZero, pair.event, 0,
                 "repeated neutral release was suppressed");
    require(repeatedZero.state == neutral.state,
            "repeated neutral release changed state");

    auto positive = reduce({}, pair.positive, true);
    auto reverseBoth = reduce(positive.state, pair.negative, true);
    requireEvent(reverseBoth, pair.event, -pair.magnitude,
                 "last negative press did not win");
    const auto releasedPositive =
        reduce(reverseBoth.state, pair.positive, false);
    requireEvent(releasedPositive, pair.event, -pair.magnitude,
                 "positive release did not restore negative-held payload");
    const auto reverseNeutral =
        reduce(releasedPositive.state, pair.negative, false);
    requireEvent(reverseNeutral, pair.event, 0,
                 "reverse sequence did not end at zero");
  }
}

void testCrossPairIsolationAndTapOrdering() {
  auto state = LegacyAircraftControlCommandState{};
  const auto turn = reduce(state, LegacyAircraftControlCommand::turnLeft, true);
  state = turn.state;
  const auto pitch = reduce(state, LegacyAircraftControlCommand::pitchUp, true);
  state = pitch.state;
  const auto thrust =
      reduce(state, LegacyAircraftControlCommand::thrustIncrease, true);
  state = thrust.state;
  const auto bank =
      reduce(state, LegacyAircraftControlCommand::bankRight, true);
  state = bank.state;

  require(state.active(LegacyAircraftControlCommand::turnLeft) &&
              state.active(LegacyAircraftControlCommand::pitchUp) &&
              state.active(LegacyAircraftControlCommand::thrustIncrease) &&
              state.active(LegacyAircraftControlCommand::bankRight),
          "one command pair overwrote another pair");

  const auto tapPress =
      reduce({}, LegacyAircraftControlCommand::pitchDown, true);
  const auto tapRelease =
      reduce(tapPress.state, LegacyAircraftControlCommand::pitchDown, false);
  requireEvent(tapPress, 0x5FU, -32, "tap press event was lost");
  requireEvent(tapRelease, 0x5FU, 0, "tap release event was lost");
}

void testInvalidCommandFailsWithoutMutation() {
  const LegacyAircraftControlCommandState original{0xA5U};
  const auto result =
      reduce(original, static_cast<LegacyAircraftControlCommand>(0xFFU), true);
  require(
      result.failed() &&
          result.status ==
              LegacyAircraftControlCommandReduceStatus::unsupportedCommand &&
          !result.event.has_value(),
      "forged command was accepted");
  require(result.state == original, "forged command partially mutated state");
}

enum class ApplyOutcome : std::uint8_t {
  committed,
  ignored,
  failed,
};

[[nodiscard]] ApplyOutcome
apply(LegacyAircraftControlEventStateOwner &owner,
      const LegacyAircraftControlCommandEventInput &event) {
  return std::visit(
      [&owner](const auto &input) -> ApplyOutcome {
        using Input = std::remove_cvref_t<decltype(input)>;
        if constexpr (std::is_same_v<Input,
                                     LegacyAircraftNativeTurnEventInput>) {
          const auto decoded = legacyAircraftDecodeNativeTurnEvent(input);
          if (decoded.ignored()) {
            return ApplyOutcome::ignored;
          }
          if (!decoded.decoded()) {
            return ApplyOutcome::failed;
          }
          return owner.tryApply(*decoded.write) ==
                         LegacyAircraftControlEventCommitStatus::committed
                     ? ApplyOutcome::committed
                     : ApplyOutcome::failed;
        } else if constexpr (std::is_same_v<
                                 Input,
                                 LegacyAircraftNativePitchBankEventInput>) {
          const auto decoded = legacyAircraftDecodeNativePitchBankEvent(input);
          if (decoded.ignored()) {
            return ApplyOutcome::ignored;
          }
          if (!decoded.decoded()) {
            return ApplyOutcome::failed;
          }
          return owner.tryApply(*decoded.write) ==
                         LegacyAircraftControlEventCommitStatus::committed
                     ? ApplyOutcome::committed
                     : ApplyOutcome::failed;
        } else {
          const auto decoded = legacyAircraftDecodeNativeThrustEvent(input);
          if (decoded.ignored()) {
            return ApplyOutcome::ignored;
          }
          if (!decoded.decoded()) {
            return ApplyOutcome::failed;
          }
          return owner.tryApply(*decoded.write) ==
                         LegacyAircraftControlEventCommitStatus::committed
                     ? ApplyOutcome::committed
                     : ApplyOutcome::failed;
        }
      },
      event);
}

[[nodiscard]] LegacyAircraftControlEventState sentinelState() {
  return {
      .thrustControl =
          {
              .thrustApply = 0.25F,
              .targetThrust = 0.5F,
              .smoothedThrust = 0.75F,
          },
      .turnBits = 0x3E800000U,
      .pitchBits = 0xBF000000U,
      .bankBits = 0x3F400000U,
      .restDurationMilliseconds = 997,
  };
}

[[nodiscard]] std::uint32_t
selectedBits(const LegacyAircraftControlEventState &state,
             const std::uint8_t event) {
  switch (event) {
  case 0x5DU:
    return state.turnBits;
  case 0x5FU:
    return state.pitchBits;
  case 0x64U:
    return std::bit_cast<std::uint32_t>(state.thrustControl.thrustApply);
  case 0x65U:
    return state.bankBits;
  default:
    fail("test requested an unsupported event field");
  }
}

[[nodiscard]] std::uint32_t expectedBits(const std::uint8_t event,
                                         const std::int32_t payload) {
  if (payload == 0) {
    return 0U;
  }
  if (event == 0x64U) {
    return payload < 0 ? 0xBCA3D70BU : 0x3CA3D70BU;
  }
  return payload < 0 ? 0xBFB020C5U : 0x3FB020C5U;
}

void testTypedDecoderAndOwnerComposition() {
  for (const auto &pair : commandPairs) {
    const auto negative = reduce({}, pair.negative, true);
    auto initial = sentinelState();
    LegacyAircraftControlEventStateOwner negativeOwner{initial};
    require(apply(negativeOwner, *negative.event) == ApplyOutcome::committed,
            "negative command did not compose through decoder and owner");
    require(selectedBits(negativeOwner.snapshot(), pair.event) ==
                    expectedBits(pair.event, -pair.magnitude) &&
                negativeOwner.snapshot().restDurationMilliseconds == 0,
            "negative composition wrote wrong bits or retained rest");

    const auto both = reduce(negative.state, pair.positive, true);
    const auto equalPositive = reduce(both.state, pair.negative, false);
    initial = sentinelState();
    if (pair.event == 0x5DU) {
      initial.turnBits = expectedBits(pair.event, pair.magnitude);
    } else if (pair.event == 0x5FU) {
      initial.pitchBits = expectedBits(pair.event, pair.magnitude);
    } else if (pair.event == 0x64U) {
      initial.thrustControl.thrustApply =
          std::bit_cast<float>(expectedBits(pair.event, pair.magnitude));
    } else {
      initial.bankBits = expectedBits(pair.event, pair.magnitude);
    }
    LegacyAircraftControlEventStateOwner equalOwner{initial};
    require(apply(equalOwner, *equalPositive.event) ==
                    ApplyOutcome::committed &&
                selectedBits(equalOwner.snapshot(), pair.event) ==
                    expectedBits(pair.event, pair.magnitude) &&
                equalOwner.snapshot().restDurationMilliseconds == 0,
            "equal opposite-held emission was suppressed");

    const auto zero = reduce(equalPositive.state, pair.positive, false);
    initial = sentinelState();
    LegacyAircraftControlEventStateOwner zeroOwner{initial};
    require(apply(zeroOwner, *zero.event) == ApplyOutcome::committed,
            "zero release did not compose");
    require(selectedBits(zeroOwner.snapshot(), pair.event) == 0U &&
                zeroOwner.snapshot().restDurationMilliseconds ==
                    initial.restDurationMilliseconds,
            "zero release cleared rest or failed to write positive zero");
  }
}

void testInactiveAndNumericPolicyRemainDownstream() {
  const auto unsupportedPolicy =
      static_cast<LegacyAircraftAngularSetNumericPolicy>(0xFFU);
  const auto inactive = reduce({}, LegacyAircraftControlCommand::turnRight,
                               true, true, unsupportedPolicy);
  const auto observed = view(inactive);
  require(observed.vehicleInactive && observed.hasAngularPolicy &&
              observed.angularNumericPolicy == unsupportedPolicy,
          "command reducer interpreted downstream event policy");

  const auto initial = sentinelState();
  LegacyAircraftControlEventStateOwner inactiveOwner{initial};
  require(apply(inactiveOwner, *inactive.event) == ApplyOutcome::ignored &&
              inactiveOwner.snapshot() == initial,
          "inactive event mutated owner or validated policy first");

  const auto active = reduce({}, LegacyAircraftControlCommand::turnRight, true,
                             false, unsupportedPolicy);
  LegacyAircraftControlEventStateOwner activeOwner{initial};
  require(apply(activeOwner, *active.event) == ApplyOutcome::failed &&
              activeOwner.snapshot() == initial,
          "active unsupported policy did not fail closed");

  const auto inactiveThrust =
      reduce({}, LegacyAircraftControlCommand::thrustIncrease, true, true,
             unsupportedPolicy);
  require(!view(inactiveThrust).hasAngularPolicy,
          "thrust event unexpectedly acquired an angular policy");
  LegacyAircraftControlEventStateOwner inactiveThrustOwner{initial};
  require(apply(inactiveThrustOwner, *inactiveThrust.event) ==
                  ApplyOutcome::ignored &&
              inactiveThrustOwner.snapshot() == initial,
          "inactive thrust event mutated owner");
}

} // namespace

int main() {
  testEveryActiveCommandMapping();
  testExhaustiveAllFlagSnapshots();
  testOpposingCommandSequences();
  testCrossPairIsolationAndTapOrdering();
  testInvalidCommandFailsWithoutMutation();
  testTypedDecoderAndOwnerComposition();
  testInactiveAndNumericPolicyRemainDownstream();
  std::cout << "Legacy aircraft control-command reducer tests passed.\n";
  return EXIT_SUCCESS;
}
