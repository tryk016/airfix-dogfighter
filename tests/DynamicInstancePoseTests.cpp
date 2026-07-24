#include "airfix/render/DynamicInstancePose.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

using namespace airfix::render;

static_assert(!std::is_copy_constructible_v<ScenePoseHandle>);
static_assert(!std::is_copy_assignable_v<ScenePoseHandle>);
static_assert(std::is_nothrow_move_constructible_v<ScenePoseHandle>);
static_assert(std::is_nothrow_move_assignable_v<ScenePoseHandle>);
static_assert(!std::is_copy_constructible_v<DynamicInstancePoseLease>);
static_assert(!std::is_copy_assignable_v<DynamicInstancePoseLease>);
static_assert(
    std::is_nothrow_move_constructible_v<DynamicInstancePoseLease>);
static_assert(
    std::is_nothrow_move_assignable_v<DynamicInstancePoseLease>);
static_assert(!std::is_move_constructible_v<ScenePoseExchange>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] DynamicInstancePoseLimits limits(
    const std::size_t maximumOverrides = 4U,
    const std::size_t maximumFrameBytes =
        4U * sizeof(DynamicInstancePoseOverride)) {
    return {
        .maximumInstances = 8U,
        .maximumOverrides = maximumOverrides,
        .maximumFrameBytes = maximumFrameBytes,
    };
}

[[nodiscard]] DynamicInstancePoseOverride pose(
    const std::uint32_t instanceIndex,
    const float marker) {
    DynamicInstancePoseOverride result{
        .instanceIndex = instanceIndex,
        .modelTranslation = {marker, marker + 1.0F, marker + 2.0F},
    };
    result.modelLinear.columns[0].x = marker;
    return result;
}

[[nodiscard]] ScenePoseHandle begin(
    ScenePoseExchange& exchange,
    const std::uint32_t instanceCount) {
    auto scene = exchange.tryBeginScene(instanceCount);
    require(scene.has_value(), "scene binding failed");
    return std::move(*scene);
}

[[nodiscard]] DynamicInstancePoseLease acquire(
    ScenePoseExchange& exchange,
    const ScenePoseHandle& scene,
    const std::string& message) {
    auto lease = exchange.tryAcquire(scene);
    require(lease.has_value(), message);
    return std::move(*lease);
}

void testWrongAndStaleSceneIdentity() {
    ScenePoseExchange first(limits());
    ScenePoseExchange second(limits());
    auto firstScene = begin(first, 3U);
    auto secondScene = begin(second, 3U);
    const std::array frame{pose(0U, 1.0F)};

    require(
        first.tryPublish(
            secondScene, {1U, frame}) ==
            DynamicInstancePosePublishResult::wrongOrStaleScene,
        "another exchange's scene identity was accepted");
    require(
        second.tryPublish(
            firstScene, {1U, frame}) ==
            DynamicInstancePosePublishResult::wrongOrStaleScene,
        "scene identity was forgeable by equivalent limits/content");
    require(
        first.tryPublish(firstScene, {1U, frame}) ==
            DynamicInstancePosePublishResult::published,
        "valid first-scene publication failed");

    auto held = acquire(
        first, firstScene, "first-scene lease was unavailable");
    require(
        !first.tryBeginScene(3U).has_value(),
        "scene rebound while a slot lease was retained");
    held.reset();

    auto replacement = begin(first, 3U);
    require(
        !firstScene.valid() && replacement.valid(),
        "stale/current scene handle validity was not observable");
    require(
        first.tryPublish(firstScene, {2U, frame}) ==
            DynamicInstancePosePublishResult::wrongOrStaleScene,
        "stale same-exchange scene identity was accepted");
    require(
        !first.tryAcquire(replacement).has_value(),
        "replacement scene inherited the old scene's frame");
    require(
        first.tryPublish(replacement, {0U, {}}) ==
            DynamicInstancePosePublishResult::published,
        "replacement scene did not get a fresh step domain");
}

