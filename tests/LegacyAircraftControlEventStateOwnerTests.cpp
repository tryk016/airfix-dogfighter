#include "airfix/simulation/LegacyAircraftControlEventStateOwner.hpp"
#include "airfix/simulation/LegacyVehicleSleepState.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

using namespace airfix::simulation;

static_assert(std::is_trivially_copyable_v<LegacyAircraftControlEventState>);
static_assert(noexcept(LegacyAircraftControlEventStateOwner{
    LegacyAircraftControlEventState{}}));
static_assert(
    noexcept(std::declval<LegacyAircraftControlEventStateOwner&>().tryApply(
        std::declval<const LegacyAircraftThrustWrite&>())));
static_assert(
    noexcept(std::declval<LegacyAircraftControlEventStateOwner&>().tryApply(
        std::declval<const LegacyAircraftPitchBankWrite&>())));
static_assert(
    noexcept(std::declval<LegacyAircraftControlEventStateOwner&>().tryApply(
        std::declval<const LegacyAircraftTurnWrite&>())));

[[noreturn]] void fail(const char* const message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* const message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] LegacyAircraftControlEventState sentinelState() {
    return {
        .thrustControl =
            {
                .thrustApply = 0.25F,
                .targetThrust = 0.5F,
                .smoothedThrust = 0.75F,
            },
        .turnBits = 0x3E800000U,
        .pitchBits = 0xBF000000U,
        .bankBits = 0x3F400000U,
        .restDurationMilliseconds = 2011,
    };
}

void testWakeControlOrderAndExactBits() {
    const LegacyAircraftControlEventState state{
        .thrustControl =
            {
                .thrustApply = std::bit_cast<float>(0x3EAAAAABU),
                .targetThrust = std::bit_cast<float>(0x3F19999AU),
                .smoothedThrust = std::bit_cast<float>(0x3F4CCCCDU),
            },
        .turnBits = 0xBE000000U,
        .pitchBits = 0xBF400000U,
        .bankBits = 0x3F000000U,
        .restDurationMilliseconds = 1999,
    };

    const auto wake = state.wakeControlValues();
    const std::array<std::uint32_t, 5U> expected{{
        0x3F19999AU,
        0x3EAAAAABU,
        0xBE000000U,
        0x3F000000U,
        0xBF400000U,
    }};
    for (std::size_t index = 0; index < wake.size(); ++index) {
        require(std::bit_cast<std::uint32_t>(wake[index]) == expected[index],
                "wake-control order or exact bits changed");
    }

    const LegacyAircraftControlEventStateOwner owner{state};
    require(owner.wakeControlValues() == wake,
            "owner wake-control view differs from snapshot");
}

