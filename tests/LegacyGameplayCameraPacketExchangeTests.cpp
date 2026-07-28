#include "airfix/render/LegacyGameplayCameraPacketExchange.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

std::atomic<std::size_t> allocationCount{0U};
std::atomic<bool> countAllocations{false};

[[nodiscard]] void* allocate(const std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const memory = std::malloc(size == 0U ? 1U : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

} // namespace

void* operator new(const std::size_t size) {
    return allocate(size);
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* const memory) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory) noexcept {
    std::free(memory);
}

void operator delete(void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

namespace {

using namespace airfix::render;

static_assert(
    !std::is_copy_constructible_v<LegacyGameplayCameraPacketExchange>);
static_assert(
    !std::is_move_constructible_v<LegacyGameplayCameraPacketExchange>);
static_assert(!std::is_copy_constructible_v<LegacyGameplayCameraPacketLease>);
static_assert(
    std::is_nothrow_move_constructible_v<LegacyGameplayCameraPacketLease>);
static_assert(
    noexcept(std::declval<LegacyGameplayCameraPacketExchange&>().tryPublish(
        std::declval<const LegacyGameplayCameraClipPacket&>())));
static_assert(
    noexcept(std::declval<LegacyGameplayCameraPacketExchange&>().tryAcquire()));

void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] LegacyGameplayCameraClipPacket
packet(const std::uint64_t simulationStep,
       const std::uint64_t cameraGeneration) {
    const float marker = static_cast<float>(simulationStep);
    const Vec3 anchor{marker, marker * 2.0F, marker * 3.0F};
    const LegacyGameplayCameraFrameSnapshot frame{
        .simulationStep = simulationStep,
        .publicationGeneration = cameraGeneration,
        .state =
            {
                .roomState =
                    {
                        .runtimeWorldPosition =
                            {
                                anchor.x,
                                anchor.y + 0.1F,
                                anchor.z - 0.75F,
                            },
                        .worldRoomIndex =
                            static_cast<std::size_t>(simulationStep % 7U),
                    },
                .axisFactors = {1.0F, 1.0F, 1.0F},
            },
    };
    const auto pose = buildLegacyGameplayCameraPoseSnapshot(frame, anchor);
    require(pose.complete(), "packet fixture pose failed");
    const auto built = buildLegacyGameplayCameraClipPacket(*pose.snapshot);
    require(built.complete(), "packet fixture clip build failed");
    return *built.packet;
}

void requireLease(const LegacyGameplayCameraPacketLease& lease,
                  const std::uint64_t exchangeGeneration,
                  const std::uint64_t simulationStep,
                  const std::uint64_t cameraGeneration,
                  const char* const message) {
    const auto* value = lease.packet();
    require(lease.valid() && value != nullptr &&
                lease.exchangePublicationGeneration() == exchangeGeneration &&
                lease.simulationStep() == simulationStep &&
                lease.cameraPublicationGeneration() == cameraGeneration &&
                value->pose().vehicleWorldAnchor().x ==
                    static_cast<float>(simulationStep),
            message);
}

void testInitialPacketIsImmediatelyAcquirable() {
    const auto initial = packet(0U, 1U);
    LegacyGameplayCameraPacketExchange exchange(initial);
    const auto lease = exchange.tryAcquire();
    require(LegacyGameplayCameraPacketExchange::retainedBytes() >=
                    2U * sizeof(LegacyGameplayCameraClipPacket) &&
                lease.has_value(),
            "initial packet storage or lease was invalid");
    requireLease(*lease, 1U, 0U, 1U, "initial lease was incoherent");
}

void testRegressionsPreserveLatestPacket() {
    LegacyGameplayCameraPacketExchange exchange(packet(3U, 7U));
    const auto first = packet(4U, 8U);
    require(exchange.tryPublish(first) ==
                LegacyGameplayCameraPacketPublishResult::published,
            "baseline packet publication failed");

    const auto staleStep = packet(4U, 9U);
    const auto staleGeneration = packet(5U, 8U);
    require(exchange.tryPublish(staleStep) ==
                    LegacyGameplayCameraPacketPublishResult::
                        simulationStepNotIncreasing &&
                exchange.tryPublish(staleGeneration) ==
                    LegacyGameplayCameraPacketPublishResult::
                        cameraGenerationNotIncreasing,
            "packet regression was accepted");

    const auto latest = exchange.tryAcquire();
    require(latest.has_value(), "regression erased the last accepted packet");
    requireLease(
        *latest, 2U, 4U, 8U, "regression changed the last accepted packet");
}

void testBusySlotDoesNotPreventLaterGenerationSkip() {
    LegacyGameplayCameraPacketExchange exchange(packet(0U, 1U));
    auto oldLease = exchange.tryAcquire();
    require(oldLease.has_value(), "old-slot lease unavailable");

    require(exchange.tryPublish(packet(1U, 2U)) ==
                LegacyGameplayCameraPacketPublishResult::published,
            "second-slot publication failed");
    auto frontLease = exchange.tryAcquire();
    require(frontLease.has_value(), "front-slot lease unavailable");

    require(exchange.tryPublish(packet(2U, 3U)) ==
                LegacyGameplayCameraPacketPublishResult::busy,
            "writer overwrote a retained old slot");
    requireLease(
        *oldLease, 1U, 0U, 1U, "busy publication corrupted the old lease");
    requireLease(
        *frontLease, 2U, 1U, 2U, "busy publication corrupted the front lease");

    oldLease.reset();
    require(exchange.tryPublish(packet(3U, 4U)) ==
                LegacyGameplayCameraPacketPublishResult::published,
            "newer packet after a dropped busy frame was rejected");
    const auto newest = exchange.tryAcquire();
    require(newest.has_value(), "newer packet was unavailable");
    requireLease(
        *newest, 3U, 3U, 4U, "generation skip produced an incoherent packet");
}

void testLeaseMoveAndFacadeLifetime() {
    std::optional<LegacyGameplayCameraPacketLease> surviving;
    {
        auto exchange = std::make_unique<LegacyGameplayCameraPacketExchange>(
            packet(11U, 17U));
        surviving = exchange->tryAcquire();
        require(surviving.has_value(), "surviving lease unavailable");
    }
    requireLease(
        *surviving, 1U, 11U, 17U, "lease did not outlive the exchange facade");

    LegacyGameplayCameraPacketLease moved(std::move(*surviving));
    require(!surviving->valid() && surviving->packet() == nullptr,
            "moved-from lease retained its reader claim");
    requireLease(moved, 1U, 11U, 17U, "moved lease lost the retained packet");
    moved.reset();
    require(!moved.valid(), "reset lease remained valid");
}

void testHotPathDoesNotAllocate() {
    LegacyGameplayCameraPacketExchange exchange(packet(0U, 1U));

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    bool complete = true;
    for (std::uint64_t step = 1U; step <= 4'096U; ++step) {
        const auto next = packet(step, step + 1U);
        complete =
            complete && exchange.tryPublish(next) ==
                            LegacyGameplayCameraPacketPublishResult::published;
        auto lease = exchange.tryAcquire();
        complete = complete && lease.has_value() &&
                   lease->simulationStep() == step &&
                   lease->cameraPublicationGeneration() == step + 1U;
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(complete, "allocation probe publication failed");
    require(allocationCount.load(std::memory_order_relaxed) == 0U,
            "camera packet exchange allocated in the hot path");
}

void testConcurrentSpscCoherence() {
    constexpr std::uint64_t frameCount = 20'000U;
    LegacyGameplayCameraPacketExchange exchange(packet(0U, 1U));
    std::atomic<bool> producerDone{false};
    std::atomic<bool> failed{false};

    std::thread producer([&]() {
        for (std::uint64_t step = 1U; step <= frameCount; ++step) {
            const auto next = packet(step, step + 1U);
            auto result = exchange.tryPublish(next);
            while (result == LegacyGameplayCameraPacketPublishResult::busy) {
                std::this_thread::yield();
                result = exchange.tryPublish(next);
            }
            if (result != LegacyGameplayCameraPacketPublishResult::published) {
                failed.store(true, std::memory_order_release);
                producerDone.store(true, std::memory_order_release);
                return;
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::uint64_t lastStep = 0U;
    while ((!producerDone.load(std::memory_order_acquire) ||
            lastStep < frameCount) &&
           !failed.load(std::memory_order_acquire)) {
        auto lease = exchange.tryAcquire();
        if (!lease.has_value()) {
            std::this_thread::yield();
        } else {
            const auto* value = lease->packet();
            const auto step = lease->simulationStep();
            if (value == nullptr || step < lastStep ||
                lease->cameraPublicationGeneration() != step + 1U ||
                value->pose().vehicleWorldAnchor().x !=
                    static_cast<float>(step)) {
                failed.store(true, std::memory_order_release);
            } else {
                lastStep = step;
            }
        }
    }
    producer.join();

    require(!failed.load(std::memory_order_acquire),
            "concurrent reader observed a torn or regressed packet");
    const auto finalLease = exchange.tryAcquire();
    require(finalLease.has_value() &&
                finalLease->simulationStep() == frameCount &&
                finalLease->cameraPublicationGeneration() == frameCount + 1U,
            "concurrent exchange lost its final packet");
}

} // namespace

int main() {
    try {
        testInitialPacketIsImmediatelyAcquirable();
        testRegressionsPreserveLatestPacket();
        testBusySlotDoesNotPreventLaterGenerationSkip();
        testLeaseMoveAndFacadeLifetime();
        testHotPathDoesNotAllocate();
        testConcurrentSpscCoherence();
        std::cout << "LegacyGameplayCameraPacketExchange tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        countAllocations.store(false, std::memory_order_relaxed);
        std::cerr << "LegacyGameplayCameraPacketExchange tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
