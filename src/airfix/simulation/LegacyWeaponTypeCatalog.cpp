#include "airfix/simulation/LegacyWeaponTypeCatalog.hpp"

#include <array>

namespace airfix::simulation {
namespace {

constexpr std::array<LegacyWeaponTypeRecord, legacyWeaponTypeCount> catalog{{
    {
        .id = LegacyWeaponTypeId::machineGun,
        .registeredName = "WpMGun",
        .sightRole = LegacyWeaponSightRole::machineGun,
    },
    {
        .id = LegacyWeaponTypeId::rocket,
        .registeredName = "WpRocket",
        .sightRole = LegacyWeaponSightRole::rocket,
    },
    {
        .id = LegacyWeaponTypeId::bomb,
        .registeredName = "WpBomb",
        .sightRole = LegacyWeaponSightRole::bomb,
    },
    {
        .id = LegacyWeaponTypeId::cannon,
        .registeredName = "WpCannon",
        .sightRole = LegacyWeaponSightRole::rocket,
    },
    {
        .id = LegacyWeaponTypeId::teslaCoil,
        .registeredName = "WpTeslacoil",
        .sightRole = LegacyWeaponSightRole::rocket,
    },
    {
        .id = LegacyWeaponTypeId::particleBeam,
        .registeredName = "WpParticleBeam",
        .sightRole = LegacyWeaponSightRole::rocket,
    },
    {
        .id = LegacyWeaponTypeId::missile,
        .registeredName = "WpMissile",
        .sightRole = LegacyWeaponSightRole::rocket,
    },
    {
        .id = LegacyWeaponTypeId::paraMine,
        .registeredName = "WpParaMine",
        .sightRole = std::nullopt,
    },
    {
        .id = LegacyWeaponTypeId::atomicBomb,
        .registeredName = "WpABomb",
        .sightRole = LegacyWeaponSightRole::bomb,
    },
}};

} // namespace

std::span<const LegacyWeaponTypeRecord, legacyWeaponTypeCount>
legacyWeaponTypeCatalog() noexcept {
  return catalog;
}

const LegacyWeaponTypeRecord *
findLegacyWeaponType(const LegacyWeaponTypeId id) noexcept {
  const auto index = static_cast<std::size_t>(id);
  if (index >= catalog.size() || catalog[index].id != id) {
    return nullptr;
  }
  return &catalog[index];
}

const LegacyWeaponTypeRecord *
findLegacyWeaponType(const std::string_view registeredName) noexcept {
  for (const auto &record : catalog) {
    if (record.registeredName == registeredName) {
      return &record;
    }
  }
  return nullptr;
}

} // namespace airfix::simulation