void testEachWritePreservesUnselectedState() {
    {
        const auto initial = sentinelState();
        LegacyAircraftControlEventStateOwner owner{initial};
        require(owner.tryApply({
                    .field = LegacyAircraftThrustWriteField::targetThrust,
                    .value = -0.125F,
                    .clearRestDuration = false,
                }) == LegacyAircraftControlEventCommitStatus::committed,
                "target-thrust write failed");
        auto expected = initial;
        expected.thrustControl.targetThrust = -0.125F;
        require(owner.snapshot() == expected,
                "target-thrust write changed unrelated state");
    }

    {
        const auto initial = sentinelState();
        LegacyAircraftControlEventStateOwner owner{initial};
        require(owner.tryApply({
                    .field = LegacyAircraftThrustWriteField::thrustApply,
                    .value = 0.02F,
                    .clearRestDuration = true,
                }) == LegacyAircraftControlEventCommitStatus::committed,
                "thrust-apply write failed");
        auto expected = initial;
        expected.thrustControl.thrustApply = 0.02F;
        expected.restDurationMilliseconds = 0;
        require(owner.snapshot() == expected,
                "thrust-apply transaction was not isolated");
    }

    {
        const auto initial = sentinelState();
        LegacyAircraftControlEventStateOwner owner{initial};
        require(owner.tryApply({
                    .field = LegacyAircraftPitchBankWriteField::pitch,
                    .valueBits = 0x80000000U,
                    .clearRestDuration = false,
                }) == LegacyAircraftControlEventCommitStatus::committed,
                "pitch write failed");
        auto expected = initial;
        expected.pitchBits = 0x80000000U;
        require(owner.snapshot() == expected,
                "pitch exact-bit write changed unrelated state");
    }

    {
        const auto initial = sentinelState();
        LegacyAircraftControlEventStateOwner owner{initial};
        require(owner.tryApply({
                    .field = LegacyAircraftPitchBankWriteField::bank,
                    .valueBits = 0x3FB020C5U,
                    .clearRestDuration = true,
                }) == LegacyAircraftControlEventCommitStatus::committed,
                "bank write failed");
        auto expected = initial;
        expected.bankBits = 0x3FB020C5U;
        expected.restDurationMilliseconds = 0;
        require(owner.snapshot() == expected,
                "bank transaction was not isolated");
    }

    {
        const auto initial = sentinelState();
        LegacyAircraftControlEventStateOwner owner{initial};
        require(owner.tryApply(LegacyAircraftTurnWrite{
                    .valueBits = 0xBFB020C5U,
                    .clearRestDuration = true,
                }) == LegacyAircraftControlEventCommitStatus::committed,
                "turn write failed");
        auto expected = initial;
        expected.turnBits = 0xBFB020C5U;
        expected.restDurationMilliseconds = 0;
        require(owner.snapshot() == expected,
                "turn transaction was not isolated");
    }
}

