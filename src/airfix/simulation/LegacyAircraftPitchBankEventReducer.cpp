#include "airfix/simulation/LegacyAircraftPitchBankEventReducer.hpp"

#include <bit>
#include <cstdint>

namespace airfix::simulation {
namespace {

constexpr std::uint64_t scaleSignificand = 11'542'725U;
constexpr std::int32_t scaleExponent2 = -28;

struct RoundedUnsigned final {
    std::uint64_t significand{};
    std::int32_t exponent2{};
};

[[nodiscard]] constexpr std::uint64_t roundRightToNearestEven(
    const std::uint64_t value,
    const std::uint32_t shift) noexcept {
    if (shift == 0U) {
        return value;
    }

    const std::uint64_t rounded = value >> shift;
    const std::uint64_t remainderMask =
        (std::uint64_t{1} << shift) - 1U;
    const std::uint64_t remainder = value & remainderMask;
    const std::uint64_t halfway =
        std::uint64_t{1} << (shift - 1U);
    return rounded +
        static_cast<std::uint64_t>(
            remainder > halfway ||
            (remainder == halfway && (rounded & 1U) != 0U));
}

[[nodiscard]] constexpr RoundedUnsigned roundToPrecision(
    const std::uint64_t significand,
    const std::int32_t exponent2,
    const std::uint32_t precisionBits) noexcept {
    const auto highestBit =
        static_cast<std::uint32_t>(
            std::bit_width(significand)) -
        1U;
    if (highestBit < precisionBits) {
        return {
            .significand = significand,
            .exponent2 = exponent2,
        };
    }

    const std::uint32_t shift =
        highestBit - (precisionBits - 1U);
    std::uint64_t rounded =
        roundRightToNearestEven(significand, shift);
    std::int32_t roundedExponent2 =
        exponent2 + static_cast<std::int32_t>(shift);
    if (static_cast<std::uint32_t>(std::bit_width(rounded)) >
        precisionBits) {
        rounded >>= 1U;
        ++roundedExponent2;
    }
    return {
        .significand = rounded,
        .exponent2 = roundedExponent2,
    };
}

[[nodiscard]] constexpr std::uint32_t
startupPc53RoundToNearestEvenBits(
    const std::int32_t payload) noexcept {
    if (payload == 0) {
        return 0U;
    }

    const std::int64_t signedPayload = payload;
    const std::uint64_t magnitude = static_cast<std::uint64_t>(
        signedPayload < 0 ? -signedPayload : signedPayload);
    const std::uint64_t exactProduct =
        magnitude * scaleSignificand;

    const RoundedUnsigned pc53 = roundToPrecision(
        exactProduct, scaleExponent2, 53U);
    const RoundedUnsigned binary32 =
        roundToPrecision(pc53.significand, pc53.exponent2, 24U);

    const auto highestBit =
        static_cast<std::int32_t>(
            std::bit_width(binary32.significand)) -
        1;
    const std::int32_t unbiasedExponent =
        highestBit + binary32.exponent2;
    const auto exponentBits = static_cast<std::uint32_t>(
        unbiasedExponent + 127);
    const std::uint32_t fractionBits =
        static_cast<std::uint32_t>(binary32.significand) &
        0x007FFFFFU;
    const std::uint32_t signBits =
        payload < 0 ? 0x80000000U : 0U;
    return signBits | (exponentBits << 23U) | fractionBits;
}

} // namespace

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
                startupPc53RoundToNearestEvenBits(input.payload),
            .clearRestDuration = input.payload != 0,
        },
    };
}

} // namespace airfix::simulation
