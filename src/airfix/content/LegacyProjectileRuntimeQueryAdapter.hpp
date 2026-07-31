#pragma once

#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"
#include "airfix/simulation/LegacyMachineGunProjectileCollisionCommit.hpp"
#include "airfix/simulation/LegacyMachineGunProjectileRuntime.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace airfix::content {

enum class LegacyProjectileLiveActorQueryStatus : std::uint8_t {
    notFound,
    resolved,
    rejected,
};

struct LegacyProjectileLiveActorQueryResult final {
    LegacyProjectileLiveActorQueryStatus status{
        LegacyProjectileLiveActorQueryStatus::rejected};
    bool projectileActorCollisionsEnabled{};
    bool actorAcceptsProjectileCollision{};
    bool actorActive{};
};

using LegacyProjectileLiveActorQuery =
    LegacyProjectileLiveActorQueryResult (*)(
        void* context,
        std::uint32_t actorObjectId) noexcept;

enum class LegacyProjectileCreatorBspDisableStatus : std::uint8_t {
    actorNotFound,
    completed,
    rejected,
};

struct LegacyProjectileCreatorBspDisableResult final {
    LegacyProjectileCreatorBspDisableStatus status{
        LegacyProjectileCreatorBspDisableStatus::rejected};
    // Opaque stable identity for the synchronously retained live actor. It is
    // returned to enableBsp only when DisableBsp reported a previously enabled
    // BSP. The callback owns the handle and must keep it valid for the call.
    void* actorHandle{};
    bool bspWasEnabled{};
};

using LegacyProjectileCreatorBspDisable =
    LegacyProjectileCreatorBspDisableResult (*)(
        void* context,
        std::uint32_t creatorUid) noexcept;

enum class LegacyProjectileCreatorBspEnableStatus : std::uint8_t {
    completed,
    rejected,
};

using LegacyProjectileCreatorBspEnable =
    LegacyProjectileCreatorBspEnableStatus (*)(
        void* context,
        void* actorHandle) noexcept;

struct LegacyProjectileCreatorBspGuard final {
    LegacyProjectileCreatorBspDisable disableBsp{};
    LegacyProjectileCreatorBspEnable enableBsp{};
    void* context{};
};

enum class LegacyProjectileCreatorBspGuardStatus : std::uint8_t {
    notRequested,
    notEntered,
    noCreatorUid,
    actorNotFound,
    alreadyDisabled,
    restored,
    disableRejected,
    enableRejected,
};

struct LegacyPublishedProjectileCollisionOptions final {
    render::MissionWorldRuntimeCombinedPortalLineTraceOptions lineTrace{};
    simulation::LegacyProjectileCollisionLoopOptions collisionLoop{};
};

struct LegacyPublishedMachineGunProjectileCollisionResult final {
    simulation::LegacyProjectileCollisionLoopResult collision{};
    std::optional<
        simulation::LegacyMachineGunProjectileCollisionCommitResult>
        commit;
    LegacyProjectileCreatorBspGuardStatus creatorBspGuard{
        LegacyProjectileCreatorBspGuardStatus::notRequested};

    [[nodiscard]] constexpr bool committed() const noexcept {
        return collision.completed() &&
            commit.has_value() &&
            commit->committed() &&
            creatorBspGuard !=
                LegacyProjectileCreatorBspGuardStatus::enableRejected;
    }
};

enum class LegacyPublishedMachineGunProjectileSlotAdvanceStatus : std::uint8_t {
    notVisited,
    inactive,
    invalidSlot,
    deactivatedByLifetime,
    advanced,
    collisionRejected,
    creatorRestoreFailed,
};

