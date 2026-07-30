#include "airfix/simulation/LegacyAircraftTurnEventReducer.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

using namespace airfix::simulation;

static_assert(
    static_cast<std::uint8_t>(LegacyAircraftNativeTurnEvent::turnSet) == 0x5DU);
static_assert(legacyAircraftAngularSetScaleBits == 0x3D3020C5U);
static_assert(std::bit_cast<std::uint32_t>(legacyAircraftAngularSetScale) ==
              0x3D3020C5U);
static_assert(noexcept(legacyAircraftDecodeNativeTurnEvent({})));
static_assert(std::is_trivially_copyable_v<LegacyAircraftTurnWrite>);
static_assert(
    std::is_trivially_copyable_v<LegacyAircraftTurnEventDecodeResult>);

[[noreturn]] void fail(const char* const message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* const message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] LegacyAircraftTurnEventDecodeResult
decode(const std::int32_t payload,
       const bool inactive = false,
       const LegacyAircraftAngularSetNumericPolicy policy =
           LegacyAircraftAngularSetNumericPolicy::
               startupPc53RoundToNearestEven) noexcept {
    return legacyAircraftDecodeNativeTurnEvent({
        .event = LegacyAircraftNativeTurnEvent::turnSet,
        .payload = payload,
        .vehicleInactive = inactive,
        .numericPolicy = policy,
    });
}

struct NativeVector final {
    std::int32_t payload;
    std::uint32_t storedBits;
};

constexpr std::array<NativeVector, 11U> nativeVectors{{
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

void testExactNativeVectors() {
    for (const NativeVector& vector : nativeVectors) {
        const auto result = decode(vector.payload);
        require(result.decoded(), "TURN_SET vector was not decoded");
        require(result.write->valueBits == vector.storedBits,
                "TURN_SET exact bits changed");
        require(std::bit_cast<std::uint32_t>(result.write->value()) ==
                    vector.storedBits,
                "TURN_SET value accessor changed exact bits");
        require(result.write->clearRestDuration == (vector.payload != 0),
                "TURN_SET rest-clear predicate changed");
    }

    require(decode(std::numeric_limits<std::int32_t>::min()).write->valueBits ==
                0xCCB020C5U,
            "TURN_SET INT32_MIN changed");
    require(decode(std::numeric_limits<std::int32_t>::max()).write->valueBits ==
                0x4CB020C5U,
            "TURN_SET INT32_MAX changed");
}

void testInactiveAndUnsupportedInputs() {
    for (const std::int32_t payload :
         {std::numeric_limits<std::int32_t>::min(),
          -1,
          0,
          1,
          std::numeric_limits<std::int32_t>::max()}) {
        const auto result = decode(payload, true);
        require(result.ignored(), "inactive TURN_SET was not ignored");
    }

    const auto unsupportedEvent = legacyAircraftDecodeNativeTurnEvent({
        .event = static_cast<LegacyAircraftNativeTurnEvent>(0x5EU),
        .payload = 1,
        .vehicleInactive = true,
    });
    require(unsupportedEvent.failed() &&
                unsupportedEvent.status ==
                    LegacyAircraftTurnEventDecodeStatus::unsupportedEvent,
            "unsupported turn event was accepted");

    const auto unsupportedPolicy = decode(
        1, false, static_cast<LegacyAircraftAngularSetNumericPolicy>(0xFFU));
    require(
        unsupportedPolicy.failed() &&
            unsupportedPolicy.status ==
                LegacyAircraftTurnEventDecodeStatus::unsupportedNumericPolicy,
        "unsupported turn numeric policy was accepted");

    const auto inactiveUnsupportedPolicy =
        decode(std::numeric_limits<std::int32_t>::min(),
               true,
               static_cast<LegacyAircraftAngularSetNumericPolicy>(0xFFU));
    require(inactiveUnsupportedPolicy.ignored(),
            "inactive turn gate did not precede policy validation");
}

void testZeroAndRepeatedWrites() {
    const auto zero = decode(0);
    require(zero.decoded() && zero.write->valueBits == 0U &&
                !zero.write->clearRestDuration,
            "active zero TURN_SET did not emit +0 without rest clear");

    const auto first = decode(32);
    const auto repeated = decode(32);
    require(first.write == repeated.write && repeated.write->clearRestDuration,
            "repeated TURN_SET was suppressed");
}

} // namespace

int main() {
    testExactNativeVectors();
    testInactiveAndUnsupportedInputs();
    testZeroAndRepeatedWrites();
    std::cout << "Legacy aircraft turn-event reducer tests passed.\n";
    return EXIT_SUCCESS;
}
