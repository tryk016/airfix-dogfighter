#include "airfix/simulation/LegacyAircraftTurnEventReducer.hpp"

#include "airfix/simulation/detail/LegacyAircraftAngularSetDecode.hpp"

namespace airfix::simulation {

LegacyAircraftTurnEventDecodeResult
legacyAircraftDecodeNativeTurnEvent(
    const LegacyAircraftNativeTurnEventInput input) noexcept {
    if (input.event != LegacyAircraftNativeTurnEvent::turnSet) {
        return {
            .status =
                LegacyAircraftTurnEventDecodeStatus::
                    unsupportedEvent,
            .write = std::nullopt,
        };
    }

    if (input.vehicleInactive) {
        return {
            .status =
                LegacyAircraftTurnEventDecodeStatus::
                    ignoredInactive,
            .write = std::nullopt,
        };
    }

    if (input.numericPolicy !=
        LegacyAircraftAngularSetNumericPolicy::
            startupPc53RoundToNearestEven) {
        return {
            .status =
                LegacyAircraftTurnEventDecodeStatus::
                    unsupportedNumericPolicy,
            .write = std::nullopt,
        };
    }

    return {
        .status = LegacyAircraftTurnEventDecodeStatus::decoded,
        .write = LegacyAircraftTurnWrite{
            .valueBits =
                detail::legacyAircraftDecodeAngularSetBits(
                    input.payload),
            .clearRestDuration = input.payload != 0,
        },
    };
}

} // namespace airfix::simulation