void testValidationIsAtomic() {
    ScenePoseExchange exchange(limits());
    auto scene = begin(exchange, 3U);
    const std::array valid{pose(1U, 10.0F)};
    require(
        exchange.tryPublish(scene, {10U, valid}) ==
            DynamicInstancePosePublishResult::published,
        "validation baseline publication failed");

    const auto expectRejected =
        [&](const DynamicInstancePosePublishResult expected,
            const std::uint64_t step,
            const std::span<const DynamicInstancePoseOverride> values,
            const std::string& message) {
            require(
                exchange.tryPublish(scene, {step, values}) == expected,
                message);
            auto lease = acquire(
                exchange,
                scene,
                "rejected frame erased the prior publication");
            require(
                lease.simulationStep() == 10U &&
                    lease.publicationGeneration() == 1U &&
                    lease.overrides().size() == 1U &&
                    lease.overrides()[0] == valid[0],
                "rejected frame partially changed the prior frame");
        };

    const std::array outOfRange{pose(3U, 20.0F)};
    expectRejected(
        DynamicInstancePosePublishResult::instanceIndexOutOfRange,
        11U,
        outOfRange,
        "out-of-range instance index was accepted");

    const std::array duplicate{
        pose(1U, 20.0F), pose(1U, 21.0F)};
    expectRejected(
        DynamicInstancePosePublishResult::
            overridesNotStrictlyIncreasing,
        11U,
        duplicate,
        "duplicate instance overrides were accepted");

    const std::array descending{
        pose(2U, 20.0F), pose(1U, 21.0F)};
    expectRejected(
        DynamicInstancePosePublishResult::
            overridesNotStrictlyIncreasing,
        11U,
        descending,
        "unsorted instance overrides were accepted");

    auto nanPose = pose(1U, 20.0F);
    nanPose.modelLinear.columns[2].z =
        std::numeric_limits<float>::quiet_NaN();
    const std::array nanFrame{nanPose};
    expectRejected(
        DynamicInstancePosePublishResult::nonFiniteTransform,
        11U,
        nanFrame,
        "NaN transform was accepted");

    auto infinitePose = pose(1U, 20.0F);
    infinitePose.modelTranslation.y =
        std::numeric_limits<float>::infinity();
    const std::array infiniteFrame{infinitePose};
    expectRejected(
        DynamicInstancePosePublishResult::nonFiniteTransform,
        11U,
        infiniteFrame,
        "infinite transform was accepted");

    expectRejected(
        DynamicInstancePosePublishResult::
            simulationStepNotIncreasing,
        10U,
        valid,
        "duplicate simulation step was accepted");
    expectRejected(
        DynamicInstancePosePublishResult::
            simulationStepNotIncreasing,
        9U,
        valid,
        "backward simulation step was accepted");

    require(
        exchange.tryPublish(scene, {12U, valid}) ==
            DynamicInstancePosePublishResult::published,
        "a forward simulation-step gap was rejected");
}

void testExactAndOverLimits() {
    {
        ScenePoseExchange exchange(limits(2U, 2U *
            sizeof(DynamicInstancePoseOverride)));
        auto scene = begin(exchange, 4U);
        const std::array exact{
            pose(0U, 1.0F), pose(3U, 2.0F)};
        require(
            exchange.tryPublish(scene, {1U, exact}) ==
                DynamicInstancePosePublishResult::published,
            "exact override and byte limits were rejected");

        const std::array over{
            pose(0U, 1.0F),
            pose(1U, 2.0F),
            pose(2U, 3.0F)};
        require(
            exchange.tryPublish(scene, {2U, over}) ==
                DynamicInstancePosePublishResult::
                    overrideLimitExceeded,
            "one-over override count was accepted");
    }
    {
        ScenePoseExchange exchange(limits(
            3U, 2U * sizeof(DynamicInstancePoseOverride)));
        auto scene = begin(exchange, 4U);
        const std::array overBytes{
            pose(0U, 1.0F),
            pose(1U, 2.0F),
            pose(2U, 3.0F)};
        require(
            exchange.tryPublish(scene, {1U, overBytes}) ==
                DynamicInstancePosePublishResult::
                    frameByteLimitExceeded,
            "one-over frame byte limit was accepted");
    }
    {
        ScenePoseExchange exchange(limits(0U, 0U));
        auto scene = begin(exchange, 0U);
        require(
            exchange.tryPublish(scene, {0U, {}}) ==
                DynamicInstancePosePublishResult::published,
            "empty frame at zero limits was rejected");
        const auto lease = acquire(
            exchange, scene, "empty published frame was unavailable");
        require(
            lease.simulationStep() == 0U &&
                lease.overrides().empty(),
            "empty frame gained an override");
    }
}

