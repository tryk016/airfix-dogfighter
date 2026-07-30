#include "airfix/script/LegacyAfsFunctionSchedule.hpp"

namespace airfix::script {

LegacyAfsFunctionSchedule legacyAfsBuildInitialFunctionSchedule(
    const std::span<const LegacyAfsFunctionKind> sourceDeclarations) {
  std::size_t autoexecCount = 0;
  for (const LegacyAfsFunctionKind kind : sourceDeclarations) {
    switch (legacyAfsFunctionActivation(kind)) {
    case LegacyAfsFunctionActivation::explicitCall:
      break;
    case LegacyAfsFunctionActivation::autoexec:
      ++autoexecCount;
      break;
    case LegacyAfsFunctionActivation::unsupported:
    default:
      return {
          .status = LegacyAfsFunctionScheduleStatus::unsupportedKind,
          .functions = {},
      };
    }
  }

  LegacyAfsFunctionSchedule result{
      .status = LegacyAfsFunctionScheduleStatus::ready,
      .functions = {},
  };
  result.functions.reserve(autoexecCount);

  for (std::size_t sourceIndex = sourceDeclarations.size(); sourceIndex > 0;
       --sourceIndex) {
    const LegacyAfsFunctionKind kind = sourceDeclarations[sourceIndex - 1];
    if (legacyAfsFunctionActivation(kind) ==
        LegacyAfsFunctionActivation::autoexec) {
      result.functions.push_back({
          .sourceIndex = sourceIndex - 1,
          .kind = kind,
      });
    }
  }

  return result;
}

} // namespace airfix::script
