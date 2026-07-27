#pragma once

#include "airfix/assets/MissionSetup.hpp"
#include "airfix/assets/MissionWorldRooms.hpp"
#include "airfix/render/LegacyGeometry.hpp"
#include "airfix/simulation/PlayerSpawnPose.hpp"

#include <cstdint>
#include <optional>

namespace airfix::content {

enum class PlayerSpawnPoseBuildIssue : std::uint8_t {
    invalidSelection,
    invalidTransform,
};

struct PlayerSpawnPoseBuildResult final {
    std::optional<simulation::PlayerSpawnPose> pose;
    std::optional<PlayerSpawnPoseBuildIssue> issue;

    [[nodiscard]] bool success() const noexcept {
        return pose.has_value() && !issue.has_value();
    }
};

// Allocation-free conversion of one already-resolved mission start. The
// selection and optional table entry must form the same canonical root/table
// pair accepted by MissionWorldRoomPublication.
[[nodiscard]] PlayerSpawnPoseBuildResult buildPlayerSpawnPose(
    const assets::MissionWorldStartSelection& selection,
    const std::optional<assets::MissionStartPosition>& selectedStart,
    const render::BasisTransform& basis) noexcept;

} // namespace airfix::content