void testDirectiveControlsRestWithoutEqualitySuppression() {
    auto initial = sentinelState();
    initial.restDurationMilliseconds = 1777;
    LegacyAircraftControlEventStateOwner owner{initial};

    require(owner.tryApply(LegacyAircraftTurnWrite{
                .valueBits = initial.turnBits,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::committed,
            "repeated turn write failed");
    require(owner.snapshot().restDurationMilliseconds == 0,
            "repeated nonzero write did not honor rest clear");

    auto sleeping = sentinelState();
    LegacyAircraftControlEventStateOwner zeroOwner{sleeping};
    require(zeroOwner.tryApply({
                .field = LegacyAircraftPitchBankWriteField::bank,
                .valueBits = 0U,
                .clearRestDuration = false,
            }) == LegacyAircraftControlEventCommitStatus::committed,
            "zero bank release failed");
    require(zeroOwner.snapshot().restDurationMilliseconds ==
                sleeping.restDurationMilliseconds,
            "zero write cleared rest without a directive");
}

void testInvalidWritesRollBackCompletely() {
    const auto initial = sentinelState();
    LegacyAircraftControlEventStateOwner owner{initial};

    require(owner.tryApply({
                .field = static_cast<LegacyAircraftThrustWriteField>(0xFFU),
                .value = 1.0F,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::unsupportedField,
            "invalid thrust field was accepted");
    require(owner.snapshot() == initial,
            "invalid thrust field partially mutated state");

    require(owner.tryApply({
                .field = static_cast<LegacyAircraftPitchBankWriteField>(0xFFU),
                .valueBits = 0x3F800000U,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::unsupportedField,
            "invalid angular field was accepted");
    require(owner.snapshot() == initial,
            "invalid angular field partially mutated state");

    require(owner.tryApply({
                .field = LegacyAircraftThrustWriteField::targetThrust,
                .value = std::numeric_limits<float>::infinity(),
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::nonFiniteValue,
            "infinite thrust write was accepted");
    require(owner.tryApply({
                .field = LegacyAircraftPitchBankWriteField::pitch,
                .valueBits = 0x7FC00000U,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::nonFiniteValue,
            "NaN pitch write was accepted");
    require(owner.tryApply(LegacyAircraftTurnWrite{
                .valueBits = 0xFF800000U,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::nonFiniteValue,
            "infinite turn write was accepted");
    require(owner.snapshot() == initial,
            "non-finite writes partially mutated state");
}

void testAlreadyOrderedLastWriteWins() {
    auto initial = sentinelState();
    initial.restDurationMilliseconds = 900;
    LegacyAircraftControlEventStateOwner owner{initial};

    require(owner.tryApply({
                .field = LegacyAircraftPitchBankWriteField::pitch,
                .valueBits = 0x3D3020C5U,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::committed,
            "first pitch write failed");
    require(owner.tryApply(LegacyAircraftTurnWrite{
                .valueBits = 0xBE041894U,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::committed,
            "interleaved turn write failed");
    require(owner.tryApply({
                .field = LegacyAircraftPitchBankWriteField::pitch,
                .valueBits = 0xC93020C6U,
                .clearRestDuration = true,
            }) == LegacyAircraftControlEventCommitStatus::committed,
            "last pitch write failed");

    const auto state = owner.snapshot();
    require(state.pitchBits == 0xC93020C6U && state.turnBits == 0xBE041894U &&
                state.bankBits == initial.bankBits &&
                state.restDurationMilliseconds == 0,
            "already-ordered last-write-wins behavior changed");
}

void testCompositionWithSleepState() {
    auto sleeping = sentinelState();
    sleeping.restDurationMilliseconds = 2011;
    LegacyAircraftControlEventStateOwner owner{sleeping};

    const auto turn = legacyAircraftDecodeNativeTurnEvent({
        .event = LegacyAircraftNativeTurnEvent::turnSet,
        .payload = 1,
        .vehicleInactive = false,
    });
    require(turn.decoded() &&
                owner.tryApply(*turn.write) ==
                    LegacyAircraftControlEventCommitStatus::committed,
            "decoded turn write did not commit");

    const LegacyVehicleSleepStepInput activeInput{
        .wakeControlValues = owner.wakeControlValues(),
        .linearVelocitySquared = 0.0F,
        .onGround = true,
        .waterUnit = false,
        .refreshDeltaMilliseconds = 7,
    };
    const auto active = legacyVehicleAdvanceSleepStep(
        owner.snapshot().restDurationMilliseconds, activeInput);
    require(active.has_value() && active->integratePhysics &&
                active->restDurationMilliseconds == 0 && !active->clearDynamics,
            "control transaction did not wake before refresh");

    LegacyAircraftControlEventStateOwner releaseOwner{sleeping};
    const auto zero = legacyAircraftDecodeNativeTurnEvent({
        .event = LegacyAircraftNativeTurnEvent::turnSet,
        .payload = 0,
        .vehicleInactive = false,
    });
    require(zero.decoded() &&
                releaseOwner.tryApply(*zero.write) ==
                    LegacyAircraftControlEventCommitStatus::committed,
            "zero turn release did not commit");
    const auto stillSleeping = legacyVehicleAdvanceSleepStep(
        releaseOwner.snapshot().restDurationMilliseconds,
        {
            .wakeControlValues = releaseOwner.wakeControlValues(),
            .linearVelocitySquared = 0.0F,
            .onGround = true,
            .waterUnit = false,
            .refreshDeltaMilliseconds = 7,
        });
    require(stillSleeping.has_value() && !stillSleeping->integratePhysics &&
                stillSleeping->restDurationMilliseconds == 2011,
            "zero release incorrectly woke a sleeping vehicle");
}

} // namespace

int main() {
    testWakeControlOrderAndExactBits();
    testEachWritePreservesUnselectedState();
    testDirectiveControlsRestWithoutEqualitySuppression();
    testInvalidWritesRollBackCompletely();
    testAlreadyOrderedLastWriteWins();
    testCompositionWithSleepState();
    std::cout << "Legacy aircraft control-event state-owner tests passed.\n";
    return EXIT_SUCCESS;
}
