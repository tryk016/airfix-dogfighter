#include "airfix/content/LegacyWeaponCrosshairSpriteSubmission.hpp"

#include <cmath>

namespace airfix::content {
namespace {

[[nodiscard]] constexpr std::optional<std::size_t>
roleIndex(const LegacyWeaponCrosshairTextureRole role) noexcept {
  const auto index = static_cast<std::size_t>(role);
  return index < legacyWeaponCrosshairTextureCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

[[nodiscard]] bool
validOutputRectangle(const render::OutputPixelRect &rectangle) noexcept {
  return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
         std::isfinite(rectangle.width) && std::isfinite(rectangle.height) &&
         rectangle.width > 0.0F && rectangle.height > 0.0F;
}

} // namespace

bool LegacyWeaponCrosshairSpriteSubmission::belongsTo(
    const LoadedLegacyWeaponCrosshairTextureSet &textures) const noexcept {
  const auto expectedIndex = roleIndex(role);
  return expectedIndex.has_value() && textures.valid() &&
         transactionIdentity.valid() &&
         transactionIdentity == textures.transactionIdentity &&
         revision == textures.revision && textureId.value == *expectedIndex &&
         textures.textures[*expectedIndex].role == role &&
         textures.textures[*expectedIndex].textureId == textureId;
}

LegacyWeaponCrosshairSpriteSubmissionResult
buildLegacyWeaponCrosshairSpriteSubmission(
    const render::LegacyWeaponCrosshairSpritePlan &plan,
    const LegacyWeaponCrosshairBinding &binding,
    const LegacyWeaponCrosshairVisibilityDecision visibility) noexcept {
  if (visibility == LegacyWeaponCrosshairVisibilityDecision::suppress) {
    return {
        .status = LegacyWeaponCrosshairSpriteSubmissionStatus::suppressed,
        .submission = std::nullopt,
    };
  }

  const auto expectedIndex = roleIndex(binding.role);
  if (!expectedIndex.has_value() || !binding.transactionIdentity.valid() ||
      binding.textureId.value != *expectedIndex) {
    return {
        .status = LegacyWeaponCrosshairSpriteSubmissionStatus::invalidBinding,
        .submission = std::nullopt,
    };
  }
  if (!validOutputRectangle(plan.outputRect)) {
    return {
        .status =
            LegacyWeaponCrosshairSpriteSubmissionStatus::invalidOutputRectangle,
        .submission = std::nullopt,
    };
  }

  return {
      .status = LegacyWeaponCrosshairSpriteSubmissionStatus::ready,
      .submission =
          LegacyWeaponCrosshairSpriteSubmission{
              .weaponType = binding.weaponType,
              .role = binding.role,
              .textureId = binding.textureId,
              .revision = binding.revision,
              .transactionIdentity = binding.transactionIdentity,
              .outputRect = plan.outputRect,
              .uv = {},
              .tintArgb = legacyWeaponCrosshairTintArgb,
              .blendMode = LegacyWeaponCrosshairBlendMode::
                  sourceAlphaOneMinusSourceAlpha,
              .depthMode = LegacyWeaponCrosshairDepthMode::alwaysWrite,
              .insideSceneViewport = plan.insideSceneViewport,
              .withinRecoveredDepthRange = plan.withinRecoveredDepthRange,
          },
  };
}

const LegacyWeaponCrosshairCompositionEntry *
LegacyWeaponCrosshairComposition::entry(
    const std::size_t index) const noexcept {
  if (index >= count || index >= orderedEntries.size() ||
      !orderedEntries[index].has_value()) {
    return nullptr;
  }
  return &*orderedEntries[index];
}

LegacyWeaponCrosshairComposition composeLegacyWeaponCrosshairRenderEvent(
    const LegacyWeaponCrosshairRenderEventState &state,
    const LoadedLegacyWeaponCrosshairTextureSet &textures,
    const std::optional<LegacyWeaponCrosshairSpriteSubmission>
        &selectedSecondary,
    const std::optional<LegacyWeaponCrosshairSpriteSubmission>
        &primary) noexcept {
  LegacyWeaponCrosshairComposition result;
  if (!state.typeRenderEligible) {
    result.status =
        LegacyWeaponCrosshairCompositionStatus::typeNotRenderEligible;
    return result;
  }
  if (state.vehicleInactive) {
    result.status = LegacyWeaponCrosshairCompositionStatus::vehicleInactive;
    return result;
  }
  if (!state.cameraAttached) {
    result.status = LegacyWeaponCrosshairCompositionStatus::cameraNotAttached;
    return result;
  }

  if ((selectedSecondary.has_value() &&
       !selectedSecondary->belongsTo(textures)) ||
      (primary.has_value() && !primary->belongsTo(textures))) {
    result.status = LegacyWeaponCrosshairCompositionStatus::invalidSubmission;
    return result;
  }

  const auto append = [&](const LegacyWeaponCrosshairRenderSlot slot,
                          const auto &submission) {
    if (!submission.has_value()) {
      return;
    }
    result.orderedEntries[result.count++] =
        LegacyWeaponCrosshairCompositionEntry{
            .slot = slot,
            .submission = *submission,
        };
  };
  append(LegacyWeaponCrosshairRenderSlot::selectedSecondary, selectedSecondary);
  append(LegacyWeaponCrosshairRenderSlot::primary, primary);
  result.status = LegacyWeaponCrosshairCompositionStatus::ready;
  return result;
}

} // namespace airfix::content
