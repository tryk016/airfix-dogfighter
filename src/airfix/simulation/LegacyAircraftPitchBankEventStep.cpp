#include "airfix/simulation/LegacyAircraftPitchBankEventStep.hpp"

namespace airfix::simulation {

LegacyAircraftPitchBankEventStepResult legacyAircraftAdvancePitchBankEventStep(
    const LegacyAircraftControlEventState state,
    const LegacyAircraftNativePitchBankEventInput input) noexcept {
  const auto decoded = legacyAircraftDecodeNativePitchBankEvent(input);
  if (decoded.ignored()) {
    return {
        LegacyAircraftPitchBankEventStepStatus::ignoredInactive,
        state,
        std::nullopt,
    };
  }
  if (!decoded.decoded()) {
    return {
        LegacyAircraftPitchBankEventStepStatus::decodeRejected,
        state,
        std::nullopt,
    };
  }

  LegacyAircraftControlEventStateOwner owner{state};
  if (owner.tryApply(*decoded.write) !=
      LegacyAircraftControlEventCommitStatus::committed) {
    return {
        LegacyAircraftPitchBankEventStepStatus::commitRejected,
        state,
        std::nullopt,
    };
  }

  return {
      LegacyAircraftPitchBankEventStepStatus::committed,
      owner.snapshot(),
      decoded.write,
  };
}

} // namespace airfix::simulation
