#pragma once

#include <bit>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::uint32_t legacyAircraftPitchBankSetScaleBits =
    0x3D3020C5U;
inline constexpr float legacyAircraftPitchBankSetScale =
    std::bit_cast<float>(legacyAircraftPitchBankSetScaleBits);

enum class LegacyAircraftNativePitchBankEvent : std::uint8_t {
    pitchSet = 0x5F,
    bankSet = 0x65,
};

enum class LegacyAircraftPitchBankWriteField : std::uint8_t {
    pitch,
    bank,
};

// This is a compatibility policy supported by the original process startup
// state. It is not a claim that the live x87 control word has been observed
// while a native event is processed.
enum class LegacyAircraftPitchBankNumericPolicy : std::uint8_t {
    startupPc53RoundToNearestEven,
};

struct LegacyAircraftNativePitchBankEventInput final {
    LegacyAircraftNativePitchBankEvent event{
        LegacyAircraftNativePitchBankEvent::pitchSet};
    std::int32_t payload{};
    bool vehicleInactive{};
    LegacyAircraftPitchBankNumericPolicy numericPolicy{
        LegacyAircraftPitchBankNumericPolicy::
            startupPc53RoundToNearestEven};
};

struct LegacyAircraftPitchBankWrite final {
    LegacyAircraftPitchBankWriteField field{
        LegacyAircraftPitchBankWriteField::pitch};
    std::uint32_t valueBits{};
    bool clearRestDuration{};

    [[nodiscard]] constexpr float value() const noexcept {
        return std::bit_cast<float>(valueBits);
    }

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyAircraftPitchBankWrite&,
        const LegacyAircraftPitchBankWrite&) noexcept = default;
};

enum class LegacyAircraftPitchBankEventDecodeStatus : std::uint8_t {
    decoded,
    ignoredInactive,
    unsupportedEvent,
    unsupportedNumericPolicy,
};

struct LegacyAircraftPitchBankEventDecodeResult final {
    LegacyAircraftPitchBankEventDecodeStatus status{
        LegacyAircraftPitchBankEventDecodeStatus::unsupportedEvent};
    std::optional<LegacyAircraftPitchBankWrite> write;

    [[nodiscard]] constexpr bool decoded() const noexcept {
        return status ==
                LegacyAircraftPitchBankEventDecodeStatus::decoded &&
            write.has_value();
    }

    [[nodiscard]] constexpr bool ignored() const noexcept {
        return status ==
                LegacyAircraftPitchBankEventDecodeStatus::
                    ignoredInactive &&
            !write.has_value();
    }

    [[nodiscard]] constexpr bool failed() const noexcept {
        return status ==
                LegacyAircraftPitchBankEventDecodeStatus::
                    unsupportedEvent ||
            status ==
                LegacyAircraftPitchBankEventDecodeStatus::
                    unsupportedNumericPolicy;
    }
};

// Decodes one already-ordered native AirCraft PITCH_SET or BANK_SET event.
// The payload is the complete signed int32 stored at event offset +0x11.
// An active event produces exactly one caller-owned field write. A nonzero
// payload also asks the future state owner to clear the shared 64-bit rest
// duration atomically with that write.
//
// The supported numeric policy models the startup-compatible x87 PC=53,
// RC=nearest-even path exactly and returns the final binary32 bits. It does
// not depend on the host floating-point rounding mode. It must not be called
// proven live full-domain parity until the native control word is captured
// at event processing.
//
// This allocation-free boundary owns no producer, Q15 conversion, input
// snapshot, event queue, scheduler, nominal 12-ms timing, state, or renderer
// publication.
[[nodiscard]] LegacyAircraftPitchBankEventDecodeResult
legacyAircraftDecodeNativePitchBankEvent(
    LegacyAircraftNativePitchBankEventInput input) noexcept;

} // namespace airfix::simulation
