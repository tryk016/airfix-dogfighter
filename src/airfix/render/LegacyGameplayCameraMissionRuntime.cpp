#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"

#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace airfix::render {
namespace {

[[nodiscard]] LegacyGameplayCameraMissionRuntimeBuildResult failure(
    const LegacyGameplayCameraMissionRuntimeBuildIssueKind kind,
    const std::optional<
        LegacyGameplayCameraStepCoordinatorInitializeStatus>
        coordinatorStatus = std::nullopt) noexcept {
    return {
        .runtime = nullptr,
        .issue =
            LegacyGameplayCameraMissionRuntimeBuildIssue{
                .kind = kind,
                .coordinatorStatus = coordinatorStatus,
            },
        .additionalRetainedBytes = 0U,
    };
}

[[nodiscard]] bool checkedAdd(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

[[nodiscard]] bool checkedMultiply(
    const std::size_t left,
    const std::size_t right,
    std::size_t& output) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool finite(const Mat3& value) noexcept {
    return finite(value.columns[0]) && finite(value.columns[1]) &&
        finite(value.columns[2]);
}

[[nodiscard]] double dot(
    const Vec3& left,
    const Vec3& right) noexcept {
    return static_cast<double>(left.x) * right.x +
        static_cast<double>(left.y) * right.y +
        static_cast<double>(left.z) * right.z;
}

[[nodiscard]] bool supportedBasis(
    const BasisTransform& basis) noexcept {
    constexpr double tolerance = 1.0e-5;
    if (!finite(basis.sourceToRuntime) ||
        !std::isfinite(basis.runtimeUnitsPerSourceUnit) ||
        !(basis.runtimeUnitsPerSourceUnit > 0.0F)) {
        return false;
    }
    for (std::size_t column = 0U; column < 3U; ++column) {
        if (std::abs(
                dot(
                    basis.sourceToRuntime.columns[column],
                    basis.sourceToRuntime.columns[column]) -
                1.0) > tolerance) {
            return false;
        }
        for (std::size_t other = column + 1U; other < 3U; ++other) {
            if (std::abs(
                    dot(
                        basis.sourceToRuntime.columns[column],
                        basis.sourceToRuntime.columns[other])) >
                tolerance) {
                return false;
            }
        }
    }
    const auto inverseBasis = inverse(basis.sourceToRuntime);
    const float inverseScale = 1.0F / basis.runtimeUnitsPerSourceUnit;
    return inverseBasis.has_value() && finite(*inverseBasis) &&
        std::isfinite(inverseScale) && inverseScale > 0.0F;
}

[[nodiscard]] std::optional<std::size_t> additionalRetainedBytesFor(
    const std::size_t polygonCount,
    const std::size_t dynamicObjectCount,
    const std::size_t dynamicRoomRangeCount) noexcept {
    std::size_t candidateBytes = 0U;
    std::size_t constraintBytes = 0U;
    std::size_t dynamicObjectBytes = 0U;
    std::size_t dynamicRoomRangeBytes = 0U;
    std::size_t retainedBytes =
        LegacyGameplayCameraPacketExchange::retainedBytes();
    if (!checkedMultiply(
            polygonCount,
            sizeof(MissionWorldRuntimeSphereCandidate),
            candidateBytes) ||
        !checkedMultiply(
            polygonCount, sizeof(Vec3), constraintBytes) ||
        !checkedMultiply(
            dynamicObjectCount,
            sizeof(LegacyDynamicBspLineObject),
            dynamicObjectBytes) ||
        !checkedMultiply(
            dynamicRoomRangeCount,
            sizeof(LegacyDynamicBspRoomObjectRange),
            dynamicRoomRangeBytes) ||
        !checkedAdd(retainedBytes, candidateBytes, retainedBytes) ||
        !checkedAdd(retainedBytes, constraintBytes, retainedBytes) ||
        !checkedAdd(
            retainedBytes, dynamicObjectBytes, retainedBytes) ||
        !checkedAdd(
            retainedBytes, dynamicRoomRangeBytes, retainedBytes)) {
        return std::nullopt;
    }
    return retainedBytes;
}

} // namespace

LegacyGameplayCameraMissionRuntime::LegacyGameplayCameraMissionRuntime(
    assets::MissionWorldSpatialArena arena,
    std::optional<MissionPlacedDynamicBspAssembly> placedCollision,
    std::optional<PlayerActorCollisionAssembly> playerCollision,
    const BasisTransform& runtimeBasis,
    std::vector<MissionWorldRuntimeSphereCandidate> candidateWorkspace,
    std::vector<Vec3> constraintPlanesHeadFirst,
    std::vector<LegacyDynamicBspLineObject> dynamicObjects,
    std::vector<LegacyDynamicBspRoomObjectRange> dynamicRoomRanges,
    const std::size_t additionalRetainedBytes)
    : arena_(std::move(arena)),
      placedCollision_(std::move(placedCollision)),
      playerCollision_(std::move(playerCollision)),
      runtimeBasis_(runtimeBasis),
      candidateWorkspace_(std::move(candidateWorkspace)),
      constraintPlanesHeadFirst_(std::move(constraintPlanesHeadFirst)),
      dynamicObjects_(std::move(dynamicObjects)),
      dynamicRoomRanges_(std::move(dynamicRoomRanges)),
      additionalRetainedBytes_(additionalRetainedBytes) {}

LegacyGameplayCameraMissionRuntimeBuildResult
LegacyGameplayCameraMissionRuntime::create(
    assets::MissionWorldSpatialArena&& arena,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStepCoordinatorInitializeInput&
        initializeInput,
    const LegacyGameplayCameraMissionRuntimeLimits& limits) {
    return createImpl(
        std::move(arena),
        std::nullopt,
        std::nullopt,
        false,
        runtimeBasis,
        initializeInput,
        limits);
}

LegacyGameplayCameraMissionRuntimeBuildResult
LegacyGameplayCameraMissionRuntime::create(
    assets::MissionWorldSpatialArena&& arena,
    MissionPlacedDynamicBspAssembly&& placedCollision,
    std::optional<PlayerActorCollisionAssembly>&& playerCollision,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStepCoordinatorInitializeInput&
        initializeInput,
    const LegacyGameplayCameraMissionRuntimeLimits& limits) {
    return createImpl(
        std::move(arena),
        std::optional<MissionPlacedDynamicBspAssembly>{
            std::move(placedCollision)},
        std::move(playerCollision),
        true,
        runtimeBasis,
        initializeInput,
        limits);
}

LegacyGameplayCameraMissionRuntimeBuildResult
LegacyGameplayCameraMissionRuntime::createImpl(
    assets::MissionWorldSpatialArena&& arena,
    std::optional<MissionPlacedDynamicBspAssembly>&& placedCollision,
    std::optional<PlayerActorCollisionAssembly>&& playerCollision,
    const bool requireDynamicCollision,
    const BasisTransform& runtimeBasis,
    const LegacyGameplayCameraStepCoordinatorInitializeInput&
        initializeInput,
    const LegacyGameplayCameraMissionRuntimeLimits& limits) {
    if (!arena.complete()) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                incompleteArena);
    }
    if (!supportedBasis(runtimeBasis)) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::invalidBasis);
    }
    if (initializeInput.worldRoomIndex >= arena.rooms.size()) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                initialWorldRoomOutOfRange);
    }

    std::size_t dynamicObjectCount = 0U;
    std::size_t dynamicRoomRangeCount = 0U;
    if (requireDynamicCollision) {
        if (!placedCollision.has_value() ||
            !placedCollision->complete()) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    invalidPlacedCollision);
        }
        if (placedCollision->roomObjectRanges.size() !=
            arena.rooms.size()) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    placedCollisionRoomCountMismatch);
        }
        if (playerCollision.has_value() &&
            !playerCollision->complete()) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    invalidPlayerCollision);
        }
        if (playerCollision.has_value() &&
            playerCollision->meshes.size() >
                std::numeric_limits<std::size_t>::max() -
                    placedCollision->meshes.size()) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    dynamicMeshCountOverflow);
        }

        const auto playerObjectCount = playerCollision.has_value()
            ? playerCollision->instances.size()
            : 0U;
        if (playerObjectCount >
            std::numeric_limits<std::size_t>::max() -
                placedCollision->objects.size()) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    dynamicObjectCountOverflow);
        }
        dynamicObjectCount =
            placedCollision->objects.size() + playerObjectCount;
        dynamicRoomRangeCount =
            placedCollision->roomObjectRanges.size();
        if (dynamicObjectCount > limits.maximumDynamicObjects) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    dynamicObjectLimitExceeded);
        }
        if (dynamicRoomRangeCount >
            limits.maximumDynamicRoomRanges) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    dynamicRoomRangeLimitExceeded);
        }
    }

    const auto polygonCount = arena.polygons.size();
    if (polygonCount > limits.maximumCandidateRecords) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                candidateRecordLimitExceeded);
    }
    if (polygonCount > limits.maximumConstraintPlanes) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                constraintPlaneLimitExceeded);
    }
    const auto retainedBytes =
        additionalRetainedBytesFor(
            polygonCount,
            dynamicObjectCount,
            dynamicRoomRangeCount);
    if (!retainedBytes.has_value()) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                retainedByteSizeOverflow);
    }
    if (*retainedBytes > limits.maximumAdditionalRetainedBytes) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                retainedByteLimitExceeded);
    }

    try {
        std::vector<MissionWorldRuntimeSphereCandidate>
            candidateWorkspace(polygonCount);
        std::vector<Vec3> constraintPlanesHeadFirst(polygonCount);
        std::vector<LegacyDynamicBspLineObject>
            dynamicObjects(dynamicObjectCount);
        std::vector<LegacyDynamicBspRoomObjectRange>
            dynamicRoomRanges(dynamicRoomRangeCount);
        auto runtime =
            std::unique_ptr<LegacyGameplayCameraMissionRuntime>(
                new LegacyGameplayCameraMissionRuntime(
                    std::move(arena),
                    std::move(placedCollision),
                    std::move(playerCollision),
                    runtimeBasis,
                    std::move(candidateWorkspace),
                    std::move(constraintPlanesHeadFirst),
                    std::move(dynamicObjects),
                    std::move(dynamicRoomRanges),
                    *retainedBytes));

        const auto initialized =
            runtime->coordinator_.tryInitialize(initializeInput);
        if (!initialized.complete()) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    coordinatorInitializationFailed,
                initialized.status);
        }

        runtime->exchange_.emplace(*initialized.packet);
        auto initialLease = runtime->exchange_->tryAcquire();
        if (!initialLease.has_value() || !initialLease->valid() ||
            initialLease->packet() == nullptr ||
            initialLease->simulationStep() !=
                initialized.packet->pose().frame().simulationStep ||
            initialLease->cameraPublicationGeneration() !=
                initialized.packet->pose()
                    .frame()
                    .publicationGeneration) {
            return failure(
                LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                    exchangeInitializationFailed);
        }

        return {
            .runtime = std::move(runtime),
            .issue = std::nullopt,
            .additionalRetainedBytes = *retainedBytes,
        };
    } catch (const std::bad_alloc&) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                allocationFailure);
    } catch (const std::length_error&) {
        return failure(
            LegacyGameplayCameraMissionRuntimeBuildIssueKind::
                allocationFailure);
    }
}

