#include "airfix/simulation/LegacyMachineGunFireState.hpp"

#include <array>
#include <cmath>

namespace airfix::simulation {

namespace {

constexpr std::array<LegacyMachineGunTechProfile, 5U> profiles{{
    {.shotIntervalSeconds = 0.12F, .projectileSpeed = 40.0F},
    {.shotIntervalSeconds = 0.11F, .projectileSpeed = 55.0F},
    {.shotIntervalSeconds = 0.10F, .projectileSpeed = 70.0F},
    {.shotIntervalSeconds = 0.09F, .projectileSpeed = 85.0F},
    {.shotIntervalSeconds = 0.08F, .projectileSpeed = 100.0F},
}};

[[nodiscard]] bool validProfile(
    const LegacyMachineGunTechProfile& profile) noexcept {
    return std::isfinite(profile.shotIntervalSeconds) &&
        profile.shotIntervalSeconds > 0.0F &&
        std::isfinite(profile.projectileSpeed) &&
        profile.projectileSpeed > 0.0F;
}

} // namespace

LegacyMachineGunTechProfile legacyMachineGunTechProfile(
    const std::uint32_t techLevel) noexcept {
    const std::size_t index =
        techLevel < legacyMachineGunTechLevelCount
        ? static_cast<std::size_t>(techLevel)
        : profiles.size() - 1U;
    return profiles[index];
}

std::optional<LegacyMachineGunFireState>
legacyMachineGunInitialFireState(
    const LegacyMachineGunTechProfile& profile,
    const std::uint32_t barrelCount,
    const std::uint32_t ammunition) noexcept {
    if (!validProfile(profile) || barrelCount == 0U) {
        return std::nullopt;
    }

    return LegacyMachineGunFireState{
        .accumulatedSeconds = profile.shotIntervalSeconds,
        .ammunition = ammunition,
        .barrelIndex = 0U,
        .barrelCount = barrelCount,
        .firing = false,
    };
}

std::optional<LegacyMachineGunFireStep>
legacyMachineGunAdvanceFire(
    const LegacyMachineGunFireState& current,
    const LegacyMachineGunTechProfile& profile,
    const float deltaSeconds,
    const bool parentAttached) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return std::nullopt;
    }

    LegacyMachineGunFireStep step{.state = current};
    if (deltaSeconds == 0.0F) {
        return step;
    }
    if (!std::isfinite(current.accumulatedSeconds)) {
        return std::nullopt;
    }

    step.state.accumulatedSeconds += deltaSeconds;
    if (!std::isfinite(step.state.accumulatedSeconds)) {
        return std::nullopt;
    }
    if (!parentAttached) {
        return step;
    }
    if (!std::isfinite(profile.shotIntervalSeconds) ||
        profile.shotIntervalSeconds <= 0.0F) {
        return std::nullopt;
    }

    const float cadenceFactor =
        current.ammunition == 0U
        ? legacyMachineGunZeroAmmunitionCadenceFactor
        : 1.0F;
    const float threshold =
        cadenceFactor * profile.shotIntervalSeconds;
    if (!std::isfinite(threshold)) {
        return std::nullopt;
    }

    if (!current.firing) {
        if (step.state.accumulatedSeconds > threshold) {
            step.state.accumulatedSeconds = threshold;
        }
        return step;
    }
    if (step.state.accumulatedSeconds < threshold) {
        return step;
    }
    if (!std::isfinite(profile.projectileSpeed) ||
        profile.projectileSpeed <= 0.0F ||
        current.barrelCount == 0U ||
        current.barrelIndex >= current.barrelCount) {
        return std::nullopt;
    }

    step.state.accumulatedSeconds -= threshold;
    if (!std::isfinite(step.state.accumulatedSeconds)) {
        return std::nullopt;
    }

    step.projectile = LegacyMachineGunProjectileCommand{
        .eventType = legacyMachineGunProjectileEvent,
        .barrelIndex = current.barrelIndex,
        .projectileSpeed = profile.projectileSpeed,
    };

    const std::uint32_t nextBarrel = current.barrelIndex + 1U;
    step.state.barrelIndex =
        nextBarrel < current.barrelCount ? nextBarrel : 0U;
    if (current.ammunition != 0U) {
        --step.state.ammunition;
    }
    return step;
}

} // namespace airfix::simulation
