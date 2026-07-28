#include "airfix/render/LegacyGameplayCameraStepCoordinator.hpp"

#include <limits>

namespace airfix::render {
namespace {

[[nodiscard]] LegacyGameplayCameraStepCoordinatorResult
failure(const LegacyGameplayCameraStepCoordinatorStatus status) noexcept {
    return {
        .status = status,
        .packet = std::nullopt,
        .committedMode = std::nullopt,
        .chaseIssue = std::nullopt,
        .staticCollisionStatus = std::nullopt,
        .retainedStaticStatus = std::nullopt,
        .poseIssue = std::nullopt,
        .clipPacketIssue = std::nullopt,
        .stateCommitResult = std::nullopt,
    };
}

[[nodiscard]] std::optional<LegacyGameplayCameraMode>
advancedMode(const LegacyGameplayCameraMode current,
             const std::uint64_t currentPressCount,
             const std::uint64_t nextPressCount) noexcept {
    if (nextPressCount < currentPressCount) {
        return std::nullopt;
    }

    auto mode = current;
    const auto advances = (nextPressCount - currentPressCount) % 3U;
    for (std::uint64_t index = 0U; index < advances; ++index) {
        const auto next =
            nextLegacyGameplayCameraMode(static_cast<std::uint32_t>(mode));
        if (!next.has_value()) {
            return std::nullopt;
        }
        mode = *next;
    }
    return mode;
}

} // namespace

LegacyGameplayCameraStepCoordinatorInitializeResult
LegacyGameplayCameraStepCoordinator::tryInitialize(
    const LegacyGameplayCameraStepCoordinatorInitializeInput& input) noexcept {
    if (initialized()) {
        return {
            .status = LegacyGameplayCameraStepCoordinatorInitializeStatus::
                alreadyInitialized,
            .packet = std::nullopt,
            .bootstrapIssue = std::nullopt,
            .stateInitializeResult =
                LegacyGameplayCameraStateInitializeResult::alreadyInitialized,
        };
    }

    const auto bootstrap = buildLegacyGameplayCameraBootstrapClipPacket({
        .vehicleWorldPosition = input.vehicleWorldPosition,
        .vehicleWorldRotation = input.vehicleWorldRotation,
        .worldRoomIndex = input.worldRoomIndex,
    });
    if (!bootstrap.complete()) {
        return {
            .status = LegacyGameplayCameraStepCoordinatorInitializeStatus::
                bootstrapFailed,
            .packet = std::nullopt,
            .bootstrapIssue = bootstrap.issue,
            .stateInitializeResult = std::nullopt,
        };
    }

    const auto& frame = bootstrap.packet->pose().frame();
    const auto stateResult =
        stateOwner_.tryInitialize(frame.state, frame.simulationStep);
    if (stateResult != LegacyGameplayCameraStateInitializeResult::initialized) {
        return {
            .status = LegacyGameplayCameraStepCoordinatorInitializeStatus::
                stateInitializeFailed,
            .packet = std::nullopt,
            .bootstrapIssue = std::nullopt,
            .stateInitializeResult = stateResult,
        };
    }

    mode_ = LegacyGameplayCameraMode::camera0;
    lastCameraCyclePressCount_ = input.cameraCyclePressCount;
    return {
        .status =
            LegacyGameplayCameraStepCoordinatorInitializeStatus::initialized,
        .packet = bootstrap.packet,
        .bootstrapIssue = std::nullopt,
        .stateInitializeResult = stateResult,
    };
}

LegacyGameplayCameraStepCoordinatorResult
LegacyGameplayCameraStepCoordinator::tryAdvance(
    const assets::MissionWorldSpatialArena& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStepCoordinatorInput& input,
    const std::span<MissionWorldRuntimeSphereCandidate> candidateWorkspace,
    const std::span<Vec3> constraintPlanesHeadFirst,
    const LegacyGameplayCameraStepCoordinatorOptions& options) noexcept {
    const auto current = stateOwner_.currentSnapshot();
    if (!current.has_value() || !mode_.has_value() ||
        !lastCameraCyclePressCount_.has_value()) {
        return failure(
            LegacyGameplayCameraStepCoordinatorStatus::notInitialized);
    }
    if (input.cameraCyclePressCount < *lastCameraCyclePressCount_) {
        return failure(LegacyGameplayCameraStepCoordinatorStatus::
                           cameraCycleCounterRegressed);
    }

    const auto nextMode = advancedMode(
        *mode_, *lastCameraCyclePressCount_, input.cameraCyclePressCount);
    if (!nextMode.has_value()) {
        return failure(
            LegacyGameplayCameraStepCoordinatorStatus::cameraModeUnavailable);
    }

    const auto recoveredFactors =
        legacyAircraftRecoverGameplayCameraAxisFactors(
            current->state.axisFactors,
            input.refreshDeltaSeconds,
            input.vehicleField98,
            input.vehicleFlag460);
    if (!recoveredFactors.has_value()) {
        return failure(LegacyGameplayCameraStepCoordinatorStatus::
                           axisFactorRecoveryFailed);
    }

    const auto preset = legacyGameplayCameraPreset(
        static_cast<std::uint32_t>(*nextMode), input.rearViewHeld);
    if (!preset.has_value()) {
        return failure(
            LegacyGameplayCameraStepCoordinatorStatus::cameraModeUnavailable);
    }

    const auto chase = legacyGameplayCameraChaseStep({
        .vehiclePosition = input.vehicleChaseWorldPosition,
        .vehicleRotation = input.vehicleWorldRotation,
        .currentCameraPosition = current->state.roomState.runtimeWorldPosition,
        .preset = *preset,
        .axisFactors = *recoveredFactors,
    });
    if (!chase.complete()) {
        auto result =
            failure(LegacyGameplayCameraStepCoordinatorStatus::chaseFailed);
        result.chaseIssue = chase.issue;
        return result;
    }

    const LegacyGameplayCameraStaticCollisionState recoveredCurrent{
        .roomState = current->state.roomState,
        .axisFactors = *recoveredFactors,
    };
    const auto staticCollision =
        proposeLegacyGameplayCameraStaticCollisionState(
            arena,
            runtimeBasis,
            recoveredCurrent,
            chase.step->candidateCameraPosition,
            legacyGameplayCameraNearDistance,
            candidateWorkspace,
            constraintPlanesHeadFirst,
            options.staticCollision);
    if (!staticCollision.valid()) {
        auto result = failure(
            LegacyGameplayCameraStepCoordinatorStatus::staticCollisionFailed);
        result.staticCollisionStatus = staticCollision.status;
        return result;
    }

    const auto retainedStatic =
        completeLegacyGameplayCameraRetainedStaticFrameState(
            arena,
            runtimeBasis,
            staticCollision,
            input.vehicleWorldAnchor,
            options.retainedStatic);
    if (!retainedStatic.valid() || !retainedStatic.proposedState.has_value()) {
        auto result = failure(
            LegacyGameplayCameraStepCoordinatorStatus::retainedStaticFailed);
        result.retainedStaticStatus = retainedStatic.status;
        return result;
    }

    if (current->publicationGeneration ==
        std::numeric_limits<std::uint64_t>::max()) {
        return failure(
            LegacyGameplayCameraStepCoordinatorStatus::generationExhausted);
    }
    const LegacyGameplayCameraFrameSnapshot nextFrame{
        .simulationStep = input.simulationStep,
        .publicationGeneration = current->publicationGeneration + 1U,
        .state = *retainedStatic.proposedState,
    };
    const auto pose = buildLegacyGameplayCameraPoseSnapshot(
        nextFrame, input.vehicleWorldAnchor);
    if (!pose.complete()) {
        auto result =
            failure(LegacyGameplayCameraStepCoordinatorStatus::poseFailed);
        result.poseIssue = pose.issue;
        return result;
    }
    const auto packet = buildLegacyGameplayCameraClipPacket(*pose.snapshot);
    if (!packet.complete()) {
        auto result = failure(
            LegacyGameplayCameraStepCoordinatorStatus::clipPacketFailed);
        result.clipPacketIssue = packet.issue;
        return result;
    }

    const auto commit =
        stateOwner_.tryCommit(retainedStatic, input.simulationStep);
    if (commit != LegacyGameplayCameraStateCommitResult::committed) {
        auto result = failure(
            LegacyGameplayCameraStepCoordinatorStatus::stateCommitFailed);
        result.stateCommitResult = commit;
        return result;
    }

    mode_ = *nextMode;
    lastCameraCyclePressCount_ = input.cameraCyclePressCount;
    return {
        .status = LegacyGameplayCameraStepCoordinatorStatus::advanced,
        .packet = packet.packet,
        .committedMode = nextMode,
        .chaseIssue = std::nullopt,
        .staticCollisionStatus = std::nullopt,
        .retainedStaticStatus = std::nullopt,
        .poseIssue = std::nullopt,
        .clipPacketIssue = std::nullopt,
        .stateCommitResult = commit,
    };
}

bool LegacyGameplayCameraStepCoordinator::initialized() const noexcept {
    return stateOwner_.initialized();
}

std::optional<LegacyGameplayCameraFrameSnapshot>
LegacyGameplayCameraStepCoordinator::currentSnapshot() const noexcept {
    return stateOwner_.currentSnapshot();
}

std::optional<LegacyGameplayCameraMode>
LegacyGameplayCameraStepCoordinator::currentMode() const noexcept {
    return mode_;
}

std::optional<std::uint64_t>
LegacyGameplayCameraStepCoordinator::lastCameraCyclePressCount()
    const noexcept {
    return lastCameraCyclePressCount_;
}

} // namespace airfix::render
