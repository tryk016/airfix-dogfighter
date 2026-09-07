#include "airfix/script/LegacyAfsWordcode.hpp"

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

namespace airfix::script {
namespace {

// EV-20260803-003, complete dispatch table. Default framing is one word with
// sequential flow; only the explicit exceptions below have an operand.
constexpr auto opcodeTable = [] {
  std::array<LegacyAfsOpcodeInfo, 0x46> table{};
  const auto operand = [&](std::uint32_t id, LegacyAfsOperand kind) {
    table[id - 1].operand = kind;
  };
  for (auto id : {0x11U, 0x12U, 0x13U})
    operand(id, LegacyAfsOperand::branchWordIndex);
  for (auto id : {0x16U, 0x17U, 0x1DU, 0x1EU})
    operand(id, LegacyAfsOperand::localWordIndex);
  for (auto id : {0x18U, 0x19U, 0x1FU, 0x20U})
    operand(id, LegacyAfsOperand::processWordIndex);
  for (auto id : {0x21U, 0x22U})
    operand(id, LegacyAfsOperand::objectFieldIndex);
  operand(0x1A, LegacyAfsOperand::inlineTextWordCount);
  operand(0x1B, LegacyAfsOperand::immediateBits);
  for (auto id : {0x25U, 0x26U, 0x27U})
    operand(id, LegacyAfsOperand::nativeDescriptor);
  operand(0x42, LegacyAfsOperand::handlerId);

  table[0x11 - 1].flow = LegacyAfsFlow::branchIfNonzero;
  table[0x12 - 1].flow = LegacyAfsFlow::branchIfZero;
  table[0x13 - 1].flow = LegacyAfsFlow::branch;
  table[0x43 - 1].flow = LegacyAfsFlow::yield;
  table[0x44 - 1].flow = LegacyAfsFlow::terminal;
  table[0x45 - 1].flow = LegacyAfsFlow::terminal;
  // 0x46 clears the active flag but does not terminate this Interpret loop.

  for (auto id : {0x05U, 0x06U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U, 0x19U,
                  0x1AU, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U,
                  0x39U, 0x3CU, 0x3DU, 0x3EU, 0x3FU, 0x42U, 0x46U})
    table[id - 1].requirements.typedOwnership = true;
  for (std::uint32_t id = 0x0D; id <= 0x10; ++id)
    table[id - 1].requirements.numericPolicy = true;
  for (std::uint32_t id = 0x28; id <= 0x3F; ++id)
    table[id - 1].requirements.numericPolicy = true;
  for (auto id : {0x41U, 0x43U})
    table[id - 1].requirements.numericPolicy = true;
  for (auto id : {0x25U, 0x26U, 0x27U, 0x43U, 0x44U, 0x45U, 0x46U})
    table[id - 1].requirements.lifecyclePolicy = true;

  const auto stack = [&](std::uint32_t id, std::uint8_t popped,
                         std::uint8_t pushed) {
    table[id - 1].stack = {LegacyAfsStackShapeKind::fixed, popped, pushed,
                           pushed};
  };
  for (auto id : {0x01U, 0x02U, 0x03U, 0x39U})
    stack(id, 2, 1);
  for (std::uint32_t id = 0x05; id <= 0x10; ++id)
    stack(id, 2, 1);
  for (std::uint32_t id = 0x28; id <= 0x2C; ++id)
    stack(id, 2, 1);
  for (std::uint32_t id = 0x2E; id <= 0x32; ++id)
    stack(id, 2, 1);
  for (auto id :
       {0x04U, 0x21U, 0x2DU, 0x33U, 0x3AU, 0x3BU, 0x3DU, 0x3FU, 0x42U})
    stack(id, 1, 1);
  for (auto id : {0x11U, 0x12U, 0x14U, 0x15U, 0x1CU, 0x1EU, 0x20U, 0x43U})
    stack(id, 1, 0);
  for (auto id : {0x1AU, 0x1BU, 0x1DU, 0x1FU, 0x23U, 0x24U})
    stack(id, 0, 1);
  stack(0x22, 2, 0);
  for (std::uint32_t id = 0x34; id <= 0x37; ++id)
    stack(id, 6, 3);
  stack(0x38, 3, 3);
  stack(0x40, 1, 3);
  stack(0x41, 1, 3);
  for (auto id : {0x3CU, 0x3EU})
    table[id - 1].stack = {LegacyAfsStackShapeKind::nullableReferenceResult, 1,
                           0, 1};
  for (auto id : {0x25U, 0x26U, 0x27U})
    table[id - 1].stack.kind = LegacyAfsStackShapeKind::descriptorDependent;
  return table;
}();

LegacyAfsWordcodeInspection reject(LegacyAfsWordcodeError error,
                                   std::size_t offset) noexcept {
  return {.error = error, .errorWord = offset};
}

} // namespace

const LegacyAfsOpcodeInfo *legacyAfsOpcodeInfo(std::uint32_t opcode) noexcept {
  if (opcode == 0 || opcode > opcodeTable.size())
    return nullptr;
  return &opcodeTable[opcode - 1];
}

LegacyAfsInstructionDecode
decodeLegacyAfsInstruction(std::span<const std::uint32_t> words,
                           std::size_t wordOffset,
                           const LegacyAfsWordcodeLimits &limits) noexcept {
  if (words.size() > limits.maxWords)
    return {.error = LegacyAfsWordcodeError::wordLimit};
  if (wordOffset >= words.size())
    return {.error = LegacyAfsWordcodeError::offsetOutOfRange};
  const auto *info = legacyAfsOpcodeInfo(words[wordOffset]);
  if (!info)
    return {.error = LegacyAfsWordcodeError::unsupportedOpcode};

  LegacyAfsInstruction instruction{.wordOffset = wordOffset,
                                   .wordCount = 1,
                                   .opcode = words[wordOffset],
                                   .info = *info};
  if (info->operand != LegacyAfsOperand::none) {
    if (words.size() - wordOffset < 2)
      return {.error = LegacyAfsWordcodeError::missingOperand};
    instruction.wordCount = 2;
    instruction.operandBits = words[wordOffset + 1];
  }
  if (info->operand == LegacyAfsOperand::inlineTextWordCount) {
    const std::size_t payloadWords = instruction.operandBits;
    if (payloadWords > words.size() - wordOffset - 2)
      return {.error = LegacyAfsWordcodeError::truncatedInlineText};
    if (payloadWords > limits.maxInlineTextBytes / 4)
      return {.error = LegacyAfsWordcodeError::inlineTextLimit};
    bool terminated = false;
    for (std::size_t index = 0; index < payloadWords && !terminated; ++index) {
      const auto word = words[wordOffset + 2 + index];
      for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        if (((word >> shift) & 0xFFU) == 0) {
          terminated = true;
          break;
        }
        ++instruction.inlineTextByteCount;
      }
    }
    if (!terminated)
      return {.error = LegacyAfsWordcodeError::missingTextTerminator};
    instruction.wordCount += payloadWords;
  }
  return {.error = LegacyAfsWordcodeError::none, .instruction = instruction};
}

