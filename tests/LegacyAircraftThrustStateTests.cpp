#include "airfix/simulation/LegacyAircraftThrustState.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using airfix::simulation::LegacyAircraftCollisionSample;
using airfix::simulation::legacyAircraftAdvanceSmoothedThrust;
using airfix::simulation::legacyAircraftApplyCollisionThrustDamage;
using airfix::simulation::legacyAircraftCollisionDamageScale;
using airfix::simulation::legacyAircraftCollisionImpulseThreshold;
using airfix::simulation::legacyAircraftCollisionNormalDotThreshold;
using airfix::simulation::legacyAircraftEngineStartDurationSeconds;
using airfix::simulation::legacyAircraftEngineStartResponseFactor;
using airfix::simulation::legacyAircraftEngineStartThrottleThreshold;
using airfix::simulation::legacyAircraftInitialThrustIntegrity;
using airfix::simulation::legacyAircraftMaximumRecoveryRandomSample;
using airfix::simulation::legacyAircraftRecoverThrustIntegrity;
using airfix::simulation::legacyAircraftThrottleResponseFactor;
using airfix::simulation::legacyAircraftThrottleResponseOffset;
using airfix::simulation::legacyAircraftThrustRecoveryRandomScale;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(
    const float actual,
    const float expected,
    const float tolerance,
    const std::string& message) {
    require(
        std::fabs(actual - expected) <= tolerance,
        message + ": expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
}

void testRecoveredConstants() {
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftThrottleResponseOffset) == 0x3E99999AU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftThrottleResponseFactor) == 0x3CA3D70AU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftEngineStartResponseFactor) == 0x39D1B717U);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftEngineStartThrottleThreshold) == 0x3A83126FU);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftEngineStartDurationSeconds) == 0x40800000U);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftInitialThrustIntegrity) == 0x3F800000U);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftCollisionNormalDotThreshold) == 0x3F666666U);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftCollisionImpulseThreshold) == 0x40000000U);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftCollisionDamageScale) == 0x3F000000U);
    static_assert(
        std::bit_cast<std::uint32_t>(
            legacyAircraftThrustRecoveryRandomScale) == 0x38000100U);
    static_assert(legacyAircraftMaximumRecoveryRandomSample == 32767);
}

void testSmoothedThrustBranches() {
    const auto regular =
        legacyAircraftAdvanceSmoothedThrust(0.2F, 0.8F, false);
    require(regular.has_value(), "regular smoothing rejected finite input");
    requireNear(
        *regular,
        0.206F,
        1.0e-6F,
        "regular smoothing recurrence changed");

    const auto starting =
        legacyAircraftAdvanceSmoothedThrust(0.2F, 0.8F, true);
    require(starting.has_value(), "start smoothing rejected finite input");
    requireNear(
        *starting,
        0.20024F,
        1.0e-7F,
        "engine-start smoothing recurrence changed");

    const auto unchanged =
        legacyAircraftAdvanceSmoothedThrust(0.7F, 0.7F, false);
    require(
        unchanged.has_value() && *unchanged == 0.7F,
        "equal target did not preserve smoothed thrust");

    const auto unclamped =
        legacyAircraftAdvanceSmoothedThrust(2.0F, 3.0F, false);
    require(
        unclamped.has_value() && *unclamped > 1.0F,
        "a non-native final clamp was added to smoothed thrust");
}

void testCollisionQualificationAndDamage() {
    constexpr std::array atThresholds{
        LegacyAircraftCollisionSample{
            .normalVelocityDot =
                legacyAircraftCollisionNormalDotThreshold,
            .collisionScalar = 3.0F,
        },
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 1.0F,
            .collisionScalar =
                legacyAircraftCollisionImpulseThreshold,
        },
    };
    const auto unchanged = legacyAircraftApplyCollisionThrustDamage(
        legacyAircraftInitialThrustIntegrity,
        atThresholds);
    require(
        unchanged.has_value() &&
            *unchanged == legacyAircraftInitialThrustIntegrity,
        "a strict collision threshold became inclusive");

    constexpr std::array oneQualified{
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 1.0F,
            .collisionScalar = 2.1F,
        },
    };
    const auto one = legacyAircraftApplyCollisionThrustDamage(
        legacyAircraftInitialThrustIntegrity,
        oneQualified);
    require(
        one.has_value() && *one == 0.5F,
        "one qualified collision did not subtract half its fourth power");

    constexpr std::array averaged{
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 1.0F,
            .collisionScalar = 2.1F,
        },
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 2.0F,
            .collisionScalar = 1.1F,
        },
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 0.5F,
            .collisionScalar = 100.0F,
        },
    };
    const auto multiple = legacyAircraftApplyCollisionThrustDamage(
        legacyAircraftInitialThrustIntegrity,
        averaged);
    require(
        multiple.has_value() && *multiple == -3.25F,
        "qualified fourth powers were not averaged before subtraction");

    constexpr std::array ignoredScalar{
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 0.5F,
            .collisionScalar =
                std::numeric_limits<float>::quiet_NaN(),
        },
    };
    const auto ignored = legacyAircraftApplyCollisionThrustDamage(
        legacyAircraftInitialThrustIntegrity,
        ignoredScalar);
    require(
        ignored.has_value() &&
            *ignored == legacyAircraftInitialThrustIntegrity,
        "the normal-dot rejection gate inspected a skipped scalar");
}

