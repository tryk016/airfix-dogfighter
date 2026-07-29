#include "airfix/simulation/LegacyAircraftThrustState.hpp"

#include <cmath>
#include <limits>

namespace airfix::simulation {

std::optional<float> legacyAircraftAdvanceSmoothedThrust(
    const float currentSmoothedThrust,
    const float targetThrust,
    const bool engineStartTransitionActive) noexcept {
    if (!std::isfinite(currentSmoothedThrust) ||
        !std::isfinite(targetThrust)) {
        return std::nullopt;
    }

    const float difference = targetThrust - currentSmoothedThrust;
    const float change = engineStartTransitionActive
        ? difference * legacyAircraftEngineStartResponseFactor
        : (currentSmoothedThrust + legacyAircraftThrottleResponseOffset) *
            difference * legacyAircraftThrottleResponseFactor;
    const float next = currentSmoothedThrust + change;
    if (!std::isfinite(difference) ||
        !std::isfinite(change) ||
        !std::isfinite(next)) {
        return std::nullopt;
    }
    return next;
}

std::optional<LegacyAircraftThrustControlState>
legacyAircraftAdvanceThrustControl(
    LegacyAircraftThrustControlState current,
    const LegacyAircraftThrustControlInput input) noexcept {
    if (!std::isfinite(input.health) ||
        !std::isfinite(current.targetThrust) ||
        !std::isfinite(current.smoothedThrust)) {
        return std::nullopt;
    }

    float nextTarget = current.targetThrust;
    if (input.health > 0.0F) {
        if (!std::isfinite(current.thrustApply)) {
            return std::nullopt;
        }
        nextTarget = current.targetThrust + current.thrustApply;
        if (!std::isfinite(nextTarget)) {
            return std::nullopt;
        }
        if (nextTarget > 1.0F) {
            nextTarget = 1.0F;
        } else if (nextTarget < 0.0F) {
            nextTarget = 0.0F;
        }
    }

    const auto nextSmoothed = legacyAircraftAdvanceSmoothedThrust(
        current.smoothedThrust,
        nextTarget,
        input.engineStartTransitionActive);
    if (!nextSmoothed.has_value()) {
        return std::nullopt;
    }

    return LegacyAircraftThrustControlState{
        .thrustApply = current.thrustApply,
        .targetThrust = nextTarget,
        .smoothedThrust = *nextSmoothed,
    };
}

std::optional<float> legacyAircraftApplyCollisionThrustDamage(
    const float currentThrustIntegrity,
    const std::span<const LegacyAircraftCollisionSample> samples) noexcept {
    if (!std::isfinite(currentThrustIntegrity) ||
        samples.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }

    float fourthPowerSum = 0.0F;
    std::int32_t qualifiedCount = 0;
    for (const LegacyAircraftCollisionSample& sample : samples) {
        if (!std::isfinite(sample.normalVelocityDot)) {
            return std::nullopt;
        }
        if (sample.normalVelocityDot >
                legacyAircraftCollisionNormalDotThreshold) {
            if (!std::isfinite(sample.collisionScalar)) {
                return std::nullopt;
            }

            const float gatedImpulse =
                sample.normalVelocityDot * sample.collisionScalar;
            if (!std::isfinite(gatedImpulse)) {
                return std::nullopt;
            }
            if (gatedImpulse > legacyAircraftCollisionImpulseThreshold) {
                const float square =
                    sample.normalVelocityDot * sample.normalVelocityDot;
                const float cube =
                    square * sample.normalVelocityDot;
                const float fourthPower =
                    cube * sample.normalVelocityDot;
                const float nextSum = fourthPowerSum + fourthPower;
                if (!std::isfinite(square) ||
                    !std::isfinite(cube) ||
                    !std::isfinite(fourthPower) ||
                    !std::isfinite(nextSum)) {
                    return std::nullopt;
                }

                fourthPowerSum = nextSum;
                ++qualifiedCount;
            }
        }
    }

    if (qualifiedCount == 0) {
        return currentThrustIntegrity;
    }

    const float reciprocalQualifiedCount =
        1.0F / static_cast<float>(qualifiedCount);
    const float averageFourthPower =
        reciprocalQualifiedCount * fourthPowerSum;
    const float next =
        currentThrustIntegrity -
        averageFourthPower * legacyAircraftCollisionDamageScale;
    if (!std::isfinite(reciprocalQualifiedCount) ||
        !std::isfinite(averageFourthPower) ||
        !std::isfinite(next)) {
        return std::nullopt;
    }
    return next;
}

std::optional<float> legacyAircraftRecoverThrustIntegrity(
    const float currentThrustIntegrity,
    const float deltaSeconds,
    const std::int32_t recoveryRandomSample) noexcept {
    if (!std::isfinite(currentThrustIntegrity)) {
        return std::nullopt;
    }

    float next = currentThrustIntegrity;
    if (currentThrustIntegrity < legacyAircraftInitialThrustIntegrity) {
        if (!std::isfinite(deltaSeconds) ||
            recoveryRandomSample < 0 ||
            recoveryRandomSample >
                legacyAircraftMaximumRecoveryRandomSample) {
            return std::nullopt;
        }

        const float recovery =
            static_cast<float>(recoveryRandomSample) *
            deltaSeconds *
            legacyAircraftThrustRecoveryRandomScale;
        next += recovery;
        if (!std::isfinite(recovery) || !std::isfinite(next)) {
            return std::nullopt;
        }
    }

    if (next > legacyAircraftInitialThrustIntegrity) {
        return legacyAircraftInitialThrustIntegrity;
    }
    if (next < 0.0F) {
        return 0.0F;
    }
    return next;
}

} // namespace airfix::simulation