LegacyGameplayCameraMissionRuntimeAdvanceResult
LegacyGameplayCameraMissionRuntime::tryAdvance(
    const LegacyGameplayCameraStepCoordinatorInput& input,
    const LegacyGameplayCameraStepCoordinatorOptions& options) noexcept {
    if (!exchange_.has_value()) {
        return {
            .status =
                LegacyGameplayCameraMissionRuntimeAdvanceStatus::notReady,
            .coordinator = {},
            .transportResult = std::nullopt,
        };
    }

    auto coordinated = coordinator_.tryAdvance(
        arena_,
        runtimeBasis_,
        input,
        candidateWorkspace_,
        constraintPlanesHeadFirst_,
        options);
    if (!coordinated.complete()) {
        return {
            .status = LegacyGameplayCameraMissionRuntimeAdvanceStatus::
                coordinatorRejected,
            .coordinator = std::move(coordinated),
            .transportResult = std::nullopt,
        };
    }

    const auto transport = exchange_->tryPublish(*coordinated.packet);
    const auto status =
        transport == LegacyGameplayCameraPacketPublishResult::published
        ? LegacyGameplayCameraMissionRuntimeAdvanceStatus::published
        : transport == LegacyGameplayCameraPacketPublishResult::busy
        ? LegacyGameplayCameraMissionRuntimeAdvanceStatus::
              advancedPacketBusy
        : LegacyGameplayCameraMissionRuntimeAdvanceStatus::
              transportRejected;
    return {
        .status = status,
        .coordinator = std::move(coordinated),
        .transportResult = transport,
    };
}