// Parallel result for exactly one caller-owned projectile slot. projectile is
// the pre-step generation identity and may no longer resolve after a lifetime
// or contact deactivation. Damage and surface values are bounded dispatch
// requests; this layer never calls an actor event or effect runtime.
struct LegacyPublishedMachineGunProjectileSlotAdvanceResult final {
    LegacyPublishedMachineGunProjectileSlotAdvanceStatus status{
        LegacyPublishedMachineGunProjectileSlotAdvanceStatus::notVisited};
    std::optional<simulation::LegacyMachineGunProjectileHandle> projectile;
    std::optional<simulation::LegacyProjectileCollisionOutcome> outcome;
    std::optional<simulation::LegacyMachineGunDamageCommand> damage;
    std::optional<simulation::LegacyMachineGunSurfaceContactResult> surface;
    LegacyProjectileCreatorBspGuardStatus creatorBspGuard{
        LegacyProjectileCreatorBspGuardStatus::notRequested};
    std::size_t queryCount{};
    std::size_t portalTransitionCount{};

    [[nodiscard]] constexpr bool stateCommitted() const noexcept {
        return status == LegacyPublishedMachineGunProjectileSlotAdvanceStatus::
                             deactivatedByLifetime ||
               status == LegacyPublishedMachineGunProjectileSlotAdvanceStatus::
                             advanced;
    }
};

enum class LegacyPublishedMachineGunProjectileSlotsAdvanceStatus : std::
    uint8_t {
        completed,
        completedWithRejectedSlots,
        invalidInput,
        outputSizeMismatch,
        creatorRestoreFailed,
    };

struct LegacyPublishedMachineGunProjectileSlotsAdvanceResult final {
    LegacyPublishedMachineGunProjectileSlotsAdvanceStatus status{
        LegacyPublishedMachineGunProjectileSlotsAdvanceStatus::invalidInput};
    std::size_t visitedSlotCount{};
    std::size_t activeSlotCount{};
    std::size_t stateCommitCount{};
    std::size_t rejectedSlotCount{};

    [[nodiscard]] constexpr bool completed() const noexcept {
        return status == LegacyPublishedMachineGunProjectileSlotsAdvanceStatus::
                             completed ||
               status == LegacyPublishedMachineGunProjectileSlotsAdvanceStatus::
                             completedWithRejectedSlots;
    }
};

// Resolves the primary player from the actor state published atomically with
// the runtime collision frame. An unpublished frame or zero query ID is
// rejected; a complete frame without that player ID is a valid lookup miss.
[[nodiscard]] LegacyProjectileLiveActorQueryResult
queryPublishedLegacyProjectilePlayerActor(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    std::uint32_t actorObjectId) noexcept;

// Converts one complete runtime PhLine-equivalent result into the simulation
// collision contract. Static polygons are ownerless. Dynamic polygons carry
// their authenticated CCF 0x2152 value bit-for-bit as native signed material.
//
// Actor lookup happens only on the native server path and only after material
// 8 has been excluded. A missing/rejected resolver fails closed; notFound is a
// valid native surface fallback. The function performs no internal allocation.
[[nodiscard]] simulation::LegacyProjectileCollisionQueryResult
legacyProjectileQueryResultFromRuntimeTrace(
    std::size_t worldRoomCount,
    const render::MissionWorldRuntimeCombinedLineTraceResult& trace,
    bool projectileIsServer,
    LegacyProjectileLiveActorQuery actorQuery,
    void* actorQueryContext) noexcept;

