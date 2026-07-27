#include "airfix/render/PlayerActorPoseRuntime.hpp"

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
#include <type_traits>
#include <utility>
#include <vector>

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

static_assert(!std::is_copy_constructible_v<PlayerActorPoseRuntime>);
static_assert(!std::is_copy_assignable_v<PlayerActorPoseRuntime>);
static_assert(!std::is_move_constructible_v<PlayerActorPoseRuntime>);
static_assert(!std::is_move_assignable_v<PlayerActorPoseRuntime>);

static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::published) ==
    PlayerActorPoseRuntimePublishResult::published);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::wrongOrStaleScene) ==
    PlayerActorPoseRuntimePublishResult::wrongOrStaleScene);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::simulationStepNotIncreasing) ==
    PlayerActorPoseRuntimePublishResult::simulationStepNotIncreasing);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::overrideLimitExceeded) ==
    PlayerActorPoseRuntimePublishResult::overrideLimitExceeded);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::frameByteLimitExceeded) ==
    PlayerActorPoseRuntimePublishResult::frameByteLimitExceeded);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::instanceIndexOutOfRange) ==
    PlayerActorPoseRuntimePublishResult::instanceIndexOutOfRange);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::
            overridesNotStrictlyIncreasing) ==
    PlayerActorPoseRuntimePublishResult::overridesNotStrictlyIncreasing);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::nonFiniteTransform) ==
    PlayerActorPoseRuntimePublishResult::nonFiniteTransform);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::generationExhausted) ==
    PlayerActorPoseRuntimePublishResult::generationExhausted);
static_assert(
    mapPlayerActorPoseRuntimePublishResult(
        DynamicInstancePosePublishResult::busy) ==
    PlayerActorPoseRuntimePublishResult::busy);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] ConvertedNodeTransform transform(
    const Mat3& linear,
    const Vec3 translation,
    const float rawScalar = 1.0F) {
    return {
        .linear = linear,
        .translation = translation,
        .rawScalar = rawScalar,
    };
}

[[nodiscard]] ConvertedNodeTransform identityWorld() {
    return transform({}, {0.0F, 0.0F, 0.0F});
}

[[nodiscard]] ConvertedNodeTransform initialWorld() {
    return transform(
        Mat3{{
            Vec3{0.0F, 2.0F, 0.0F},
            Vec3{-3.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 0.5F},
        }},
        Vec3{11.0F, -13.0F, 17.0F});
}

[[nodiscard]] ConvertedNodeTransform updatedWorld() {
    return transform(
        Mat3{{
            Vec3{1.0F, 0.5F, 0.0F},
            Vec3{0.0F, 2.0F, 0.0F},
            Vec3{0.0F, 0.0F, 3.0F},
        }},
        Vec3{-19.0F, 23.0F, 29.0F});
}

[[nodiscard]] ConvertedNodeTransform local0() {
    return transform(
        Mat3{{
            Vec3{1.0F, 1.0F, 0.0F},
            Vec3{0.0F, 2.0F, 0.0F},
            Vec3{0.0F, 0.0F, 0.5F},
        }},
        Vec3{2.0F, -3.0F, 4.0F},
        7.0F);
}

[[nodiscard]] ConvertedNodeTransform local1() {
    return transform(
        Mat3{{
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{-1.0F, 0.0F, 0.0F},
            Vec3{0.0F, 0.0F, 2.0F},
        }},
        Vec3{-5.0F, 6.0F, 8.0F},
        9.0F);
}

[[nodiscard]] PlayerActorSceneBinding binding() {
    return {
        .firstMeshSlot = 2U,
        .meshCount = 2U,
        .firstInstanceIndex = 4U,
        .instanceCount = 2U,
    };
}

[[nodiscard]] PlayerActorSceneInstanceProvenance provenance(
    const std::uint32_t finalInstanceIndex,
    const ConvertedNodeTransform& actorLocal,
    const std::size_t marker) {
    return {
        .actor =
            {
                .legacySkinSlot = 0U,
                .blueprintIndex = marker,
                .blueprintReference =
                    static_cast<std::uint32_t>(100U + marker),
                .physicalMeshIndex = marker,
            },
        .finalInstanceIndex = finalInstanceIndex,
        .actorLocal = actorLocal,
    };
}

[[nodiscard]] std::vector<PlayerActorSceneInstanceProvenance>
validProvenance() {
    return {
        provenance(4U, local0(), 0U),
        provenance(5U, local1(), 1U),
    };
}

