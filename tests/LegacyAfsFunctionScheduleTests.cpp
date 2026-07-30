#include "airfix/script/LegacyAfsFunctionSchedule.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>

namespace {

using namespace airfix::script;

static_assert(legacyAfsFunctionActivation(LegacyAfsFunctionKind::event) ==
              LegacyAfsFunctionActivation::explicitCall);
static_assert(legacyAfsFunctionActivation(LegacyAfsFunctionKind::action) ==
              LegacyAfsFunctionActivation::autoexec);
static_assert(legacyAfsFunctionActivation(LegacyAfsFunctionKind::timer) ==
              LegacyAfsFunctionActivation::autoexec);
static_assert(legacyAfsFunctionActivation(static_cast<LegacyAfsFunctionKind>(
                  0xFFU)) == LegacyAfsFunctionActivation::unsupported);

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

void testEmptySourceProducesEmptyReadySchedule() {
  const auto result = legacyAfsBuildInitialFunctionSchedule(
      std::span<const LegacyAfsFunctionKind>{});
  require(result.ready(), "empty source was rejected");
  require(result.functions.empty(), "empty source scheduled a function");
}

void testEventsRemainExplicitOnly() {
  constexpr std::array declarations{
      LegacyAfsFunctionKind::event,
      LegacyAfsFunctionKind::event,
      LegacyAfsFunctionKind::event,
  };

  const auto result = legacyAfsBuildInitialFunctionSchedule(declarations);
  require(result.ready(), "event-only source was rejected");
  require(result.functions.empty(), "an event was scheduled as Autoexec");
}

void testActionsAndTimersUseReverseSourceOrder() {
  constexpr std::array declarations{
      LegacyAfsFunctionKind::event,  LegacyAfsFunctionKind::action,
      LegacyAfsFunctionKind::timer,  LegacyAfsFunctionKind::event,
      LegacyAfsFunctionKind::action,
  };
  constexpr std::array expected{
      LegacyAfsScheduledFunction{4, LegacyAfsFunctionKind::action},
      LegacyAfsScheduledFunction{2, LegacyAfsFunctionKind::timer},
      LegacyAfsScheduledFunction{1, LegacyAfsFunctionKind::action},
  };

  const auto result = legacyAfsBuildInitialFunctionSchedule(declarations);
  require(result.ready(), "mixed source was rejected");
  require(result.functions.size() == expected.size(),
          "mixed source produced the wrong schedule size");
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(result.functions[index] == expected[index],
            "mixed source schedule order changed");
  }
}

void testRepeatedKindsAreNotDeduplicated() {
  constexpr std::array declarations{
      LegacyAfsFunctionKind::action,
      LegacyAfsFunctionKind::action,
      LegacyAfsFunctionKind::timer,
      LegacyAfsFunctionKind::timer,
  };
  constexpr std::array expected{
      LegacyAfsScheduledFunction{3, LegacyAfsFunctionKind::timer},
      LegacyAfsScheduledFunction{2, LegacyAfsFunctionKind::timer},
      LegacyAfsScheduledFunction{1, LegacyAfsFunctionKind::action},
      LegacyAfsScheduledFunction{0, LegacyAfsFunctionKind::action},
  };

  const auto result = legacyAfsBuildInitialFunctionSchedule(declarations);
  require(result.ready(), "repeated source kinds were rejected");
  require(result.functions.size() == expected.size(),
          "repeated source kinds were dropped");
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(result.functions[index] == expected[index],
            "repeated source-kind order changed");
  }
}

void testUnsupportedKindRejectsAtomically() {
  constexpr auto unsupported =
      static_cast<LegacyAfsFunctionKind>(std::uint8_t{0xFE});
  constexpr std::array declarations{
      LegacyAfsFunctionKind::action,
      unsupported,
      LegacyAfsFunctionKind::timer,
  };

  const auto result = legacyAfsBuildInitialFunctionSchedule(declarations);
  require(result.status == LegacyAfsFunctionScheduleStatus::unsupportedKind &&
              !result.ready(),
          "unsupported function kind was accepted");
  require(result.functions.empty(),
          "unsupported function kind returned a partial schedule");
}

} // namespace

int main() {
  testEmptySourceProducesEmptyReadySchedule();
  testEventsRemainExplicitOnly();
  testActionsAndTimersUseReverseSourceOrder();
  testRepeatedKindsAreNotDeduplicated();
  testUnsupportedKindRejectsAtomically();
  std::cout << "Legacy AFS function schedule tests passed\n";
  return EXIT_SUCCESS;
}
