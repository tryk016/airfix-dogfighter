#pragma once

#include "airfix/simulation/LegacyMachineGunFireState.hpp"
#include "airfix/simulation/LegacyMachineGunProjectile.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace airfix::simulation {

struct LegacyMachineGunShotPreparationInput final {
    LegacyMachineGunFireState fireState{};
    std::uint32_t requestedTechLevel{};
    float deltaSeconds{};
    bool parentAttached{};

    LegacyMachineGunVector3 creatorPosition{};
    // One already-rotated authored muzzle offset per attachment barrel. The
    // span is read only when the timing transition emits a projectile.
    std::span<const LegacyMachineGunVector3> rotatedMuzzleOffsets;
    LegacyMachineGunVector3 aimPointRelativeToCreator{};
    std::optional<LegacyMachineGunTargetLead> targetLead;
    std::int32_t roomId{};
    std::uint32_t creatorUid{};
};

struct LegacyMachineGunPreparedShot final {
    // WpMGun::SetTechLevel caps every requested value above four.
    std::uint32_t effectiveTechLevel{};
    LegacyMachineGunProjectileCommand command{};
    LegacyMachineGunAmmoProfile ammoProfile{};
    LegacyMachineGunProjectileSpawnPayload payload{};
};

struct LegacyMachineGunShotPreparationStep final {
    // Native cadence, barrel, and ammunition state advance before private ammo
    // allocation. A later allocation failure therefore must not roll this
    // state back.
    LegacyMachineGunFireState fireState{};
    std::optional<LegacyMachineGunPreparedShot> shot;
};

// Eagerly composes the recovered WpMGun timing transition with the selected
// muzzle and complete event-0xE2 payload. This pure preparation helper
// allocates nothing and does not create or activate an ammo object. Consumers
// that model native allocation order must commit fireState before acquiring
// capacity; LegacyMachineGunProjectileRuntime provides that boundary without
// reading payload-only input before capacity succeeds. Invalid adapter input
// fails closed.
[[nodiscard]] std::optional<LegacyMachineGunShotPreparationStep>
legacyMachineGunPrepareShot(
    const LegacyMachineGunShotPreparationInput& input) noexcept;

} // namespace airfix::simulation