[[nodiscard]] DynamicInstancePoseLimits exactLimits() {
    return {
        .maximumInstances = 6U,
        .maximumOverrides = 2U,
        .maximumFrameBytes =
            2U * sizeof(DynamicInstancePoseOverride),
    };
}

[[nodiscard]] PlayerActorPoseRuntimeBuildResult buildRuntime(
    std::vector<PlayerActorSceneInstanceProvenance> values =
        validProvenance(),
    const ConvertedNodeTransform& world = initialWorld(),
    const std::uint64_t initialStep = 0U) {
    return PlayerActorPoseRuntime::create(
        binding(),
        values,
        6U,
        world,
        initialStep,
        exactLimits());
}

[[nodiscard]] DynamicInstancePoseLease acquire(
    PlayerActorPoseRuntime& runtime,
    const std::string& message) {
    auto lease = runtime.tryAcquire();
    require(lease.has_value(), message);
    return std::move(*lease);
}

void requireTransform(
    const ResolvedInstancePose& actual,
    const ConvertedNodeTransform& expected,
    const std::string& message) {
    require(
        actual.modelLinear == expected.linear &&
            actual.modelTranslation == expected.translation,
        message);
}

void requireBuildIssue(
    const PlayerActorPoseRuntimeBuildResult& result,
    const PlayerActorPoseRuntimeBuildIssueKind kind,
    const std::string& message) {
    require(
        result.runtime == nullptr &&
            result.issue.has_value() &&
            result.issue->kind == kind &&
            result.retainedBytes == 0U,
        message);
}

void testInitialStepAndStaticFallback() {
    auto built = buildRuntime();
    require(built.complete(), "valid runtime factory failed");
    auto lease = acquire(*built.runtime, "initial frame unavailable");
    require(
        lease.simulationStep() == 0U &&
            lease.overrides().size() == 2U,
        "initial step/count was not published");

    requireTransform(
        lease.resolve(4U, {}, {}),
        composeNodeTransforms(initialWorld(), local0()),
        "initial actor transform mismatch");
    const Mat3 staticLinear{{
        Vec3{2.0F, 0.0F, 0.0F},
        Vec3{0.0F, 3.0F, 0.0F},
        Vec3{0.0F, 0.0F, 4.0F},
    }};
    const Vec3 staticTranslation{-7.0F, 8.0F, 9.0F};
    require(
        lease.resolve(0U, staticLinear, staticTranslation) ==
            ResolvedInstancePose{
                .modelLinear = staticLinear,
                .modelTranslation = staticTranslation,
            },
        "static authored fallback was overridden");
}

void testChangedWorldAndIdenticalSuccessiveSteps() {
    auto built = buildRuntime();
    require(built.complete(), "runtime factory failed");
    const auto step1 =
        built.runtime->tryPublish(updatedWorld(), 1U);
    require(
        step1.result == PlayerActorPoseRuntimePublishResult::published,
        "changed step-one world was rejected");
    {
        auto lease = acquire(
            *built.runtime, "step-one frame unavailable");
        const auto expected =
            composeNodeTransforms(updatedWorld(), local0());
        const auto reversed =
            composeNodeTransforms(local0(), updatedWorld());
        const auto actual = lease.resolve(4U, {}, {});
        requireTransform(
            actual,
            expected,
            "non-commutative world/local composition changed");
        require(
            actual.modelLinear != reversed.linear ||
                actual.modelTranslation != reversed.translation,
            "runtime reversed actorWorld/actorLocal");
    }

    require(
        built.runtime->tryPublish(updatedWorld(), 2U).result ==
            PlayerActorPoseRuntimePublishResult::published,
        "identical pose at a later step was rejected");
    auto lease = acquire(*built.runtime, "step-two frame unavailable");
    require(
        lease.simulationStep() == 2U,
        "later identical frame did not advance its step");
    requireTransform(
        lease.resolve(5U, {}, {}),
        composeNodeTransforms(updatedWorld(), local1()),
        "later identical frame changed its transform");
}

