#include "airfix/render/LegacyGameplayCameraPreset.hpp"

namespace airfix::render {
namespace {

constexpr LegacyGameplayCameraPreset camera0{
    .speed = 0.7F,
    .offset = {0.0F, 0.1F, -0.75F},
};
constexpr LegacyGameplayCameraPreset camera1{
    .speed = 0.07F,
    .offset = {0.0F, 0.2F, -0.85F},
};
constexpr LegacyGameplayCameraPreset camera2{
    .speed = 0.7F,
    .offset = {0.0F, 1.8F, -0.75F},
};
constexpr LegacyGameplayCameraPreset rearView{
    .speed = 0.7F,
    .offset = {0.0F, 0.1F, 1.0F},
};

[[nodiscard]] std::optional<LegacyGameplayCameraPreset>
persistentPreset(const std::uint32_t rawMode) noexcept {
    switch (rawMode) {
    case 0U:
        return camera0;
    case 1U:
        return camera1;
    case 2U:
        return camera2;
    default:
        return std::nullopt;
    }
}

} // namespace

std::optional<LegacyGameplayCameraPreset>
legacyGameplayCameraPreset(
    const std::uint32_t rawMode,
    const bool rearViewHeld) noexcept {
    const auto persistent = persistentPreset(rawMode);
    if (!persistent.has_value()) {
        return std::nullopt;
    }
    return rearViewHeld
        ? std::optional<LegacyGameplayCameraPreset>{rearView}
        : persistent;
}

std::optional<LegacyGameplayCameraMode>
nextLegacyGameplayCameraMode(
    const std::uint32_t rawMode) noexcept {
    switch (rawMode) {
    case 0U:
        return LegacyGameplayCameraMode::camera1;
    case 1U:
        return LegacyGameplayCameraMode::camera2;
    case 2U:
        return LegacyGameplayCameraMode::camera0;
    default:
        return std::nullopt;
    }
}

} // namespace airfix::render
