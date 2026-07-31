#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace airfix::simulation {

enum class LegacyWeaponTypeId : std::uint8_t {
  machineGun,
  rocket,
  bomb,
  cannon,
  teslaCoil,
  particleBeam,
  missile,
  paraMine,
  atomicBomb,
};

enum class LegacyWeaponSightRole : std::uint8_t {
  machineGun = 0U,
  rocket = 1U,
  bomb = 2U,
};

inline constexpr std::size_t legacyWeaponTypeCount = 9U;

struct LegacyWeaponTypeRecord final {
  LegacyWeaponTypeId id{};
  std::string_view registeredName;
  std::optional<LegacyWeaponSightRole> sightRole;

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyWeaponTypeRecord &,
             const LegacyWeaponTypeRecord &) noexcept = default;
};

// Exact typeCreate registration names and the sight loaded by each concrete
// AfWeaponType. WpParaMine is the sole registered weapon whose Load override
// does not populate the inherited sight reference.
[[nodiscard]] std::span<const LegacyWeaponTypeRecord, legacyWeaponTypeCount>
legacyWeaponTypeCatalog() noexcept;

[[nodiscard]] const LegacyWeaponTypeRecord *
findLegacyWeaponType(LegacyWeaponTypeId id) noexcept;

// Registration names are exact, case-sensitive binary identifiers. They are
// not user-facing labels and must not be normalized as host paths.
[[nodiscard]] const LegacyWeaponTypeRecord *
findLegacyWeaponType(std::string_view registeredName) noexcept;

} // namespace airfix::simulation
