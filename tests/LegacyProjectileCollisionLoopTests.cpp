#include "airfix/simulation/LegacyProjectileCollisionLoop.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <utility>

namespace {

using airfix::simulation::LegacyMachineGunVector3;
using airfix::simulation::LegacyProjectileCollisionActorOwner;
using airfix::simulation::LegacyProjectileCollisionHit;
using airfix::simulation::LegacyProjectileCollisionLoopOptions;
using airfix::simulation::LegacyProjectileCollisionLoopStatus;
using airfix::simulation::LegacyProjectileCollisionOutcome;
using airfix::simulation::LegacyProjectileCollisionQueryInput;
using airfix::simulation::LegacyProjectileCollisionQueryResult;
using airfix::simulation::LegacyProjectileCollisionQueryStatus;
using airfix::simulation::legacyProjectileHardMaximumPortalTransitions;
using airfix::simulation::resolveLegacyProjectileCollisionLoop;

std::atomic_bool countAllocations{};
std::atomic_size_t allocationCount{};

struct ScriptedQuery final {
    std::array<LegacyProjectileCollisionQueryResult, 4U> results{};
    std::array<LegacyProjectileCollisionQueryInput, 4U> observed{};
    std::size_t resultCount{};
    std::size_t callCount{};
};

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] LegacyProjectileCollisionHit portal(
    const float fraction,
    const std::int32_t targetRoom) {
    return {
        .fraction = fraction,
        .normal = {0.0F, 1.0F, 0.0F},
        .material = 4,
        .ownerObjectPresent = true,
        .portalType = 0,
        .portalRoomId = targetRoom,
    };
}

[[nodiscard]] LegacyProjectileCollisionHit surface(
    const float fraction) {
    return {
        .fraction = fraction,
        .normal = {0.0F, 2.0F, 0.0F},
        .material = std::nullopt,
        .ownerObjectPresent = false,
    };
}

[[nodiscard]] LegacyProjectileCollisionQueryResult hit(
    const LegacyProjectileCollisionHit& value) {
    return {
        .status = LegacyProjectileCollisionQueryStatus::hit,
        .hit = value,
    };
}

[[nodiscard]] LegacyProjectileCollisionQueryResult noHit() {
    return {
        .status = LegacyProjectileCollisionQueryStatus::noHit,
        .hit = std::nullopt,
    };
}

[[nodiscard]] LegacyProjectileCollisionQueryResult scriptedQuery(
    void* const context,
    const LegacyProjectileCollisionQueryInput& input) noexcept {
    auto& script = *static_cast<ScriptedQuery*>(context);
    if (script.callCount >= script.resultCount ||
        script.callCount >= script.observed.size()) {
        return {
            .status = LegacyProjectileCollisionQueryStatus::rejected,
            .hit = std::nullopt,
        };
    }
    script.observed[script.callCount] = input;
    return script.results[script.callCount++];
}

void testNoHitCompletesAtEndpoint() {
    ScriptedQuery script;
    script.results[0] = noHit();
    script.resultCount = 1U;

    const LegacyProjectileCollisionQueryInput input{
        .segmentStart = {0.0F, 0.0F, 0.0F},
        .segmentEnd = {10.0F, 0.0F, 0.0F},
        .roomId = 3,
    };
    const auto result = resolveLegacyProjectileCollisionLoop(
        input,
        scriptedQuery,
        &script,
        {.maximumPortalTransitions = 0U});
    require(
        result.completed() &&
            result.queryCount == 1U &&
            result.portalTransitionCount == 0U &&
            result.decision->outcome ==
                LegacyProjectileCollisionOutcome::advanceNoHit &&
            result.decision->position == input.segmentEnd &&
            result.decision->previousPosition == input.segmentStart &&
            result.decision->roomId == 3,
        "no-hit loop result mismatch");
}

void testTwoPortalsThenSurface() {
    ScriptedQuery script;
    script.results[0] = hit(portal(0.25F, 4));
    script.results[1] = hit(portal(0.5F, 5));
    script.results[2] = hit(surface(0.5F));
    script.resultCount = 3U;

    const auto result = resolveLegacyProjectileCollisionLoop(
        {
            .segmentStart = {0.0F, 0.0F, 0.0F},
            .segmentEnd = {10.0F, 0.0F, 0.0F},
            .roomId = 3,
        },
        scriptedQuery,
        &script);
    require(
        result.completed() &&
            result.queryCount == 3U &&
            result.portalTransitionCount == 2U &&
            result.decision->outcome ==
                LegacyProjectileCollisionOutcome::surfaceContact &&
            result.decision->position ==
                LegacyMachineGunVector3{8.125F, 0.0F, 0.0F} &&
            result.decision->previousPosition ==
                LegacyMachineGunVector3{6.25F, 0.0F, 0.0F} &&
            result.decision->roomId == 5 &&
            result.decision->normal ==
                LegacyMachineGunVector3{0.0F, 1.0F, 0.0F},
        "two-portal surface result mismatch");
    require(
        script.observed[0].segmentStart ==
                LegacyMachineGunVector3{0.0F, 0.0F, 0.0F} &&
            script.observed[0].segmentEnd ==
                LegacyMachineGunVector3{10.0F, 0.0F, 0.0F} &&
            script.observed[0].roomId == 3 &&
            script.observed[1].segmentStart ==
                LegacyMachineGunVector3{2.5F, 0.0F, 0.0F} &&
            script.observed[1].segmentEnd ==
                LegacyMachineGunVector3{10.0F, 0.0F, 0.0F} &&
            script.observed[1].roomId == 4 &&
            script.observed[2].segmentStart ==
                LegacyMachineGunVector3{6.25F, 0.0F, 0.0F} &&
            script.observed[2].segmentEnd ==
                LegacyMachineGunVector3{10.0F, 0.0F, 0.0F} &&
            script.observed[2].roomId == 5,
        "portal loop did not preserve the original endpoint");
}