void testNonIncreasingStepsLeaveLatestPublication() {
    auto built = buildRuntime();
    require(built.complete(), "runtime factory failed");
    require(
        built.runtime->tryPublish(updatedWorld(), 4U).result ==
            PlayerActorPoseRuntimePublishResult::published,
        "baseline later step failed");
    require(
        built.runtime->tryPublish(initialWorld(), 4U).result ==
            PlayerActorPoseRuntimePublishResult::
                simulationStepNotIncreasing,
        "equal simulation step was accepted");
    require(
        built.runtime->tryPublish(initialWorld(), 3U).result ==
            PlayerActorPoseRuntimePublishResult::
                simulationStepNotIncreasing,
        "stale simulation step was accepted");

    auto lease = acquire(*built.runtime, "latest frame unavailable");
    require(
        lease.simulationStep() == 4U,
        "failed stale publications changed the latest step");
    requireTransform(
        lease.resolve(4U, {}, {}),
        composeNodeTransforms(updatedWorld(), local0()),
        "failed stale publications changed the latest pose");
}

void testInvalidWorldsLeaveLatestPublication() {
    auto built = buildRuntime();
    require(built.complete(), "runtime factory failed");
    require(
        built.runtime->tryPublish(updatedWorld(), 1U).result ==
            PlayerActorPoseRuntimePublishResult::published,
        "baseline update failed");

    auto nonFinite = initialWorld();
    nonFinite.translation.x =
        std::numeric_limits<float>::infinity();
    const auto nonFiniteResult =
        built.runtime->tryPublish(nonFinite, 2U);
    require(
        nonFiniteResult.result ==
                PlayerActorPoseRuntimePublishResult::invalidActorWorld &&
            nonFiniteResult.geometryError ==
                GeometryErrorCode::nonFiniteValue,
        "non-finite world did not return a typed failure");

    auto singular = initialWorld();
    singular.linear.columns[0] = {0.0F, 0.0F, 0.0F};
    const auto singularResult =
        built.runtime->tryPublish(singular, 2U);
    require(
        singularResult.result ==
                PlayerActorPoseRuntimePublishResult::invalidActorWorld &&
            singularResult.geometryError ==
                GeometryErrorCode::singularTransform,
        "singular world did not return a typed failure");

    auto lease = acquire(*built.runtime, "latest valid frame unavailable");
    require(
        lease.simulationStep() == 1U,
        "invalid worlds changed the published step");
    requireTransform(
        lease.resolve(4U, {}, {}),
        composeNodeTransforms(updatedWorld(), local0()),
        "invalid worlds changed the published pose");
}

void testCompositionOverflowLeavesLatestPublication() {
    auto values = validProvenance();
    values[1].actorLocal.translation.x =
        std::numeric_limits<float>::max() * 0.75F;
    auto built = buildRuntime(values, identityWorld());
    require(built.complete(), "large finite local was rejected initially");

    const auto scalingWorld = transform(
        Mat3{{
            Vec3{2.0F, 0.0F, 0.0F},
            Vec3{0.0F, 1.0F, 0.0F},
            Vec3{0.0F, 0.0F, 1.0F},
        }},
        {});
    const auto outcome =
        built.runtime->tryPublish(scalingWorld, 1U);
    require(
        outcome.result ==
                PlayerActorPoseRuntimePublishResult::invalidComposition &&
            outcome.actorInstanceIndex == 1U &&
            outcome.geometryError == GeometryErrorCode::nonFiniteValue,
        "composition overflow did not retain typed diagnostics");

    auto lease = acquire(*built.runtime, "initial frame unavailable");
    require(
        lease.simulationStep() == 0U,
        "composition failure changed the published step");
    requireTransform(
        lease.resolve(5U, {}, {}),
        composeNodeTransforms(identityWorld(), values[1].actorLocal),
        "composition failure changed the published pose");
}

