#include "airfix/content/LegacyWeaponCrosshairBinding.hpp"

namespace airfix::content {

bool LegacyWeaponCrosshairBinding::belongsTo(
    const VerifiedContentSession &session) const noexcept {
  return transactionIdentity.valid() &&
         transactionIdentity == session.transactionIdentity() &&
         revision == session.revision();
}

LegacyWeaponCrosshairBindingResult bindLegacyWeaponCrosshairTexture(
    const LoadedLegacyWeaponCrosshairTextureSet &textures,
    const VerifiedContentSession &session,
    const simulation::LegacyWeaponTypeId weaponType) noexcept {
  const auto *weapon = simulation::findLegacyWeaponType(weaponType);
  if (weapon == nullptr) {
    return {
        .status = LegacyWeaponCrosshairBindingStatus::invalidWeaponType,
        .binding = std::nullopt,
    };
  }
  if (!weapon->sightRole.has_value()) {
    return {
        .status = LegacyWeaponCrosshairBindingStatus::noSight,
        .binding = std::nullopt,
    };
  }
  if (!textures.valid()) {
    return {
        .status = LegacyWeaponCrosshairBindingStatus::invalidTextureSet,
        .binding = std::nullopt,
    };
  }
  if (!textures.belongsTo(session)) {
    return {
        .status = LegacyWeaponCrosshairBindingStatus::sessionIdentityMismatch,
        .binding = std::nullopt,
    };
  }

  const auto role = *weapon->sightRole;
  const auto *texture = textures.texture(role);
  if (texture == nullptr) {
    return {
        .status = LegacyWeaponCrosshairBindingStatus::invalidTextureSet,
        .binding = std::nullopt,
    };
  }

  return {
      .status = LegacyWeaponCrosshairBindingStatus::bound,
      .binding =
          LegacyWeaponCrosshairBinding{
              .weaponType = weaponType,
              .role = role,
              .textureId = texture->textureId,
              .revision = textures.revision,
              .transactionIdentity = textures.transactionIdentity,
          },
  };
}

} // namespace airfix::content
