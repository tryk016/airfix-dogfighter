#include "airfix/simulation/LegacyMachineGunProjectileCollisionCommit.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
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

[[nodiscard]] LegacyMachineGunAmmoProfile profile() {
    const auto value = legacyMachineGunAmmoProfile(0U);
    require(value.has_value(), "technology-zero profile missing");
    return *value;
}

[[nodiscard]] LegacyMachineGunProjectileState state(
    const std::uint32_t creatorUid = 7U) {
    return {
        .position = {0.0F, 0.0F, 10.0F},
        .velocity = {0.0F, 0.0F, 20.0F},
        .ageSeconds = 1.0F,
        .roomId = 3,
        .creatorUid = creatorUid,
        .targetUid = 0U,
        .active = true,
        .waterContacted = false,
    };
}

[[nodiscard]] LegacyProjectileCollisionLoopResult completed(
    const LegacyProjectileCollisionDecision& decision) {
    return {
        .status = LegacyProjectileCollisionLoopStatus::completed,
        .decision = decision,
        .queryCount = 1U,
        .portalTransitionCount = 0U,
    };
}

[[nodiscard]] LegacyProjectileCollisionDecision noHit() {
    return {
        .outcome = LegacyProjectileCollisionOutcome::advanceNoHit,
        .position = {0.0F, 0.0F, 10.0F},
        .previousPosition = {},
        .roomId = 3,
        .collisionFraction = 0.0F,
        .normal = {},
        .material = std::nullopt,
        .actorUid = std::nullopt,
        .marksWater = false,
    };
}

[[nodiscard]] LegacyProjectileCollisionDecision actorContact(
    const std::uint32_t actorUid) {
    return {
        .outcome = LegacyProjectileCollisionOutcome::actorContact,
        .position = {0.0F, 0.0F, 4.0F},
        .previousPosition = {},
        .roomId = 3,
        .collisionFraction = 0.4F,
        .normal = {0.0F, 0.0F, 1.0F},
        .material = 4,
        .actorUid = actorUid,
        .marksWater = false,
    };
}

[[nodiscard]] LegacyProjectileCollisionDecision surfaceContact(
    const std::optional<std::int32_t> material = 4,
    const std::optional<std::uint32_t> actorUid = std::nullopt) {
    return {
        .outcome = LegacyProjectileCollisionOutcome::surfaceContact,
        .position =
            material == legacyMachineGunInterpolatedMaterial
            ? LegacyMachineGunVector3{0.0F, 0.0F, 10.0F}
            : LegacyMachineGunVector3{0.0F, 0.0F, 4.0F},
        .previousPosition = {},
        .roomId = 3,
        .collisionFraction = 0.4F,
        .normal = {0.0F, 0.0F, 1.0F},
        .material = material,
        .actorUid = actorUid,
        .marksWater =
            material == legacyMachineGunInterpolatedMaterial,
    };
}

void testAdvanceCommit() {
    const auto current = state();
    const auto result =
        commitLegacyMachineGunProjectileCollision(
            current, profile(), completed(noHit()));
    require(
        result.committed() &&
            result.outcome ==
                LegacyProjectileCollisionOutcome::advanceNoHit &&
            result.state == current &&
            !result.damage.has_value() &&
            !result.surface.has_value(),
        "no-hit terminal commit mismatch");
}

void testActorDamageAndSelfGate() {
    const auto current = state();
    auto result = commitLegacyMachineGunProjectileCollision(
        current, profile(), completed(actorContact(91U)));
    require(
        result.committed() &&
            !result.state.active &&
            result.state.position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 4.0F} &&
            result.damage.has_value() &&
            result.damage->targetUid == 91U &&
            result.damage->creatorUid == current.creatorUid &&
            result.damage->damage == 3.0F &&
            result.damage->deactivateProjectile &&
            !result.surface.has_value(),
        "actor damage terminal commit mismatch");

    result = commitLegacyMachineGunProjectileCollision(
        current,
        profile(),
        completed(actorContact(current.creatorUid)));
    require(
        result.committed() &&
            result.state.active &&
            result.state.position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 4.0F} &&
            !result.damage.has_value(),
        "creator actor-hit gate deactivated or damaged");

    const auto ownerless = state(0U);
    result = commitLegacyMachineGunProjectileCollision(
        ownerless,
        profile(),
        completed(actorContact(91U)));
    require(
        result.committed() &&
            result.state.active &&
            !result.damage.has_value(),
        "zero-creator actor hit deactivated or damaged");
}

