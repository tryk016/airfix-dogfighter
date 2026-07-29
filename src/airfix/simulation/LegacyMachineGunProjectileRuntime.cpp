#include "airfix/simulation/LegacyMachineGunProjectileRuntime.hpp"

#include <algorithm>
#include <limits>

namespace airfix::simulation {

LegacyMachineGunShotRuntimeResult
legacyMachineGunAdvanceAndActivateShot(
    LegacyMachineGunFireState& fireState,
    const std::span<LegacyMachineGunProjectileSlot> projectileSlots,
    const LegacyMachineGunShotRuntimeInput& input) noexcept {
    const std::uint32_t effectiveTechLevel = std::min(
        input.requestedTechLevel,
        legacyMachineGunTechLevelCount - 1U);
    const LegacyMachineGunTechProfile techProfile =
        legacyMachineGunTechProfile(effectiveTechLevel);
    const auto fireStep = legacyMachineGunAdvanceFire(
        fireState,
        techProfile,
        input.deltaSeconds,
        input.parentAttached);
    if (!fireStep.has_value()) {
        return {};
    }

    // Native WpMGun commits these fields before CreatePrivateInstance.
    fireState = fireStep->state;
    if (!fireStep->projectile.has_value()) {
        return {
            .status = LegacyMachineGunShotRuntimeStatus::noShotDue,
            .projectile = std::nullopt,
        };
    }

    bool sawGenerationExhaustedSlot = false;
    std::size_t selectedSlot = projectileSlots.size();
    for (std::size_t index = 0U;
         index < projectileSlots.size() &&
             selectedSlot == projectileSlots.size();
         ++index) {
        const auto& slot = projectileSlots[index];
        if (!slot.state.active) {
            if (slot.generation ==
                std::numeric_limits<std::uint64_t>::max()) {
                sawGenerationExhaustedSlot = true;
            } else {
                selectedSlot = index;
            }
        }
    }

    if (selectedSlot == projectileSlots.size()) {
        return {
            .status =
                sawGenerationExhaustedSlot
                ? LegacyMachineGunShotRuntimeStatus::generationExhausted
                : LegacyMachineGunShotRuntimeStatus::capacityExhausted,
            .projectile = std::nullopt,
        };
    }

    // Native WpMGun reads attachment transforms and builds event 0xE2 only
    // after private instance creation succeeds.
    const auto& command = *fireStep->projectile;
    if (command.eventType != legacyMachineGunProjectileEvent ||
        input.rotatedMuzzleOffsets.size() !=
            fireState.barrelCount ||
        command.barrelIndex >= input.rotatedMuzzleOffsets.size()) {
        return {
            .status =
                LegacyMachineGunShotRuntimeStatus::activationRejected,
            .projectile = std::nullopt,
        };
    }

    const auto ammoProfile =
        legacyMachineGunAmmoProfile(effectiveTechLevel);
    if (!ammoProfile.has_value()) {
        return {
            .status =
                LegacyMachineGunShotRuntimeStatus::activationRejected,
            .projectile = std::nullopt,
        };
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
        return {
            .status =
                LegacyMachineGunShotRuntimeStatus::activationRejected,
            .projectile = std::nullopt,
        };
    }
    const auto initialState = legacyMachineGunProjectileInitialState(
        *payload, *ammoProfile);
    if (!initialState.has_value()) {
        return {
            .status =
                LegacyMachineGunShotRuntimeStatus::activationRejected,
            .projectile = std::nullopt,
        };
    }

    auto& slot = projectileSlots[selectedSlot];
    const std::uint64_t nextGeneration = slot.generation + 1U;
    slot = LegacyMachineGunProjectileSlot{
        .state = *initialState,
        .ammoProfile = *ammoProfile,
        .effectiveTechLevel = effectiveTechLevel,
        .generation = nextGeneration,
    };
    return {
        .status = LegacyMachineGunShotRuntimeStatus::activated,
        .projectile =
            LegacyMachineGunProjectileHandle{
                .slotIndex = selectedSlot,
                .generation = nextGeneration,
            },
    };
}

} // namespace airfix::simulation