void testPortalThenTerminalMaterialAndActorOutcomes() {
    ScriptedQuery materialScript;
    materialScript.results[0] = hit(portal(0.25F, 4));
    auto materialHit = surface(0.5F);
    materialHit.ownerObjectPresent = true;
    materialHit.material =
        airfix::simulation::legacyProjectilePassThroughMaterial;
    materialScript.results[1] = hit(materialHit);
    materialScript.resultCount = 2U;

    const auto materialResult = resolveLegacyProjectileCollisionLoop(
        {
            .segmentStart = {},
            .segmentEnd = {10.0F, 0.0F, 0.0F},
            .roomId = 3,
        },
        scriptedQuery,
        &materialScript);
    require(
        materialResult.completed() &&
            materialResult.decision->outcome ==
                LegacyProjectileCollisionOutcome::
                    advanceMaterialPassThrough &&
            materialResult.decision->position ==
                LegacyMachineGunVector3{10.0F, 0.0F, 0.0F} &&
            materialResult.decision->previousPosition ==
                LegacyMachineGunVector3{2.5F, 0.0F, 0.0F},
        "material terminal after portal mismatch");

    ScriptedQuery actorScript;
    actorScript.results[0] = hit(portal(0.25F, 4));
    auto actorHit = surface(0.5F);
    actorHit.ownerObjectPresent = true;
    actorHit.material = 4;
    actorHit.actorOwner = LegacyProjectileCollisionActorOwner{
        .uid = 91U,
        .projectileIsServer = true,
        .actorResolved = true,
        .projectileActorCollisionsEnabled = true,
        .actorAcceptsProjectileCollision = true,
        .actorActive = true,
    };
    actorScript.results[1] = hit(actorHit);
    actorScript.resultCount = 2U;
    const auto actorResult = resolveLegacyProjectileCollisionLoop(
        {
            .segmentStart = {},
            .segmentEnd = {10.0F, 0.0F, 0.0F},
            .roomId = 3,
        },
        scriptedQuery,
        &actorScript);
    require(
        actorResult.completed() &&
            actorResult.decision->outcome ==
                LegacyProjectileCollisionOutcome::actorContact &&
            actorResult.decision->position ==
                LegacyMachineGunVector3{6.25F, 0.0F, 0.0F} &&
            actorResult.decision->previousPosition ==
                LegacyMachineGunVector3{2.5F, 0.0F, 0.0F} &&
            actorResult.decision->actorUid ==
                std::optional<std::uint32_t>{91U},
        "actor terminal after portal mismatch");
}

void testQueryAndDecisionFailuresAreTyped() {
    ScriptedQuery rejected;
    rejected.results[0] = {
        .status = LegacyProjectileCollisionQueryStatus::rejected,
        .hit = std::nullopt,
    };
    rejected.resultCount = 1U;
    auto result = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 0},
        scriptedQuery,
        &rejected);
    require(
        result.status ==
                LegacyProjectileCollisionLoopStatus::queryRejected &&
            !result.decision.has_value() &&
            result.queryCount == 1U,
        "query rejection was not typed");

    ScriptedQuery inconsistent;
    inconsistent.results[0] = hit(surface(0.5F));
    inconsistent.results[0].status =
        LegacyProjectileCollisionQueryStatus::noHit;
    inconsistent.resultCount = 1U;
    result = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 0},
        scriptedQuery,
        &inconsistent);
    require(
        result.status ==
            LegacyProjectileCollisionLoopStatus::queryRejected,
        "inconsistent no-hit query was accepted");

    ScriptedQuery missingHit;
    missingHit.results[0] = {
        .status = LegacyProjectileCollisionQueryStatus::hit,
        .hit = std::nullopt,
    };
    missingHit.resultCount = 1U;
    result = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 0},
        scriptedQuery,
        &missingHit);
    require(
        result.status ==
            LegacyProjectileCollisionLoopStatus::queryRejected,
        "hit query without a hit was accepted");

    ScriptedQuery unknownStatus;
    unknownStatus.results[0] = {
        .status =
            static_cast<LegacyProjectileCollisionQueryStatus>(0xFFU),
        .hit = std::nullopt,
    };
    unknownStatus.resultCount = 1U;
    result = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 0},
        scriptedQuery,
        &unknownStatus);
    require(
        result.status ==
            LegacyProjectileCollisionLoopStatus::queryRejected,
        "unknown query status was accepted");

    ScriptedQuery malformed;
    auto malformedHit = surface(0.5F);
    malformedHit.normal.x =
        std::numeric_limits<float>::quiet_NaN();
    malformed.results[0] = hit(malformedHit);
    malformed.resultCount = 1U;
    result = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 0},
        scriptedQuery,
        &malformed);
    require(
        result.status ==
                LegacyProjectileCollisionLoopStatus::decisionRejected &&
            !result.decision.has_value(),
        "malformed hit did not fail at the decision boundary");
}

