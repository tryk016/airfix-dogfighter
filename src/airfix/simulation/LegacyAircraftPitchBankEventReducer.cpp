#include "airfix/simulation/LegacyAircraftPitchBankEventReducer.hpp"

#include "airfix/simulation/detail/LegacyAircraftAngularSetDecode.hpp"

namespace airfix::simulation {

LegacyAircraftPitchBankEventDecodeResult
legacyAircraftDecodeNativePitchBankEvent(
    const LegacyAircraftNativePitchBankEventInput input) noexcept {
    LegacyAircraftPitchBankWriteField field{};
    switch (input.event) {
    case LegacyAircraftNativePitchBankEvent::pitchSet:
        field = LegacyAircraftPitchBankWriteField::pitch;
        break;
    case LegacyAircraftNativePitchBankEvent::bankSet:
        field = LegacyAircraftPitchBankWriteField::bank;
        break;
    default:
        return {
            .status =
                LegacyAircraftPitchBankEventDecodeStatus::
                    unsupportedEvent,
            .write = std::nullopt,
        };
    }

    if (input.vehicleInactive) {
        return {
            .status =
                LegacyAircraftPitchBankEventDecodeStatus::
                    ignoredInactive,
            .write = std::nullopt,
        };
    }

    if (input.numericPolicy !=
        LegacyAircraftPitchBankNumericPolicy::
            startupPc53RoundToNearestEven) {
        return {
            .status =
                LegacyAircraftPitchBankEventDecodeStatus::
                    unsupportedNumericPolicy,
            .write = std::nullopt,
        };
    }

    return {
        .status =
            LegacyAircraftPitchBankEventDecodeStatus::decoded,
        .write = LegacyAircraftPitchBankWrite{
            .field = field,
            .valueBits =
                detail::legacyAircraftDecodeAngularSetBits(
                    input.payload),
            .clearRestDuration = input.payload != 0,
        },
    };
}

} // namespace airfix::simulation
