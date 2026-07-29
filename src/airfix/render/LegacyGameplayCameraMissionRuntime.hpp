#pragma once

#include "airfix/render/LegacyGameplayCameraPacketExchange.hpp"
#include "airfix/render/LegacyGameplayCameraStepCoordinator.hpp"
#include "airfix/render/MissionWorldDynamicCollisionFrame.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace airfix::render {

struct LegacyGameplayCameraMissionRuntimeLimits final {
    std::size_t maximumCandidateRecords{2'000'000U};
    std::size_t maximumConstraintPlanes{2'000'000U};
    // Matches the combined line tracer's default per-query ceiling so every
    // default-admitted frame is traceable without weakening query options.
    std::size_t maximumDynamicObjects{65'536U};
    std::size_t maximumDynamicRoomRanges{65'536U};
    // Additional logical storage only: both workspaces plus the two-slot
    // packet exchange and, when supplied, the reusable dynamic-object and
    // room-range frame buffers. Moved mission assets were already admitted by
    // the loader.
    std::size_t maximumAdditionalRetainedBytes{128U * 1024U * 1024U};
};

enum class LegacyGameplayCameraMissionRuntimeBuildIssueKind : std::uint8_t {
    incompleteArena,
    invalidBasis,
    initialWorldRoomOutOfRange,
    invalidPlacedCollision,
    placedCollisionRoomCountMismatch,
    invalidPlayerCollision,
    dynamicMeshCountOverflow,
    dynamicObjectCountOverflow,
    dynamicObjectLimitExceeded,
    dynamicRoomRangeLimitExceeded,
    candidateRecordLimitExceeded,
    constraintPlaneLimitExceeded,
    retainedByteSizeOverflow,
    retainedByteLimitExceeded,
    coordinatorInitializationFailed,
    exchangeInitializationFailed,
    allocationFailure,
};

struct LegacyGameplayCameraMissionRuntimeBuildIssue final {
    LegacyGameplayCameraMissionRuntimeBuildIssueKind kind{
        LegacyGameplayCameraMissionRuntimeBuildIssueKind::incompleteArena};
    std::optional<LegacyGameplayCameraStepCoordinatorInitializeStatus>
        coordinatorStatus;
};

enum class LegacyGameplayCameraMissionRuntimeAdvanceStatus : std::uint8_t {
    published,
    advancedPacketBusy,
    coordinatorRejected,
    transportRejected,
    notReady,
};

struct LegacyGameplayCameraMissionRuntimeAdvanceResult final {
    LegacyGameplayCameraMissionRuntimeAdvanceStatus status{
        LegacyGameplayCameraMissionRuntimeAdvanceStatus::notReady};
    LegacyGameplayCameraStepCoordinatorResult coordinator;
    std::optional<LegacyGameplayCameraPacketPublishResult> transportResult;

    // True whenever the coordinator's authoritative state committed, including
    // a later transport rejection. A busy packet slot drops only this visual
    // publication and a later generation may still be published.
    [[nodiscard]] constexpr bool advanced() const noexcept {
        return status != LegacyGameplayCameraMissionRuntimeAdvanceStatus::
                             coordinatorRejected &&
            status !=
            LegacyGameplayCameraMissionRuntimeAdvanceStatus::notReady;
    }

    [[nodiscard]] constexpr bool published() const noexcept {
        return status ==
            LegacyGameplayCameraMissionRuntimeAdvanceStatus::published;
    }
};

struct LegacyGameplayCameraMissionRuntimeBuildResult;

// One non-moving runtime owns every object needed by the retained-static
// gameplay camera and the authenticated dynamic line-collision frame for
// exactly one mission: the arena, placed/player collision assets, runtime
// basis, bounded workspaces, producer coordinator, and render packet exchange.
// A weak producer endpoint may therefore be locked briefly without borrowing
// any memory from the Objective-C mission envelope.
//
// create() is the only allocating path. Camera advance/acquire, dynamic-frame
// publication, and collision tracing are bounded and allocation-free after
// handoff. One simulation producer owns all mutating calls; one render
// consumer owns tryAcquire().
class LegacyGameplayCameraMissionRuntime final {
  public:
    LegacyGameplayCameraMissionRuntime(
        const LegacyGameplayCameraMissionRuntime&) = delete;
    LegacyGameplayCameraMissionRuntime&
    operator=(const LegacyGameplayCameraMissionRuntime&) = delete;
    LegacyGameplayCameraMissionRuntime(
        LegacyGameplayCameraMissionRuntime&&) = delete;
    LegacyGameplayCameraMissionRuntime&
    operator=(LegacyGameplayCameraMissionRuntime&&) = delete;
    ~LegacyGameplayCameraMissionRuntime() = default;

    [[nodiscard]] static LegacyGameplayCameraMissionRuntimeBuildResult create(
        assets::MissionWorldSpatialArena&& arena,
        const BasisTransform& runtimeBasis,
        const LegacyGameplayCameraStepCoordinatorInitializeInput&
            initializeInput,
        const LegacyGameplayCameraMissionRuntimeLimits& limits = {});

    // Mission-load ownership form. Immutable colliders are moved beside the
    // arena, while only exact-size flat frame buffers add retained bytes.
    [[nodiscard]] static LegacyGameplayCameraMissionRuntimeBuildResult create(
        assets::MissionWorldSpatialArena&& arena,
        MissionPlacedDynamicBspAssembly&& placedCollision,
        std::optional<PlayerActorCollisionAssembly>&& playerCollision,
        const BasisTransform& runtimeBasis,
        const LegacyGameplayCameraStepCoordinatorInitializeInput&
            initializeInput,
        const LegacyGameplayCameraMissionRuntimeLimits& limits = {});

    [[nodiscard]] LegacyGameplayCameraMissionRuntimeAdvanceResult tryAdvance(
        const LegacyGameplayCameraStepCoordinatorInput& input,
        const LegacyGameplayCameraStepCoordinatorOptions& options = {})
        noexcept;

    [[nodiscard]] std::optional<LegacyGameplayCameraPacketLease>
    tryAcquire() noexcept;

    [[nodiscard]] MissionWorldDynamicCollisionPublicationResult
    tryPublishDynamicCollisionFrame(
        const ConvertedNodeTransform& playerWorld,
        std::uint32_t playerObjectId,
        bool playerActive,
        std::size_t playerWorldRoomIndex) noexcept;

    // Producer-only view. Its spans remain valid for the runtime lifetime but
    // object/range contents are replaced by the next successful publication.
    [[nodiscard]] std::optional<MissionWorldDynamicCollisionFrameView>
    currentDynamicCollisionFrame() const noexcept;

    // Queries the most recently published dynamic frame against the runtime's
    // owned static arena. Calling before a successful publication fails
    // closed with invalidInput.
    [[nodiscard]] MissionWorldRuntimeCombinedLineTraceResult
    tracePublishedDynamicCollisionPortalLine(
        std::size_t worldRoomIndex,
        const Vec3& runtimeStart,
        const Vec3& runtimeEnd,
        const MissionWorldRuntimeCombinedPortalLineTraceOptions& options = {})
        const noexcept;

    // Producer-only diagnostics. Render consumers must use a packet lease.
    [[nodiscard]] std::optional<LegacyGameplayCameraFrameSnapshot>
    currentSnapshot() const noexcept;
    [[nodiscard]] std::optional<LegacyGameplayCameraMode>
    currentMode() const noexcept;

    [[nodiscard]] std::size_t candidateCapacity() const noexcept {
        return candidateWorkspace_.size();
    }

    [[nodiscard]] std::size_t constraintPlaneCapacity() const noexcept {
        return constraintPlanesHeadFirst_.size();
    }

    [[nodiscard]] bool dynamicCollisionAvailable() const noexcept {
        return placedCollision_.has_value();
    }

    [[nodiscard]] bool dynamicCollisionFramePublished() const noexcept {
        return dynamicCollisionFramePublished_;
    }

    [[nodiscard]] std::size_t dynamicObjectCapacity() const noexcept {
        return dynamicObjects_.size();
    }

    [[nodiscard]] std::size_t dynamicRoomRangeCapacity() const noexcept {
        return dynamicRoomRanges_.size();
    }

    [[nodiscard]] std::size_t worldRoomCount() const noexcept {
        return arena_.rooms.size();
    }

    [[nodiscard]] std::size_t additionalRetainedBytes() const noexcept {
        return additionalRetainedBytes_;
    }

  private:
    LegacyGameplayCameraMissionRuntime(
        assets::MissionWorldSpatialArena arena,
        std::optional<MissionPlacedDynamicBspAssembly> placedCollision,
        std::optional<PlayerActorCollisionAssembly> playerCollision,
        const BasisTransform& runtimeBasis,
        std::vector<MissionWorldRuntimeSphereCandidate> candidateWorkspace,
        std::vector<Vec3> constraintPlanesHeadFirst,
        std::vector<LegacyDynamicBspLineObject> dynamicObjects,
        std::vector<LegacyDynamicBspRoomObjectRange> dynamicRoomRanges,
        std::size_t additionalRetainedBytes);

    [[nodiscard]] static LegacyGameplayCameraMissionRuntimeBuildResult
    createImpl(
        assets::MissionWorldSpatialArena&& arena,
        std::optional<MissionPlacedDynamicBspAssembly>&& placedCollision,
        std::optional<PlayerActorCollisionAssembly>&& playerCollision,
        bool requireDynamicCollision,
        const BasisTransform& runtimeBasis,
        const LegacyGameplayCameraStepCoordinatorInitializeInput&
            initializeInput,
        const LegacyGameplayCameraMissionRuntimeLimits& limits);

    assets::MissionWorldSpatialArena arena_;
    std::optional<MissionPlacedDynamicBspAssembly> placedCollision_;
    std::optional<PlayerActorCollisionAssembly> playerCollision_;
    BasisTransform runtimeBasis_;
    std::vector<MissionWorldRuntimeSphereCandidate> candidateWorkspace_;
    std::vector<Vec3> constraintPlanesHeadFirst_;
    std::vector<LegacyDynamicBspLineObject> dynamicObjects_;
    std::vector<LegacyDynamicBspRoomObjectRange> dynamicRoomRanges_;
    bool dynamicCollisionFramePublished_{};
    LegacyGameplayCameraStepCoordinator coordinator_;
    std::optional<LegacyGameplayCameraPacketExchange> exchange_;
    std::size_t additionalRetainedBytes_{};
};

struct LegacyGameplayCameraMissionRuntimeBuildResult final {
    std::unique_ptr<LegacyGameplayCameraMissionRuntime> runtime;
    std::optional<LegacyGameplayCameraMissionRuntimeBuildIssue> issue;
    std::size_t additionalRetainedBytes{};

    [[nodiscard]] bool complete() const noexcept {
        return runtime != nullptr && !issue.has_value() &&
            additionalRetainedBytes != 0U;
    }
};

} // namespace airfix::render
