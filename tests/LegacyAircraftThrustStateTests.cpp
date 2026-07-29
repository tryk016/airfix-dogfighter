#include "airfix/simulation/LegacyAircraftThrustState.hpp"
#include "airfix/simulation/LegacyVehicleSleepState.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using airfix::simulation::LegacyAircraftCollisionSample;
using airfix::simulation::LegacyAircraftThrustControlInput;
using airfix::simulation::LegacyAircraftThrustControlState;
using airfix::simulation::LegacyVehicleSleepStepInput;
using airfix::simulation::legacyAircraftAdvanceSmoothedThrust;
using airfix::simulation::legacyAircraftAdvanceThrustControl;
using airfix::simulation::legacyVehicleAdvanceSleepStep;
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

static_assert(noexcept(legacyAircraftAdvanceThrustControl({}, {})));

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

void requireBits(
    const float actual,
    const std::uint32_t expected,
    const std::string& message) {
    const auto actualBits = std::bit_cast<std::uint32_t>(actual);
    require(
        actualBits == expected,
        message + ": expected bits " + std::to_string(expected) +
            ", got " + std::to_string(actualBits));
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

void testOrderedThrustControlStep() {
    constexpr LegacyAircraftThrustControlState initial{
        .thrustApply = 0.1F,
        .targetThrust = 0.5F,
        .smoothedThrust = 0.2F,
    };
    const auto first = legacyAircraftAdvanceThrustControl(
        initial,
        {
            .health = 1.0F,
            .engineStartTransitionActive = false,
        });
    const auto expectedFirstSmoothed =
        legacyAircraftAdvanceSmoothedThrust(0.2F, 0.6F, false);
    require(
        first.has_value() && expectedFirstSmoothed.has_value() &&
            first->thrustApply == 0.1F &&
            first->targetThrust == 0.6F &&
            first->smoothedThrust == *expectedFirstSmoothed,
        "target update did not precede same-step smoothing");

    const auto second = legacyAircraftAdvanceThrustControl(
        *first,
        {
            .health = 1.0F,
            .engineStartTransitionActive = false,
        });
    const float expectedSecondTarget =
        first->targetThrust + first->thrustApply;
    const auto expectedSecondSmoothed =
        legacyAircraftAdvanceSmoothedThrust(
            first->smoothedThrust,
            expectedSecondTarget,
            false);
    require(
        second.has_value() && expectedSecondSmoothed.has_value() &&
            second->thrustApply == 0.1F &&
            second->targetThrust == expectedSecondTarget &&
            second->smoothedThrust == *expectedSecondSmoothed,
        "persistent thrust apply did not advance the next force step");

    const auto starting = legacyAircraftAdvanceThrustControl(
        initial,
        {
            .health = 1.0F,
            .engineStartTransitionActive = true,
        });
    const auto expectedStarting =
        legacyAircraftAdvanceSmoothedThrust(0.2F, 0.6F, true);
    require(
        starting.has_value() && expectedStarting.has_value() &&
            starting->targetThrust == 0.6F &&
            starting->smoothedThrust == *expectedStarting,
        "engine-start smoothing did not consume the updated target");
}

void testThrustControlClampAndHealthGate() {
    const auto clampedHigh = legacyAircraftAdvanceThrustControl(
        {
            .thrustApply = 0.2F,
            .targetThrust = 0.9F,
            .smoothedThrust = 0.2F,
        },
        {
            .health = 1.0F,
            .engineStartTransitionActive = false,
        });
    require(
        clampedHigh.has_value() &&
            clampedHigh->targetThrust == 1.0F,
        "positive target update did not clamp to one");

    const auto clampedLow = legacyAircraftAdvanceThrustControl(
        {
            .thrustApply = -0.2F,
            .targetThrust = 0.1F,
            .smoothedThrust = 0.2F,
        },
        {
            .health = 1.0F,
            .engineStartTransitionActive = false,
        });
    require(
        clampedLow.has_value() &&
            clampedLow->targetThrust == 0.0F,
        "negative target update did not clamp to zero");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const LegacyAircraftThrustControlState gated{
        .thrustApply = nan,
        .targetThrust = 0.8F,
        .smoothedThrust = 0.2F,
    };
    const auto zeroHealth = legacyAircraftAdvanceThrustControl(
        gated,
        {
            .health = 0.0F,
            .engineStartTransitionActive = false,
        });
    const auto expected =
        legacyAircraftAdvanceSmoothedThrust(0.2F, 0.8F, false);
    require(
        zeroHealth.has_value() && expected.has_value() &&
            std::isnan(zeroHealth->thrustApply) &&
            zeroHealth->targetThrust == 0.8F &&
            zeroHealth->smoothedThrust == *expected,
        "zero health did not skip apply while retaining smoothing");

    const auto negativeHealth = legacyAircraftAdvanceThrustControl(
        gated,
        {
            .health = -1.0F,
            .engineStartTransitionActive = true,
        });
    const auto expectedStarting =
        legacyAircraftAdvanceSmoothedThrust(0.2F, 0.8F, true);
    require(
        negativeHealth.has_value() &&
            expectedStarting.has_value() &&
            negativeHealth->targetThrust == 0.8F &&
            negativeHealth->smoothedThrust ==
                *expectedStarting,
        "negative health skipped the unconditional smoothing path");

    require(
        !legacyAircraftAdvanceThrustControl(
             gated,
             {
                 .health = 1.0F,
                 .engineStartTransitionActive = false,
             })
             .has_value(),
        "positive health accepted a non-finite apply field");
}

void testThrustControlPortableVectors() {
    constexpr LegacyAircraftThrustControlState initial{
        .thrustApply = std::bit_cast<float>(0x3CA3D70BU),
        .targetThrust = std::bit_cast<float>(0x3F4CCCCDU),
        .smoothedThrust = std::bit_cast<float>(0x3E4CCCCDU),
    };

    const auto healthZero = legacyAircraftAdvanceThrustControl(
        initial,
        {
            .health = 0.0F,
            .engineStartTransitionActive = false,
        });
    require(healthZero.has_value(), "health-zero vector was rejected");
    requireBits(
        healthZero->targetThrust,
        0x3F4CCCCDU,
        "health-zero vector changed target");
    requireBits(
        healthZero->smoothedThrust,
        0x3E52F1AAU,
        "health-zero smoothing vector changed");

    const auto normal = legacyAircraftAdvanceThrustControl(
        initial,
        {
            .health = 1.0F,
            .engineStartTransitionActive = false,
        });
    require(normal.has_value(), "normal portable vector was rejected");
    requireBits(
        normal->targetThrust,
        0x3F51EB85U,
        "normal target vector changed");
    requireBits(
        normal->smoothedThrust,
        0x3E532618U,
        "normal smoothing vector changed");

    const auto starting = legacyAircraftAdvanceThrustControl(
        initial,
        {
            .health = 1.0F,
            .engineStartTransitionActive = true,
        });
    require(starting.has_value(), "starting portable vector was rejected");
    requireBits(
        starting->targetThrust,
        0x3F51EB85U,
        "starting target vector changed");
    requireBits(
        starting->smoothedThrust,
        0x3E4D0DD0U,
        "starting smoothing vector changed");

    constexpr LegacyAircraftThrustControlState phaseOrdering{
        .thrustApply = 0.0F,
        .targetThrust = 1.0F,
        .smoothedThrust = 0.0F,
    };
    const auto beforeSlot44 = legacyAircraftAdvanceThrustControl(
        phaseOrdering,
        {
            .health = 1.0F,
            .engineStartTransitionActive = false,
        });
    require(beforeSlot44.has_value(), "phase-order vector was rejected");
    requireBits(
        beforeSlot44->smoothedThrust,
        0x3BC49BA6U,
        "slot 45 did not use the entry engine phase");
    const auto afterSlot44 = legacyAircraftAdvanceThrustControl(
        *beforeSlot44,
        {
            .health = 1.0F,
            .engineStartTransitionActive = true,
        });
    require(afterSlot44.has_value(), "next phase-order vector was rejected");
    requireBits(
        afterSlot44->smoothedThrust,
        0x3BD1A2F6U,
        "next slot 45 did not observe the updated engine phase");
}

void testSleepThresholdCompositionUsesEntryDecision() {
    constexpr LegacyVehicleSleepStepInput input{
        .wakeControlValues = {},
        .linearVelocitySquared = 0.0F,
        .onGround = true,
        .waterUnit = false,
        .refreshDeltaMilliseconds = 12,
    };
    const auto threshold = legacyVehicleAdvanceSleepStep(1999, input);
    require(
        threshold.has_value() &&
            threshold->restDurationMilliseconds == 2011 &&
            threshold->integratePhysics &&
            threshold->clearDynamics && threshold->sleeping(),
        "sleep threshold fixture did not preserve the entry decision");

    constexpr LegacyAircraftThrustControlState initial{
        .thrustApply = 0.0F,
        .targetThrust = 0.0F,
        .smoothedThrust = std::bit_cast<float>(0x3E4CCCCDU),
    };
    const auto thresholdStep =
        threshold->integratePhysics
        ? legacyAircraftAdvanceThrustControl(
              initial,
              {
                  .health = 1.0F,
                  .engineStartTransitionActive = false,
              })
        : std::optional<LegacyAircraftThrustControlState>{initial};
    require(
        thresholdStep.has_value(),
        "threshold-crossing slot 45 was rejected");
    requireBits(
        thresholdStep->smoothedThrust,
        0x3E4AC083U,
        "post-threshold sleeping state incorrectly skipped current slot 45");

    const auto sleeping =
        legacyVehicleAdvanceSleepStep(
            threshold->restDurationMilliseconds,
            {
                .wakeControlValues =
                    {
                        std::numeric_limits<float>::quiet_NaN(),
                    },
                .linearVelocitySquared =
                    std::numeric_limits<float>::quiet_NaN(),
                .onGround = false,
                .waterUnit = false,
                .refreshDeltaMilliseconds =
                    std::numeric_limits<std::int64_t>::max(),
            });
    require(
        sleeping.has_value() && !sleeping->integratePhysics,
        "next sleeping refresh did not skip the active path");
    auto skippedState = *thresholdStep;
    if (sleeping->integratePhysics) {
        const auto unexpected = legacyAircraftAdvanceThrustControl(
            skippedState,
            {
                .health = 1.0F,
                .engineStartTransitionActive = false,
            });
        require(unexpected.has_value(), "unexpected active step failed");
        skippedState = *unexpected;
    }
    require(
        skippedState == *thresholdStep,
        "skipped slot 45 changed the caller-owned state");
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
    require(
        !legacyAircraftAdvanceThrustControl(
             {
                 .thrustApply = 0.0F,
                 .targetThrust = 0.5F,
                 .smoothedThrust = 0.2F,
             },
             {
                 .health = nan,
                 .engineStartTransitionActive = false,
             })
             .has_value(),
        "non-finite health was accepted");
    require(
        !legacyAircraftAdvanceThrustControl(
             {
                 .thrustApply = 0.0F,
                 .targetThrust = infinity,
                 .smoothedThrust = 0.2F,
             },
             {
                 .health = 1.0F,
                 .engineStartTransitionActive = false,
             })
             .has_value(),
        "non-finite target state was accepted");
    require(
        !legacyAircraftAdvanceThrustControl(
             {
                 .thrustApply =
                     std::numeric_limits<float>::max(),
                 .targetThrust =
                     std::numeric_limits<float>::max(),
                 .smoothedThrust = 0.2F,
             },
             {
                 .health = 1.0F,
                 .engineStartTransitionActive = false,
             })
             .has_value(),
        "overflowing target update was accepted");

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
        testOrderedThrustControlStep();
        testThrustControlClampAndHealthGate();
        testThrustControlPortableVectors();
        testSleepThresholdCompositionUsesEntryDecision();
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