std::optional<LegacyGameplayCameraPacketLease>
LegacyGameplayCameraMissionRuntime::tryAcquire() noexcept {
    if (!exchange_.has_value()) {
        return std::nullopt;
    }
    return exchange_->tryAcquire();
}

MissionWorldDynamicCollisionPublicationResult
LegacyGameplayCameraMissionRuntime::tryPublishDynamicCollisionFrame(
    const ConvertedNodeTransform& playerWorld,
    const std::uint32_t playerObjectId,
    const bool playerActive,
    const std::size_t playerWorldRoomIndex) noexcept {
    if (!placedCollision_.has_value()) {
        return {
            .status =
                MissionWorldDynamicCollisionPublicationStatus::
                    invalidPlacedAssembly,
            .frame = {},
        };
    }

    auto published = publishMissionWorldDynamicCollisionFrame(
        *placedCollision_,
        playerCollision_.has_value()
            ? &*playerCollision_
            : nullptr,
        playerWorld,
        playerObjectId,
        playerActive,
        playerWorldRoomIndex,
        dynamicObjects_,
        dynamicRoomRanges_);
    if (published.published()) {
        dynamicCollisionFramePublished_ = true;
    }
    return published;
}

std::optional<MissionWorldDynamicCollisionFrameView>
LegacyGameplayCameraMissionRuntime::currentDynamicCollisionFrame()
    const noexcept {
    if (!dynamicCollisionFramePublished_ ||
        !placedCollision_.has_value()) {
        return std::nullopt;
    }
    return MissionWorldDynamicCollisionFrameView{
        .meshes =
            {
                .primary = placedCollision_->meshes,
                .secondary = playerCollision_.has_value()
                    ? std::span<const LegacyDynamicBspMesh>{
                          playerCollision_->meshes}
                    : std::span<const LegacyDynamicBspMesh>{},
            },
        .objects = dynamicObjects_,
        .roomObjectRanges = dynamicRoomRanges_,
    };
}

