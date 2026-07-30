#include "airfix/simulation/LegacyMissionOutcomeState.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(
    static_cast<std::uint8_t>(LegacyMissionOutcomeCall::missionFail) == 0x47U);
static_assert(static_cast<std::uint8_t>(
                  LegacyMissionOutcomeCall::missionSuccess) == 0x48U);
static_assert(std::is_trivially_copyable_v<LegacyMissionOutcomeState>);
static_assert(std::is_trivially_copyable_v<LegacyMissionOutcomeStep>);
static_assert(noexcept(
    legacyMissionApplyOutcomeCall({}, LegacyMissionOutcomeCall::missionFail)));
static_assert(noexcept(legacyMissionMarkFailed({})));
static_assert(noexcept(legacyMissionResetOutcomeState()));

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

void requireStep(const LegacyMissionOutcomeStep &actual,
                 const LegacyMissionOutcomeState expectedState,
                 const bool expectedPauseThenMenu, const char *const message) {
  require(actual.applied(), message);
  require(actual.state == expectedState, message);
  require(actual.requestPauseThenMenu == expectedPauseThenMenu, message);
}

void testInitialResetAndDirectFail() {
  require(legacyMissionResetOutcomeState() == LegacyMissionOutcomeState{},
          "mission reset did not clear both outcome bytes");

  const LegacyMissionOutcomeState successOnly{
      .failed = false,
      .accomplished = true,
  };
  require(legacyMissionMarkFailed(successOnly) ==
              LegacyMissionOutcomeState{
                  .failed = true,
                  .accomplished = true,
              },
          "direct NfMission::Fail changed the accomplished byte");

  const LegacyMissionOutcomeState failedOnly{
      .failed = true,
      .accomplished = false,
  };
  require(legacyMissionMarkFailed(failedOnly) == failedOnly,
          "repeated direct NfMission::Fail changed state");
}

void testFirstTerminalCallRequestsPauseThenMenu() {
  requireStep(
      legacyMissionApplyOutcomeCall({}, LegacyMissionOutcomeCall::missionFail),
      {
          .failed = true,
          .accomplished = false,
      },
      true, "first MissionFail call changed");

  requireStep(legacyMissionApplyOutcomeCall(
                  {}, LegacyMissionOutcomeCall::missionSuccess),
              {
                  .failed = false,
                  .accomplished = true,
              },
              true, "first MissionSuccess call changed");
}

void testEveryNativeStartingStateAndCall() {
  struct Vector final {
    LegacyMissionOutcomeState initial;
    LegacyMissionOutcomeCall call;
    LegacyMissionOutcomeState expected;
    bool requestPauseThenMenu;
  };

  constexpr std::array<Vector, 8> vectors{{
      {{false, false},
       LegacyMissionOutcomeCall::missionFail,
       {true, false},
       true},
      {{false, false},
       LegacyMissionOutcomeCall::missionSuccess,
       {false, true},
       true},
      {{true, false},
       LegacyMissionOutcomeCall::missionFail,
       {true, false},
       false},
      {{true, false},
       LegacyMissionOutcomeCall::missionSuccess,
       {true, true},
       false},
      {{false, true},
       LegacyMissionOutcomeCall::missionFail,
       {true, true},
       false},
      {{false, true},
       LegacyMissionOutcomeCall::missionSuccess,
       {false, true},
       false},
      {{true, true},
       LegacyMissionOutcomeCall::missionFail,
       {true, true},
       false},
      {{true, true},
       LegacyMissionOutcomeCall::missionSuccess,
       {true, true},
       false},
  }};

  for (const Vector &vector : vectors) {
    requireStep(legacyMissionApplyOutcomeCall(vector.initial, vector.call),
                vector.expected, vector.requestPauseThenMenu,
                "exhaustive mission outcome vector changed");
  }
}

void testOrderingConflictsAndReset() {
  auto state = LegacyMissionOutcomeState{};
  const auto first = legacyMissionApplyOutcomeCall(
      state, LegacyMissionOutcomeCall::missionFail);
  state = first.state;

  const auto repeated = legacyMissionApplyOutcomeCall(
      state, LegacyMissionOutcomeCall::missionFail);
  requireStep(repeated,
              {
                  .failed = true,
                  .accomplished = false,
              },
              false, "repeated MissionFail requested presentation");

  const auto conflict = legacyMissionApplyOutcomeCall(
      repeated.state, LegacyMissionOutcomeCall::missionSuccess);
  requireStep(conflict,
              {
                  .failed = true,
                  .accomplished = true,
              },
              false, "MissionSuccess conflict overwrote MissionFail");

  state = legacyMissionResetOutcomeState();
  const auto success = legacyMissionApplyOutcomeCall(
      state, LegacyMissionOutcomeCall::missionSuccess);
  const auto reverseConflict = legacyMissionApplyOutcomeCall(
      success.state, LegacyMissionOutcomeCall::missionFail);
  requireStep(reverseConflict,
              {
                  .failed = true,
                  .accomplished = true,
              },
              false, "MissionFail conflict overwrote MissionSuccess");

  require(legacyMissionResetOutcomeState() == LegacyMissionOutcomeState{},
          "reset after conflicting calls changed");
}

void testUnsupportedCallsFailClosed() {
  constexpr LegacyMissionOutcomeState initial{
      .failed = true,
      .accomplished = false,
  };

  for (const std::uint8_t raw : {0x00U, 0x46U, 0x49U, 0xFFU}) {
    const auto result = legacyMissionApplyOutcomeCall(
        initial, static_cast<LegacyMissionOutcomeCall>(raw));
    require(result.status == LegacyMissionOutcomeApplyStatus::unsupportedCall &&
                !result.applied(),
            "unsupported mission call was accepted");
    require(result.state == initial && !result.requestPauseThenMenu,
            "unsupported mission call changed state or emitted commands");
  }
}

} // namespace

int main() {
  testInitialResetAndDirectFail();
  testFirstTerminalCallRequestsPauseThenMenu();
  testEveryNativeStartingStateAndCall();
  testOrderingConflictsAndReset();
  testUnsupportedCallsFailClosed();
  std::cout << "Legacy mission outcome state tests passed\n";
  return EXIT_SUCCESS;
}
