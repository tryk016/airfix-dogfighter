#include "airfix/content/PlayerSpawnPoseBuilder.hpp"

namespace airfix::content {
namespace {

[[nodiscard]] PlayerSpawnPoseBuildResult fail(
    const PlayerSpawnPoseBuildIssue issue) noexcept {
    return {
        .pose = std::nullopt,
        .issue = issue,
    };
}

[[nodiscard]] std::array<float, 3> vectorArray(
    const render::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] std::array<std::array<float, 3>, 3> matrixColumns(
    const render::Mat3& matrix) noexcept {
    return {
        vectorArray(matrix.columns[0]),
        vectorArray(matrix.columns[1]),
        vectorArray(matrix.columns[2]),
    };
}

} // namespace

PlayerSpawnPoseBuildResult buildPlayerSpawnPose(
    const assets::MissionWorldStartSelection& selection,
    const std::optional<assets::MissionStartPosition>& selectedStart,
    const render::BasisTransform& basis) noexcept {
    simulation::PlayerSpawnPose pose;
    render::Vec3 legacyPosition{};
    std::array<float, 3> legacyAxisRotation{};

    switch (selection.source) {
    case assets::MissionWorldStartSelectionSource::rootRoomFallback:
        if (selection.startPositionIndex.has_value() ||
            selection.worldRoomIndex != 0U ||
            selectedStart.has_value()) {
            return fail(PlayerSpawnPoseBuildIssue::invalidSelection);
        }
        break;
    case assets::MissionWorldStartSelectionSource::table: {
        if (!selection.startPositionIndex.has_value() ||
            *selection.startPositionIndex >=
                assets::legacyMissionStartCapacity ||
            *selection.startPositionIndex >=
                simulation::noPlayerStartPositionIndex ||
            selection.worldRoomIndex == 0U ||
            !selectedStart.has_value() ||
            selectedStart->roomName.empty()) {
            return fail(PlayerSpawnPoseBuildIssue::invalidSelection);
        }
        pose.source =
            simulation::PlayerSpawnPoseSource::authenticatedStartTable;
        pose.startPositionIndex = static_cast<std::uint8_t>(
            *selection.startPositionIndex);
        pose.worldRoomIndex = selection.worldRoomIndex;
        pose.legacyWorldPosition = selectedStart->position;
        pose.legacyAxisRotationRadians = selectedStart->axisRotation;
        legacyPosition = {
            selectedStart->position[0],
            selectedStart->position[1],
            selectedStart->position[2],
        };
        legacyAxisRotation = selectedStart->axisRotation;
        break;
    }
    default:
        return fail(PlayerSpawnPoseBuildIssue::invalidSelection);
    }

    const auto converted = render::tryConvertLegacyAxisRotationWorldPose(
        legacyPosition, legacyAxisRotation, basis);
    if (!converted.has_value()) {
        return fail(PlayerSpawnPoseBuildIssue::invalidTransform);
    }
    pose.runtimeWorldPosition = vectorArray(converted->translation);
    pose.runtimeWorldRotationColumns =
        matrixColumns(converted->linear);

    return {
        .pose = pose,
        .issue = std::nullopt,
    };
}

} // namespace airfix::content
