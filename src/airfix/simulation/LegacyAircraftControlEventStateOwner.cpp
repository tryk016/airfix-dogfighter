#include "airfix/simulation/LegacyAircraftControlEventStateOwner.hpp"

#include <cmath>

namespace airfix::simulation {
namespace {

void clearRestIfRequested(LegacyAircraftControlEventState& state,
                          const bool clearRestDuration) noexcept {
    if (clearRestDuration) {
        state.restDurationMilliseconds = 0;
    }
}

} // namespace

LegacyAircraftControlEventCommitStatus
LegacyAircraftControlEventStateOwner::tryApply(
    const LegacyAircraftThrustWrite& write) noexcept {
    switch (write.field) {
    case LegacyAircraftThrustWriteField::targetThrust:
    case LegacyAircraftThrustWriteField::thrustApply:
        break;
    default:
        return LegacyAircraftControlEventCommitStatus::unsupportedField;
    }

    if (!std::isfinite(write.value)) {
        return LegacyAircraftControlEventCommitStatus::nonFiniteValue;
    }

    LegacyAircraftControlEventState next = state_;
    switch (write.field) {
    case LegacyAircraftThrustWriteField::targetThrust:
        next.thrustControl.targetThrust = write.value;
        break;
    case LegacyAircraftThrustWriteField::thrustApply:
        next.thrustControl.thrustApply = write.value;
        break;
    default:
        return LegacyAircraftControlEventCommitStatus::unsupportedField;
    }
    clearRestIfRequested(next, write.clearRestDuration);
    state_ = next;
    return LegacyAircraftControlEventCommitStatus::committed;
}

LegacyAircraftControlEventCommitStatus
LegacyAircraftControlEventStateOwner::tryApply(
    const LegacyAircraftPitchBankWrite& write) noexcept {
    switch (write.field) {
    case LegacyAircraftPitchBankWriteField::pitch:
    case LegacyAircraftPitchBankWriteField::bank:
        break;
    default:
        return LegacyAircraftControlEventCommitStatus::unsupportedField;
    }

    if (!std::isfinite(write.value())) {
        return LegacyAircraftControlEventCommitStatus::nonFiniteValue;
    }

    LegacyAircraftControlEventState next = state_;
    switch (write.field) {
    case LegacyAircraftPitchBankWriteField::pitch:
        next.pitchBits = write.valueBits;
        break;
    case LegacyAircraftPitchBankWriteField::bank:
        next.bankBits = write.valueBits;
        break;
    default:
        return LegacyAircraftControlEventCommitStatus::unsupportedField;
    }
    clearRestIfRequested(next, write.clearRestDuration);
    state_ = next;
    return LegacyAircraftControlEventCommitStatus::committed;
}

LegacyAircraftControlEventCommitStatus
LegacyAircraftControlEventStateOwner::tryApply(
    const LegacyAircraftTurnWrite& write) noexcept {
    if (!std::isfinite(write.value())) {
        return LegacyAircraftControlEventCommitStatus::nonFiniteValue;
    }

    LegacyAircraftControlEventState next = state_;
    next.turnBits = write.valueBits;
    clearRestIfRequested(next, write.clearRestDuration);
    state_ = next;
    return LegacyAircraftControlEventCommitStatus::committed;
}

} // namespace airfix::simulation
