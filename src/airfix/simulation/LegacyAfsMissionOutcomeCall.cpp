#include "airfix/simulation/LegacyAfsMissionOutcomeCall.hpp"

namespace airfix::simulation {

LegacyAfsMissionOutcomeCallDecodeResult legacyAfsDecodeMissionOutcomeCall(
    const std::span<const std::uint32_t> instructionWords,
    const LegacyAfsNativeFunctionDescriptorView descriptor) noexcept {
  if (instructionWords.size() <
      legacyAfsMissionOutcomeCallInstructionWords) {
    return {
        .status =
            LegacyAfsMissionOutcomeCallDecodeStatus::truncatedInstruction,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  if (instructionWords[0] != legacyAfsPushImmediateOpcode ||
      instructionWords[1] != legacyAfsTrueImmediate) {
    return {
        .status = LegacyAfsMissionOutcomeCallDecodeStatus::
            unsupportedArgumentProducer,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  if (instructionWords[2] != legacyAfsPushExecutionOpcode ||
      instructionWords[3] != legacyAfsCallNativeOpcode) {
    return {
        .status =
            LegacyAfsMissionOutcomeCallDecodeStatus::unsupportedInstruction,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  if (descriptor.descriptorToken == 0U) {
    return {
        .status =
            LegacyAfsMissionOutcomeCallDecodeStatus::invalidDescriptorToken,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  if (instructionWords[4] != descriptor.descriptorToken) {
    return {
        .status =
            LegacyAfsMissionOutcomeCallDecodeStatus::descriptorTokenMismatch,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  if (!descriptor.hasExplicitHandler) {
    return {
        .status =
            LegacyAfsMissionOutcomeCallDecodeStatus::missingExplicitHandler,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  if (descriptor.argumentWordCount !=
      legacyAfsMissionOutcomeArgumentWords) {
    return {
        .status = LegacyAfsMissionOutcomeCallDecodeStatus::
            unexpectedArgumentWordCount,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  LegacyMissionOutcomeCall call{};
  switch (descriptor.functionId) {
  case static_cast<std::uint32_t>(
      LegacyMissionOutcomeCall::missionFail):
    call = LegacyMissionOutcomeCall::missionFail;
    break;
  case static_cast<std::uint32_t>(
      LegacyMissionOutcomeCall::missionSuccess):
    call = LegacyMissionOutcomeCall::missionSuccess;
    break;
  default:
    return {
        .status =
            LegacyAfsMissionOutcomeCallDecodeStatus::unsupportedFunction,
        .call = std::nullopt,
        .consumedWords = 0U,
    };
  }

  return {
      .status = LegacyAfsMissionOutcomeCallDecodeStatus::decoded,
      .call = call,
      .consumedWords = legacyAfsMissionOutcomeCallInstructionWords,
  };
}

} // namespace airfix::simulation
