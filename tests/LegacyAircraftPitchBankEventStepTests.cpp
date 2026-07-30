#include "airfix/simulation/LegacyAircraftPitchBankEventStep.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(noexcept(legacyAircraftAdvancePitchBankEventStep({}, {})));
static_assert(
    std::is_trivially_copyable_v<LegacyAircraftPitchBankEventStepResult>);

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] LegacyAircraftControlEventState sentinelState() {
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

[[nodiscard]] LegacyAircraftPitchBankEventStepResult advance(
    const LegacyAircraftControlEventState state,
    const LegacyAircraftNativePitchBankEvent event, const std::int32_t payload,
    const bool inactive = false,
    const LegacyAircraftPitchBankNumericPolicy policy =
        LegacyAircraftPitchBankNumericPolicy::startupPc53RoundToNearestEven) {
  return legacyAircraftAdvancePitchBankEventStep(
      state, {
                 .event = event,
                 .payload = payload,
                 .vehicleInactive = inactive,
                 .numericPolicy = policy,
             });
}

void testEachAxisCommitsOneIsolatedWrite() {
  const auto initial = sentinelState();
  const auto pitch =
      advance(initial, LegacyAircraftNativePitchBankEvent::pitchSet, 32);
  require(pitch.committed() && pitch.write->valueBits == 0x3FB020C5U &&
              pitch.write->field == LegacyAircraftPitchBankWriteField::pitch,
          "PITCH_SET produced the wrong exact write");
  auto expected = initial;
  expected.pitchBits = 0x3FB020C5U;
  expected.restDurationMilliseconds = 0;
  require(pitch.state == expected, "PITCH_SET changed an unrelated field");

  const auto bank =
      advance(initial, LegacyAircraftNativePitchBankEvent::bankSet, -32);
  require(bank.committed() && bank.write->valueBits == 0xBFB020C5U &&
              bank.write->field == LegacyAircraftPitchBankWriteField::bank,
          "BANK_SET produced the wrong exact write");
  expected = initial;
  expected.bankBits = 0xBFB020C5U;
  expected.restDurationMilliseconds = 0;
  require(bank.state == expected, "BANK_SET changed an unrelated field");
}

[[nodiscard]] LegacyAircraftControlEventState
replay(LegacyAircraftControlEventState state,
       const std::span<const LegacyAircraftNativePitchBankEventInput> events) {
  for (const auto event : events) {
    const auto result = legacyAircraftAdvancePitchBankEventStep(state, event);
    require(result.committed(), "ordered synthetic event did not commit");
    state = result.state;
  }
  return state;
}

void testCallerOrderAndLastProcessedSetWinPerAxis() {
  constexpr std::array joystickThenKeyboard{
      LegacyAircraftNativePitchBankEventInput{
          .event = LegacyAircraftNativePitchBankEvent::bankSet,
          .payload = 32,
      },
      LegacyAircraftNativePitchBankEventInput{
          .event = LegacyAircraftNativePitchBankEvent::pitchSet,
          .payload = -32,
      },
      LegacyAircraftNativePitchBankEventInput{
          .event = LegacyAircraftNativePitchBankEvent::bankSet,
          .payload = -32,
      },
      LegacyAircraftNativePitchBankEventInput{
          .event = LegacyAircraftNativePitchBankEvent::pitchSet,
          .payload = 0,
      },
  };
  const auto initial = sentinelState();
  const auto whole = replay(initial, joystickThenKeyboard);
  require(whole.pitchBits == 0U && whole.bankBits == 0xBFB020C5U,
          "last processed SET did not win independently per axis");

  constexpr std::size_t split = 2U;
  const auto prefix =
      replay(initial, std::span{joystickThenKeyboard}.first(split));
  require(prefix.pitchBits == 0xBFB020C5U && prefix.bankBits == 0x3FB020C5U,
          "joystick BANK-before-PITCH intermediate state was changed");
  const auto composed =
      replay(prefix, std::span{joystickThenKeyboard}.subspan(split));
  require(composed == whole,
          "prefix and suffix differed from caller-ordered replay");

  constexpr std::array aiOrder{
      LegacyAircraftNativePitchBankEventInput{
          .event = LegacyAircraftNativePitchBankEvent::pitchSet,
          .payload = 32,
      },
      LegacyAircraftNativePitchBankEventInput{
          .event = LegacyAircraftNativePitchBankEvent::bankSet,
          .payload = -32,
      },
  };
  const auto ai = replay(initial, aiOrder);
  require(ai.pitchBits == 0x3FB020C5U && ai.bankBits == 0xBFB020C5U,
          "AI PITCH-before-BANK caller order was changed");
}

void testZeroRepeatInactiveAndNoReplay() {
  const auto initial = sentinelState();
  const auto zero =
      advance(initial, LegacyAircraftNativePitchBankEvent::bankSet, 0);
  require(zero.committed() && zero.write->valueBits == 0U &&
              !zero.write->clearRestDuration && zero.state.bankBits == 0U &&
              zero.state.restDurationMilliseconds ==
                  initial.restDurationMilliseconds,
          "active zero did not store +0 while preserving rest");

  auto first =
      advance(initial, LegacyAircraftNativePitchBankEvent::pitchSet, 32);
  require(first.committed(), "first repeated SET did not commit");
  first.state.restDurationMilliseconds = 1777;
  const auto repeated =
      advance(first.state, LegacyAircraftNativePitchBankEvent::pitchSet, 32);
  require(repeated.committed() &&
              repeated.state.pitchBits == first.state.pitchBits &&
              repeated.state.restDurationMilliseconds == 0,
          "equal repeated SET was suppressed");

  const auto unsupportedPolicy =
      static_cast<LegacyAircraftPitchBankNumericPolicy>(0xFFU);
  const auto inactive =
      advance(initial, LegacyAircraftNativePitchBankEvent::pitchSet, 1555145203,
              true, unsupportedPolicy);
  require(inactive.ignored() && inactive.state == initial,
          "inactive event did not no-op before numeric policy");

  const auto next =
      advance(inactive.state, LegacyAircraftNativePitchBankEvent::bankSet, -32);
  require(next.committed() && next.state.pitchBits == initial.pitchBits &&
              next.state.bankBits == 0xBFB020C5U,
          "inactive drop was replayed by the stateless adapter");
}

void testRejectedInputsAreTransactional() {
  const auto initial = sentinelState();
  const auto unsupportedEvent = advance(
      initial, static_cast<LegacyAircraftNativePitchBankEvent>(0xFFU), 32);
  require(unsupportedEvent.rejected() &&
              unsupportedEvent.status ==
                  LegacyAircraftPitchBankEventStepStatus::decodeRejected &&
              unsupportedEvent.state == initial,
          "unsupported event partially mutated state");

  const auto unsupportedPolicy =
      static_cast<LegacyAircraftPitchBankNumericPolicy>(0xFFU);
  const auto activeUnsupportedPolicy =
      advance(initial, LegacyAircraftNativePitchBankEvent::pitchSet, 32, false,
              unsupportedPolicy);
  require(activeUnsupportedPolicy.rejected() &&
              activeUnsupportedPolicy.state == initial,
          "unsupported numeric policy partially mutated state");
}

} // namespace

int main() {
  testEachAxisCommitsOneIsolatedWrite();
  testCallerOrderAndLastProcessedSetWinPerAxis();
  testZeroRepeatInactiveAndNoReplay();
  testRejectedInputsAreTransactional();
  std::cout << "Legacy aircraft pitch/bank event-step tests passed.\n";
  return EXIT_SUCCESS;
}
