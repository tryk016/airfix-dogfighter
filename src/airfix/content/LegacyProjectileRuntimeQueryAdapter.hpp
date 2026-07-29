#pragma once

#include "airfix/assets/MissionWorldRooms.hpp"
#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"
#include "airfix/simulation/LegacyMachineGunProjectileCollisionCommit.hpp"

#include <cstdint>
#include <optional>

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
    const assets::MissionWorldRoomCatalog& catalog,
    const render::MissionWorldRuntimeCombinedLineTraceResult& trace,
    bool projectileIsServer,
    LegacyProjectileLiveActorQuery actorQuery,
    void* actorQueryContext) noexcept;

// Executes the implemented projectile-level portal loop against the most
// recently published mission dynamic-collision frame. The catalog must be the
// authenticated catalog parallel to the runtime-owned arena. Creator collision
// BSP control and terminal actor/surface reduction remain higher-level
// transactions and are intentionally not dispatched by this loop-only API.
[[nodiscard]] simulation::LegacyProjectileCollisionLoopResult
resolvePublishedLegacyProjectileCollisionLoop(
    const assets::MissionWorldRoomCatalog& catalog,
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
    const assets::MissionWorldRoomCatalog& catalog,
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
    const assets::MissionWorldRoomCatalog& catalog,
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
    const assets::MissionWorldRoomCatalog& catalog,
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
    const assets::MissionWorldRoomCatalog& catalog,
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
    const assets::MissionWorldRoomCatalog& catalog,
    const render::LegacyGameplayCameraMissionRuntime& runtime,
    const simulation::LegacyMachineGunProjectileState& current,
    const simulation::LegacyMachineGunAmmoProfile& profile,
    const simulation::LegacyProjectileCollisionQueryInput& input,
    bool projectileIsServer,
    const LegacyProjectileCreatorBspGuard& creatorBspGuard,
    const LegacyPublishedProjectileCollisionOptions& options = {}) noexcept;

} // namespace airfix::content
