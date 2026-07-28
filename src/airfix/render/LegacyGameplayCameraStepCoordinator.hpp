#pragma once

#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"
#include "airfix/render/LegacyGameplayCameraStateOwner.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace airfix::render {

struct LegacyGameplayCameraStepCoordinatorInitializeInput final {
    Vec3 vehicleWorldPosition{};
    Mat3 vehicleWorldRotation{};
    std::size_t worldRoomIndex{};
    std::uint64_t cameraCyclePressCount{};
};

enum class LegacyGameplayCameraStepCoordinatorInitializeStatus : std::uint8_t {
    initialized,
    alreadyInitialized,
    bootstrapFailed,
    stateInitializeFailed,
};

struct LegacyGameplayCameraStepCoordinatorInitializeResult final {
    LegacyGameplayCameraStepCoordinatorInitializeStatus status{
        LegacyGameplayCameraStepCoordinatorInitializeStatus::bootstrapFailed};
    std::optional<LegacyGameplayCameraClipPacket> packet;
    std::optional<LegacyGameplayCameraBootstrapIssue> bootstrapIssue;
    std::optional<LegacyGameplayCameraStateInitializeResult>
        stateInitializeResult;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return status == LegacyGameplayCameraStepCoordinatorInitializeStatus::
                             initialized &&
               packet.has_value() && !bootstrapIssue.has_value() &&
               stateInitializeResult ==
                   LegacyGameplayCameraStateInitializeResult::initialized;
    }
};

struct LegacyGameplayCameraStepCoordinatorInput final {
    // The recovered AirCraft path reads these from different object fields.
    // Keep them separate even when a port bootstrap supplies the same value.
    Vec3 vehicleChaseWorldPosition{};
    Vec3 vehicleWorldAnchor{};
    Mat3 vehicleWorldRotation{};

    // Recovered AirCraft.type factor-recovery inputs. The first value remains
    // deliberately raw until its physical unit is proven.
    float rawRefreshArgument{};
    float vehicleField98{1.0F};
    bool vehicleFlag460{};

    std::uint64_t cameraCyclePressCount{};
    bool rearViewHeld{};
    std::uint64_t simulationStep{};
};

struct LegacyGameplayCameraStepCoordinatorOptions final {
    LegacyGameplayCameraStaticCollisionOptions staticCollision{};
    LegacyGameplayCameraRetainedStaticFrameOptions retainedStatic{};
};

enum class LegacyGameplayCameraStepCoordinatorStatus : std::uint8_t {
    advanced,
    notInitialized,
    cameraCycleCounterRegressed,
    cameraModeUnavailable,
    axisFactorRecoveryFailed,
    chaseFailed,
    staticCollisionFailed,
    retainedStaticFailed,
    generationExhausted,
    poseFailed,
    clipPacketFailed,
    stateCommitFailed,
};

struct LegacyGameplayCameraStepCoordinatorResult final {
    LegacyGameplayCameraStepCoordinatorStatus status{
        LegacyGameplayCameraStepCoordinatorStatus::notInitialized};
    std::optional<LegacyGameplayCameraClipPacket> packet;
    std::optional<LegacyGameplayCameraMode> committedMode;
    std::optional<LegacyGameplayCameraChaseIssue> chaseIssue;
    std::optional<LegacyGameplayCameraStaticCollisionStatus>
        staticCollisionStatus;
    std::optional<LegacyGameplayCameraRetainedStaticFrameStatus>
        retainedStaticStatus;
    std::optional<LegacyGameplayCameraPoseBuildIssue> poseIssue;
    std::optional<LegacyGameplayCameraClipBuildIssue> clipPacketIssue;
    std::optional<LegacyGameplayCameraStateCommitResult> stateCommitResult;

    [[nodiscard]] constexpr bool complete() const noexcept {
        return status == LegacyGameplayCameraStepCoordinatorStatus::advanced &&
               packet.has_value() && committedMode.has_value() &&
               !chaseIssue.has_value() && !staticCollisionStatus.has_value() &&
               !retainedStaticStatus.has_value() && !poseIssue.has_value() &&
               !clipPacketIssue.has_value() &&
               stateCommitResult ==
                   LegacyGameplayCameraStateCommitResult::committed;
    }
};

// Producer-side composition of one recovered AirCraft gameplay-camera step.
// Initialization owns the explicit camera0 bootstrap. Every later advance
// preserves native order:
//
// factor recovery -> preset/rear selection -> chase -> static sphere/portal
// -> retained-static vehicle line -> look-at/pose -> clip packet -> commit.
//
// The caller owns the immutable mission arena and bounded workspaces. Dynamic
// objects and transparent collision portals remain outside the retained
// backend. This coordinator does not guess the raw refresh argument or publish
// across threads; a later runtime exchange consumes only complete packets.
class LegacyGameplayCameraStepCoordinator final {
  public:
    LegacyGameplayCameraStepCoordinator() noexcept = default;
    ~LegacyGameplayCameraStepCoordinator() = default;

    LegacyGameplayCameraStepCoordinator(
        const LegacyGameplayCameraStepCoordinator&) = delete;
    LegacyGameplayCameraStepCoordinator&
    operator=(const LegacyGameplayCameraStepCoordinator&) = delete;
    LegacyGameplayCameraStepCoordinator(LegacyGameplayCameraStepCoordinator&&) =
        delete;
    LegacyGameplayCameraStepCoordinator&
    operator=(LegacyGameplayCameraStepCoordinator&&) = delete;

    [[nodiscard]] LegacyGameplayCameraStepCoordinatorInitializeResult
    tryInitialize(const LegacyGameplayCameraStepCoordinatorInitializeInput&
                      input) noexcept;

    [[nodiscard]] LegacyGameplayCameraStepCoordinatorResult
    tryAdvance(const assets::MissionWorldSpatialArena& arena,
               const BasisTransform& runtimeBasis,
               const LegacyGameplayCameraStepCoordinatorInput& input,
               std::span<MissionWorldRuntimeSphereCandidate> candidateWorkspace,
               std::span<Vec3> constraintPlanesHeadFirst,
               const LegacyGameplayCameraStepCoordinatorOptions& options =
                   {}) noexcept;

    [[nodiscard]] bool initialized() const noexcept;

    // Producer-only diagnostics.
    [[nodiscard]] std::optional<LegacyGameplayCameraFrameSnapshot>
    currentSnapshot() const noexcept;
    [[nodiscard]] std::optional<LegacyGameplayCameraMode>
    currentMode() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    lastCameraCyclePressCount() const noexcept;

  private:
    LegacyGameplayCameraStateOwner stateOwner_;
    std::optional<LegacyGameplayCameraMode> mode_;
    std::optional<std::uint64_t> lastCameraCyclePressCount_;
};

} // namespace airfix::render