void testRecoveryAndOrdering() {
    const auto recovered = legacyAircraftRecoverThrustIntegrity(
        0.5F,
        0.012F,
        legacyAircraftMaximumRecoveryRandomSample);
    require(recovered.has_value(), "valid recovery input was rejected");
    requireNear(
        *recovered,
        0.5120004F,
        1.0e-6F,
        "random recovery scale or multiplication order changed");

    const auto clampedHigh = legacyAircraftRecoverThrustIntegrity(
        0.999F,
        1.0F,
        legacyAircraftMaximumRecoveryRandomSample);
    require(
        clampedHigh.has_value() &&
            *clampedHigh == legacyAircraftInitialThrustIntegrity,
        "recovery did not clamp a threshold crossing to one");

    const auto clampedLow =
        legacyAircraftRecoverThrustIntegrity(-2.0F, 0.012F, 0);
    require(
        clampedLow.has_value() && *clampedLow == 0.0F,
        "post-collision refresh did not clamp a negative factor");

    const auto skippedInputs = legacyAircraftRecoverThrustIntegrity(
        2.0F,
        std::numeric_limits<float>::quiet_NaN(),
        -1);
    require(
        skippedInputs.has_value() &&
            *skippedInputs == legacyAircraftInitialThrustIntegrity,
        "the factor-at-or-above-one path inspected skipped recovery inputs");

    constexpr std::array impact{
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 1.0F,
            .collisionScalar = 2.1F,
        },
    };
    const auto damaged = legacyAircraftApplyCollisionThrustDamage(
        legacyAircraftInitialThrustIntegrity,
        impact);
    require(damaged.has_value(), "collision ordering fixture failed");
    const auto sameRefresh = legacyAircraftRecoverThrustIntegrity(
        *damaged,
        0.012F,
        0);
    require(
        sameRefresh.has_value() && *sameRefresh == 0.5F,
        "post-collision recovery did not consume the degraded factor");
}

void testValidation() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    require(
        !legacyAircraftAdvanceSmoothedThrust(nan, 0.5F, false)
             .has_value(),
        "non-finite smoothed thrust was accepted");
    require(
        !legacyAircraftAdvanceSmoothedThrust(0.5F, infinity, false)
             .has_value(),
        "non-finite target thrust was accepted");

    constexpr std::array invalidCollision{
        LegacyAircraftCollisionSample{
            .normalVelocityDot = 1.0F,
            .collisionScalar =
                std::numeric_limits<float>::infinity(),
        },
    };
    require(
        !legacyAircraftApplyCollisionThrustDamage(
             legacyAircraftInitialThrustIntegrity,
             invalidCollision)
             .has_value(),
        "non-finite collision input was accepted");
    require(
        !legacyAircraftRecoverThrustIntegrity(nan, 0.012F, 0)
             .has_value(),
        "non-finite thrust integrity was accepted");
    require(
        !legacyAircraftRecoverThrustIntegrity(0.5F, infinity, 0)
             .has_value(),
        "non-finite recovery delta was accepted");
    require(
        !legacyAircraftRecoverThrustIntegrity(0.5F, 0.012F, -1)
             .has_value(),
        "negative recovery sample was accepted");
    require(
        !legacyAircraftRecoverThrustIntegrity(
             0.5F,
             0.012F,
             legacyAircraftMaximumRecoveryRandomSample + 1)
             .has_value(),
        "out-of-range recovery sample was accepted");
}

} // namespace

int main() {
    try {
        testRecoveredConstants();
        testSmoothedThrustBranches();
        testCollisionQualificationAndDamage();
        testRecoveryAndOrdering();
        testValidation();
    }
    catch (const std::exception& error) {
        std::cerr << "Legacy aircraft thrust-state test failure: "
                  << error.what() << '\n';
        return 1;
    }

    std::cout << "Legacy aircraft thrust-state tests passed.\n";
    return 0;
}
