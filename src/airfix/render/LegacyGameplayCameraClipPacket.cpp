#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"

#include <cmath>

namespace airfix::render {
namespace {

[[nodiscard]] LegacyGameplayCameraClipResult
failure(const LegacyGameplayCameraClipIssueKind kind,
        const std::optional<LegacyCameraTransformIssue> transformIssue =
            std::nullopt) noexcept {
    return {
        .point = std::nullopt,
        .issue =
            LegacyGameplayCameraClipIssue{
                .kind = kind,
                .transformIssue = transformIssue,
            },
    };
}

} // namespace

LegacyGameplayCameraClipBuildResult buildLegacyGameplayCameraClipPacket(
    const LegacyGameplayCameraPoseSnapshot &pose,
    const LegacyGameplayCameraClipConfig &config) noexcept {
    if (!std::isfinite(config.logicalCanvasHeight) ||
        !(config.logicalCanvasHeight > 0.0F)) {
        return {
            .packet = std::nullopt,
            .issue =
                LegacyGameplayCameraClipBuildIssue{
                    .kind = LegacyGameplayCameraClipBuildIssueKind::
                        invalidLogicalCanvasHeight,
                },
        };
    }

    return {
        .packet =
            LegacyGameplayCameraClipPacket{
                pose,
                config.logicalCanvasHeight,
            },
        .issue = std::nullopt,
    };
}

LegacyGameplayCameraClipResult LegacyGameplayCameraClipPacket::project(
    const Vec3 &worldPosition) const noexcept {
    const auto transformed = pose_.worldToView().transform(worldPosition);
    if (!transformed.complete()) {
        return failure(LegacyGameplayCameraClipIssueKind::transformFailed,
                       transformed.issue);
    }

    const auto &camera = *transformed.cameraSpacePosition;
    const auto &projection = pose_.projection();
    const float nearDistance = projection.nearDistance();
    const float farDistance = projection.farDistance();
    const float width = projection.windowWidth();
    const auto centre = projection.centre();
    const float w = camera.z / nearDistance;
    const float x = ((2.0F * projection.projectScale()) / width) * camera.x +
                    ((2.0F * centre.x) / width - 1.0F) * w;
    const float y =
        ((2.0F * projection.projectScale()) / logicalCanvasHeight_) * camera.y +
        (1.0F - (2.0F * centre.y) / logicalCanvasHeight_) * w;
    constexpr float z = 1.0F;
    const float farClipDistance = farDistance - camera.z;
    if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(farClipDistance)) {
        return failure(
            LegacyGameplayCameraClipIssueKind::nonFiniteDerivedValue);
    }

    return {
        .point =
            LegacyGameplayCameraHomogeneousClipPoint{
                .cameraSpacePosition = camera,
                .x = x,
                .y = y,
                .z = z,
                .w = w,
                .farClipDistance = farClipDistance,
                .withinRecoveredDepthRange =
                    camera.z >= nearDistance && camera.z <= farDistance,
            },
        .issue = std::nullopt,
    };
}

LegacyGameplayCameraBootstrapResult
buildLegacyGameplayCameraBootstrapClipPacket(
    const LegacyGameplayCameraBootstrapInput &input) noexcept {
    const auto preset = legacyGameplayCameraPreset(0U, false);
    if (!preset.has_value()) {
        return {
            .packet = std::nullopt,
            .issue =
                LegacyGameplayCameraBootstrapIssue{
                    .kind = LegacyGameplayCameraBootstrapIssueKind::
                        cameraPresetUnavailable,
                    .chaseIssue = std::nullopt,
                    .poseIssue = std::nullopt,
                    .clipPacketIssue = std::nullopt,
                },
        };
    }

    const auto chase = legacyGameplayCameraChaseStep({
        .vehiclePosition = input.vehicleWorldPosition,
        .vehicleRotation = input.vehicleWorldRotation,
        .currentCameraPosition = input.vehicleWorldPosition,
        .preset = *preset,
        .axisFactors = {1.0F, 1.0F, 1.0F},
    });
    if (!chase.complete()) {
        return {
            .packet = std::nullopt,
            .issue =
                LegacyGameplayCameraBootstrapIssue{
                    .kind = LegacyGameplayCameraBootstrapIssueKind::chaseFailed,
                    .chaseIssue = chase.issue,
                    .poseIssue = std::nullopt,
                    .clipPacketIssue = std::nullopt,
                },
        };
    }

    const LegacyGameplayCameraFrameSnapshot frame{
        .simulationStep = 0U,
        .publicationGeneration = 1U,
        .state =
            {
                .roomState =
                    {
                        .runtimeWorldPosition = chase.step->target,
                        .worldRoomIndex = input.worldRoomIndex,
                    },
                .axisFactors = {1.0F, 1.0F, 1.0F},
            },
    };
    const auto pose = buildLegacyGameplayCameraPoseSnapshot(
        frame, input.vehicleWorldPosition);
    if (!pose.complete()) {
        return {
            .packet = std::nullopt,
            .issue =
                LegacyGameplayCameraBootstrapIssue{
                    .kind = LegacyGameplayCameraBootstrapIssueKind::poseFailed,
                    .chaseIssue = std::nullopt,
                    .poseIssue = pose.issue,
                    .clipPacketIssue = std::nullopt,
                },
        };
    }

    const auto packet = buildLegacyGameplayCameraClipPacket(*pose.snapshot);
    if (!packet.complete()) {
        return {
            .packet = std::nullopt,
            .issue =
                LegacyGameplayCameraBootstrapIssue{
                    .kind = LegacyGameplayCameraBootstrapIssueKind::
                        clipPacketFailed,
                    .chaseIssue = std::nullopt,
                    .poseIssue = std::nullopt,
                    .clipPacketIssue = packet.issue,
                },
        };
    }

    return {
        .packet = packet.packet,
        .issue = std::nullopt,
    };
}

} // namespace airfix::render