void testSurfaceAndWaterCommit() {
    const auto current = state();
    auto result = commitLegacyMachineGunProjectileCollision(
        current, profile(), completed(surfaceContact()));
    require(
        result.committed() &&
            !result.state.active &&
            !result.state.waterContacted &&
            result.state.position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 4.0F} &&
            result.surface.has_value() &&
            result.surface->deactivateProjectile &&
            result.surface->ricochet.has_value() &&
            result.surface->ricochet->material == 4,
        "regular surface terminal commit mismatch");

    result = commitLegacyMachineGunProjectileCollision(
        current,
        profile(),
        completed(surfaceContact(
            legacyMachineGunInterpolatedMaterial)));
    require(
        result.committed() &&
            !result.state.active &&
            result.state.waterContacted &&
            result.state.position ==
                LegacyMachineGunVector3{0.0F, 0.0F, 4.0F} &&
            result.surface.has_value() &&
            result.surface->ricochet.has_value() &&
            result.surface->ricochet->position == result.state.position,
        "material-15 surface terminal commit mismatch");
}

void testCreatorAndOwnerlessSurfacePolicies() {
    const auto current = state();
    auto result = commitLegacyMachineGunProjectileCollision(
        current,
        profile(),
        completed(surfaceContact(4, current.creatorUid)));
    require(
        result.committed() &&
            result.state.active &&
            result.surface.has_value() &&
            result.surface->ignoredCreatorSurface &&
            !result.surface->deactivateProjectile &&
            !result.surface->ricochet.has_value(),
        "resolved creator surface was not ignored");

    result = commitLegacyMachineGunProjectileCollision(
        current,
        profile(),
        completed(surfaceContact(std::nullopt)));
    require(
        result.committed() &&
            !result.state.active &&
            result.surface.has_value() &&
            result.surface->deactivateProjectile &&
            !result.surface->ricochet.has_value(),
        "ownerless material policy mismatch");
}

void testFailuresLeaveStateUnchanged() {
    const auto current = state();
    auto inactive = current;
    inactive.active = false;
    const auto inactiveResult =
        commitLegacyMachineGunProjectileCollision(
            inactive, profile(), completed(noHit()));

    LegacyProjectileCollisionLoopResult incomplete{
        .status = LegacyProjectileCollisionLoopStatus::queryRejected,
    };
    const auto incompleteResult =
        commitLegacyMachineGunProjectileCollision(
            current, profile(), incomplete);

    auto portal = noHit();
    portal.outcome = LegacyProjectileCollisionOutcome::followPortal;
    const auto portalResult =
        commitLegacyMachineGunProjectileCollision(
            current, profile(), completed(portal));

    auto inconsistentWater = surfaceContact(4);
    inconsistentWater.marksWater = true;
    const auto waterResult =
        commitLegacyMachineGunProjectileCollision(
            current, profile(), completed(inconsistentWater));

    auto invalidProfile = profile();
    invalidProfile.impactDamage =
        std::numeric_limits<float>::quiet_NaN();
    const auto profileResult =
        commitLegacyMachineGunProjectileCollision(
            current, invalidProfile, completed(noHit()));

    require(
        inactiveResult.status ==
                LegacyMachineGunProjectileCollisionCommitStatus::
                    invalidInput &&
            inactiveResult.state == inactive &&
            incompleteResult.status ==
                LegacyMachineGunProjectileCollisionCommitStatus::
                    incompleteCollision &&
            incompleteResult.state == current &&
            portalResult.status ==
                LegacyMachineGunProjectileCollisionCommitStatus::
                    rejectedTerminalDecision &&
            portalResult.state == current &&
            waterResult.status ==
                LegacyMachineGunProjectileCollisionCommitStatus::
                    rejectedTerminalDecision &&
            waterResult.state == current &&
            profileResult.status ==
                LegacyMachineGunProjectileCollisionCommitStatus::
                    invalidInput &&
            profileResult.state == current,
        "fail-closed commit status or state mismatch");
}

void testSteadyStateDoesNotAllocate() {
    const auto current = state();
    const auto ammo = profile();
    const auto collision = completed(actorContact(91U));
    bool complete = true;

    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t index = 0U; index < 4'096U; ++index) {
        const auto result =
            commitLegacyMachineGunProjectileCollision(
                current, ammo, collision);
        complete = complete &&
            result.committed() &&
            result.damage.has_value() &&
            !result.state.active;
    }
    countAllocations.store(false, std::memory_order_relaxed);

    require(complete, "steady-state terminal commit failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "steady-state terminal commit allocated");
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
        commitLegacyMachineGunProjectileCollision(
            std::declval<
                const LegacyMachineGunProjectileState&>(),
            std::declval<
                const LegacyMachineGunAmmoProfile&>(),
            std::declval<
                const LegacyProjectileCollisionLoopResult&>())));

    testAdvanceCommit();
    testActorDamageAndSelfGate();
    testSurfaceAndWaterCommit();
    testCreatorAndOwnerlessSurfacePolicies();
    testFailuresLeaveStateUnchanged();
    testSteadyStateDoesNotAllocate();

    std::cout
        << "Legacy machine-gun projectile collision commit tests passed\n";
    return EXIT_SUCCESS;
}
