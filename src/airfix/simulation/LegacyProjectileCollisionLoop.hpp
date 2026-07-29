#pragma once

#include "airfix/simulation/LegacyMachineGunProjectile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::size_t legacyProjectileHardMaximumPortalTransitions =
    256U;

struct LegacyProjectileCollisionQueryInput final {
    LegacyMachineGunVector3 segmentStart{};
    LegacyMachineGunVector3 segmentEnd{};
    std::int32_t roomId{};
};

enum class LegacyProjectileCollisionQueryStatus : std::uint8_t {
    noHit,
    hit,
    rejected,
};

struct LegacyProjectileCollisionQueryResult final {
    LegacyProjectileCollisionQueryStatus status{
        LegacyProjectileCollisionQueryStatus::rejected};
    std::optional<LegacyProjectileCollisionHit> hit;
};

using LegacyProjectileCollisionQuery = LegacyProjectileCollisionQueryResult (*)(
    void* context,
    const LegacyProjectileCollisionQueryInput& input) noexcept;

struct LegacyProjectileCollisionLoopOptions final {
    // The native loop is unbounded. The portable default matches the automatic
    // PhLine portal-continuation adapter and the hard maximum matches the
    // retained-world ceiling.
    std::size_t maximumPortalTransitions{64U};
};

enum class LegacyProjectileCollisionLoopStatus : std::uint8_t {
    completed,
    invalidInput,
    queryRejected,
    decisionRejected,
    portalTransitionLimitExceeded,
};

struct LegacyProjectileCollisionLoopResult final {
    LegacyProjectileCollisionLoopStatus status{
        LegacyProjectileCollisionLoopStatus::invalidInput};
    std::optional<LegacyProjectileCollisionDecision> decision;
    std::size_t queryCount{};
    std::size_t portalTransitionCount{};

    [[nodiscard]] constexpr bool completed() const noexcept {
        return status == LegacyProjectileCollisionLoopStatus::completed &&
            decision.has_value();
    }
};

// Reconstructs the repeated-query portion of NfProjectile::DetectCollisions.
// The callback supplies one complete PhLine result after its own automatic
// visible-portal continuation, together with live actor gates. A remaining
// type-zero projectile portal advances the query start and room while
// preserving the original endpoint; every other decision is terminal.
//
// The callback and this loop are noexcept and allocation-free. Malformed
// callback states, unsafe hit metadata, and excessive/cyclic portal chains
// fail closed without exposing a terminal decision. The future live adapter
// still owns the creator collision guard and terminal actor/surface callback
// dispatch, which bracket the complete native loop rather than one query.
[[nodiscard]] LegacyProjectileCollisionLoopResult
resolveLegacyProjectileCollisionLoop(
    const LegacyProjectileCollisionQueryInput& input,
    LegacyProjectileCollisionQuery query,
    void* queryContext,
    const LegacyProjectileCollisionLoopOptions& options = {}) noexcept;

} // namespace airfix::simulation
