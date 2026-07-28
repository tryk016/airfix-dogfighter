#include "airfix/simulation/LegacyVehicleSleepState.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::simulation::LegacyVehicleSleepStepInput;
using airfix::simulation::LegacyVehicleSleepStepResult;
using airfix::simulation::legacyVehicleAdvanceSleepStep;
using airfix::simulation::legacyVehicleGroundRestSpeedSquaredThreshold;
using airfix::simulation::legacyVehicleInitialRestDurationMilliseconds;
using airfix::simulation::legacyVehicleSleepThresholdMilliseconds;
using airfix::simulation::legacyVehicleWaterRestSpeedSquaredThreshold;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

LegacyVehicleSleepStepInput groundRestingInput() {
    return LegacyVehicleSleepStepInput{
        .wakeControlValues = {},
        .linearVelocitySquared = 0.0F,
        .onGround = true,
        .waterUnit = false,
        .refreshDeltaMilliseconds = 12,
    };
}

void testRecoveredConstants() {
    static_assert(
        legacyVehicleInitialRestDurationMilliseconds == 1999);
    static_assert(legacyVehicleSleepThresholdMilliseconds == 2000);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyVehicleGroundRestSpeedSquaredThreshold) ==
        0x3CF5C28FU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyVehicleWaterRestSpeedSquaredThreshold) ==
        0x3DA3D70AU);
}

void testThresholdCrossingAndSleepingStep() {
    const auto crossed = legacyVehicleAdvanceSleepStep(
        legacyVehicleInitialRestDurationMilliseconds,
        groundRestingInput());
    require(
        crossed == LegacyVehicleSleepStepResult{
            .restDurationMilliseconds = 2011,
            .integratePhysics = true,
            .clearDynamics = true,
        },
        "the initial active step did not cross and clear at 2000 ms");
    require(crossed->sleeping(), "threshold result was not sleeping");

    const auto sleeping = legacyVehicleAdvanceSleepStep(
        crossed->restDurationMilliseconds,
        groundRestingInput());
    require(
        sleeping == LegacyVehicleSleepStepResult{
            .restDurationMilliseconds = 2011,
            .integratePhysics = false,
            .clearDynamics = false,
        },
        "a sleeping refresh changed duration or repeated dynamics clearing");

    auto unappliedControlEvent = groundRestingInput();
    unappliedControlEvent.wakeControlValues[0] = 1.0F;
    const auto stillSleeping = legacyVehicleAdvanceSleepStep(
        crossed->restDurationMilliseconds,
        unappliedControlEvent);
    require(
        stillSleeping == sleeping,
        "refresh input silently replaced the separate control-event wake");

    auto ignoredActiveInputs = groundRestingInput();
    ignoredActiveInputs.linearVelocitySquared =
        std::numeric_limits<float>::quiet_NaN();
    ignoredActiveInputs.wakeControlValues[1] =
        std::numeric_limits<float>::infinity();
    require(
        legacyVehicleAdvanceSleepStep(
            crossed->restDurationMilliseconds,
            ignoredActiveInputs) == sleeping,
        "a sleeping refresh inspected inputs from the skipped active path");
}

void testControlsAndMotionWakeTheAccumulator() {
    auto input = groundRestingInput();
    input.wakeControlValues[0] = 1.0F;
    const auto controlled = legacyVehicleAdvanceSleepStep(1500, input);
    require(
        controlled == LegacyVehicleSleepStepResult{
            .restDurationMilliseconds = 0,
            .integratePhysics = true,
            .clearDynamics = false,
        },
        "a nonzero wake control did not reset rest duration");

    input = groundRestingInput();
    input.linearVelocitySquared =
        legacyVehicleGroundRestSpeedSquaredThreshold;
    const auto groundThreshold =
        legacyVehicleAdvanceSleepStep(1500, input);
    require(
        groundThreshold.has_value() &&
            groundThreshold->restDurationMilliseconds == 0,
        "the strict ground-speed threshold became inclusive");

    input.onGround = false;
    input.waterUnit = true;
    input.linearVelocitySquared =
        legacyVehicleWaterRestSpeedSquaredThreshold;
    const auto waterThreshold =
        legacyVehicleAdvanceSleepStep(1500, input);
    require(
        waterThreshold.has_value() &&
            waterThreshold->restDurationMilliseconds == 0,
        "the strict water-speed threshold became inclusive");
}

void testGroundAndWaterRestPredicates() {
    auto input = groundRestingInput();
    input.linearVelocitySquared =
        legacyVehicleGroundRestSpeedSquaredThreshold / 2.0F;
    const auto ground = legacyVehicleAdvanceSleepStep(100, input);
    require(
        ground.has_value() &&
            ground->restDurationMilliseconds == 112,
        "low ground motion did not accumulate signed milliseconds");

    input.onGround = false;
    input.waterUnit = true;
    input.linearVelocitySquared =
        legacyVehicleWaterRestSpeedSquaredThreshold / 2.0F;
    const auto water = legacyVehicleAdvanceSleepStep(100, input);
    require(
        water.has_value() &&
            water->restDurationMilliseconds == 112,
        "low water motion did not accumulate signed milliseconds");

    input.onGround = true;
    input.linearVelocitySquared = 0.05F;
    const auto waterAfterGroundMiss =
        legacyVehicleAdvanceSleepStep(100, input);
    require(
        waterAfterGroundMiss.has_value() &&
            waterAfterGroundMiss->restDurationMilliseconds == 112,
        "water fallback after the ground threshold changed");

    input.onGround = false;
    input.waterUnit = false;
    const auto airborne = legacyVehicleAdvanceSleepStep(100, input);
    require(
        airborne.has_value() &&
            airborne->restDurationMilliseconds == 0,
        "an unsupported rest predicate accumulated time");
}

void testSignedTimeAndValidation() {
    auto input = groundRestingInput();
    input.refreshDeltaMilliseconds = -12;
    const auto backwards = legacyVehicleAdvanceSleepStep(100, input);
    require(
        backwards.has_value() &&
            backwards->restDurationMilliseconds == 88 &&
            backwards->integratePhysics &&
            !backwards->clearDynamics,
        "finite signed negative scheduler behavior changed");

    input.refreshDeltaMilliseconds =
        std::numeric_limits<std::int64_t>::max();
    require(
        !legacyVehicleAdvanceSleepStep(
             legacyVehicleInitialRestDurationMilliseconds,
             input)
             .has_value(),
        "signed rest-duration overflow was accepted");

    input.linearVelocitySquared = -0.01F;
    require(
        !legacyVehicleAdvanceSleepStep(0, input).has_value(),
        "negative derived speed squared was accepted");

    input.linearVelocitySquared =
        std::numeric_limits<float>::infinity();
    require(
        !legacyVehicleAdvanceSleepStep(0, input).has_value(),
        "non-finite speed squared was accepted");

    input = groundRestingInput();
    input.wakeControlValues[2] =
        std::numeric_limits<float>::quiet_NaN();
    require(
        !legacyVehicleAdvanceSleepStep(0, input).has_value(),
        "non-finite wake control was accepted");
}

} // namespace

int main() {
    try {
        testRecoveredConstants();
        testThresholdCrossingAndSleepingStep();
        testControlsAndMotionWakeTheAccumulator();
        testGroundAndWaterRestPredicates();
        testSignedTimeAndValidation();
    }
    catch (const std::exception& error) {
        std::cerr << "Legacy vehicle sleep-state test failure: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "Legacy vehicle sleep-state tests passed.\n";
    return 0;
}
