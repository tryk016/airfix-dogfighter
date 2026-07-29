#pragma once

#include "airfix/assets/MissionWorldRooms.hpp"
#include "airfix/render/LegacyGameplayCameraMissionRuntime.hpp"
#include "airfix/simulation/LegacyProjectileCollisionLoop.hpp"

#include <cstdint>

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

} // namespace airfix::content
