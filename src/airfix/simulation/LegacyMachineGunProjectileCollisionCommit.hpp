#pragma once

#include "airfix/simulation/LegacyMachineGunProjectile.hpp"
#include "airfix/simulation/LegacyProjectileCollisionLoop.hpp"

#include <cstdint>
#include <optional>

namespace airfix::simulation {

enum class LegacyMachineGunProjectileCollisionCommitStatus : std::uint8_t {
    committed,
    invalidInput,
    incompleteCollision,
    rejectedTerminalDecision,
};

struct LegacyMachineGunProjectileCollisionCommitResult final {
    LegacyMachineGunProjectileCollisionCommitStatus status{
        LegacyMachineGunProjectileCollisionCommitStatus::
            invalidInput};
    LegacyMachineGunProjectileState state{};
    LegacyProjectileCollisionOutcome outcome{
        LegacyProjectileCollisionOutcome::advanceNoHit};
    std::optional<LegacyMachineGunDamageCommand> damage;
    std::optional<LegacyMachineGunSurfaceContactResult> surface;

    [[nodiscard]] constexpr bool committed() const noexcept {
        return status ==
            LegacyMachineGunProjectileCollisionCommitStatus::committed;
    }
};

// Commits one completed shared projectile collision loop into the portable
// WpMGunAmmo state and its bounded terminal commands. Damage and ricochet
// values are data requests only; live event/effect dispatch remains outside
// this allocation-free reducer.
//
// An inactive or malformed projectile, an incomplete loop, a remaining portal
// decision, or an inconsistent terminal payload fails closed and leaves the
// returned state equal to current.
[[nodiscard]] LegacyMachineGunProjectileCollisionCommitResult
commitLegacyMachineGunProjectileCollision(
    const LegacyMachineGunProjectileState& current,
    const LegacyMachineGunAmmoProfile& profile,
    const LegacyProjectileCollisionLoopResult& collision) noexcept;

} // namespace airfix::simulation
