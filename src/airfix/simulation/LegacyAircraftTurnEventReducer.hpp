#pragma once

#include "airfix/simulation/LegacyAircraftAngularSetNumericPolicy.hpp"

#include <bit>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

enum class LegacyAircraftNativeTurnEvent : std::uint8_t {
    turnSet = 0x5D,
};

struct LegacyAircraftNativeTurnEventInput final {
    LegacyAircraftNativeTurnEvent event{
        LegacyAircraftNativeTurnEvent::turnSet};
    std::int32_t payload{};
    bool vehicleInactive{};
    LegacyAircraftAngularSetNumericPolicy numericPolicy{
        LegacyAircraftAngularSetNumericPolicy::
            startupPc53RoundToNearestEven};
};

struct LegacyAircraftTurnWrite final {
    std::uint32_t valueBits{};
    bool clearRestDuration{};

    [[nodiscard]] constexpr float value() const noexcept {
        return std::bit_cast<float>(valueBits);
    }

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyAircraftTurnWrite&,
        const LegacyAircraftTurnWrite&) noexcept = default;
};

enum class LegacyAircraftTurnEventDecodeStatus : std::uint8_t {
    decoded,
    ignoredInactive,
    unsupportedEvent,
    unsupportedNumericPolicy,
};

struct LegacyAircraftTurnEventDecodeResult final {
    LegacyAircraftTurnEventDecodeStatus status{
        LegacyAircraftTurnEventDecodeStatus::unsupportedEvent};
    std::optional<LegacyAircraftTurnWrite> write;

    [[nodiscard]] constexpr bool decoded() const noexcept {
        return status ==
                LegacyAircraftTurnEventDecodeStatus::decoded &&
            write.has_value();
    }

    [[nodiscard]] constexpr bool ignored() const noexcept {
        return status ==
                LegacyAircraftTurnEventDecodeStatus::
                    ignoredInactive &&
            !write.has_value();
    }

    [[nodiscard]] constexpr bool failed() const noexcept {
        return status ==
                LegacyAircraftTurnEventDecodeStatus::
                    unsupportedEvent ||
            status ==
                LegacyAircraftTurnEventDecodeStatus::
                    unsupportedNumericPolicy;
    }
};

// Decodes one already-ordered native AirCraft TURN_SET event. The complete
// signed int32 payload at native event offset +0x11 uses the same exact
// FILD -> FMUL -> FST compatibility policy as PITCH_SET and BANK_SET.
// An active event writes the control corresponding to AfVehicle +0x450 and
// requests the shared rest-duration clear exactly when its payload is
// nonzero.
//
// The native event name and field lifecycle are confirmed. A downstream
// AirCraft force-law consumer has not yet been established, so this API does
// not rename the control to yaw or assign it physical units. It owns no
// producer, Q15 conversion, event queue, scheduler, nominal 12-ms timing,
// state, physics, or renderer publication.
[[nodiscard]] LegacyAircraftTurnEventDecodeResult
legacyAircraftDecodeNativeTurnEvent(
    LegacyAircraftNativeTurnEventInput input) noexcept;

} // namespace airfix::simulation