LegacyAfsWordcodeInspection
inspectLegacyAfsWordcode(std::span<const std::uint32_t> words,
                         std::size_t localWordCount,
                         std::size_t processWordCount,
                         const LegacyAfsWordcodeLimits &limits) noexcept {
  if (words.size() > limits.maxWords)
    return reject(LegacyAfsWordcodeError::wordLimit, 0);
  if (words.empty())
    return reject(LegacyAfsWordcodeError::emptyCode, 0);
  std::size_t offset = 0;
  try {
    LegacyAfsWordcodeInspection result{.error = LegacyAfsWordcodeError::none};
    result.instructions.reserve(std::min(words.size(), limits.maxInstructions));
    while (offset < words.size()) {
      if (result.instructions.size() == limits.maxInstructions)
        return reject(LegacyAfsWordcodeError::instructionLimit, offset);
      const auto decoded = decodeLegacyAfsInstruction(words, offset, limits);
      if (!decoded.decoded())
        return reject(decoded.error, offset);
      const auto &instruction = decoded.instruction;
      const auto operand = instruction.info.operand;
      if (operand == LegacyAfsOperand::localWordIndex &&
          instruction.operandBits >= localWordCount)
        return reject(LegacyAfsWordcodeError::localIndexOutOfRange, offset);
      if (operand == LegacyAfsOperand::processWordIndex &&
          instruction.operandBits >= processWordCount)
        return reject(LegacyAfsWordcodeError::processIndexOutOfRange, offset);
      result.requirements.typedOwnership |=
          instruction.info.requirements.typedOwnership;
      result.requirements.numericPolicy |=
          instruction.info.requirements.numericPolicy;
      result.requirements.lifecyclePolicy |=
          instruction.info.requirements.lifecyclePolicy;
      result.instructions.push_back(instruction);
      offset += instruction.wordCount; // Decoder proved this is within span.
    }
    for (const auto &instruction : result.instructions) {
      offset = instruction.wordOffset;
      const auto flow = instruction.info.flow;
      if (instruction.info.operand == LegacyAfsOperand::branchWordIndex) {
        const std::size_t target = instruction.operandBits;
        if (target >= words.size())
          return reject(LegacyAfsWordcodeError::branchOutOfRange, offset);
        const auto found = std::lower_bound(
            result.instructions.begin(), result.instructions.end(), target,
            [](const LegacyAfsInstruction &candidate, std::size_t word) {
              return candidate.wordOffset < word;
            });
        if (found == result.instructions.end() || found->wordOffset != target)
          return reject(LegacyAfsWordcodeError::branchNotInstruction, offset);
        const auto kind = flow == LegacyAfsFlow::branch
                              ? LegacyAfsEdgeKind::branch
                          : flow == LegacyAfsFlow::branchIfNonzero
                              ? LegacyAfsEdgeKind::conditionNonzero
                              : LegacyAfsEdgeKind::conditionZero;
        result.edges.push_back({offset, target, kind});
      }
      if (flow != LegacyAfsFlow::branch && flow != LegacyAfsFlow::terminal) {
        const auto next = offset + instruction.wordCount;
        if (next == words.size())
          return reject(LegacyAfsWordcodeError::fallthroughPastEnd, offset);
        const auto kind = flow == LegacyAfsFlow::yield
                              ? LegacyAfsEdgeKind::resumeAfterYield
                          : flow == LegacyAfsFlow::branchIfNonzero
                              ? LegacyAfsEdgeKind::conditionZero
                          : flow == LegacyAfsFlow::branchIfZero
                              ? LegacyAfsEdgeKind::conditionNonzero
                              : LegacyAfsEdgeKind::sequential;
        result.edges.push_back({offset, next, kind});
      }
    }
    return result;
  } catch (const std::bad_alloc &) {
    return reject(LegacyAfsWordcodeError::allocationFailure, offset);
  } catch (const std::length_error &) {
    return reject(LegacyAfsWordcodeError::allocationFailure, offset);
  }
}

} // namespace airfix::script
