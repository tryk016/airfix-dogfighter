#include "airfix/simulation/LegacyAircraftThrustEventReducer.hpp"

namespace airfix::simulation {
namespace {

[[nodiscard]] float decodeSet(const std::int32_t payload) noexcept {
    return static_cast<float>(
        static_cast<double>(payload) *
        static_cast<double>(legacyAircraftNativeControlPayloadScale));
}

[[nodiscard]] float decodeApply(const std::int32_t payload) noexcept {
    const double scaledApply =
        static_cast<double>(payload) *
        static_cast<double>(legacyAircraftNativeThrustApplyScale);
    return static_cast<float>(
        scaledApply *
        static_cast<double>(legacyAircraftNativeControlPayloadScale));
}

} // namespace

LegacyAircraftThrustEventDecodeResult
legacyAircraftDecodeNativeThrustEvent(
    const LegacyAircraftNativeThrustEventInput input) noexcept {
    LegacyAircraftThrustWriteField field{};
    switch (input.event) {
    case LegacyAircraftNativeThrustEvent::thrustSet:
        field = LegacyAircraftThrustWriteField::targetThrust;
        break;
    case LegacyAircraftNativeThrustEvent::thrustApply:
        field = LegacyAircraftThrustWriteField::thrustApply;
        break;
    default:
        return {
            .status =
                LegacyAircraftThrustEventDecodeStatus::unsupportedEvent,
            .write = std::nullopt,
        };
    }

    if (input.vehicleInactive) {
        return {
            .status =
                LegacyAircraftThrustEventDecodeStatus::ignoredInactive,
            .write = std::nullopt,
        };
    }

    if (input.payload < legacyAircraftNativeControlPayloadMinimum ||
        input.payload > legacyAircraftNativeControlPayloadMaximum) {
        return {
            .status =
                LegacyAircraftThrustEventDecodeStatus::
                    payloadOutsideEvidenceRange,
            .write = std::nullopt,
        };
    }

    const float value =
        input.event == LegacyAircraftNativeThrustEvent::thrustSet
        ? decodeSet(input.payload)
        : decodeApply(input.payload);
    return {
        .status = LegacyAircraftThrustEventDecodeStatus::decoded,
        .write = LegacyAircraftThrustWrite{
            .field = field,
            .value = value,
            .clearRestDuration = value != 0.0F,
        },
    };
}

} // namespace airfix::simulation
