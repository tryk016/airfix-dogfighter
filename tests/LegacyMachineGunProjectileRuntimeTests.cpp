#include "airfix/simulation/LegacyMachineGunProjectileRuntime.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace {

using namespace airfix::simulation;

std::atomic_bool countAllocations{};
std::atomic_size_t allocationCount{};

[[noreturn]] void fail(const char* const message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* const message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] bool close(
    const float actual,
    const float expected,
    const float tolerance = 0.00001F) {
    return std::fabs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyMachineGunFireState readyFireState(
    const std::uint32_t barrelCount = 1U,
    const std::uint32_t barrelIndex = 0U) {
    return {
        .accumulatedSeconds = 0.12F,
        .ammunition = 5U,
        .barrelIndex = barrelIndex,
        .barrelCount = barrelCount,
        .firing = true,
    };
}

[[nodiscard]] LegacyMachineGunShotRuntimeInput runtimeInput(
    const std::span<const LegacyMachineGunVector3> muzzleOffsets) {
    return {
        .requestedTechLevel = 0U,
        .deltaSeconds = 0.01F,
        .parentAttached = true,
        .creatorPosition = {10.0F, 20.0F, 30.0F},
        .rotatedMuzzleOffsets = muzzleOffsets,
        .aimPointRelativeToCreator = {1.0F, 12.0F, 3.0F},
        .targetLead = std::nullopt,
        .roomId = 17,
        .creatorUid = 41U,
    };
}

void testNoShotDoesNotInspectPoolOrPayload() {
    LegacyMachineGunFireState fireState{
        .accumulatedSeconds = 0.0F,
        .ammunition = 5U,
        .barrelIndex = 0U,
        .barrelCount = 2U,
        .firing = false,
    };
    std::array<LegacyMachineGunProjectileSlot, 1U> slots{};
    slots[0].state.active = true;
    slots[0].generation = 9U;
    auto input = runtimeInput({});
    input.deltaSeconds = 0.25F;
    input.creatorPosition.x =
        std::numeric_limits<float>::quiet_NaN();

    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, input);
    require(
        result.status ==
                LegacyMachineGunShotRuntimeStatus::noShotDue &&
            !result.projectile.has_value() &&
            fireState.accumulatedSeconds == 0.12F &&
            fireState.ammunition == 5U &&
            slots[0].state.active &&
            slots[0].generation == 9U,
        "no-shot refresh inspected payload or changed the pool");
}

void testActivationCommitsStateAndInitializesFirstFreeSlot() {
    const std::array muzzles{
        LegacyMachineGunVector3{7.0F, 8.0F, 9.0F},
        LegacyMachineGunVector3{1.0F, 2.0F, 3.0F},
    };
    auto fireState = readyFireState(2U, 1U);
    std::array<LegacyMachineGunProjectileSlot, 2U> slots{};
    slots[0].generation = 7U;
    slots[1].state.active = true;
    slots[1].generation = 4U;

    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, runtimeInput(muzzles));
    require(
        result.activated() &&
            result.projectile ==
                LegacyMachineGunProjectileHandle{
                    .slotIndex = 0U,
                    .generation = 8U,
                },
        "due shot did not activate the first free generation");
    require(
        close(fireState.accumulatedSeconds, 0.01F) &&
            fireState.ammunition == 4U &&
            fireState.barrelIndex == 0U,
        "activation did not commit cadence/barrel/ammunition");

    const auto& slot = slots[0];
    require(
        slot.state.active &&
            slot.state.position ==
                LegacyMachineGunVector3{11.0F, 22.0F, 33.0F} &&
            slot.state.velocity ==
                LegacyMachineGunVector3{0.0F, 40.0F, 0.0F} &&
            slot.state.roomId == 17 &&
            slot.state.creatorUid == 41U &&
            slot.state.targetUid == 0U &&
            slot.state.ageSeconds == 0.0F &&
            !slot.state.waterContacted &&
            slot.ammoProfile.impactDamage == 3.0F &&
            slot.effectiveTechLevel == 0U &&
            slot.generation == 8U,
        "event-0xE2 slot state/profile mapping mismatch");
    require(
        legacyMachineGunProjectileSlotForHandle(
            std::span{slots}, *result.projectile) == &slots[0],
        "active projectile handle did not resolve");
}

