#include "airfix/render/LegacyGameplayCameraStateOwner.hpp"

#include <cmath>
#include <limits>

namespace airfix::render {
namespace {

constexpr std::uint32_t kWriterClaim =
    std::uint32_t{1U} << 31U;
constexpr std::uint32_t kMaximumReaderCount = kWriterClaim - 1U;

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] bool validState(
    const LegacyGameplayCameraStaticCollisionState& state) noexcept {
    return finite(state.roomState.runtimeWorldPosition) &&
        finite(state.axisFactors) &&
        state.axisFactors.x >= 0.0F &&
        state.axisFactors.y >= 0.0F &&
        state.axisFactors.z >= 0.0F;
}

} // namespace

LegacyGameplayCameraStateInitializeResult
LegacyGameplayCameraStateOwner::tryInitialize(
    const LegacyGameplayCameraStaticCollisionState& initialState,
    const std::uint64_t initialSimulationStep) noexcept {
    if (currentSnapshot_.has_value() ||
        publishedGeneration_.load(std::memory_order_acquire) != 0U) {
        return LegacyGameplayCameraStateInitializeResult::
            alreadyInitialized;
    }
    if (!validState(initialState)) {
        return LegacyGameplayCameraStateInitializeResult::
            invalidInitialState;
    }

    auto& slot = slots_[0U];
    std::uint32_t expectedAccessState = 0U;
    if (!slot.accessState.compare_exchange_strong(
            expectedAccessState,
            kWriterClaim,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return LegacyGameplayCameraStateInitializeResult::busy;
    }

    const LegacyGameplayCameraFrameSnapshot initial{
        .simulationStep = initialSimulationStep,
        .publicationGeneration = 1U,
        .state = initialState,
    };
    slot.snapshot = initial;
    currentSnapshot_ = initial;
    publishedGeneration_.store(1U, std::memory_order_release);
    slot.accessState.store(0U, std::memory_order_release);
    return LegacyGameplayCameraStateInitializeResult::initialized;
}

LegacyGameplayCameraStateCommitResult
LegacyGameplayCameraStateOwner::tryCommit(
    const LegacyGameplayCameraRetainedStaticFrameResult& proposal,
    const std::uint64_t simulationStep) noexcept {
    if (!currentSnapshot_.has_value()) {
        return LegacyGameplayCameraStateCommitResult::notInitialized;
    }
    if (!proposal.valid() || !proposal.proposedState.has_value()) {
        return LegacyGameplayCameraStateCommitResult::invalidProposal;
    }
    if (!validState(*proposal.proposedState)) {
        return LegacyGameplayCameraStateCommitResult::invalidState;
    }
    if (simulationStep <= currentSnapshot_->simulationStep) {
        return LegacyGameplayCameraStateCommitResult::
            simulationStepNotIncreasing;
    }
    if (currentSnapshot_->publicationGeneration ==
        std::numeric_limits<std::uint64_t>::max()) {
        return LegacyGameplayCameraStateCommitResult::
            generationExhausted;
    }

    const auto nextGeneration =
        currentSnapshot_->publicationGeneration + 1U;
    const auto target = static_cast<std::size_t>(
        (nextGeneration - 1U) % slots_.size());
    auto& slot = slots_[target];
    std::uint32_t expectedAccessState = 0U;
    if (!slot.accessState.compare_exchange_strong(
            expectedAccessState,
            kWriterClaim,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return LegacyGameplayCameraStateCommitResult::busy;
    }

    const LegacyGameplayCameraFrameSnapshot next{
        .simulationStep = simulationStep,
        .publicationGeneration = nextGeneration,
        .state = *proposal.proposedState,
    };
    slot.snapshot = next;
    currentSnapshot_ = next;
    publishedGeneration_.store(
        nextGeneration, std::memory_order_release);
    slot.accessState.store(0U, std::memory_order_release);
    return LegacyGameplayCameraStateCommitResult::committed;
}

std::optional<LegacyGameplayCameraFrameSnapshot>
LegacyGameplayCameraStateOwner::currentSnapshot() const noexcept {
    return currentSnapshot_;
}

std::optional<LegacyGameplayCameraFrameSnapshot>
LegacyGameplayCameraStateOwner::tryAcquire() noexcept {
    const auto observed =
        publishedGeneration_.load(std::memory_order_acquire);
    if (observed == 0U) {
        return std::nullopt;
    }

    const auto slotIndex = static_cast<std::size_t>(
        (observed - 1U) % slots_.size());
    auto& slot = slots_[slotIndex];
    auto accessState =
        slot.accessState.load(std::memory_order_acquire);
    if ((accessState & kWriterClaim) != 0U ||
        accessState == kMaximumReaderCount ||
        !slot.accessState.compare_exchange_strong(
            accessState,
            accessState + 1U,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return std::nullopt;
    }

    const auto confirmed =
        publishedGeneration_.load(std::memory_order_acquire);
    std::optional<LegacyGameplayCameraFrameSnapshot> result;
    if (confirmed == observed &&
        slot.snapshot.publicationGeneration == observed) {
        result = slot.snapshot;
    }
    slot.accessState.fetch_sub(1U, std::memory_order_release);
    return result;
}

bool LegacyGameplayCameraStateOwner::initialized() const noexcept {
    return publishedGeneration_.load(
        std::memory_order_acquire) != 0U;
}

} // namespace airfix::render