MissionWorldRuntimeCombinedLineTraceResult
LegacyGameplayCameraMissionRuntime::
    tracePublishedDynamicCollisionPortalLine(
        const std::size_t worldRoomIndex,
        const Vec3& runtimeStart,
        const Vec3& runtimeEnd,
        const MissionWorldRuntimeCombinedPortalLineTraceOptions&
            options) const noexcept {
    const auto frame = currentDynamicCollisionFrame();
    if (!frame.has_value()) {
        return {
            .status =
                MissionWorldRuntimeCombinedLineTraceStatus::invalidInput,
            .hit = std::nullopt,
            .portalTransitionCount = 0U,
        };
    }
    return traceMissionWorldRuntimeCombinedPortalLine(
        arena_,
        runtimeBasis_,
        worldRoomIndex,
        runtimeStart,
        runtimeEnd,
        frame->meshes,
        frame->objects,
        frame->roomObjectRanges,
        options);
}

std::optional<LegacyGameplayCameraFrameSnapshot>
LegacyGameplayCameraMissionRuntime::currentSnapshot() const noexcept {
    return coordinator_.currentSnapshot();
}

std::optional<LegacyGameplayCameraMode>
LegacyGameplayCameraMissionRuntime::currentMode() const noexcept {
    return coordinator_.currentMode();
}

} // namespace airfix::render
