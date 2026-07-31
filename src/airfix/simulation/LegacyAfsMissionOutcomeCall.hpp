#pragma once

#include "airfix/simulation/LegacyMissionOutcomeState.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::simulation {

inline constexpr std::uint32_t legacyAfsPushImmediateOpcode = 0x1BU;
inline constexpr std::uint32_t legacyAfsTrueImmediate = 1U;
inline constexpr std::uint32_t legacyAfsPushExecutionOpcode = 0x24U;
inline constexpr std::uint32_t legacyAfsCallNativeOpcode = 0x26U;
inline constexpr std::size_t legacyAfsNativeCallInstructionWords = 3U;
inline constexpr std::size_t legacyAfsMissionOutcomeCallInstructionWords = 5U;
inline constexpr std::uint32_t legacyAfsMissionOutcomeArgumentWords = 1U;

// Portable projection of the descriptor fields consumed by opcode 0x26.
// descriptorToken is an opaque 32-bit bytecode word, not a host pointer.
struct LegacyAfsNativeFunctionDescriptorView final {
  std::uint32_t descriptorToken{};
  std::uint32_t argumentWordCount{};
  std::uint32_t functionId{};
  bool hasExplicitHandler{};
};

enum class LegacyAfsMissionOutcomeCallDecodeStatus : std::uint8_t {
  decoded,
  truncatedInstruction,
  unsupportedArgumentProducer,
  unsupportedInstruction,
  invalidDescriptorToken,
  descriptorTokenMismatch,
  missingExplicitHandler,
  unexpectedArgumentWordCount,
  unsupportedFunction,
};

struct LegacyAfsMissionOutcomeCallDecodeResult final {
  LegacyAfsMissionOutcomeCallDecodeStatus status{
      LegacyAfsMissionOutcomeCallDecodeStatus::unsupportedInstruction};
  std::optional<LegacyMissionOutcomeCall> call;
  std::size_t consumedWords{};

  [[nodiscard]] constexpr bool decoded() const noexcept {
    return status == LegacyAfsMissionOutcomeCallDecodeStatus::decoded &&
           call.has_value() &&
           consumedWords == legacyAfsMissionOutcomeCallInstructionWords;
  }
};

// Recognizes only the proven complete mission-outcome call site:
//
//   0x1B, 0x00000001, 0x24, 0x26, descriptor-token
//
// Opcode 0x1B pushes the immediate true argument. Opcodes 0x24 and 0x26 form
// the embedded native-call triplet. This function deliberately does not
// inspect or consume a VM stack, execute the call, resolve native pointers, or
// model scheduler behavior. It validates the portable descriptor projection
// and returns one typed call for the existing LegacyMissionOutcomeState
// boundary.
[[nodiscard]] LegacyAfsMissionOutcomeCallDecodeResult
legacyAfsDecodeMissionOutcomeCall(
    std::span<const std::uint32_t> instructionWords,
    LegacyAfsNativeFunctionDescriptorView descriptor) noexcept;

} // namespace airfix::simulation