void testExactAndOneBelowLimits() {
    auto exact = buildRuntime();
    require(exact.complete(), "exact runtime limits were rejected");

    auto limits = exactLimits();
    --limits.maximumInstances;
    requireBuildIssue(
        PlayerActorPoseRuntime::create(
            binding(),
            validProvenance(),
            6U,
            initialWorld(),
            0U,
            limits),
        PlayerActorPoseRuntimeBuildIssueKind::instanceLimitNotExact,
        "N-1 instance limit allocated a runtime");

    limits = exactLimits();
    --limits.maximumOverrides;
    requireBuildIssue(
        PlayerActorPoseRuntime::create(
            binding(),
            validProvenance(),
            6U,
            initialWorld(),
            0U,
            limits),
        PlayerActorPoseRuntimeBuildIssueKind::overrideLimitNotExact,
        "N-1 override limit allocated a runtime");

    limits = exactLimits();
    --limits.maximumFrameBytes;
    requireBuildIssue(
        PlayerActorPoseRuntime::create(
            binding(),
            validProvenance(),
            6U,
            initialWorld(),
            0U,
            limits),
        PlayerActorPoseRuntimeBuildIssueKind::frameByteLimitNotExact,
        "N-1 byte limit allocated a runtime");

    if constexpr (
        std::numeric_limits<std::size_t>::max() >
        std::numeric_limits<std::uint32_t>::max()) {
        requireBuildIssue(
            PlayerActorPoseRuntime::create(
                binding(),
                validProvenance(),
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()) +
                    1U,
                initialWorld(),
                0U,
                exactLimits()),
            PlayerActorPoseRuntimeBuildIssueKind::
                totalInstanceCountNotRepresentable,
            "non-uint32 total instance count was accepted");
    }

    auto hugeBinding = binding();
    hugeBinding.instanceCount =
        std::numeric_limits<std::size_t>::max() /
            sizeof(DynamicInstancePoseOverride) +
        1U;
    DynamicInstancePoseLimits hugeLimits{
        .maximumInstances = 6U,
        .maximumOverrides = hugeBinding.instanceCount,
        .maximumFrameBytes = 0U,
    };
    requireBuildIssue(
        PlayerActorPoseRuntime::create(
            hugeBinding,
            validProvenance(),
            6U,
            initialWorld(),
            0U,
            hugeLimits),
        PlayerActorPoseRuntimeBuildIssueKind::frameByteSizeOverflow,
        "overflowing frame-byte product was accepted");

    hugeBinding.instanceCount =
        std::numeric_limits<std::size_t>::max() /
            (2U * sizeof(DynamicInstancePoseOverride)) +
        1U;
    hugeLimits.maximumOverrides = hugeBinding.instanceCount;
    hugeLimits.maximumFrameBytes =
        hugeBinding.instanceCount *
        sizeof(DynamicInstancePoseOverride);
    requireBuildIssue(
        PlayerActorPoseRuntime::create(
            hugeBinding,
            validProvenance(),
            6U,
            initialWorld(),
            0U,
            hugeLimits),
        PlayerActorPoseRuntimeBuildIssueKind::retainedByteSizeOverflow,
        "overflowing four-array retained bytes were accepted");
}

void testEmptyMissingGapsAndTamperingFailClosed() {
    requireBuildIssue(
        PlayerActorPoseRuntime::create(
            std::nullopt,
            validProvenance(),
            6U,
            initialWorld(),
            0U,
            exactLimits()),
        PlayerActorPoseRuntimeBuildIssueKind::missingActorBinding,
        "missing actor binding was accepted");

    auto emptyBinding = binding();
    emptyBinding.instanceCount = 0U;
    const DynamicInstancePoseLimits emptyLimits{
        .maximumInstances = 4U,
        .maximumOverrides = 0U,
        .maximumFrameBytes = 0U,
    };
    requireBuildIssue(
        PlayerActorPoseRuntime::create(
            emptyBinding,
            {},
            4U,
            initialWorld(),
            0U,
            emptyLimits),
        PlayerActorPoseRuntimeBuildIssueKind::emptyActor,
        "zero-instance actor was accepted");

    auto values = validProvenance();
    values[1].finalInstanceIndex = 6U;
    const auto gap = buildRuntime(values);
    requireBuildIssue(
        gap,
        PlayerActorPoseRuntimeBuildIssueKind::initialFrameFailure,
        "gapped actor provenance was accepted");
    require(
        gap.issue->frameIssue ==
            PlayerActorPoseFrameIssueKind::
                provenanceInstanceIndexMismatch,
        "gap lost its upstream typed issue");

    values = validProvenance();
    values[1].finalInstanceIndex = 4U;
    const auto duplicate = buildRuntime(values);
    requireBuildIssue(
        duplicate,
        PlayerActorPoseRuntimeBuildIssueKind::initialFrameFailure,
        "duplicate actor provenance was accepted");
    require(
        duplicate.issue->frameIssue ==
            PlayerActorPoseFrameIssueKind::
                provenanceInstanceIndexMismatch,
        "duplicate lost its upstream typed issue");

    values = validProvenance();
    values[0].actorLocal.linear.columns[0] =
        {0.0F, 0.0F, 0.0F};
    const auto singularLocal = buildRuntime(values);
    requireBuildIssue(
        singularLocal,
        PlayerActorPoseRuntimeBuildIssueKind::initialFrameFailure,
        "singular actor-local tampering was accepted");
    require(
        singularLocal.issue->frameIssue ==
            PlayerActorPoseFrameIssueKind::invalidActorLocalTransform,
        "local tampering lost its upstream typed issue");

    auto invalidInitialWorld = initialWorld();
    invalidInitialWorld.translation.y =
        std::numeric_limits<float>::quiet_NaN();
    const auto invalidInitial = buildRuntime(
        validProvenance(), invalidInitialWorld);
    requireBuildIssue(
        invalidInitial,
        PlayerActorPoseRuntimeBuildIssueKind::initialFrameFailure,
        "invalid initial actor world was accepted");
    require(
        invalidInitial.issue->frameIssue ==
            PlayerActorPoseFrameIssueKind::invalidActorWorldTransform,
        "invalid initial world lost its upstream typed issue");
}

