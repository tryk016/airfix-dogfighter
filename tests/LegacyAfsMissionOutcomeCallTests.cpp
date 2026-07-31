#include "airfix/simulation/LegacyAfsMissionOutcomeCall.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(legacyAfsPushImmediateOpcode == 0x1BU);
static_assert(legacyAfsTrueImmediate == 1U);
static_assert(legacyAfsPushExecutionOpcode == 0x24U);
static_assert(legacyAfsCallNativeOpcode == 0x26U);
static_assert(legacyAfsNativeCallInstructionWords == 3U);
static_assert(legacyAfsMissionOutcomeCallInstructionWords == 5U);
static_assert(legacyAfsMissionOutcomeArgumentWords == 1U);
static_assert(std::is_trivially_copyable_v<
              LegacyAfsNativeFunctionDescriptorView>);
static_assert(std::is_trivially_copyable_v<
              LegacyAfsMissionOutcomeCallDecodeResult>);
static_assert(noexcept(legacyAfsDecodeMissionOutcomeCall({}, {})));

[[noreturn]] void fail(const char *const message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char *const message) {
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] LegacyAfsNativeFunctionDescriptorView descriptor(
    const std::uint32_t functionId,
    const std::uint32_t token = 0xA5F00001U) noexcept {
  return {
      .descriptorToken = token,
      .argumentWordCount = legacyAfsMissionOutcomeArgumentWords,
      .functionId = functionId,
      .hasExplicitHandler = true,
  };
}

[[nodiscard]] std::array<
    std::uint32_t,
    legacyAfsMissionOutcomeCallInstructionWords>
instruction(const std::uint32_t token = 0xA5F00001U) noexcept {
  return {
      legacyAfsPushImmediateOpcode,
      legacyAfsTrueImmediate,
      legacyAfsPushExecutionOpcode,
      legacyAfsCallNativeOpcode,
      token,
  };
}

void requireDecoded(
    const LegacyAfsMissionOutcomeCallDecodeResult &result,
    const LegacyMissionOutcomeCall expected,
    const char *const message) {
  require(result.decoded(), message);
  require(result.call == expected, message);
  require(result.consumedWords ==
              legacyAfsMissionOutcomeCallInstructionWords,
          message);
}

void testExactOutcomeCallsAndTrailingWords() {
  const auto words = instruction();
  requireDecoded(
      legacyAfsDecodeMissionOutcomeCall(
          words,
          descriptor(static_cast<std::uint32_t>(
              LegacyMissionOutcomeCall::missionFail))),
      LegacyMissionOutcomeCall::missionFail,
      "MissionFail instruction was not decoded");
  requireDecoded(
      legacyAfsDecodeMissionOutcomeCall(
          words,
          descriptor(static_cast<std::uint32_t>(
              LegacyMissionOutcomeCall::missionSuccess))),
      LegacyMissionOutcomeCall::missionSuccess,
      "MissionSuccess instruction was not decoded");

  constexpr std::array<std::uint32_t, 7U> withTrailingWords{
      legacyAfsPushImmediateOpcode,
      legacyAfsTrueImmediate,
      legacyAfsPushExecutionOpcode,
      legacyAfsCallNativeOpcode,
      0xF00D1234U,
      0x45U,
      0xDEADBEEFU,
  };
  requireDecoded(
      legacyAfsDecodeMissionOutcomeCall(
          withTrailingWords,
          descriptor(
              static_cast<std::uint32_t>(
                  LegacyMissionOutcomeCall::missionSuccess),
              0xF00D1234U)),
      LegacyMissionOutcomeCall::missionSuccess,
      "decoder consumed or rejected trailing bytecode");
}

void testDecodedCallComposesWithOutcomeState() {
  const auto words = instruction();
  const auto decoded = legacyAfsDecodeMissionOutcomeCall(
      words,
      descriptor(static_cast<std::uint32_t>(
          LegacyMissionOutcomeCall::missionFail)));
  require(decoded.decoded(), "composed call did not decode");

  const auto applied =
      legacyMissionApplyOutcomeCall({}, *decoded.call);
  require(applied.applied() && applied.state.failed &&
              !applied.state.accomplished &&
              applied.requestPauseThenMenu,
          "decoded call did not compose with mission outcome state");
}

void testTruncatedInstructionFailsClosed() {
  const auto words = instruction();
  for (std::size_t size = 0U;
       size < legacyAfsMissionOutcomeCallInstructionWords;
       ++size) {
    const auto result = legacyAfsDecodeMissionOutcomeCall(
        std::span<const std::uint32_t>{words}.first(size),
        descriptor(static_cast<std::uint32_t>(
            LegacyMissionOutcomeCall::missionFail)));
    require(
        result.status ==
                LegacyAfsMissionOutcomeCallDecodeStatus::
                    truncatedInstruction &&
            !result.call.has_value() && result.consumedWords == 0U &&
            !result.decoded(),
        "truncated instruction was accepted");
  }
}

