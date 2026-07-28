#pragma once

#include "airfix/render/LegacyGameplayCameraRoomState.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::render {

struct LegacyGameplayCameraFrameSnapshot final {
    std::uint64_t simulationStep{};
    std::uint64_t publicationGeneration{};
    LegacyGameplayCameraStaticCollisionState state{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyGameplayCameraFrameSnapshot&,
        const LegacyGameplayCameraFrameSnapshot&) noexcept = default;
};

enum class LegacyGameplayCameraStateInitializeResult : std::uint8_t {
    initialized,
    alreadyInitialized,
    invalidInitialState,
    busy,
};

enum class LegacyGameplayCameraStateCommitResult : std::uint8_t {
    committed,
    notInitialized,
    invalidProposal,
    invalidState,
    simulationStepNotIncreasing,
    generationExhausted,
    busy,
};

// Owns the authoritative retained-static event-5 camera state for one runtime.
// Initialization is a lifecycle operation and must finish before the producer
// and consumer start. Afterwards exactly one simulation/camera producer may
// call currentSnapshot/tryCommit while one render consumer calls tryAcquire.
//
// Publication uses two bounded slots. The producer never overwrites a slot
// while the consumer is copying it, and the consumer returns an owning value,
// not a view into mutable storage. All operations are allocation-free,
// bounded, noexcept, and use no explicit mutex. Contention reports busy/empty
// rather than waiting. Destruction requires producer and consumer quiescence.
class LegacyGameplayCameraStateOwner final {
public:
    LegacyGameplayCameraStateOwner() noexcept = default;
    ~LegacyGameplayCameraStateOwner() = default;

    LegacyGameplayCameraStateOwner(
        const LegacyGameplayCameraStateOwner&) = delete;
    LegacyGameplayCameraStateOwner& operator=(
        const LegacyGameplayCameraStateOwner&) = delete;
    LegacyGameplayCameraStateOwner(
        LegacyGameplayCameraStateOwner&&) = delete;
    LegacyGameplayCameraStateOwner& operator=(
        LegacyGameplayCameraStateOwner&&) = delete;

    [[nodiscard]] LegacyGameplayCameraStateInitializeResult
    tryInitialize(
        const LegacyGameplayCameraStaticCollisionState& initialState,
        std::uint64_t initialSimulationStep) noexcept;

    // Accepts only a complete valid sphere plus retained-static line proposal.
    // A rejected proposal, stale step, exhausted generation, or busy target
    // slot leaves both authoritative state and published frame unchanged.
    [[nodiscard]] LegacyGameplayCameraStateCommitResult tryCommit(
        const LegacyGameplayCameraRetainedStaticFrameResult& proposal,
        std::uint64_t simulationStep) noexcept;

    // Producer-only access to the exact last committed snapshot.
    [[nodiscard]] std::optional<LegacyGameplayCameraFrameSnapshot>
    currentSnapshot() const noexcept;

    // Consumer operation. Empty means no initialized frame or a transient
    // publication race; the next render iteration may retry.
    [[nodiscard]] std::optional<LegacyGameplayCameraFrameSnapshot>
    tryAcquire() noexcept;

    [[nodiscard]] bool initialized() const noexcept;

private:
    struct Slot final {
        std::atomic<std::uint32_t> accessState{0U};
        LegacyGameplayCameraFrameSnapshot snapshot{};
    };

    std::array<Slot, 2U> slots_;
    // Zero means uninitialized. A nonzero value also selects the front slot:
    // slot = (generation - 1) % 2.
    std::atomic<std::uint64_t> publishedGeneration_{0U};

    // Producer-only after initialization.
    std::optional<LegacyGameplayCameraFrameSnapshot> currentSnapshot_;
};

} // namespace airfix::render