void testCapacityFailureKeepsAdvancedFireState() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
        LegacyMachineGunVector3{},
    };
    auto fireState = readyFireState(2U, 1U);
    std::array<LegacyMachineGunProjectileSlot, 2U> slots{};
    slots[0].state.active = true;
    slots[0].generation = 3U;
    slots[1].state.active = true;
    slots[1].generation = 4U;

    auto input = runtimeInput(muzzles);
    input.creatorPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, input);
    require(
        result.status ==
                LegacyMachineGunShotRuntimeStatus::capacityExhausted &&
            !result.projectile.has_value() &&
            close(fireState.accumulatedSeconds, 0.01F) &&
            fireState.ammunition == 4U &&
            fireState.barrelIndex == 0U &&
            slots[0].state.active &&
            slots[0].generation == 3U &&
            slots[1].state.active &&
            slots[1].generation == 4U,
        "capacity failure consumed payload, rolled back state, or changed pool");
}

void testGenerationFailureKeepsAdvancedFireState() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
    };
    auto fireState = readyFireState();
    std::array<LegacyMachineGunProjectileSlot, 1U> slots{};
    slots[0].generation =
        std::numeric_limits<std::uint64_t>::max();

    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, runtimeInput(muzzles));
    require(
        result.status ==
                LegacyMachineGunShotRuntimeStatus::generationExhausted &&
            close(fireState.accumulatedSeconds, 0.01F) &&
            fireState.ammunition == 4U &&
            fireState.barrelIndex == 0U &&
            !slots[0].state.active &&
            slots[0].generation ==
                std::numeric_limits<std::uint64_t>::max(),
        "generation exhaustion rolled back state or wrapped identity");
}

void testExhaustedGenerationDoesNotBlockLaterFreeSlot() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
    };
    auto fireState = readyFireState();
    std::array<LegacyMachineGunProjectileSlot, 2U> slots{};
    slots[0].generation =
        std::numeric_limits<std::uint64_t>::max();
    slots[1].generation = 5U;

    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, runtimeInput(muzzles));
    require(
        result.activated() &&
            result.projectile ==
                LegacyMachineGunProjectileHandle{
                    .slotIndex = 1U,
                    .generation = 6U,
                } &&
            !slots[0].state.active &&
            slots[0].generation ==
                std::numeric_limits<std::uint64_t>::max() &&
            slots[1].state.active &&
            slots[1].generation == 6U,
        "exhausted generation blocked a later reusable slot");
}

void testEmptyPoolKeepsAdvancedFireState() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
    };
    auto fireState = readyFireState();
    std::span<LegacyMachineGunProjectileSlot> noSlots;

    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, noSlots, runtimeInput(muzzles));
    require(
        result.status ==
                LegacyMachineGunShotRuntimeStatus::capacityExhausted &&
            !result.projectile.has_value() &&
            close(fireState.accumulatedSeconds, 0.01F) &&
            fireState.ammunition == 4U &&
            fireState.barrelIndex == 0U,
        "empty capacity rolled back advanced fire state");
}

void testInvalidTimingInputChangesNothing() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
    };
    auto fireState = readyFireState();
    const auto originalFireState = fireState;
    std::array<LegacyMachineGunProjectileSlot, 1U> slots{};
    slots[0].generation = 12U;
    auto input = runtimeInput(muzzles);
    input.deltaSeconds = -0.01F;

    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, input);
    require(
        result.status ==
                LegacyMachineGunShotRuntimeStatus::invalidInput &&
            fireState.accumulatedSeconds ==
                originalFireState.accumulatedSeconds &&
            fireState.ammunition == originalFireState.ammunition &&
            fireState.barrelIndex == originalFireState.barrelIndex &&
            slots[0].generation == 12U &&
            !slots[0].state.active,
        "invalid input changed fire state or projectile storage");
}