void testUnsupportedArgumentProducerFailsClosed() {
  for (const auto words : {
           std::array<std::uint32_t, 5U>{
               0x1AU,
               legacyAfsTrueImmediate,
               legacyAfsPushExecutionOpcode,
               legacyAfsCallNativeOpcode,
               0xA5F00001U},
           std::array<std::uint32_t, 5U>{
               legacyAfsPushImmediateOpcode,
               0U,
               legacyAfsPushExecutionOpcode,
               legacyAfsCallNativeOpcode,
               0xA5F00001U},
           std::array<std::uint32_t, 5U>{
               legacyAfsPushImmediateOpcode,
               2U,
               legacyAfsPushExecutionOpcode,
               legacyAfsCallNativeOpcode,
               0xA5F00001U},
           std::array<std::uint32_t, 5U>{
               0x1DU,
               legacyAfsTrueImmediate,
               legacyAfsPushExecutionOpcode,
               legacyAfsCallNativeOpcode,
               0xA5F00001U},
       }) {
    const auto result = legacyAfsDecodeMissionOutcomeCall(
        words,
        descriptor(static_cast<std::uint32_t>(
            LegacyMissionOutcomeCall::missionFail)));
    require(
        result.status ==
                LegacyAfsMissionOutcomeCallDecodeStatus::
                    unsupportedArgumentProducer &&
            !result.call.has_value() && result.consumedWords == 0U,
        "unsupported argument producer was accepted");
  }
}

void testUnsupportedInstructionFailsClosed() {
  for (const auto words : {
           std::array<std::uint32_t, 5U>{
               legacyAfsPushImmediateOpcode,
               legacyAfsTrueImmediate,
               0x23U,
               legacyAfsCallNativeOpcode,
               0xA5F00001U},
           std::array<std::uint32_t, 5U>{
               legacyAfsPushImmediateOpcode,
               legacyAfsTrueImmediate,
               legacyAfsPushExecutionOpcode,
               0x25U,
               0xA5F00001U},
           std::array<std::uint32_t, 5U>{
               legacyAfsPushImmediateOpcode,
               legacyAfsTrueImmediate,
               legacyAfsPushExecutionOpcode,
               0x27U,
               0xA5F00001U},
           std::array<std::uint32_t, 5U>{
               legacyAfsPushImmediateOpcode,
               legacyAfsTrueImmediate,
               0x23U,
               0x25U,
               0xA5F00001U},
       }) {
    const auto result = legacyAfsDecodeMissionOutcomeCall(
        words,
        descriptor(static_cast<std::uint32_t>(
            LegacyMissionOutcomeCall::missionFail)));
    require(
        result.status ==
                LegacyAfsMissionOutcomeCallDecodeStatus::
                    unsupportedInstruction &&
            !result.call.has_value() && result.consumedWords == 0U,
        "unsupported opcode pair was accepted");
  }
}

void testDescriptorIdentityAndShapeFailClosed() {
  const auto words = instruction();

  auto invalidToken = descriptor(static_cast<std::uint32_t>(
      LegacyMissionOutcomeCall::missionFail));
  invalidToken.descriptorToken = 0U;
  auto result =
      legacyAfsDecodeMissionOutcomeCall(words, invalidToken);
  require(
      result.status ==
              LegacyAfsMissionOutcomeCallDecodeStatus::
                  invalidDescriptorToken &&
          !result.call.has_value() && result.consumedWords == 0U,
      "zero descriptor token was accepted");

  result = legacyAfsDecodeMissionOutcomeCall(
      words,
      descriptor(
          static_cast<std::uint32_t>(
              LegacyMissionOutcomeCall::missionFail),
          0xA5F00002U));
  require(
      result.status ==
              LegacyAfsMissionOutcomeCallDecodeStatus::
                  descriptorTokenMismatch &&
          !result.call.has_value() && result.consumedWords == 0U,
      "mismatched descriptor token was accepted");

  auto fallbackDescriptor = descriptor(static_cast<std::uint32_t>(
      LegacyMissionOutcomeCall::missionFail));
  fallbackDescriptor.hasExplicitHandler = false;
  result =
      legacyAfsDecodeMissionOutcomeCall(words, fallbackDescriptor);
  require(
      result.status ==
              LegacyAfsMissionOutcomeCallDecodeStatus::
                  missingExplicitHandler &&
          !result.call.has_value() && result.consumedWords == 0U,
      "fallback-only descriptor was accepted");

  for (const std::uint32_t argumentWords : {0U, 2U, 3U, 0xFFFFFFFFU}) {
    auto wrongArguments = descriptor(static_cast<std::uint32_t>(
        LegacyMissionOutcomeCall::missionFail));
    wrongArguments.argumentWordCount = argumentWords;
    result =
        legacyAfsDecodeMissionOutcomeCall(words, wrongArguments);
    require(
        result.status ==
                LegacyAfsMissionOutcomeCallDecodeStatus::
                    unexpectedArgumentWordCount &&
            !result.call.has_value() && result.consumedWords == 0U,
        "wrong descriptor argument width was accepted");
  }
}

void testUnsupportedFunctionFailsClosed() {
  const auto words = instruction();
  for (const std::uint32_t functionId :
       {0U, 0x46U, 0x49U, 0xFFFFFFFFU}) {
    const auto result = legacyAfsDecodeMissionOutcomeCall(
        words, descriptor(functionId));
    require(
        result.status ==
                LegacyAfsMissionOutcomeCallDecodeStatus::
                    unsupportedFunction &&
            !result.call.has_value() && result.consumedWords == 0U,
        "unsupported native function was accepted");
  }
}

} // namespace

int main() {
  testExactOutcomeCallsAndTrailingWords();
  testDecodedCallComposesWithOutcomeState();
  testTruncatedInstructionFailsClosed();
  testUnsupportedArgumentProducerFailsClosed();
  testUnsupportedInstructionFailsClosed();
  testDescriptorIdentityAndShapeFailClosed();
  testUnsupportedFunctionFailsClosed();
  std::cout << "Legacy AFS mission outcome call tests passed\n";
  return EXIT_SUCCESS;
}
