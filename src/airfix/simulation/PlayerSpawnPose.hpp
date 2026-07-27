#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace airfix::simulation {

enum class PlayerSpawnPoseSource : std::uint8_t {
    rootRoomFallback = 0,
    authenticatedStartTable = 1,
};

inline constexpr std::uint8_t noPlayerStartPositionIndex = 0xffU;

// Immutable mission-start state, kept separate from PlayerAircraftState so
// input-intention hashing and progression remain unchanged. Runtime rotation
// is a mathematical column-vector matrix stored as three columns.
struct PlayerSpawnPose final {
    PlayerSpawnPoseSource source{
        PlayerSpawnPoseSource::rootRoomFallback};
    std::uint8_t startPositionIndex{noPlayerStartPositionIndex};
    std::size_t worldRoomIndex{};

    // Exact authenticated authoring values. Angles are radians in legacy
    // x/y/z field order; position remains in source world units.
    std::array<float, 3> legacyWorldPosition{};
    std::array<float, 3> legacyAxisRotationRadians{};

    // Values converted through the same BasisTransform used for room
    // geometry. Columns are indexed first, then x/y/z component.
    std::array<float, 3> runtimeWorldPosition{};
    std::array<std::array<float, 3>, 3> runtimeWorldRotationColumns{
        std::array<float, 3>{1.0F, 0.0F, 0.0F},
        std::array<float, 3>{0.0F, 1.0F, 0.0F},
        std::array<float, 3>{0.0F, 0.0F, 1.0F},
    };

    [[nodiscard]] friend constexpr bool operator==(
        const PlayerSpawnPose&,
        const PlayerSpawnPose&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<PlayerSpawnPose>);
static_assert(std::is_nothrow_copy_constructible_v<PlayerSpawnPose>);

} // namespace airfix::simulation
