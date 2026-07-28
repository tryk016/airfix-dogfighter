#include "airfix/simulation/LegacyMachineGunShotCoordinator.hpp"

#include <algorithm>

namespace airfix::simulation {

std::optional<LegacyMachineGunShotPreparationStep>
legacyMachineGunPrepareShot(
    const LegacyMachineGunShotPreparationInput& input) noexcept {
    const std::uint32_t effectiveTechLevel = std::min(
        input.requestedTechLevel,
        legacyMachineGunTechLevelCount - 1U);
    const LegacyMachineGunTechProfile techProfile =
        legacyMachineGunTechProfile(effectiveTechLevel);
    const auto fireStep = legacyMachineGunAdvanceFire(
        input.fireState,
        techProfile,
        input.deltaSeconds,
        input.parentAttached);
    if (!fireStep.has_value()) {
        return std::nullopt;
    }

    LegacyMachineGunShotPreparationStep result{
        .fireState = fireStep->state,
        .shot = std::nullopt,
    };
    if (!fireStep->projectile.has_value()) {
        return result;
    }

    const auto& command = *fireStep->projectile;
    if (command.eventType != legacyMachineGunProjectileEvent ||
        input.rotatedMuzzleOffsets.size() !=
            input.fireState.barrelCount ||
        command.barrelIndex >= input.rotatedMuzzleOffsets.size()) {
        return std::nullopt;
    }

    const auto ammoProfile =
        legacyMachineGunAmmoProfile(effectiveTechLevel);
    if (!ammoProfile.has_value()) {
        return std::nullopt;
    }
    const auto payload = legacyMachineGunProjectileSpawnPayload({
        .creatorPosition = input.creatorPosition,
        .rotatedMuzzleOffset =
            input.rotatedMuzzleOffsets[command.barrelIndex],
        .aimPointRelativeToCreator =
            input.aimPointRelativeToCreator,
        .targetLead = input.targetLead,
        .projectileSpeed = command.projectileSpeed,
        .roomId = input.roomId,
        .creatorUid = input.creatorUid,
    });
    if (!payload.has_value()) {
        return std::nullopt;
    }

    result.shot = LegacyMachineGunPreparedShot{
        .effectiveTechLevel = effectiveTechLevel,
        .command = command,
        .ammoProfile = *ammoProfile,
        .payload = *payload,
    };
    return result;
}

} // namespace airfix::simulation
