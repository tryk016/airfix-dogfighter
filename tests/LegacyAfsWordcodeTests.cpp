#include "airfix/script/LegacyAfsWordcode.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <vector>

namespace {
// Exercise every vector allocation failure without enormous host allocations.
long allocationsUntilFailure = -1;
long liveAllocations = 0;
} // namespace

void *operator new(std::size_t size) {
  if (allocationsUntilFailure == 0)
    throw std::bad_alloc{};
  if (allocationsUntilFailure > 0)
    --allocationsUntilFailure;
  if (auto *memory = std::malloc(size == 0 ? 1 : size)) {
    ++liveAllocations;
    return memory;
  }
  throw std::bad_alloc{};
}
void operator delete(void *memory) noexcept {
  if (memory)
    --liveAllocations;
  std::free(memory);
}
#if defined(__cpp_sized_deallocation) || defined(_MSC_VER)
void operator delete(void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}
#endif

namespace {
using namespace airfix::script;

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void rejected(const LegacyAfsWordcodeInspection &result,
              LegacyAfsWordcodeError error, std::size_t offset) {
  require(!result.structurallyValid() && result.error == error &&
              result.errorWord == offset,
          "wrong structural rejection or error location");
  require(result.instructions.empty() && result.edges.empty() &&
              result.requirements == LegacyAfsExecutionRequirements{},
          "rejection leaked a partial inspection");
}

// Independently transcribed widths from EV-20260803-003, rows 01..46.
// Zero denotes the variable-width inline-text instruction, not zero words.
constexpr std::array<unsigned, 70> expectedWidths{
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 01..10
    2, 2, 2, 1, 1, 2, 2, 2, 2, 0, 2, 1, 2, 2, 2, 2, // 11..20
    2, 2, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 21..30
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 31..40
    1, 2, 1, 1, 1, 1,                               // 41..46
};

void testEveryOpcodeAndFraming() {
  for (std::uint32_t opcode = 1; opcode <= 0x46; ++opcode) {
    const bool text = opcode == 0x1A;
    const auto width = expectedWidths[opcode - 1];
    std::vector<std::uint32_t> words{opcode};
    if (text) {
      words.insert(words.end(), {2, 0x64636261U, 0x00786665U});
    } else if (width == 2) {
      words.push_back(0);
    }
    words.push_back(0x44);
    const auto decoded = decodeLegacyAfsInstruction(words, 0);
    require(decoded.decoded() && decoded.instruction.opcode == opcode &&
                decoded.instruction.wordCount == (text ? 4 : width),
            "one of 70 opcode widths differs from evidence");
    require(decoded.instruction.inlineTextByteCount == (text ? 7U : 0U),
            "inline text length incorrect");
    const auto whole = inspectLegacyAfsWordcode(words, 1, 1);
    require(whole.structurallyValid() && whole.instructions.size() == 2,
            "one of 70 opcodes failed whole-function framing");
    require(whole.instructions[1].wordOffset == words.size() - 1,
            "an operand or text word became an instruction");
    if (width == 2 || text) {
      const auto truncated = decodeLegacyAfsInstruction(
          std::span<const std::uint32_t>{words}.first(1), 0);
      require(truncated.error == LegacyAfsWordcodeError::missingOperand &&
                  truncated.instruction.wordCount == 0,
              "missing argument was not rejected atomically");
    }
  }
  for (auto invalid : {0U, 0x47U, 0x101U, 0x10001U, 0xFFFFFFFFU}) {
    require(legacyAfsOpcodeInfo(invalid) == nullptr,
            "invalid opcode narrowed into a valid one");
    const std::array words{invalid};
    require(decodeLegacyAfsInstruction(words, 0).error ==
                LegacyAfsWordcodeError::unsupportedOpcode,
            "unsupported opcode decoded");
    rejected(inspectLegacyAfsWordcode(words, 0, 0),
             LegacyAfsWordcodeError::unsupportedOpcode, 0);
  }
}

void testInlineTextBoundariesAndBits() {
  for (std::uint32_t byte = 0; byte < 4; ++byte) {
    const auto payload = 0x78787878U & ~(0xFFU << (byte * 8));
    const std::array words{0x1AU, 1U, payload, 0x44U};
    const auto decoded = decodeLegacyAfsInstruction(words, 0);
    require(decoded.decoded() &&
                decoded.instruction.inlineTextByteCount == byte,
            "little-endian NUL position incorrect");
  }
  const std::array zeroCount{0x1AU, 0U, 0x44U};
  rejected(inspectLegacyAfsWordcode(zeroCount, 0, 0),
           LegacyAfsWordcodeError::missingTextTerminator, 0);
  const std::array noNul{0x1AU, 1U, 0xFFFFFFFFU, 0x44U};
  rejected(inspectLegacyAfsWordcode(noNul, 0, 0),
           LegacyAfsWordcodeError::missingTextTerminator, 0);
  const std::array shortPayload{0x1AU, 3U, 0U};
  rejected(inspectLegacyAfsWordcode(shortPayload, 0, 0),
           LegacyAfsWordcodeError::truncatedInlineText, 0);
  const std::array hugePayload{0x1AU, 0xFFFFFFFFU, 0U};
  rejected(inspectLegacyAfsWordcode(hugePayload, 0, 0),
           LegacyAfsWordcodeError::truncatedInlineText, 0);
  const std::array payload{0x1AU, 2U, 0U, 0xFFFFFFFFU, 0x44U};
  // Padding after the first NUL is opaque, not a string or an opcode.
  require(
      inspectLegacyAfsWordcode(payload, 0, 0, {5, 2, 8}).structurallyValid(),
      "exact text budget or opaque padding rejected");
  rejected(inspectLegacyAfsWordcode(payload, 0, 0, {5, 2, 7}),
           LegacyAfsWordcodeError::inlineTextLimit, 0);

  for (auto bits : {0U, 0x80000000U, 0x7FC01234U, 0xFFFFFFFFU}) {
    const std::array immediate{0x1BU, bits, 0x44U};
    const auto result = inspectLegacyAfsWordcode(immediate, 0, 0);
    require(result.structurallyValid() &&
                result.instructions[0].operandBits == bits,
            "immediate NaN/signed-zero/integer bits were changed");
    const std::array descriptor{0x26U, bits, 0x44U};
    const auto opaque = inspectLegacyAfsWordcode(descriptor, 0, 0);
    require(opaque.structurallyValid() &&
                opaque.instructions[0].operandBits == bits &&
                opaque.requirements.typedOwnership &&
                opaque.requirements.lifecyclePolicy,
            "descriptor was dereferenced or marked executable");
  }
}

void testGraphAndContinuation() {
  const std::array words{0x1BU, 1U, 0x11U, 8U, 0x43U, 0x46U, 0x13U, 0U, 0x45U};
  const std::array expected{
      LegacyAfsControlFlowEdge{0, 2, LegacyAfsEdgeKind::sequential},
      LegacyAfsControlFlowEdge{2, 8, LegacyAfsEdgeKind::conditionNonzero},
      LegacyAfsControlFlowEdge{2, 4, LegacyAfsEdgeKind::conditionZero},
      LegacyAfsControlFlowEdge{4, 5, LegacyAfsEdgeKind::resumeAfterYield},
      LegacyAfsControlFlowEdge{5, 6, LegacyAfsEdgeKind::sequential},
      LegacyAfsControlFlowEdge{6, 0, LegacyAfsEdgeKind::branch},
  };
  const auto result = inspectLegacyAfsWordcode(words, 0, 0);
  require(result.structurallyValid() &&
              std::equal(result.edges.begin(), result.edges.end(),
                         expected.begin(), expected.end()),
          "conditional/absolute/backward/yield/46 graph differs");
  const std::array zeroBranch{0x12U, 2U, 0x44U};
  const auto zero = inspectLegacyAfsWordcode(zeroBranch, 0, 0);
  require(zero.structurallyValid() && zero.edges.size() == 2 &&
              zero.edges[0].kind == LegacyAfsEdgeKind::conditionZero &&
              zero.edges[1].kind == LegacyAfsEdgeKind::conditionNonzero,
          "zero branch or coincident destinations lost their labels");
  // A self-loop is structurally valid, but no termination or runtime fuel
  // guarantee is claimed by this layer.
  const std::array loop{0x13U, 0U};
  require(inspectLegacyAfsWordcode(loop, 0, 0).structurallyValid(),
          "valid absolute self-loop rejected");
  for (auto terminal : {0x44U, 0x45U}) {
    const std::array code{terminal};
    const auto stop = inspectLegacyAfsWordcode(code, 0, 0);
    require(stop.structurallyValid() && stop.edges.empty(),
            "terminal instruction gained a continuation");
  }
  for (auto fallthrough : {0x01U, 0x43U, 0x46U}) {
    const std::array code{0x44U, fallthrough};
    rejected(inspectLegacyAfsWordcode(code, 0, 0),
             LegacyAfsWordcodeError::fallthroughPastEnd, 1);
  }
  const std::array deadInvalid{0x44U, 0U};
  rejected(inspectLegacyAfsWordcode(deadInvalid, 0, 0),
           LegacyAfsWordcodeError::unsupportedOpcode, 1);
}

void testInvalidBranchTargets() {
  // Instruction starts are 0, 2, 4, 8; argument and payload words cannot be
  // branch destinations, even when their raw bits are valid opcode IDs.
  std::array words{0x13U, 0U, 0x1BU, 1U, 0x1AU, 2U, 0x44U, 0x45U, 0x44U};
  for (auto target : {1U, 3U, 5U, 6U, 7U}) {
    words[1] = target;
    rejected(inspectLegacyAfsWordcode(words, 0, 0),
             LegacyAfsWordcodeError::branchNotInstruction, 0);
  }
  for (auto target : {9U, 10U, 0xFFFFFFFFU}) {
    words[1] = target;
    rejected(inspectLegacyAfsWordcode(words, 0, 0),
             LegacyAfsWordcodeError::branchOutOfRange, 0);
  }
  for (auto target : {0U, 2U, 4U, 8U}) {
    words[1] = target;
    require(inspectLegacyAfsWordcode(words, 0, 0).structurallyValid(),
            "valid forward/backward instruction target rejected");
  }
  const std::array conditionalEnd{0x44U, 0x11U, 0U};
  rejected(inspectLegacyAfsWordcode(conditionalEnd, 0, 0),
           LegacyAfsWordcodeError::fallthroughPastEnd, 1);
}

void testIndexAndResourceLimits() {
  require(!LegacyAfsInstructionDecode{}.decoded() &&
              !LegacyAfsWordcodeInspection{}.structurallyValid(),
          "default-constructed result claims successful validation");
  for (auto opcode : {0x16U, 0x17U, 0x1DU, 0x1EU, 0x18U, 0x19U, 0x1FU, 0x20U}) {
    const bool local =
        opcode == 0x16 || opcode == 0x17 || opcode == 0x1D || opcode == 0x1E;
    const auto error = local ? LegacyAfsWordcodeError::localIndexOutOfRange
                             : LegacyAfsWordcodeError::processIndexOutOfRange;
    const std::array words{0x44U, opcode, 2U, 0x45U};
    require(inspectLegacyAfsWordcode(words, local ? 3 : 0, local ? 0 : 3)
                .structurallyValid(),
            "index checked against the wrong variable array");
    rejected(inspectLegacyAfsWordcode(words, 2, 2), error, 1);
    const std::array emptySlot{opcode, 0U, 0x44U};
    rejected(inspectLegacyAfsWordcode(emptySlot, 0, 0), error, 0);
  }
  const std::array words{0x1BU, 0U, 0x44U};
  require(inspectLegacyAfsWordcode(words, 0, 0, {3, 2, 0}).structurallyValid(),
          "exact code/instruction limits rejected");
  rejected(inspectLegacyAfsWordcode(words, 0, 0, {2, 2, 0}),
           LegacyAfsWordcodeError::wordLimit, 0);
  rejected(inspectLegacyAfsWordcode(words, 0, 0, {3, 1, 0}),
           LegacyAfsWordcodeError::instructionLimit, 2);
  rejected(inspectLegacyAfsWordcode(words, 0, 0, {3, 0, 0}),
           LegacyAfsWordcodeError::instructionLimit, 0);
  rejected(inspectLegacyAfsWordcode({}, 0, 0),
           LegacyAfsWordcodeError::emptyCode, 0);
  for (auto offset : {words.size(), std::numeric_limits<std::size_t>::max()})
    require(decodeLegacyAfsInstruction(words, offset).error ==
                LegacyAfsWordcodeError::offsetOutOfRange,
            "invalid start offset was added before checking");
  require(decodeLegacyAfsInstruction(words, 0, {2, 2, 0}).error ==
              LegacyAfsWordcodeError::wordLimit,
          "single decode bypassed word limit");
}

void testRequirementsRemainExplicit() {
  for (std::uint32_t opcode = 1; opcode <= 0x46; ++opcode) {
    const auto &gates = legacyAfsOpcodeInfo(opcode)->requirements;
    const bool numeric = (opcode >= 0x0D && opcode <= 0x10) ||
                         (opcode >= 0x28 && opcode <= 0x3F) || opcode == 0x41 ||
                         opcode == 0x43;
    const bool lifecycle = (opcode >= 0x25 && opcode <= 0x27) || opcode >= 0x43;
    const bool ownership =
        opcode == 0x05 || opcode == 0x06 ||
        (opcode >= 0x14 && opcode <= 0x1A) ||
        (opcode >= 0x21 && opcode <= 0x27) || opcode == 0x39 ||
        (opcode >= 0x3C && opcode <= 0x3F) || opcode == 0x42 || opcode == 0x46;
    require(gates ==
                LegacyAfsExecutionRequirements{ownership, numeric, lifecycle},
            "an unresolved execution gate was dropped");
  }
  // Framing deliberately does not claim stack/type safety: this underflow
  // requires rejection by a later typed verifier, NOT by inventing operands.
  const std::array stackUnderflow{0x01U, 0x44U};
  require(inspectLegacyAfsWordcode(stackUnderflow, 0, 0).structurallyValid(),
          "framing was confused with stack validation");
}

void testStackShapesMatchRecoveredWordCounts() {
  // Fixed pop/push pairs packed as hexadecimal nibbles, rows 01..46.
  // FE = null-dependent result; FF = descriptor-dependent call.
  constexpr std::array<unsigned, 70> expected{
      0x21, 0x21, 0x21, 0x11, 0x21, 0x21, 0x21, 0x21, // 01..08
      0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, // 09..10
      0x10, 0x10, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, // 11..18
      0x00, 0x01, 0x01, 0x10, 0x01, 0x10, 0x01, 0x10, // 19..20
      0x11, 0x20, 0x01, 0x01, 0xFF, 0xFF, 0xFF, 0x21, // 21..28
      0x21, 0x21, 0x21, 0x21, 0x11, 0x21, 0x21, 0x21, // 29..30
      0x21, 0x21, 0x11, 0x63, 0x63, 0x63, 0x63, 0x33, // 31..38
      0x21, 0x11, 0x11, 0xFE, 0x11, 0xFE, 0x11, 0x13, // 39..40
      0x13, 0x11, 0x10, 0x00, 0x00, 0x00,             // 41..46
  };
  for (std::uint32_t opcode = 1; opcode <= 0x46; ++opcode) {
    const auto &shape = legacyAfsOpcodeInfo(opcode)->stack;
    const auto pair = expected[opcode - 1];
    if (pair == 0xFF) {
      require(shape.kind == LegacyAfsStackShapeKind::descriptorDependent,
              "native call was given a guessed fixed stack delta");
    } else if (pair == 0xFE) {
      require(shape.kind == LegacyAfsStackShapeKind::nullableReferenceResult &&
                  shape.poppedWords == 1 && shape.minimumPushedWords == 0 &&
                  shape.maximumPushedWords == 1,
              "null text conversion incorrectly promises a result word");
    } else {
      require(shape.kind == LegacyAfsStackShapeKind::fixed &&
                  shape.poppedWords == (pair >> 4) &&
                  shape.minimumPushedWords == (pair & 0xF) &&
                  shape.maximumPushedWords == (pair & 0xF),
              "opcode stack word count differs from evidence");
    }
  }
}

void testAllocationFailureIsAtomicAndRetryable() {
  const std::array words{0x1BU, 1U, 0x11U, 0U, 0x43U, 0x46U, 0x44U};
  bool succeeded = false;
  long failures = 0;
  for (long after = 0; after < 32; ++after) {
    const auto beforeAllocations = liveAllocations;
    allocationsUntilFailure = after;
    const auto result = inspectLegacyAfsWordcode(words, 0, 0);
    allocationsUntilFailure = -1;
    if (result.structurallyValid()) {
      succeeded = true;
      break;
    }
    require(result.error == LegacyAfsWordcodeError::allocationFailure,
            "allocation exception escaped the fail-closed result");
    rejected(result, LegacyAfsWordcodeError::allocationFailure,
             result.errorWord);
    require(liveAllocations == beforeAllocations,
            "allocation failure leaked intermediate vector storage");
    ++failures;
  }
  require(succeeded && failures >= 3,
          "did not exercise both instruction and graph allocation failures");
  require(inspectLegacyAfsWordcode(words, 0, 0).structurallyValid(),
          "allocation failure contaminated later inspection");
}

void testSyntheticMutationInvariants() {
  std::uint32_t random = 0xA15F0046U;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  std::array<std::uint32_t, 48> words{};
  for (unsigned run = 0; run < 10000; ++run) {
    const auto count = next() % words.size() + 1;
    for (auto &word : words)
      word = next() % 0x48;
    words[count - 1] = 0x44;
    const auto before = words;
    const auto result = inspectLegacyAfsWordcode(
        std::span<const std::uint32_t>{words}.first(count), 4, 4, {48, 48, 64});
    require(words == before, "inspector modified input words");
    if (!result.structurallyValid()) {
      rejected(result, result.error, result.errorWord);
      continue;
    }
    std::size_t covered = 0;
    for (const auto &instruction : result.instructions) {
      require(instruction.wordOffset == covered && instruction.wordCount > 0 &&
                  instruction.wordCount <= count - covered,
              "accepted code contains an overlap, gap, or overflow");
      covered += instruction.wordCount;
    }
    require(covered == count, "accepted code omitted trailing words");
    for (const auto &edge : result.edges) {
      const auto startsAt = [&](std::size_t offset) {
        return std::any_of(result.instructions.begin(),
                           result.instructions.end(),
                           [&](const LegacyAfsInstruction &instruction) {
                             return instruction.wordOffset == offset;
                           });
      };
      require(startsAt(edge.fromWord) && startsAt(edge.toWord),
              "accepted graph points outside instruction boundaries");
    }
  }
}

} // namespace

int main() {
  testEveryOpcodeAndFraming();
  testInlineTextBoundariesAndBits();
  testGraphAndContinuation();
  testInvalidBranchTargets();
  testIndexAndResourceLimits();
  testRequirementsRemainExplicit();
  testStackShapesMatchRecoveredWordCounts();
  testAllocationFailureIsAtomicAndRetryable();
  testSyntheticMutationInvariants();
  std::cout
      << "Legacy AFS wordcode: 70 opcodes, bounds, CFG, allocation faults, "
         "and 10000 mutations passed\n";
  return EXIT_SUCCESS;
}
