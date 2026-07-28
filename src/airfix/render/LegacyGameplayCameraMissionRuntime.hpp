#pragma once

#include "airfix/render/LegacyGameplayCameraPacketExchange.hpp"
#include "airfix/render/LegacyGameplayCameraStepCoordinator.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace airfix::render {

struct LegacyGameplayCameraMissionRuntimeLimits final {
    std::size_t maximumCandidateRecords{2'000'000U};
    std::size_t maximumConstraintPlanes{2'000'000U};
    // Additional logical storage only: both workspaces plus the two-slot
    // packet exchange. The moved arena was already admitted by the loader.
    std::size_t maximumAdditionalRetainedBytes{128U * 1024U * 1024U};
};

enum class LegacyGameplayCameraMissionRuntimeBuildIssueKind : std::uint8_t {
    incompleteArena,
    invalidBasis,
    initialWorldRoomOutOfRange,
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
// gameplay camera for exactly one mission: the authenticated arena, runtime
// basis, bounded collision workspaces, producer coordinator, and render packet
// exchange. A weak producer endpoint may therefore be locked briefly without
// borrowing any memory from the Objective-C mission envelope.
//
// create() is the only allocating path. tryAdvance() and tryAcquire() are
// bounded, allocation-free SPSC operations after handoff: one simulation
// producer and one render consumer.
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

    [[nodiscard]] LegacyGameplayCameraMissionRuntimeAdvanceResult tryAdvance(
        const LegacyGameplayCameraStepCoordinatorInput& input,
        const LegacyGameplayCameraStepCoordinatorOptions& options = {})
        noexcept;

    [[nodiscard]] std::optional<LegacyGameplayCameraPacketLease>
    tryAcquire() noexcept;

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

    [[nodiscard]] std::size_t additionalRetainedBytes() const noexcept {
        return additionalRetainedBytes_;
    }

  private:
    LegacyGameplayCameraMissionRuntime(
        assets::MissionWorldSpatialArena arena,
        const BasisTransform& runtimeBasis,
        std::vector<MissionWorldRuntimeSphereCandidate> candidateWorkspace,
        std::vector<Vec3> constraintPlanesHeadFirst,
        std::size_t additionalRetainedBytes);

    assets::MissionWorldSpatialArena arena_;
    BasisTransform runtimeBasis_;
    std::vector<MissionWorldRuntimeSphereCandidate> candidateWorkspace_;
    std::vector<Vec3> constraintPlanesHeadFirst_;
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
