#pragma once

#include "airfix/content/LegacyWeaponCrosshairTextureSet.hpp"
#include "airfix/simulation/LegacyWeaponTypeCatalog.hpp"

#include <cstdint>
#include <optional>

namespace airfix::content {

enum class LegacyWeaponCrosshairBindingStatus : std::uint8_t {
  bound,
  noSight,
  invalidWeaponType,
  invalidTextureSet,
  sessionIdentityMismatch,
};

struct LegacyWeaponCrosshairBinding final {
  simulation::LegacyWeaponTypeId weaponType;
  LegacyWeaponCrosshairTextureRole role;
  render::TextureAssetId textureId;
  ContentRevision revision;
  VerifiedContentTransactionIdentity transactionIdentity;

  [[nodiscard]] bool
  belongsTo(const VerifiedContentSession &session) const noexcept;

  [[nodiscard]] friend bool
  operator==(const LegacyWeaponCrosshairBinding &,
             const LegacyWeaponCrosshairBinding &) = default;
};

struct LegacyWeaponCrosshairBindingResult final {
  LegacyWeaponCrosshairBindingStatus status{
      LegacyWeaponCrosshairBindingStatus::invalidWeaponType};
  std::optional<LegacyWeaponCrosshairBinding> binding;

  [[nodiscard]] bool success() const noexcept {
    return status == LegacyWeaponCrosshairBindingStatus::bound &&
           binding.has_value();
  }
};

// Resolves one concrete native weapon type to the exact authenticated sight
// role and HUD-local texture ID. The caller invokes this independently for the
// primary and selected-secondary weapon slots. WpParaMine returns noSight as a
// valid, non-error outcome. Invalid provenance always fails closed.
[[nodiscard]] LegacyWeaponCrosshairBindingResult
bindLegacyWeaponCrosshairTexture(
    const LoadedLegacyWeaponCrosshairTextureSet &textures,
    const VerifiedContentSession &session,
    simulation::LegacyWeaponTypeId weaponType) noexcept;

} // namespace airfix::content