void testStableLeasesBusyAndReuse() {
    ScenePoseExchange exchange(limits());
    auto scene = begin(exchange, 3U);
    const std::array first{pose(0U, 1.0F)};
    const std::array second{pose(0U, 2.0F)};
    const std::array third{pose(0U, 3.0F)};

    require(
        exchange.tryPublish(scene, {1U, first}) ==
            DynamicInstancePosePublishResult::published,
        "first two-slot publication failed");
    auto oldLease = acquire(
        exchange, scene, "old-slot lease acquisition failed");

    require(
        exchange.tryPublish(scene, {2U, second}) ==
            DynamicInstancePosePublishResult::published,
        "swap while the old front was leased failed");
    auto frontLease = acquire(
        exchange, scene, "new-front lease acquisition failed");
    require(
        oldLease.simulationStep() == 1U &&
            oldLease.publicationGeneration() == 1U &&
            oldLease.overrides()[0] == first[0] &&
            frontLease.simulationStep() == 2U &&
            frontLease.publicationGeneration() == 2U &&
            frontLease.overrides()[0] == second[0],
        "leases mixed data from two frame generations");

    require(
        exchange.tryPublish(scene, {3U, third}) ==
            DynamicInstancePosePublishResult::busy,
        "writer overwrote a retained non-front slot");
    require(
        oldLease.overrides()[0] == first[0] &&
            frontLease.overrides()[0] == second[0],
        "busy publication corrupted either retained slot");

    oldLease.reset();
    require(
        exchange.tryPublish(scene, {3U, third}) ==
            DynamicInstancePosePublishResult::published,
        "released old slot was not reusable");
    const auto newest = acquire(
        exchange, scene, "reused-slot frame was unavailable");
    require(
        newest.simulationStep() == 3U &&
            newest.publicationGeneration() == 3U &&
            newest.overrides()[0] == third[0] &&
            frontLease.simulationStep() == 2U &&
            frontLease.overrides()[0] == second[0],
        "slot reuse destabilized a different held lease");
}

void testAuthoredFallbackLookup() {
    ScenePoseExchange exchange(limits());
    auto scene = begin(exchange, 3U);
    const std::array dynamic{pose(1U, 40.0F)};
    require(
        exchange.tryPublish(scene, {7U, dynamic}) ==
            DynamicInstancePosePublishResult::published,
        "fallback test publication failed");
    const auto lease = acquire(
        exchange, scene, "fallback test lease failed");

    Mat3 authoredLinear;
    authoredLinear.columns[0].x = 99.0F;
    const Vec3 authoredTranslation{90.0F, 91.0F, 92.0F};
    require(
        lease.resolve(
            1U, authoredLinear, authoredTranslation) ==
            ResolvedInstancePose{
                dynamic[0].modelLinear,
                dynamic[0].modelTranslation},
        "exact dynamic override did not win");
    require(
        lease.resolve(
            2U, authoredLinear, authoredTranslation) ==
            ResolvedInstancePose{
                authoredLinear,
                authoredTranslation},
        "missing override did not use authored fallback exactly");
}

void testHandleAndLeaseOutliveExchangeFacade() {
    std::optional<ScenePoseHandle> survivingHandle;
    std::optional<DynamicInstancePoseLease> survivingLease;
    const std::array published{pose(1U, 73.0F)};

    {
        ScenePoseExchange exchange(limits());
        auto scene = begin(exchange, 3U);
        require(
            exchange.tryPublish(scene, {5U, published}) ==
                DynamicInstancePosePublishResult::published,
            "outliving-object test publication failed");
        auto lease = acquire(
            exchange,
            scene,
            "outliving-object test lease failed");
        survivingHandle.emplace(std::move(scene));
        survivingLease.emplace(std::move(lease));
    }

    require(
        survivingHandle.has_value() &&
            !survivingHandle->valid(),
        "exchange destruction did not invalidate its scene handle");
    require(
        survivingLease.has_value() &&
            survivingLease->valid() &&
            survivingLease->simulationStep() == 5U &&
            survivingLease->publicationGeneration() == 1U &&
            survivingLease->overrides().size() == 1U &&
            survivingLease->overrides()[0] == published[0],
        "lease did not retain a stable slot after facade destruction");

    ScenePoseHandle movedClosedHandle(
        std::move(*survivingHandle));
    require(
        !survivingHandle->valid() &&
            !movedClosedHandle.valid(),
        "moving a closed handle revived it or left two owners");

    ScenePoseExchange later(limits());
    auto laterScene = begin(later, 3U);
    require(
        later.tryPublish(movedClosedHandle, {1U, published}) ==
            DynamicInstancePosePublishResult::wrongOrStaleScene,
        "a later exchange accepted a destroyed exchange's identity");
    require(
        later.tryPublish(laterScene, {1U, published}) ==
            DynamicInstancePosePublishResult::published,
        "valid later-exchange identity was rejected");

    survivingLease->reset();
    survivingLease->reset();
    require(
        !survivingLease->valid() &&
            survivingLease->overrides().empty(),
        "post-destruction lease reset was not idempotent");
}