// Executes the implemented projectile-level portal loop against the most
// recently published mission dynamic-collision frame. Legacy CcRoom IDs are
// translated from the immutable room count owned by that same runtime, so the
// temporary build catalog does not have to survive mission publication.
// Creator collision BSP control and terminal actor/surface reduction remain
// higher-level transactions and are intentionally not dispatched by this
// loop-only API.
[[nodiscard]] simulation::LegacyProjectileCollisionLoopResult
resolvePublishedLegacyProjectileCollisionLoop(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyProjectileCollisionQueryInput& input,
    bool projectileIsServer,
    LegacyProjectileLiveActorQuery actorQuery,
    void* actorQueryContext,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

// Primary-player convenience overload. It uses only the actor state committed
// with the current runtime collision frame and therefore cannot observe gates
// from a different geometry generation. Other actors still require the
// explicit callback overload above.
[[nodiscard]] simulation::LegacyProjectileCollisionLoopResult
resolvePublishedLegacyProjectileCollisionLoop(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyProjectileCollisionQueryInput& input,
    bool projectileIsServer,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

// Runs the published collision loop and reduces its terminal result into a
// new portable WpMGunAmmo state plus bounded damage/surface command data.
// The current state must be the already-integrated flight state at
// input.segmentEnd in input.roomId. No private actor event or effect is
// dispatched here.
[[nodiscard]] LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollision(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const simulation::LegacyProjectileCollisionQueryInput& input,
    bool projectileIsServer,
    LegacyProjectileLiveActorQuery actorQuery,
    void* actorQueryContext,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

// Primary-player convenience overload using the actor state committed with
// the published collision frame.
[[nodiscard]] LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollision(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const simulation::LegacyProjectileCollisionQueryInput& input,
    bool projectileIsServer,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

// Mirrors NfProjectile::DetectCollisions' creator BSP transaction. The live
// adapter resolves current.creatorUid and invokes DisableBsp once before the
// complete query/portal/terminal-reduction sequence. EnableBsp receives the
// same opaque actor handle exactly once afterwards only when DisableBsp
// reported that BSP had previously been enabled.
//
// A missing creator is a valid native path. A rejected or malformed disable
// result prevents the query. Enable rejection clears terminal commit/command
// data so callers cannot apply it after restoration failed.
[[nodiscard]] LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollisionWithCreatorBspGuard(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const simulation::LegacyProjectileCollisionQueryInput& input,
    bool projectileIsServer,
    LegacyProjectileLiveActorQuery actorQuery,
    void* actorQueryContext,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

// Primary-player actor-query convenience overload. Creator BSP control still
// comes from the explicit live-actor guard because the immutable published
// collision frame cannot safely expose mutable actor methods.
[[nodiscard]] LegacyPublishedMachineGunProjectileCollisionResult
resolvePublishedLegacyMachineGunProjectileCollisionWithCreatorBspGuard(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const simulation::LegacyProjectileCollisionQueryInput& input,
    bool projectileIsServer,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

// Advances each active generation-tagged slot in stable caller-owned index
// order through the recovered ballistic/lifetime transition and the complete
// published collision transaction. The fixed pool and slot order are explicit
// deterministic port policy, not a native allocator/scheduler claim.
//
// Input shape, delta, and runtime-owned room identity are validated before any
// output or slot changes. Inactive slots do not inspect their profile. One
// malformed slot or rejected collision remains unchanged and does not block a
// later slot. A lifetime expiry commits without touching collision callbacks.
// A successful collision commits state before exposing bounded damage/surface
// requests. Creator-BSP restoration failure aborts the remaining slots and
// must be treated by the caller as a fatal live-runtime condition.
//
// results must have exactly one element per slot. The operation is single-
// writer, bounded, allocation-free, and noexcept. It owns no scheduler,
// projectile producer, live callback dispatch, tracer, ricochet effect, or
// render publication.
[[nodiscard]] LegacyPublishedMachineGunProjectileSlotsAdvanceResult
advancePublishedLegacyMachineGunProjectileSlots(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    std::span<simulation::LegacyMachineGunProjectileSlot> slots,
    std::span<LegacyPublishedMachineGunProjectileSlotAdvanceResult> results,
    float deltaSeconds, bool projectileIsServer,
    LegacyProjectileLiveActorQuery actorQuery, void* actorQueryContext,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

// Primary-player actor-query convenience overload. Mutable creator-BSP
// bracketing remains explicit because the immutable published frame cannot
// expose live actor methods safely.
[[nodiscard]] LegacyPublishedMachineGunProjectileSlotsAdvanceResult
advancePublishedLegacyMachineGunProjectileSlots(
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    std::span<simulation::LegacyMachineGunProjectileSlot> slots,
    std::span<LegacyPublishedMachineGunProjectileSlotAdvanceResult> results,
    float deltaSeconds, bool projectileIsServer,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

} // namespace airfix::content
