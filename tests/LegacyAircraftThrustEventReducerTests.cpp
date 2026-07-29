#include "airfix/simulation/LegacyAircraftThrustEventReducer.hpp"
#include "airfix/simulation/LegacyAircraftThrustState.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(
    static_cast<std::uint8_t>(
        LegacyAircraftNativeThrustEvent::thrustSet) == 0x63U);
static_assert(
    static_cast<std::uint8_t>(
        LegacyAircraftNativeThrustEvent::thrustApply) == 0x64U);
static_assert(
    std::bit_cast<std::uint32_t>(
        legacyAircraftNativeControlPayloadScale) == 0x3B808081U);
static_assert(
    std::bit_cast<std::uint32_t>(
        legacyAircraftNativeThrustApplyScale) == 0x3CA3D70AU);
static_assert(
    noexcept(legacyAircraftDecodeNativeThrustEvent({})));
static_assert(
    std::is_trivially_copyable_v<
        LegacyAircraftThrustEventDecodeResult>);

[[noreturn]] void fail(const char* const message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* const message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] std::uint32_t bits(const float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::uint64_t significand(
    const std::uint32_t valueBits) noexcept {
    return static_cast<std::uint64_t>(
        (valueBits & 0x007FFFFFU) | 0x00800000U);
}

[[nodiscard]] constexpr std::int32_t exponent2(
    const std::uint32_t valueBits) noexcept {
    return static_cast<std::int32_t>((valueBits >> 23U) & 0xFFU) -
        127 - 23;
}

[[nodiscard]] std::uint32_t roundExactPositiveProductToBinary32(
    const std::uint64_t exactSignificand,
    const std::int32_t productExponent2) {
    if (exactSignificand == 0U) {
        return 0U;
    }

    const auto highestBit =
        static_cast<std::uint32_t>(
            std::bit_width(exactSignificand) - 1U);
    std::int32_t unbiasedExponent =
        static_cast<std::int32_t>(highestBit) + productExponent2;

    std::uint64_t roundedSignificand = exactSignificand;
    if (highestBit > 23U) {
        const std::uint32_t shift = highestBit - 23U;
        roundedSignificand = exactSignificand >> shift;
        const std::uint64_t remainderMask =
            (std::uint64_t{1} << shift) - 1U;
        const std::uint64_t remainder =
            exactSignificand & remainderMask;
        const std::uint64_t halfway =
            std::uint64_t{1} << (shift - 1U);
        if (remainder > halfway ||
            (remainder == halfway &&
             (roundedSignificand & 1U) != 0U)) {
            ++roundedSignificand;
        }
        if (roundedSignificand == (std::uint64_t{1} << 24U)) {
            roundedSignificand >>= 1U;
            ++unbiasedExponent;
        }
    } else if (highestBit < 23U) {
        roundedSignificand <<= 23U - highestBit;
    }

    require(
        unbiasedExponent >= -126 && unbiasedExponent <= 127,
        "exact oracle produced non-normal binary32");
    const auto exponentBits = static_cast<std::uint32_t>(
        unbiasedExponent + 127);
    return (exponentBits << 23U) |
        (static_cast<std::uint32_t>(roundedSignificand) &
         0x007FFFFFU);
}

[[nodiscard]] std::uint32_t exactReferenceBits(
    const LegacyAircraftNativeThrustEvent event,
    const std::int32_t payload) {
    const auto signedPayload = static_cast<std::int64_t>(payload);
    const std::uint64_t magnitude = static_cast<std::uint64_t>(
        signedPayload < 0 ? -signedPayload : signedPayload);

    constexpr std::uint32_t payloadScaleBits = 0x3B808081U;
    constexpr std::uint32_t applyScaleBits = 0x3CA3D70AU;
    std::uint64_t exactProduct =
        magnitude * significand(payloadScaleBits);
    std::int32_t productExponent2 = exponent2(payloadScaleBits);
    if (event == LegacyAircraftNativeThrustEvent::thrustApply) {
        exactProduct *= significand(applyScaleBits);
        productExponent2 += exponent2(applyScaleBits);
    }

    const std::uint32_t positiveBits =
        roundExactPositiveProductToBinary32(
            exactProduct, productExponent2);
    return payload < 0 ? positiveBits | 0x80000000U : positiveBits;
}

[[nodiscard]] LegacyAircraftThrustEventDecodeResult decode(
    const LegacyAircraftNativeThrustEvent event,
    const std::int32_t payload,
    const bool inactive = false) noexcept {
    return legacyAircraftDecodeNativeThrustEvent({
        .event = event,
        .payload = payload,
        .vehicleInactive = inactive,
    });
}

void requireDecoded(
    const LegacyAircraftThrustEventDecodeResult& result,
    const LegacyAircraftThrustWriteField field,
    const std::uint32_t expectedBits,
    const bool clearRestDuration,
    const char* const message) {
    require(result.decoded(), message);
    require(result.write->field == field, message);
    require(bits(result.write->value) == expectedBits, message);
    require(
        result.write->clearRestDuration == clearRestDuration,
        message);
}

struct NativeVector final {
    std::int32_t payload;
    std::uint32_t setBits;
    std::uint32_t applyBits;
};

constexpr std::array<NativeVector, 9> nativeVectors{{
    {-255, 0xBF800000U, 0xBCA3D70BU},
    {-249, 0xBF79F9FBU, 0xBC9FFC25U},
    {-127, 0xBEFEFF00U, 0xBC23328FU},
    {-1, 0xBB808081U, 0xB8A47B86U},
    {0, 0x00000000U, 0x00000000U},
    {1, 0x3B808081U, 0x38A47B86U},
    {127, 0x3EFEFF00U, 0x3C23328FU},
    {249, 0x3F79F9FBU, 0x3C9FFC25U},
    {255, 0x3F800000U, 0x3CA3D70BU},
}};

void testRecoveredBinary32Vectors() {
    for (const NativeVector& vector : nativeVectors) {
        const bool clear = vector.payload != 0;
        requireDecoded(
            decode(
                LegacyAircraftNativeThrustEvent::thrustSet,
                vector.payload),
            LegacyAircraftThrustWriteField::targetThrust,
            vector.setBits,
            clear,
            "THRUST_SET binary32 vector changed");
        requireDecoded(
            decode(
                LegacyAircraftNativeThrustEvent::thrustApply,
                vector.payload),
            LegacyAircraftThrustWriteField::thrustApply,
            vector.applyBits,
            clear,
            "THRUST_APPLY binary32 vector changed");
    }
}

void testExhaustiveAdmittedPayloadRange() {
    std::uint32_t previousSet = 0U;
    std::uint32_t previousApply = 0U;
    for (std::int32_t payload = 0;
         payload <= legacyAircraftNativeControlPayloadMaximum;
         ++payload) {
        const auto set = decode(
            LegacyAircraftNativeThrustEvent::thrustSet,
            payload);
        const auto apply = decode(
            LegacyAircraftNativeThrustEvent::thrustApply,
            payload);
        require(set.decoded() && apply.decoded(), "range decode failed");
        require(
            set.write->field ==
                LegacyAircraftThrustWriteField::targetThrust,
            "SET selected wrong field");
        require(
            apply.write->field ==
                LegacyAircraftThrustWriteField::thrustApply,
            "APPLY selected wrong field");
        require(
            set.write->clearRestDuration == (payload != 0) &&
                apply.write->clearRestDuration == (payload != 0),
            "rest-clear predicate changed");

        const std::uint32_t setBits = bits(set.write->value);
        const std::uint32_t applyBits = bits(apply.write->value);
        require(
            setBits ==
                exactReferenceBits(
                    LegacyAircraftNativeThrustEvent::thrustSet,
                    payload),
            "SET disagrees with exact RNE32 oracle");
        require(
            applyBits ==
                exactReferenceBits(
                    LegacyAircraftNativeThrustEvent::thrustApply,
                    payload),
            "APPLY disagrees with exact RNE32 oracle");
        if (payload > 0) {
            require(setBits > previousSet, "SET is not monotonic");
            require(applyBits > previousApply, "APPLY is not monotonic");

            const auto negativeSet = decode(
                LegacyAircraftNativeThrustEvent::thrustSet,
                -payload);
            const auto negativeApply = decode(
                LegacyAircraftNativeThrustEvent::thrustApply,
                -payload);
            require(
                bits(negativeSet.write->value) ==
                    (setBits | 0x80000000U),
                "SET sign symmetry changed");
            require(
                bits(negativeApply.write->value) ==
                    (applyBits | 0x80000000U),
                "APPLY sign symmetry changed");
            require(
                bits(negativeSet.write->value) ==
                    exactReferenceBits(
                        LegacyAircraftNativeThrustEvent::thrustSet,
                        -payload),
                "negative SET disagrees with exact RNE32 oracle");
            require(
                bits(negativeApply.write->value) ==
                    exactReferenceBits(
                        LegacyAircraftNativeThrustEvent::thrustApply,
                        -payload),
                "negative APPLY disagrees with exact RNE32 oracle");
        }
        previousSet = setBits;
        previousApply = applyBits;
    }
}

void testInactiveGatePrecedesPayloadValidation() {
    for (const auto event : {
             LegacyAircraftNativeThrustEvent::thrustSet,
             LegacyAircraftNativeThrustEvent::thrustApply}) {
        for (const std::int32_t payload : {-255, -1, 0, 1, 255}) {
            const auto result = decode(event, payload, true);
            require(
                result.ignored(),
                "in-range inactive event was not ignored");
        }
        for (const std::int32_t payload : {
                 std::numeric_limits<std::int32_t>::min(),
                 -256,
                 256,
                 std::numeric_limits<std::int32_t>::max()}) {
            const auto result = decode(event, payload, true);
            require(result.ignored(), "inactive event was not ignored");
            require(
                !result.failed(),
                "inactive event was treated as failure");
        }
    }
}

void testActivePayloadEvidenceBoundary() {
    for (const auto event : {
             LegacyAircraftNativeThrustEvent::thrustSet,
             LegacyAircraftNativeThrustEvent::thrustApply}) {
        for (const std::int32_t payload : {
                 std::numeric_limits<std::int32_t>::min(),
                 -256,
                 256,
                 std::numeric_limits<std::int32_t>::max()}) {
            const auto result = decode(event, payload);
            require(result.failed(), "outside payload was accepted");
            require(
                result.status ==
                    LegacyAircraftThrustEventDecodeStatus::
                        payloadOutsideEvidenceRange,
                "outside payload reported wrong status");
            require(!result.write.has_value(), "failed event emitted write");
        }
    }
}

void testUnsupportedEventsFailBeforeInactiveGate() {
    for (const std::uint8_t eventValue : {0x61U, 0x62U, 0x65U}) {
        const auto unsupported =
            static_cast<LegacyAircraftNativeThrustEvent>(eventValue);
        const auto result = decode(
            unsupported,
            std::numeric_limits<std::int32_t>::max(),
            true);
        require(result.failed(), "unsupported event was accepted");
        require(
            result.status ==
                LegacyAircraftThrustEventDecodeStatus::unsupportedEvent,
            "unsupported event reported wrong status");
        require(
            !result.write.has_value(),
            "unsupported event emitted write");
    }
}

void applyWrite(
    LegacyAircraftThrustControlState& state,
    const LegacyAircraftThrustWrite& write) {
    switch (write.field) {
    case LegacyAircraftThrustWriteField::targetThrust:
        state.targetThrust = write.value;
        return;
    case LegacyAircraftThrustWriteField::thrustApply:
        state.thrustApply = write.value;
        return;
    }
    fail("write selected unsupported field");
}

void testTypedWriteComposition() {
    LegacyAircraftThrustControlState state{
        .thrustApply = -7.0F,
        .targetThrust = 9.0F,
        .smoothedThrust = 0.25F,
    };

    const auto set = decode(
        LegacyAircraftNativeThrustEvent::thrustSet, -255);
    applyWrite(state, *set.write);
    require(
        bits(state.targetThrust) == 0xBF800000U &&
            state.thrustApply == -7.0F &&
            state.smoothedThrust == 0.25F,
        "SET changed more than target");

    const auto apply = decode(
        LegacyAircraftNativeThrustEvent::thrustApply, 255);
    applyWrite(state, *apply.write);
    require(
        bits(state.thrustApply) == 0x3CA3D70BU &&
            bits(state.targetThrust) == 0xBF800000U &&
            state.smoothedThrust == 0.25F,
        "APPLY changed more than apply field");

    const auto next = legacyAircraftAdvanceThrustControl(
        state,
        {
            .health = 1.0F,
            .engineStartTransitionActive = false,
        });
    require(next.has_value(), "typed writes did not compose with slot45");
    require(
        bits(next->targetThrust) == 0x00000000U,
        "slot45 did not clamp the event-produced target");
    require(
        bits(next->smoothedThrust) == 0x3E7D2F1BU,
        "slot45 smoothing ordering changed after event writes");
}

void testReplacementAndRestClearSemantics() {
    const auto first = decode(
        LegacyAircraftNativeThrustEvent::thrustApply, 255);
    const auto same = decode(
        LegacyAircraftNativeThrustEvent::thrustApply, 255);
    const auto release = decode(
        LegacyAircraftNativeThrustEvent::thrustApply, 0);
    require(
        first.write == same.write &&
            same.write->clearRestDuration,
        "identical non-zero replacement lost rest clear");
    requireDecoded(
        release,
        LegacyAircraftThrustWriteField::thrustApply,
        0x00000000U,
        false,
        "zero release semantics changed");

    LegacyAircraftThrustControlState state{
        .thrustApply = -1.0F,
        .targetThrust = -1.0F,
        .smoothedThrust = 0.5F,
    };
    applyWrite(state, *first.write);
    applyWrite(state, *release.write);
    require(
        bits(state.thrustApply) == 0x00000000U,
        "last APPLY write did not win");

    const auto set127 = decode(
        LegacyAircraftNativeThrustEvent::thrustSet, 127);
    const auto set255 = decode(
        LegacyAircraftNativeThrustEvent::thrustSet, 255);
    applyWrite(state, *set127.write);
    applyWrite(state, *set255.write);
    require(
        bits(state.targetThrust) == 0x3F800000U,
        "last SET write did not win");
    require(
        state.smoothedThrust == 0.5F,
        "replacement sequence changed smoothed thrust");
}

} // namespace

int main() {
    testRecoveredBinary32Vectors();
    testExhaustiveAdmittedPayloadRange();
    testInactiveGatePrecedesPayloadValidation();
    testActivePayloadEvidenceBoundary();
    testUnsupportedEventsFailBeforeInactiveGate();
    testTypedWriteComposition();
    testReplacementAndRestClearSemantics();
    std::cout << "Legacy aircraft thrust event reducer tests passed\n";
    return EXIT_SUCCESS;
}
