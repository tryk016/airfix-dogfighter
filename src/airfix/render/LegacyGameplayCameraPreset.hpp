#pragma once

#include "airfix/render/LegacyGeometry.hpp"

#include <cstdint>
#include <optional>

namespace airfix::render {

enum class LegacyGameplayCameraMode : std::uint8_t {
    camera0 = 0U,
    camera1 = 1U,
    camera2 = 2U,
};

struct LegacyGameplayCameraPreset final {
    float speed{};
    Vec3 offset{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraPreset&,
        const LegacyGameplayCameraPreset&) noexcept = default;
};

inline constexpr float legacyGameplayCameraNearDistance = 0.25F;
inline constexpr float legacyGameplayCameraFarDistance = 200.0F;
inline constexpr float legacyGameplayCameraHorizontalFovDegrees = 90.0F;

// Maps the persistent raw mode and held rear-view state to one of the four
// recovered camera tuples. Invalid raw state is rejected even while rear view
// is held so corrupt state cannot be silently hidden.
[[nodiscard]] std::optional<LegacyGameplayCameraPreset>
legacyGameplayCameraPreset(
    std::uint32_t rawMode,
    bool rearViewHeld) noexcept;

// Reconstructs the confirmed toggle cycle 0 -> 1 -> 2 -> 0.
[[nodiscard]] std::optional<LegacyGameplayCameraMode>
nextLegacyGameplayCameraMode(std::uint32_t rawMode) noexcept;

} // namespace airfix::render
