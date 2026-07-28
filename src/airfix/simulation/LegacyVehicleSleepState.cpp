#include "airfix/simulation/LegacyVehicleSleepState.hpp"

#include <cmath>
#include <limits>

namespace airfix::simulation {
namespace {

[[nodiscard]] bool finiteWakeControls(
    const std::array<float, 5U>& controls) noexcept {
    for (const float value : controls) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool hasWakeControl(
    const std::array<float, 5U>& controls) noexcept {
    for (const float value : controls) {
        if (value != 0.0F) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool additionWouldOverflow(
    const std::int64_t left, const std::int64_t right) noexcept {
    if (right > 0) {
        return left > std::numeric_limits<std::int64_t>::max() - right;
    }
    if (right < 0) {
        return left < std::numeric_limits<std::int64_t>::min() - right;
    }
    return false;
}

} // namespace

std::optional<LegacyVehicleSleepStepResult>
legacyVehicleAdvanceSleepStep(
    const std::int64_t currentRestDurationMilliseconds,
    const LegacyVehicleSleepStepInput& input) noexcept {
    if (currentRestDurationMilliseconds >=
        legacyVehicleSleepThresholdMilliseconds) {
        return LegacyVehicleSleepStepResult{
            .restDurationMilliseconds =
                currentRestDurationMilliseconds,
            .integratePhysics = false,
            .clearDynamics = false,
        };
    }

    if (!finiteWakeControls(input.wakeControlValues) ||
        !std::isfinite(input.linearVelocitySquared) ||
        input.linearVelocitySquared < 0.0F) {
        return std::nullopt;
    }

    const bool lowGroundMotion =
        input.onGround &&
        input.linearVelocitySquared <
            legacyVehicleGroundRestSpeedSquaredThreshold;
    const bool lowWaterMotion =
        input.waterUnit &&
        input.linearVelocitySquared <
            legacyVehicleWaterRestSpeedSquaredThreshold;
    const bool accumulatingRest =
        !hasWakeControl(input.wakeControlValues) &&
        (lowGroundMotion || lowWaterMotion);

    std::int64_t nextRestDuration = 0;
    if (accumulatingRest) {
        if (additionWouldOverflow(
                currentRestDurationMilliseconds,
                input.refreshDeltaMilliseconds)) {
            return std::nullopt;
        }
        nextRestDuration =
            currentRestDurationMilliseconds +
            input.refreshDeltaMilliseconds;
    }

    return LegacyVehicleSleepStepResult{
        .restDurationMilliseconds = nextRestDuration,
        .integratePhysics = true,
        .clearDynamics =
            nextRestDuration >=
            legacyVehicleSleepThresholdMilliseconds,
    };
}

} // namespace airfix::simulation