void testRejectedActivationKeepsAdvancedFireState() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
    };
    auto fireState = readyFireState();
    std::array<LegacyMachineGunProjectileSlot, 1U> slots{};
    slots[0].generation = 12U;
    auto input = runtimeInput(muzzles);
    input.creatorPosition.x =
        std::numeric_limits<float>::quiet_NaN();

    const auto result = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, input);
    require(
        result.status ==
                LegacyMachineGunShotRuntimeStatus::activationRejected &&
            !result.projectile.has_value() &&
            close(fireState.accumulatedSeconds, 0.01F) &&
            fireState.ammunition == 4U &&
            fireState.barrelIndex == 0U &&
            slots[0].generation == 12U &&
            !slots[0].state.active,
        "payload rejection rolled back state or partially changed a slot");
}

void testReuseInvalidatesStaleHandle() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
    };
    auto fireState = readyFireState();
    std::array<LegacyMachineGunProjectileSlot, 1U> slots{};

    const auto first = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, runtimeInput(muzzles));
    require(first.activated(), "first reusable-slot activation failed");
    const auto staleHandle = *first.projectile;
    slots[0].state.active = false;
    require(
        legacyMachineGunProjectileSlotForHandle(
            std::span{slots}, staleHandle) == nullptr,
        "deactivated projectile handle remained valid");

    fireState = readyFireState();
    const auto second = legacyMachineGunAdvanceAndActivateShot(
        fireState, slots, runtimeInput(muzzles));
    require(
        second.activated() &&
            second.projectile->generation == 2U &&
            legacyMachineGunProjectileSlotForHandle(
                std::span{slots}, staleHandle) == nullptr &&
            legacyMachineGunProjectileSlotForHandle(
                std::span{slots}, *second.projectile) == &slots[0],
        "slot reuse revived a stale projectile generation");

    const std::span<const LegacyMachineGunProjectileSlot> constSlots{
        slots};
    require(
        legacyMachineGunProjectileSlotForHandle(
            constSlots, *second.projectile) == &slots[0],
        "const active-handle resolution failed");
    require(
        legacyMachineGunProjectileSlotForHandle(
            constSlots,
            {.slotIndex = slots.size(), .generation = 2U}) ==
            nullptr,
        "out-of-range projectile handle resolved");
}

void testHotPathDoesNotAllocate() {
    const std::array muzzles{
        LegacyMachineGunVector3{},
    };
    std::array<LegacyMachineGunProjectileSlot, 1U> slots{};
    auto fireState = readyFireState();
    const auto input = runtimeInput(muzzles);

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t iteration = 0U;
         iteration < 4096U;
         ++iteration) {
        fireState = readyFireState();
        slots[0].state.active = false;
        const auto result = legacyMachineGunAdvanceAndActivateShot(
            fireState, slots, input);
        if (!result.activated()) {
            countAllocations.store(false, std::memory_order_relaxed);
            fail("allocation-counted activation failed");
        }
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "projectile activation performed a heap allocation");
}

} // namespace

void* operator new(const std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc{};
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

void operator delete(
    void* const memory,
    const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](
    void* const memory,
    const std::size_t) noexcept {
    std::free(memory);
}

int main() {
    static_assert(noexcept(
        legacyMachineGunAdvanceAndActivateShot(
            std::declval<LegacyMachineGunFireState&>(),
            std::declval<
                std::span<LegacyMachineGunProjectileSlot>>(),
            std::declval<
                const LegacyMachineGunShotRuntimeInput&>())));
    static_assert(noexcept(
        legacyMachineGunProjectileSlotForHandle(
            std::declval<
                std::span<LegacyMachineGunProjectileSlot>>(),
            std::declval<LegacyMachineGunProjectileHandle>())));

    testNoShotDoesNotInspectPoolOrPayload();
    testActivationCommitsStateAndInitializesFirstFreeSlot();
    testCapacityFailureKeepsAdvancedFireState();
    testGenerationFailureKeepsAdvancedFireState();
    testExhaustedGenerationDoesNotBlockLaterFreeSlot();
    testEmptyPoolKeepsAdvancedFireState();
    testInvalidTimingInputChangesNothing();
    testRejectedActivationKeepsAdvancedFireState();
    testReuseInvalidatesStaleHandle();
    testHotPathDoesNotAllocate();

    std::cout
        << "Legacy machine-gun projectile runtime tests passed\n";
    return EXIT_SUCCESS;
}