struct CyclingQuery final {
    std::size_t callCount{};
};

[[nodiscard]] LegacyProjectileCollisionQueryResult cyclingQuery(
    void* const context,
    const LegacyProjectileCollisionQueryInput& input) noexcept {
    auto& state = *static_cast<CyclingQuery*>(context);
    ++state.callCount;
    return hit(portal(0.0F, input.roomId));
}

void testPortalLimitsAndInvalidInput() {
    CyclingQuery cycle;
    const auto zeroBudget = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 7},
        cyclingQuery,
        &cycle,
        {.maximumPortalTransitions = 0U});
    require(
        zeroBudget.status ==
                LegacyProjectileCollisionLoopStatus::
                    portalTransitionLimitExceeded &&
            zeroBudget.queryCount == 1U &&
            zeroBudget.portalTransitionCount == 0U,
        "zero portal budget followed a portal");

    cycle.callCount = 0U;
    const auto limited = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 7},
        cyclingQuery,
        &cycle,
        {.maximumPortalTransitions = 1U});
    require(
        limited.status ==
                LegacyProjectileCollisionLoopStatus::
                    portalTransitionLimitExceeded &&
            !limited.decision.has_value() &&
            limited.queryCount == 2U &&
            limited.portalTransitionCount == 1U &&
            cycle.callCount == 2U,
        "portal transition limit mismatch");

    const auto invalidLimit = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 0},
        cyclingQuery,
        &cycle,
        {
            .maximumPortalTransitions =
                legacyProjectileHardMaximumPortalTransitions + 1U,
        });
    const auto missingQuery = resolveLegacyProjectileCollisionLoop(
        {{}, {1.0F, 0.0F, 0.0F}, 0}, nullptr, nullptr);
    auto nonFiniteInput = LegacyProjectileCollisionQueryInput{
        {},
        {1.0F, 0.0F, 0.0F},
        0,
    };
    nonFiniteInput.segmentStart.x =
        std::numeric_limits<float>::infinity();
    const auto nonFinite = resolveLegacyProjectileCollisionLoop(
        nonFiniteInput, cyclingQuery, &cycle);
    require(
        invalidLimit.status ==
                LegacyProjectileCollisionLoopStatus::invalidInput &&
            invalidLimit.queryCount == 0U &&
            missingQuery.status ==
                LegacyProjectileCollisionLoopStatus::invalidInput &&
            nonFinite.status ==
                LegacyProjectileCollisionLoopStatus::invalidInput,
        "invalid loop input reached the query");
}

struct ReusableQuery final {
    std::size_t callCount{};
};

[[nodiscard]] LegacyProjectileCollisionQueryResult reusableQuery(
    void* const context,
    const LegacyProjectileCollisionQueryInput&) noexcept {
    auto& state = *static_cast<ReusableQuery*>(context);
    const auto call = state.callCount++;
    return call == 0U
        ? hit(portal(0.25F, 4))
        : hit(surface(0.5F));
}

void testSteadyStateDoesNotAllocate() {
    bool complete = true;
    allocationCount.store(0U, std::memory_order_relaxed);
    countAllocations.store(true, std::memory_order_relaxed);
    for (std::size_t index = 0U; index < 4'096U; ++index) {
        ReusableQuery query;
        const auto result = resolveLegacyProjectileCollisionLoop(
            {{}, {10.0F, 0.0F, 0.0F}, 3},
            reusableQuery,
            &query);
        complete = complete && result.completed() &&
            result.queryCount == 2U &&
            result.portalTransitionCount == 1U &&
            result.decision->outcome ==
                LegacyProjectileCollisionOutcome::surfaceContact;
    }
    countAllocations.store(false, std::memory_order_relaxed);
    require(complete, "steady-state collision loop failed");
    require(
        allocationCount.load(std::memory_order_relaxed) == 0U,
        "steady-state collision loop allocated");
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
    static_assert(noexcept(resolveLegacyProjectileCollisionLoop(
        std::declval<
            const LegacyProjectileCollisionQueryInput&>(),
        scriptedQuery,
        nullptr)));

    testNoHitCompletesAtEndpoint();
    testTwoPortalsThenSurface();
    testPortalThenTerminalMaterialAndActorOutcomes();
    testQueryAndDecisionFailuresAreTyped();
    testPortalLimitsAndInvalidInput();
    testSteadyStateDoesNotAllocate();

    std::cout << "Legacy projectile collision-loop tests passed\n";
    return EXIT_SUCCESS;
}
