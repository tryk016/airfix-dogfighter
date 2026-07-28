#include "airfix/render/LegacyGameplayCameraStateOwner.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
    if (trackAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
}

} // namespace

void* operator new(const std::size_t size) {
    recordAllocation();
    if (void* memory = std::malloc(size == 0U ? 1U : size);
        memory != nullptr) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

using namespace airfix::render;

static_assert(
    !std::is_copy_constructible_v<LegacyGameplayCameraStateOwner>);
static_assert(
    !std::is_copy_assignable_v<LegacyGameplayCameraStateOwner>);
static_assert(
    !std::is_move_constructible_v<LegacyGameplayCameraStateOwner>);
static_assert(
    !std::is_move_assignable_v<LegacyGameplayCameraStateOwner>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] LegacyGameplayCameraStaticCollisionState state(
    const float marker,
    const std::size_t room) {
    return {
        .roomState =
            {
                .runtimeWorldPosition =
                    {marker, marker + 1.0F, marker + 2.0F},
                .worldRoomIndex = room,
            },
        .axisFactors =
            {marker + 3.0F, marker + 4.0F, marker + 5.0F},
    };
}

[[nodiscard]] LegacyGameplayCameraRetainedStaticFrameResult proposal(
    const LegacyGameplayCameraStaticCollisionState& value,
    const LegacyGameplayCameraRetainedStaticFrameStatus status =
        LegacyGameplayCameraRetainedStaticFrameStatus::clear) {
    return {
        .status = status,
        .proposedState = value,
        .lineTrace = std::nullopt,
        .lineRoomUpdate = std::nullopt,
    };
}

[[nodiscard]] LegacyGameplayCameraFrameSnapshot acquire(
    LegacyGameplayCameraStateOwner& owner,
    const std::string& message) {
    for (std::size_t attempt = 0U; attempt < 1'000U; ++attempt) {
        if (auto snapshot = owner.tryAcquire();
            snapshot.has_value()) {
            return *snapshot;
        }
        std::this_thread::yield();
    }
    throw std::runtime_error(message);
}

void testInitializationPublishesOneCompleteFrame() {
    LegacyGameplayCameraStateOwner owner;
    require(!owner.initialized(), "fresh owner was initialized");
    require(
        !owner.currentSnapshot().has_value() &&
            !owner.tryAcquire().has_value(),
        "fresh owner exposed a frame");

    const auto initial = state(1.0F, 7U);
    require(
        owner.tryInitialize(initial, 42U) ==
            LegacyGameplayCameraStateInitializeResult::initialized,
        "valid initialization failed");
    require(owner.initialized(), "initialized owner stayed unavailable");

    const LegacyGameplayCameraFrameSnapshot expected{
        .simulationStep = 42U,
        .publicationGeneration = 1U,
        .state = initial,
    };
    require(
        owner.currentSnapshot() == expected &&
            acquire(owner, "initial frame unavailable") == expected,
        "initial frame metadata or state was not published together");

    require(
        owner.tryInitialize(state(9.0F, 9U), 99U) ==
            LegacyGameplayCameraStateInitializeResult::
                alreadyInitialized,
        "second initialization was accepted");
    require(
        owner.currentSnapshot() == expected,
        "second initialization changed the owner");
}

void testInvalidInitializationFailsClosed() {
    const auto expectInvalid =
        [](LegacyGameplayCameraStaticCollisionState invalid,
           const std::string& message) {
            LegacyGameplayCameraStateOwner owner;
            require(
                owner.tryInitialize(invalid, 0U) ==
                    LegacyGameplayCameraStateInitializeResult::
                        invalidInitialState,
                message);
            require(
                !owner.initialized() &&
                    !owner.currentSnapshot().has_value() &&
                    !owner.tryAcquire().has_value(),
                "invalid initialization published a partial frame");
        };

    auto nanPosition = state(1.0F, 0U);
    nanPosition.roomState.runtimeWorldPosition.y =
        std::numeric_limits<float>::quiet_NaN();
    expectInvalid(nanPosition, "NaN position was accepted");

    auto infiniteFactor = state(1.0F, 0U);
    infiniteFactor.axisFactors.z =
        std::numeric_limits<float>::infinity();
    expectInvalid(infiniteFactor, "infinite factor was accepted");

    auto negativeFactor = state(1.0F, 0U);
    negativeFactor.axisFactors.x = -0.25F;
    expectInvalid(negativeFactor, "negative factor was accepted");

    LegacyGameplayCameraStateOwner retryable;
    require(
        retryable.tryInitialize(negativeFactor, 0U) ==
            LegacyGameplayCameraStateInitializeResult::
                invalidInitialState,
        "retry baseline invalid state was accepted");
    const auto recovered = state(2.0F, 3U);
    require(
        retryable.tryInitialize(recovered, 5U) ==
                LegacyGameplayCameraStateInitializeResult::initialized &&
            acquire(
                retryable,
                "valid retry after invalid initialization unavailable") ==
                LegacyGameplayCameraFrameSnapshot{
                    .simulationStep = 5U,
                    .publicationGeneration = 1U,
                    .state = recovered,
                },
        "invalid initialization poisoned a later valid retry");
}

void testCommitIsAtomicAndMonotonic() {
    LegacyGameplayCameraStateOwner owner;
    const auto initial = state(1.0F, 2U);
    require(
        owner.tryInitialize(initial, 10U) ==
            LegacyGameplayCameraStateInitializeResult::initialized,
        "commit test initialization failed");

    const auto next = state(20.0F, 3U);
    require(
        owner.tryCommit(proposal(next), 11U) ==
            LegacyGameplayCameraStateCommitResult::committed,
        "valid proposal was not committed");
    const LegacyGameplayCameraFrameSnapshot expected{
        .simulationStep = 11U,
        .publicationGeneration = 2U,
        .state = next,
    };
    require(
        owner.currentSnapshot() == expected &&
            acquire(owner, "committed frame unavailable") == expected,
        "commit mixed state, step, or generation");

    const auto expectUnchanged =
        [&](const LegacyGameplayCameraStateCommitResult expectedResult,
            const LegacyGameplayCameraRetainedStaticFrameResult& rejected,
            const std::uint64_t simulationStep,
            const std::string& message) {
            require(
                owner.tryCommit(rejected, simulationStep) ==
                    expectedResult,
                message);
            require(
                owner.currentSnapshot() == expected &&
                    acquire(owner, "prior frame disappeared") == expected,
                "rejected commit altered the prior frame");
        };

    expectUnchanged(
        LegacyGameplayCameraStateCommitResult::invalidProposal,
        {
            .status =
                LegacyGameplayCameraRetainedStaticFrameStatus::
                    invalidArena,
            .proposedState = std::nullopt,
        },
        12U,
        "failed proposal was accepted");
    expectUnchanged(
        LegacyGameplayCameraStateCommitResult::invalidProposal,
        {
            .status =
                LegacyGameplayCameraRetainedStaticFrameStatus::clear,
            .proposedState = std::nullopt,
        },
        12U,
        "valid status without a complete state was accepted");

    auto invalid = state(30.0F, 4U);
    invalid.axisFactors.y =
        std::numeric_limits<float>::quiet_NaN();
    expectUnchanged(
        LegacyGameplayCameraStateCommitResult::invalidState,
        proposal(invalid),
        12U,
        "non-finite proposed state was accepted");
    expectUnchanged(
        LegacyGameplayCameraStateCommitResult::
            simulationStepNotIncreasing,
        proposal(state(40.0F, 5U)),
        11U,
        "duplicate simulation step was accepted");
    expectUnchanged(
        LegacyGameplayCameraStateCommitResult::
            simulationStepNotIncreasing,
        proposal(state(50.0F, 6U)),
        9U,
        "backward simulation step was accepted");

    require(
        owner.tryCommit(
            proposal(
                state(60.0F, 7U),
                LegacyGameplayCameraRetainedStaticFrameStatus::occluded),
            20U) ==
            LegacyGameplayCameraStateCommitResult::committed,
        "forward step gap or transition result was rejected");
    const auto latest = acquire(owner, "latest frame unavailable");
    require(
        latest.simulationStep == 20U &&
            latest.publicationGeneration == 3U &&
            latest.state == state(60.0F, 7U),
        "forward commit published the wrong complete frame");
}

void testUninitializedCommitFailsClosed() {
    LegacyGameplayCameraStateOwner owner;
    require(
        owner.tryCommit(proposal(state(1.0F, 0U)), 1U) ==
            LegacyGameplayCameraStateCommitResult::notInitialized,
        "uninitialized owner accepted a proposal");
    require(
        !owner.currentSnapshot().has_value() &&
            !owner.tryAcquire().has_value(),
        "uninitialized commit exposed state");
}

void testConcurrentProducerConsumerObserveCoherentFrames() {
    constexpr std::uint64_t frameCount = 20'000U;
    LegacyGameplayCameraStateOwner owner;
    require(
        owner.tryInitialize(state(0.0F, 0U), 0U) ==
            LegacyGameplayCameraStateInitializeResult::initialized,
        "concurrent test initialization failed");

    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint64_t step = 1U;
             step <= frameCount;
             ++step) {
            const auto marker = static_cast<float>(step);
            const auto next = proposal(
                state(marker, static_cast<std::size_t>(step)));
            for (;;) {
                const auto result = owner.tryCommit(next, step);
                if (result ==
                    LegacyGameplayCameraStateCommitResult::committed) {
                    break;
                }
                if (result !=
                    LegacyGameplayCameraStateCommitResult::busy) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::uint64_t lastGeneration = 0U;
        while (lastGeneration <= frameCount &&
               !failed.load(std::memory_order_relaxed)) {
            const auto snapshot = owner.tryAcquire();
            if (!snapshot.has_value()) {
                std::this_thread::yield();
                continue;
            }
            const auto step = snapshot->simulationStep;
            const auto marker = static_cast<float>(step);
            if (snapshot->publicationGeneration < lastGeneration ||
                snapshot->publicationGeneration != step + 1U ||
                snapshot->state !=
                    state(marker, static_cast<std::size_t>(step))) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            lastGeneration = snapshot->publicationGeneration;
        }
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    require(
        !failed.load(std::memory_order_relaxed),
        "concurrent consumer observed a torn or backward frame");

    const auto final = acquire(owner, "final concurrent frame unavailable");
    require(
        final.simulationStep == frameCount &&
            final.publicationGeneration == frameCount + 1U &&
            final.state ==
                state(
                    static_cast<float>(frameCount),
                    static_cast<std::size_t>(frameCount)),
        "concurrent exchange lost its final frame");
}

void testHotPathDoesNotAllocate() {
    LegacyGameplayCameraStateOwner owner;
    require(
        owner.tryInitialize(state(0.0F, 0U), 0U) ==
            LegacyGameplayCameraStateInitializeResult::initialized,
        "allocation test initialization failed");

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    bool succeeded = true;
    for (std::uint64_t step = 1U; step <= 4'096U; ++step) {
        const auto next = proposal(
            state(
                static_cast<float>(step),
                static_cast<std::size_t>(step)));
        if (owner.tryCommit(next, step) !=
                LegacyGameplayCameraStateCommitResult::committed ||
            !owner.tryAcquire().has_value() ||
            !owner.currentSnapshot().has_value()) {
            succeeded = false;
            break;
        }
    }
    trackAllocations.store(false, std::memory_order_release);
    require(succeeded, "allocation-counted hot path failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "camera state hot path performed a heap allocation");
}

} // namespace

int main() {
    try {
        testInitializationPublishesOneCompleteFrame();
        testInvalidInitializationFailsClosed();
        testCommitIsAtomicAndMonotonic();
        testUninitializedCommitFailsClosed();
        testConcurrentProducerConsumerObserveCoherentFrames();
        testHotPathDoesNotAllocate();
        std::cout << "Legacy gameplay camera state owner tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr
            << "Legacy gameplay camera state owner tests failed: "
            << error.what() << '\n';
        return 1;
    }
}