void testLeaseMovesReleaseExactlyOnce() {
    ScenePoseExchange exchange(limits());
    auto scene = begin(exchange, 2U);
    const std::array first{pose(0U, 1.0F)};
    const std::array second{pose(0U, 2.0F)};
    const std::array third{pose(0U, 3.0F)};

    require(
        exchange.tryPublish(scene, {1U, first}) ==
            DynamicInstancePosePublishResult::published,
        "lease-move first publication failed");
    auto firstLease = acquire(
        exchange, scene, "lease-move first acquisition failed");
    require(
        exchange.tryPublish(scene, {2U, second}) ==
            DynamicInstancePosePublishResult::published,
        "lease-move second publication failed");
    auto secondLease = acquire(
        exchange, scene, "lease-move second acquisition failed");

    DynamicInstancePoseLease moved(std::move(firstLease));
    require(
        !firstLease.valid() &&
            firstLease.overrides().empty() &&
            moved.valid() &&
            moved.simulationStep() == 1U,
        "lease move construction did not transfer one slot owner");
    firstLease.reset();
    firstLease.reset();

    moved = std::move(secondLease);
    require(
        !secondLease.valid() &&
            secondLease.overrides().empty() &&
            moved.valid() &&
            moved.simulationStep() == 2U,
        "lease move assignment did not replace the active owner");
    secondLease.reset();
    secondLease.reset();

    require(
        exchange.tryPublish(scene, {3U, third}) ==
            DynamicInstancePosePublishResult::published,
        "move assignment did not release its previous slot");
    const auto newest = acquire(
        exchange, scene, "post-move publication was unavailable");
    require(
        newest.simulationStep() == 3U &&
            newest.overrides()[0] == third[0] &&
            moved.simulationStep() == 2U &&
            moved.overrides()[0] == second[0],
        "lease move/reuse mixed retained slot contents");

    moved.reset();
    moved.reset();
    require(
        !moved.valid() && moved.overrides().empty(),
        "active lease double reset was not idempotent");
}

void testConcurrentSpscCoherence() {
    constexpr std::uint64_t frameCount = 10'000U;
    ScenePoseExchange exchange(limits(1U));
    auto scene = begin(exchange, 1U);
    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};

    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint64_t step = 1U;
             step <= frameCount;
             ++step) {
            const std::array frame{
                pose(0U, static_cast<float>(step))};
            for (;;) {
                const auto result =
                    exchange.tryPublish(scene, {step, frame});
                if (result ==
                    DynamicInstancePosePublishResult::published) {
                    break;
                }
                if (result !=
                    DynamicInstancePosePublishResult::busy) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    std::thread reader([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::uint64_t lastStep = 0U;
        while (lastStep < frameCount &&
               !failed.load(std::memory_order_relaxed)) {
            auto lease = exchange.tryAcquire(scene);
            if (!lease.has_value()) {
                std::this_thread::yield();
                continue;
            }
            const auto step = lease->simulationStep();
            const auto generation =
                lease->publicationGeneration();
            const auto values = lease->overrides();
            if (step < lastStep ||
                generation != step ||
                values.size() != 1U ||
                values[0].instanceIndex != 0U ||
                values[0].modelLinear.columns[0].x !=
                    static_cast<float>(step) ||
                values[0].modelTranslation.x !=
                    static_cast<float>(step)) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            lastStep = step;
        }
    });

    start.store(true, std::memory_order_release);
    writer.join();
    reader.join();
    require(
        !failed.load(std::memory_order_relaxed),
        "concurrent SPSC reader observed a torn/mixed frame");
    const auto finalLease = acquire(
        exchange, scene, "concurrent final frame was unavailable");
    require(
        finalLease.simulationStep() == frameCount &&
            finalLease.publicationGeneration() == frameCount,
        "concurrent SPSC exchange lost its final publication");
}

} // namespace

int main() {
    try {
        testWrongAndStaleSceneIdentity();
        testValidationIsAtomic();
        testExactAndOverLimits();
        testStableLeasesBusyAndReuse();
        testAuthoredFallbackLookup();
        testHandleAndLeaseOutliveExchangeFacade();
        testLeaseMovesReleaseExactlyOnce();
        testConcurrentSpscCoherence();
        std::cout << "Dynamic instance pose tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Dynamic instance pose tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
