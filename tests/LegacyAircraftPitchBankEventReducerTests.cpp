#include "airfix/simulation/LegacyAircraftPitchBankEventReducer.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(
    static_cast<std::uint8_t>(
        LegacyAircraftNativePitchBankEvent::pitchSet) == 0x5FU);
static_assert(
    static_cast<std::uint8_t>(
        LegacyAircraftNativePitchBankEvent::bankSet) == 0x65U);
static_assert(
    legacyAircraftPitchBankSetScaleBits == 0x3D3020C5U);
static_assert(
    std::bit_cast<std::uint32_t>(
        legacyAircraftPitchBankSetScale) == 0x3D3020C5U);
static_assert(
    noexcept(legacyAircraftDecodeNativePitchBankEvent({})));
static_assert(
    std::is_trivially_copyable_v<
        LegacyAircraftPitchBankWrite>);
static_assert(
    std::is_trivially_copyable_v<
        LegacyAircraftPitchBankEventDecodeResult>);

[[noreturn]] void fail(const char* const message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* const message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] LegacyAircraftPitchBankEventDecodeResult decode(
    const LegacyAircraftNativePitchBankEvent event,
    const std::int32_t payload,
    const bool inactive = false,
    const LegacyAircraftPitchBankNumericPolicy policy =
        LegacyAircraftPitchBankNumericPolicy::
            startupPc53RoundToNearestEven) noexcept {
    return legacyAircraftDecodeNativePitchBankEvent({
        .event = event,
        .payload = payload,
        .vehicleInactive = inactive,
        .numericPolicy = policy,
    });
}

void requireDecoded(
    const LegacyAircraftPitchBankEventDecodeResult& result,
    const LegacyAircraftPitchBankWriteField expectedField,
    const std::uint32_t expectedBits,
    const bool expectedRestClear,
    const char* const message) {
    require(result.decoded(), message);
    require(result.write->field == expectedField, message);
    require(result.write->valueBits == expectedBits, message);
    require(
        std::bit_cast<std::uint32_t>(result.write->value()) ==
            expectedBits,
        message);
    require(
        result.write->clearRestDuration == expectedRestClear,
        message);
}

struct NativeVector final {
    std::int32_t payload;
    std::uint32_t storedBits;
};