void testBusyWithBothSlotsLeasedAndRecovery() {
    auto built = buildRuntime();
    require(built.complete(), "runtime factory failed");

    std::optional<DynamicInstancePoseLease> first =
        built.runtime->tryAcquire();
    require(first.has_value(), "initial slot lease unavailable");
    require(
        built.runtime->tryPublish(updatedWorld(), 1U).result ==
            PlayerActorPoseRuntimePublishResult::published,
        "second slot publication failed");
    std::optional<DynamicInstancePoseLease> second =
        built.runtime->tryAcquire();
    require(
        second.has_value() && second->simulationStep() == 1U,
        "second slot lease unavailable");

    require(
        built.runtime->tryPublish(initialWorld(), 2U).result ==
            PlayerActorPoseRuntimePublishResult::busy,
        "both leased slots did not report busy");
    first.reset();
    require(
        built.runtime->tryPublish(initialWorld(), 2U).result ==
            PlayerActorPoseRuntimePublishResult::published,
        "producer did not recover after target slot release");
    second.reset();

    auto latest = acquire(*built.runtime, "recovered frame unavailable");
    require(
        latest.simulationStep() == 2U,
        "busy recovery published the wrong step");
}

void testRuntimeDestructionWithActiveLease() {
    auto built = buildRuntime();
    require(built.complete(), "runtime factory failed");
    auto lease = acquire(*built.runtime, "initial lease unavailable");
    built.runtime.reset();

    require(
        lease.valid() && lease.simulationStep() == 0U,
        "active lease did not survive runtime destruction");
    requireTransform(
        lease.resolve(4U, {}, {}),
        composeNodeTransforms(initialWorld(), local0()),
        "surviving lease lost its immutable slot");
}

void testRepeatedPublishDoesNotAllocate() {
    auto built = buildRuntime();
    require(built.complete(), "runtime factory failed");

    allocationCount.store(0U, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_release);
    bool allPublished = true;
    for (std::uint64_t step = 1U; step <= 128U; ++step) {
        if (built.runtime->tryPublish(updatedWorld(), step).result !=
            PlayerActorPoseRuntimePublishResult::published) {
            allPublished = false;
            break;
        }
    }
    trackAllocations.store(false, std::memory_order_release);
    const auto observed =
        allocationCount.load(std::memory_order_relaxed);
    require(allPublished, "repeated bounded publication failed");
    require(observed == 0U, "tryPublish performed a heap allocation");
}

void testRetainedBytesAreExact() {
    auto built = buildRuntime();
    require(built.complete(), "runtime factory failed");
    constexpr std::size_t count = 2U;
    const auto expected =
        count * sizeof(PlayerActorPoseRuntimeSource) +
        3U * count * sizeof(DynamicInstancePoseOverride);
    require(
        built.retainedBytes == expected &&
            built.runtime->retainedBytes() == expected,
        "retained logical bytes did not cover sources, scratch, and two slots");
}

} // namespace

int main() {
    try {
        testInitialStepAndStaticFallback();
        testChangedWorldAndIdenticalSuccessiveSteps();
        testNonIncreasingStepsLeaveLatestPublication();
        testInvalidWorldsLeaveLatestPublication();
        testCompositionOverflowLeavesLatestPublication();
        testExactAndOneBelowLimits();
        testEmptyMissingGapsAndTamperingFailClosed();
        testBusyWithBothSlotsLeasedAndRecovery();
        testRuntimeDestructionWithActiveLease();
        testRepeatedPublishDoesNotAllocate();
        testRetainedBytesAreExact();
        std::cout << "PlayerActorPoseRuntime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        trackAllocations.store(false, std::memory_order_release);
        std::cerr << "PlayerActorPoseRuntime tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
