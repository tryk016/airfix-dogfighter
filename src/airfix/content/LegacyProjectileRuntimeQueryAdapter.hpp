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

struct LegacyPublishedProjectileCollisionOptions final {
    render::MissionWorldRuntimeCombinedPortalLineTraceOptions lineTrace{};
    simulation::LegacyProjectileCollisionLoopOptions collisionLoop{};
};

struct LegacyPublishedMachineGunProjectileCollisionResult final {
    simulation::LegacyProjectileCollisionLoopResult collision{};
    std::optional<
        simulation::LegacyMachineGunProjectileCollisionCommitResult>
        commit;

    [[nodiscard]] constexpr bool committed() const noexcept {
        return collision.completed() &&
            commit.has_value() &&
            commit->committed();
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
// guards and terminal actor/surface callbacks remain a higher-level live-actor
// transaction and are intentionally not dispatched here.
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

} // namespace airfix::content