constexpr std::array<NativeVector, 11> nativeVectors{{
    {0, 0x00000000U},
    {1, 0x3D3020C5U},
    {-1, 0xBD3020C5U},
    {32, 0x3FB020C5U},
    {-32, 0xBFB020C5U},
    {3, 0x3E041894U},
    {-3, 0xBE041894U},
    {16'777'217, 0x493020C6U},
    {-16'777'217, 0xC93020C6U},
    {1'555'145'203, 0x4C7F17F4U},
    {-1'555'145'203, 0xCC7F17F4U},
}};

void testConditionalPc53RoundToNearestEvenVectors() {
    for (const auto event : {
             LegacyAircraftNativePitchBankEvent::pitchSet,
             LegacyAircraftNativePitchBankEvent::bankSet}) {
        const auto expectedField =
            event ==
                LegacyAircraftNativePitchBankEvent::pitchSet
            ? LegacyAircraftPitchBankWriteField::pitch
            : LegacyAircraftPitchBankWriteField::bank;
        for (const NativeVector& vector : nativeVectors) {
            requireDecoded(
                decode(event, vector.payload),
                expectedField,
                vector.storedBits,
                vector.payload != 0,
                "conditional PC53/RNE vector changed");
        }
    }
}

void testDiscriminatorVectorsRejectWrongReconstructions() {
    const auto decimalDouble = decode(
        LegacyAircraftNativePitchBankEvent::pitchSet, 3);
    const auto prematureBinary32 = decode(
        LegacyAircraftNativePitchBankEvent::pitchSet, 16'777'217);
    const auto retainedPc64 = decode(
        LegacyAircraftNativePitchBankEvent::pitchSet,
        1'555'145'203);

    require(
        decimalDouble.write->valueBits == 0x3E041894U &&
            decimalDouble.write->valueBits != 0x3E041893U,
        "decimal-double discriminator failed");
    require(
        prematureBinary32.write->valueBits == 0x493020C6U &&
            prematureBinary32.write->valueBits != 0x493020C5U,
        "premature-binary32 discriminator failed");
    require(
        retainedPc64.write->valueBits == 0x4C7F17F4U &&
            retainedPc64.write->valueBits != 0x4C7F17F3U,
        "PC64 discriminator failed");
}

void testFullSignedNativePayloadDomain() {
    for (const auto event : {
             LegacyAircraftNativePitchBankEvent::pitchSet,
             LegacyAircraftNativePitchBankEvent::bankSet}) {
        const auto minimum = decode(
            event, std::numeric_limits<std::int32_t>::min());
        const auto maximum = decode(
            event, std::numeric_limits<std::int32_t>::max());
        require(
            minimum.decoded() &&
                minimum.write->valueBits == 0xCCB020C5U &&
                minimum.write->clearRestDuration,
            "INT32_MIN was not decoded over the native domain");
        require(
            maximum.decoded() &&
                maximum.write->valueBits == 0x4CB020C5U &&
                maximum.write->clearRestDuration,
            "INT32_MAX was not decoded over the native domain");
    }
}

void testInactiveGateProducesNoWrite() {
    for (const auto event : {
             LegacyAircraftNativePitchBankEvent::pitchSet,
             LegacyAircraftNativePitchBankEvent::bankSet}) {
        for (const std::int32_t payload : {
                 std::numeric_limits<std::int32_t>::min(),
                 -1,
                 0,
                 1,
                 std::numeric_limits<std::int32_t>::max()}) {
            const auto result = decode(event, payload, true);
            require(result.ignored(), "inactive SET was not ignored");
            require(
                !result.failed() && !result.write.has_value(),
                "inactive SET emitted a write or failure");
        }
    }
}

void testUnsupportedInputFailsClosed() {
    for (const std::uint8_t eventValue : {
             0x5EU, 0x60U, 0x66U, 0x67U}) {
        const auto result = decode(
            static_cast<LegacyAircraftNativePitchBankEvent>(
                eventValue),
            std::numeric_limits<std::int32_t>::max(),
            true);
        require(result.failed(), "unsupported event was accepted");
        require(
            result.status ==
                LegacyAircraftPitchBankEventDecodeStatus::
                    unsupportedEvent &&
                !result.write.has_value(),
            "unsupported event reported the wrong result");
    }

    const auto unsupportedPolicy = decode(
        LegacyAircraftNativePitchBankEvent::pitchSet,
        1,
        false,
        static_cast<LegacyAircraftPitchBankNumericPolicy>(0xFFU));
    require(
        unsupportedPolicy.failed() &&
            unsupportedPolicy.status ==
                LegacyAircraftPitchBankEventDecodeStatus::
                    unsupportedNumericPolicy &&
            !unsupportedPolicy.write.has_value(),
        "unsupported numeric policy was accepted");

    const auto inactiveUnsupportedPolicy = decode(
        LegacyAircraftNativePitchBankEvent::bankSet,
        std::numeric_limits<std::int32_t>::min(),
        true,
        static_cast<LegacyAircraftPitchBankNumericPolicy>(0xFFU));
    require(
        inactiveUnsupportedPolicy.ignored() &&
            !inactiveUnsupportedPolicy.failed() &&
            !inactiveUnsupportedPolicy.write.has_value(),
        "inactive gate did not precede numeric-policy validation");
}

struct SyntheticVehicleState final {
    std::uint32_t pitchBits{0xDEADBEEFU};
    std::uint32_t bankBits{0xBAADF00DU};
    std::int64_t restDuration{0x102030405060708LL};
    std::uint32_t writes{};
    std::uint32_t restClears{};
};

void applyWrite(
    SyntheticVehicleState& state,
    const LegacyAircraftPitchBankWrite& write) {
    switch (write.field) {
    case LegacyAircraftPitchBankWriteField::pitch:
        state.pitchBits = write.valueBits;
        break;
    case LegacyAircraftPitchBankWriteField::bank:
        state.bankBits = write.valueBits;
        break;
    }
    ++state.writes;
    if (write.clearRestDuration) {
        state.restDuration = 0;
        ++state.restClears;
    }
}

void testZeroAndRepeatedWriteSemantics() {
    for (const auto event : {
             LegacyAircraftNativePitchBankEvent::pitchSet,
             LegacyAircraftNativePitchBankEvent::bankSet}) {
        SyntheticVehicleState zeroState;
        const auto zero = decode(event, 0);
        applyWrite(zeroState, *zero.write);
        const bool zeroSelectedOnlyItsField =
            event ==
                LegacyAircraftNativePitchBankEvent::pitchSet
            ? zeroState.pitchBits == 0x00000000U &&
                zeroState.bankBits == 0xBAADF00DU
            : zeroState.pitchBits == 0xDEADBEEFU &&
                zeroState.bankBits == 0x00000000U;
        require(
            zeroSelectedOnlyItsField &&
                zeroState.restDuration ==
                    0x102030405060708LL &&
                zeroState.writes == 1U &&
                zeroState.restClears == 0U,
            "active zero changed unrelated state or cleared rest");

        SyntheticVehicleState repeated;
        const auto first = decode(event, 32);
        const auto same = decode(event, 32);
        require(first.write == same.write, "equal SET writes differ");
        applyWrite(repeated, *first.write);
        repeated.restDuration = 17;
        applyWrite(repeated, *same.write);
        const bool repeatedSelectedOnlyItsField =
            event ==
                LegacyAircraftNativePitchBankEvent::pitchSet
            ? repeated.pitchBits == 0x3FB020C5U &&
                repeated.bankBits == 0xBAADF00DU
            : repeated.pitchBits == 0xDEADBEEFU &&
                repeated.bankBits == 0x3FB020C5U;
        require(
            repeatedSelectedOnlyItsField &&
                repeated.writes == 2U &&
                repeated.restClears == 2U &&
                repeated.restDuration == 0,
            "repeated nonzero SET was suppressed");
    }
}

void testOrderedTypedWriteComposition() {
    const auto pitchFirst = decode(
        LegacyAircraftNativePitchBankEvent::pitchSet, -32);
    const auto bankFirst = decode(
        LegacyAircraftNativePitchBankEvent::bankSet, 3);
    const auto pitchLast = decode(
        LegacyAircraftNativePitchBankEvent::pitchSet, 1);
    const auto bankLast = decode(
        LegacyAircraftNativePitchBankEvent::bankSet, 0);

    SyntheticVehicleState pitchThenBank;
    applyWrite(pitchThenBank, *pitchFirst.write);
    applyWrite(pitchThenBank, *bankFirst.write);
    applyWrite(pitchThenBank, *pitchLast.write);
    applyWrite(pitchThenBank, *bankLast.write);

    SyntheticVehicleState bankThenPitch;
    applyWrite(bankThenPitch, *bankFirst.write);
    applyWrite(bankThenPitch, *pitchFirst.write);
    applyWrite(bankThenPitch, *bankLast.write);
    applyWrite(bankThenPitch, *pitchLast.write);

    for (const SyntheticVehicleState* const state :
         {&pitchThenBank, &bankThenPitch}) {
        require(
            state->pitchBits == 0x3D3020C5U &&
                state->bankBits == 0x00000000U,
            "last processed SET did not win per axis");
        require(
            state->writes == 4U && state->restClears == 3U,
            "ordered writes changed rest-clear decisions");
    }
}

} // namespace

int main() {
    testConditionalPc53RoundToNearestEvenVectors();
    testDiscriminatorVectorsRejectWrongReconstructions();
    testFullSignedNativePayloadDomain();
    testInactiveGateProducesNoWrite();
    testUnsupportedInputFailsClosed();
    testZeroAndRepeatedWriteSemantics();
    testOrderedTypedWriteComposition();
    std::cout
        << "Legacy aircraft pitch/bank event reducer tests passed\n";
    return EXIT_SUCCESS;
}
