#pragma once

#include "airfix/simulation/LegacyMachineGunShotCoordinator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::simulation {

// Inputs sampled by the single simulation owner for one weapon refresh. The
// mutable fire state is supplied separately so the runtime can guarantee the
// recovered state-before-allocation ordering.
struct LegacyMachineGunShotRuntimeInput final {
    std::uint32_t requestedTechLevel{};
    float deltaSeconds{};
    bool parentAttached{};

    LegacyMachineGunVector3 creatorPosition{};
    std::span<const LegacyMachineGunVector3> rotatedMuzzleOffsets;
    LegacyMachineGunVector3 aimPointRelativeToCreator{};
    std::optional<LegacyMachineGunTargetLead> targetLead;
    std::int32_t roomId{};
    std::uint32_t creatorUid{};
};

// Caller-owned storage for one portable projectile instance. Inactive slots
// are reusable. Generation is retained across reuse so stale handles cannot
// resolve to a later projectile.
struct LegacyMachineGunProjectileSlot final {
    LegacyMachineGunProjectileState state{};
    LegacyMachineGunAmmoProfile ammoProfile{};
    std::uint32_t effectiveTechLevel{};
    std::uint64_t generation{};
};

struct LegacyMachineGunProjectileHandle final {
    std::size_t slotIndex{};
    std::uint64_t generation{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyMachineGunProjectileHandle&,
        const LegacyMachineGunProjectileHandle&) noexcept = default;
};

enum class LegacyMachineGunShotRuntimeStatus : std::uint8_t {
    invalidInput,
    noShotDue,
    activated,
    capacityExhausted,
    generationExhausted,
    activationRejected,
};

struct LegacyMachineGunShotRuntimeResult final {
    LegacyMachineGunShotRuntimeStatus status{
        LegacyMachineGunShotRuntimeStatus::invalidInput};
    std::optional<LegacyMachineGunProjectileHandle> projectile;

    [[nodiscard]] constexpr bool activated() const noexcept {
        return status == LegacyMachineGunShotRuntimeStatus::activated &&
            projectile.has_value();
    }
};

// Composes the recovered WpMGun preparation with portable event-0xE2
// activation into caller-owned fixed-capacity storage.
//
// Invalid timing/state input changes nothing. For a valid refresh, fireState
// is committed before any slot search. Consequently capacity/generation
// failure still consumes the recovered cadence, barrel, and nonzero ammunition
// step, matching the native state-before-private-allocation order. Muzzle and
// payload-only input is read only after a reusable slot is found, just as the
// original skips attachment/event work when private allocation fails.
//
// The original type system allocated a private object. First-free inactive
// slot selection is an explicit deterministic port policy, not a claim about
// the native allocator. The operation is single-writer, bounded, noexcept,
// and allocation-free.
[[nodiscard]] LegacyMachineGunShotRuntimeResult
legacyMachineGunAdvanceAndActivateShot(
    LegacyMachineGunFireState& fireState,
    std::span<LegacyMachineGunProjectileSlot> projectileSlots,
    const LegacyMachineGunShotRuntimeInput& input) noexcept;

// Resolves only the currently active generation. Deactivation immediately
// invalidates the handle; a later slot reuse cannot revive it.
template <std::size_t Extent>
[[nodiscard]] LegacyMachineGunProjectileSlot*
legacyMachineGunProjectileSlotForHandle(
    const std::span<LegacyMachineGunProjectileSlot, Extent>
        projectileSlots,
    const LegacyMachineGunProjectileHandle handle) noexcept {
    if (handle.generation == 0U ||
        handle.slotIndex >= projectileSlots.size()) {
        return nullptr;
    }
    auto& slot = projectileSlots[handle.slotIndex];
    return slot.state.active &&
            slot.generation == handle.generation
        ? &slot
        : nullptr;
}

template <std::size_t Extent>
[[nodiscard]] const LegacyMachineGunProjectileSlot*
legacyMachineGunProjectileSlotForHandle(
    const std::span<const LegacyMachineGunProjectileSlot, Extent>
        projectileSlots,
    const LegacyMachineGunProjectileHandle handle) noexcept {
    if (handle.generation == 0U ||
        handle.slotIndex >= projectileSlots.size()) {
        return nullptr;
    }
    const auto& slot = projectileSlots[handle.slotIndex];
    return slot.state.active &&
            slot.generation == handle.generation
        ? &slot
        : nullptr;
}

} // namespace airfix::simulation
