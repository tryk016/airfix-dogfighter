#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace airfix::script {

enum class LegacyAfsOperand : std::uint8_t {
  none,
  branchWordIndex,
  localWordIndex,
  processWordIndex,
  objectFieldIndex,
  inlineTextWordCount,
  immediateBits,
  nativeDescriptor,
  handlerId,
};

enum class LegacyAfsFlow : std::uint8_t {
  sequential,
  branchIfNonzero,
  branchIfZero,
  branch,
  yield,
  terminal,
};

// These are unresolved execution requirements, not permission to execute.
// They correspond to gates T, N, and L in EV-20260803-003. Even an instruction
// with no flags still needs stack/type validation before execution.
struct LegacyAfsExecutionRequirements final {
  bool typedOwnership{};
  bool numericPolicy{};
  bool lifecyclePolicy{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAfsExecutionRequirements &,
             const LegacyAfsExecutionRequirements &) noexcept = default;
};

enum class LegacyAfsStackShapeKind : std::uint8_t {
  fixed,
  nullableReferenceResult,
  descriptorDependent,
};

// Word counts, not source-level value counts (a vector occupies three words).
// For descriptorDependent, counts are deliberately unspecified; the eventual
// typed linker must obtain them from a checked call signature. For nullable
// text conversion the result count is 0 on null, 1 otherwise, not always 1.
struct LegacyAfsStackShape final {
  LegacyAfsStackShapeKind kind{LegacyAfsStackShapeKind::fixed};
  std::uint8_t poppedWords{};
  std::uint8_t minimumPushedWords{};
  std::uint8_t maximumPushedWords{};
};

struct LegacyAfsOpcodeInfo final {
  LegacyAfsOperand operand{LegacyAfsOperand::none};
  LegacyAfsFlow flow{LegacyAfsFlow::sequential};
  LegacyAfsExecutionRequirements requirements{};
  LegacyAfsStackShape stack{};
};

// Returns null for every raw opcode outside 0x01..0x46, without narrowing it.
[[nodiscard]] const LegacyAfsOpcodeInfo *
legacyAfsOpcodeInfo(std::uint32_t opcode) noexcept;

// Safe-port budgets, not recovered original limits. Counts refer to 32-bit
// words, except maxInlineTextBytes, which includes NUL and payload padding.
struct LegacyAfsWordcodeLimits final {
  std::size_t maxWords{262144};
  std::size_t maxInstructions{65536};
  std::size_t maxInlineTextBytes{16384};
};

enum class LegacyAfsWordcodeError : std::uint8_t {
  none,
  emptyCode,
  wordLimit,
  instructionLimit,
  offsetOutOfRange,
  unsupportedOpcode,
  missingOperand,
  truncatedInlineText,
  inlineTextLimit,
  missingTextTerminator,
  localIndexOutOfRange,
  processIndexOutOfRange,
  branchOutOfRange,
  branchNotInstruction,
  fallthroughPastEnd,
  allocationFailure,
};

struct LegacyAfsInstruction final {
  std::size_t wordOffset{};
  std::size_t wordCount{};
  std::uint32_t opcode{};
  // Retained as bits only. A nativeDescriptor is NEVER a host pointer here.
  std::uint32_t operandBits{};
  LegacyAfsOpcodeInfo info{};
  // Only meaningful for 0x1A: excludes NUL and padding; no text is copied.
  std::size_t inlineTextByteCount{};
};

struct LegacyAfsInstructionDecode final {
  LegacyAfsWordcodeError error{LegacyAfsWordcodeError::offsetOutOfRange};
  LegacyAfsInstruction instruction{};

  [[nodiscard]] bool decoded() const noexcept {
    return error == LegacyAfsWordcodeError::none;
  }
};

// Allocation-free framing of one instruction, including bounded inline text.
// The supplied offset is NOT proven to be an instruction boundary by this
// operation. Use whole-function inspection to validate branches/boundaries.
[[nodiscard]] LegacyAfsInstructionDecode
decodeLegacyAfsInstruction(std::span<const std::uint32_t> words,
                           std::size_t wordOffset,
                           const LegacyAfsWordcodeLimits &limits = {}) noexcept;

enum class LegacyAfsEdgeKind : std::uint8_t {
  sequential,
  conditionNonzero,
  conditionZero,
  branch,
  resumeAfterYield,
};

struct LegacyAfsControlFlowEdge final {
  std::size_t fromWord{};
  std::size_t toWord{};
  LegacyAfsEdgeKind kind{LegacyAfsEdgeKind::sequential};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAfsControlFlowEdge &,
             const LegacyAfsControlFlowEdge &) noexcept = default;
};

struct LegacyAfsWordcodeInspection final {
  LegacyAfsWordcodeError error{LegacyAfsWordcodeError::emptyCode};
  // Structural errors identify the instruction owning the bad operand/edge.
  std::size_t errorWord{};
  std::vector<LegacyAfsInstruction> instructions{};
  std::vector<LegacyAfsControlFlowEdge> edges{};
  LegacyAfsExecutionRequirements requirements{};

  [[nodiscard]] bool structurallyValid() const noexcept {
    return error == LegacyAfsWordcodeError::none;
  }
};

// Inspects a complete internal word sequence, NOT an AFS source file or a
// portable serialized executable. All words, including unreachable tails,
// must frame valid instructions. Absolute branch destinations must name an
// instruction, never an operand/payload or one-past-end. Yield has a resumption
// edge; 0x46 continues; only 0x44/0x45 terminate the local control-flow graph.
// Index counts are supplied by the eventual compiled record, never inferred.
// Rejects atomically, including allocation failures: no partial graph escapes.
// Does NOT validate stack/types, object layouts, descriptors, runtime budgets,
// numerical behavior or lifecycle, and does not execute anything. Returned
// metadata is an inspection snapshot, not a capability to run mutable words.
[[nodiscard]] LegacyAfsWordcodeInspection
inspectLegacyAfsWordcode(std::span<const std::uint32_t> words,
                         std::size_t localWordCount,
                         std::size_t processWordCount,
                         const LegacyAfsWordcodeLimits &limits = {}) noexcept;

} // namespace airfix::script
