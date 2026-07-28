#pragma once

#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::uint32_t legacyMachineGunProjectileEvent = 0xE2U;
inline constexpr std::uint32_t legacyMachineGunTechLevelCount = 5U;
inline constexpr std::uint32_t legacyMachineGunInitialAmmunition = 250U;
inline constexpr std::uint32_t legacyMachineGunMaximumAmmunition = 500U;
inline constexpr float legacyMachineGunZeroAmmunitionCadenceFactor = 10.0F;

struct LegacyMachineGunTechProfile final {
    float shotIntervalSeconds{};
    float projectileSpeed{};
};

struct LegacyMachineGunFireState final {
    float accumulatedSeconds{};
    std::uint32_t ammunition{};
    std::uint32_t barrelIndex{};
    std::uint32_t barrelCount{};
    bool firing{};
};

// The runtime adapter must create the selected private ammunition type and,
// only if that succeeds, construct and process event 0xE2 with the recovered
// muzzle pose and projectile velocity. Those object/scene operations remain
// outside this pure timing transition.
struct LegacyMachineGunProjectileCommand final {
    std::uint32_t eventType{legacyMachineGunProjectileEvent};
    std::uint32_t barrelIndex{};
    float projectileSpeed{};
};

struct LegacyMachineGunFireStep final {
    LegacyMachineGunFireState state{};
    std::optional<LegacyMachineGunProjectileCommand> projectile{};
};

// WpMGun caps levels at four. The original method receives a float but every
// recovered producer supplies an integer technology level; this portable
// boundary therefore makes the integral domain explicit.
[[nodiscard]] LegacyMachineGunTechProfile legacyMachineGunTechProfile(
    std::uint32_t techLevel) noexcept;

// Reconstructs the state initialized by WpMGun::SetTechLevel. The attachment
// data supplies the barrel count; it is deliberately not guessed here.
[[nodiscard]] std::optional<LegacyMachineGunFireState>
legacyMachineGunInitialFireState(
    const LegacyMachineGunTechProfile& profile,
    std::uint32_t barrelCount,
    std::uint32_t ammunition =
        legacyMachineGunInitialAmmunition) noexcept;

// Reconstructs the shot-timing, barrel-selection, and ammunition subset of the
// WpMGun time-dependant refresh. At most one projectile request is produced per
// call, even when the accumulated time spans multiple shot intervals.
[[nodiscard]] std::optional<LegacyMachineGunFireStep>
legacyMachineGunAdvanceFire(
    const LegacyMachineGunFireState& current,
    const LegacyMachineGunTechProfile& profile,
    float deltaSeconds,
    bool parentAttached) noexcept;

} // namespace airfix::simulation
