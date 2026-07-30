#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace airfix::script {

enum class LegacyAfsFunctionKind : std::uint8_t {
  event,
  action,
  timer,
};

enum class LegacyAfsFunctionActivation : std::uint8_t {
  explicitCall,
  autoexec,
  unsupported,
};

// Reconstructs the native function-record Autoexec policy. Events retain the
// constructor's disabled flag and are selected by an explicit named call.
// Actions and timers set the flag and are instantiated with a new process.
[[nodiscard]] constexpr LegacyAfsFunctionActivation
legacyAfsFunctionActivation(const LegacyAfsFunctionKind kind) noexcept {
  switch (kind) {
  case LegacyAfsFunctionKind::event:
    return LegacyAfsFunctionActivation::explicitCall;
  case LegacyAfsFunctionKind::action:
  case LegacyAfsFunctionKind::timer:
    return LegacyAfsFunctionActivation::autoexec;
  default:
    return LegacyAfsFunctionActivation::unsupported;
  }
}

struct LegacyAfsScheduledFunction final {
  std::size_t sourceIndex{};
  LegacyAfsFunctionKind kind{LegacyAfsFunctionKind::event};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAfsScheduledFunction &,
             const LegacyAfsScheduledFunction &) noexcept = default;
};

enum class LegacyAfsFunctionScheduleStatus : std::uint8_t {
  ready,
  unsupportedKind,
};

struct LegacyAfsFunctionSchedule final {
  LegacyAfsFunctionScheduleStatus status{
      LegacyAfsFunctionScheduleStatus::unsupportedKind};
  std::vector<LegacyAfsScheduledFunction> functions{};

  [[nodiscard]] bool ready() const noexcept {
    return status == LegacyAfsFunctionScheduleStatus::ready;
  }
};

// Accepts function declarations in source order and reconstructs the initial
// execution order for one native AFS process. The compiler prepends every
// compiled record, while the process constructor walks that reversed list and
// appends only Autoexec records to its FIFO execution list. Events are omitted;
// actions and timers are therefore returned in reverse source order.
//
// A forged function kind rejects the complete input without partial output.
// This boundary does not parse source, compile bytecode, execute a script,
// resolve event names, own a process list, or schedule mission refreshes.
[[nodiscard]] LegacyAfsFunctionSchedule legacyAfsBuildInitialFunctionSchedule(
    std::span<const LegacyAfsFunctionKind> sourceDeclarations);

} // namespace airfix::script
