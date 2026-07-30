#pragma once

#include <bit>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::int32_t legacyAircraftNativeControlPayloadMinimum =
    -255;
inline constexpr std::int32_t legacyAircraftNativeControlPayloadMaximum =
    255;
inline constexpr float legacyAircraftNativeControlPayloadScale =
    std::bit_cast<float>(0x3B808081U);
inline constexpr float legacyAircraftNativeThrustApplyScale =
    std::bit_cast<float>(0x3CA3D70AU);

enum class LegacyAircraftNativeThrustEvent : std::uint8_t {
    thrustSet = 0x63,
    thrustApply = 0x64,
};

enum class LegacyAircraftThrustWriteField : std::uint8_t {
    targetThrust,
    thrustApply,
};

struct LegacyAircraftNativeThrustEventInput final {
    LegacyAircraftNativeThrustEvent event{
        LegacyAircraftNativeThrustEvent::thrustSet};
    std::int32_t payload{};
    bool vehicleInactive{};
};

struct LegacyAircraftThrustWrite final {
    LegacyAircraftThrustWriteField field{
        LegacyAircraftThrustWriteField::targetThrust};
    float value{};
    bool clearRestDuration{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyAircraftThrustWrite&,
        const LegacyAircraftThrustWrite&) noexcept = default;
};

enum class LegacyAircraftThrustEventDecodeStatus : std::uint8_t {
    decoded,
    ignoredInactive,
    unsupportedEvent,
    payloadOutsideEvidenceRange,
};

struct LegacyAircraftThrustEventDecodeResult final {
    LegacyAircraftThrustEventDecodeStatus status{
        LegacyAircraftThrustEventDecodeStatus::unsupportedEvent};
    std::optional<LegacyAircraftThrustWrite> write;

    [[nodiscard]] constexpr bool decoded() const noexcept {
        return status ==
                LegacyAircraftThrustEventDecodeStatus::decoded &&
            write.has_value();
    }

    [[nodiscard]] constexpr bool ignored() const noexcept {
        return status ==
                LegacyAircraftThrustEventDecodeStatus::ignoredInactive &&
            !write.has_value();
    }

    [[nodiscard]] constexpr bool failed() const noexcept {
        return status ==
                LegacyAircraftThrustEventDecodeStatus::unsupportedEvent ||
            status ==
                LegacyAircraftThrustEventDecodeStatus::
                    payloadOutsideEvidenceRange;
    }
};

// Decodes one already-formed native AirCraft THRUST_SET or THRUST_APPLY
// event into a single caller-owned field write. The payload is the signed
// int32 stored at native event offset +0x11. Recovered command and AI
// producers establish [-255, 255]; active values outside that conservative
// evidence range fail closed while the analog producer's upstream raw-axis
// domain remains unproven.
//
// A recognized event is gated by the native vehicle-inactive latch before
// its payload is inspected. Inactive input is therefore an accepted no-op,
// including when its payload lies outside the recovered producer range.
// Non-zero decoded writes request that the separate control-event state owner
// clear the 64-bit rest duration transactionally; this function owns neither
// that duration nor the thrust state.
//
// This allocation-free boundary intentionally owns no Q15 conversion,
// input device, scheduler, event queue, 12-ms timing, slot-45 force step,
// health state, pose, or renderer publication.
[[nodiscard]] LegacyAircraftThrustEventDecodeResult
legacyAircraftDecodeNativeThrustEvent(
    LegacyAircraftNativeThrustEventInput input) noexcept;

} // namespace airfix::simulation
