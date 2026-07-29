#pragma once

#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::uint32_t legacyMachineGunDamageEvent = 0x7DU;
inline constexpr std::uint32_t legacyRicochetPrimaryScalarEvent = 0xA0U;
inline constexpr std::uint32_t legacyRicochetSecondaryScalarEvent = 0xA2U;
inline constexpr std::uint32_t legacyRicochetPositionEvent = 0xA6U;
inline constexpr std::uint32_t legacyRicochetMaterialEvent = 0xB6U;
inline constexpr std::uint32_t legacyRicochetNormalEvent = 0xB7U;
inline constexpr std::int32_t legacyMachineGunInterpolatedMaterial = 15;
inline constexpr std::int32_t legacyProjectilePassThroughMaterial = 8;
inline constexpr float legacyRicochetPrimaryScalar = 1.0F;
inline constexpr float legacyRicochetSecondaryScalar = 0.2F;

struct LegacyMachineGunVector3 final {
    float x{};
    float y{};
    float z{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyMachineGunVector3&,
        const LegacyMachineGunVector3&) noexcept = default;
};

struct LegacyMachineGunAmmoProfile final {
    float impactDamage{};
    float maximumLifetimeSeconds{};
    LegacyMachineGunVector3 acceleration{};
    float broadHitRadiusSquared{};
};

// Projectiles.type registers exactly five aircraft WpMGunAmmoTechN types.
// Unlike the weapon technology setter, the ammo-type constructor does not
// define an out-of-range public domain, so unsupported levels are rejected.
[[nodiscard]] std::optional<LegacyMachineGunAmmoProfile>
legacyMachineGunAmmoProfile(std::uint32_t techLevel) noexcept;

struct LegacyMachineGunTargetLead final {
    LegacyMachineGunVector3 position{};
    LegacyMachineGunVector3 velocity{};
};

struct LegacyMachineGunProjectileSpawnInput final {
    LegacyMachineGunVector3 creatorPosition{};
    // The caller applies the parent SRT rotation to the selected authored
    // muzzle attachment before entering this portable boundary.
    LegacyMachineGunVector3 rotatedMuzzleOffset{};
    // The recovered weapon field is relative to the creator origin. Subtracting
    // the rotated muzzle offset produces the initial firing direction.
    LegacyMachineGunVector3 aimPointRelativeToCreator{};
    std::optional<LegacyMachineGunTargetLead> targetLead;
    float projectileSpeed{};
    std::int32_t roomId{};
    std::uint32_t creatorUid{};
};

// Semantic representation of the packed 53-byte NfEventProjectile payload.
// The legacy producer always writes zero to targetUid; a selected target is
// used only for velocity lead prediction.
struct LegacyMachineGunProjectileSpawnPayload final {
    std::uint32_t eventType{};
    LegacyMachineGunVector3 position{};
    LegacyMachineGunVector3 velocity{};
    std::int32_t roomId{};
    std::uint32_t creatorUid{};
    std::uint32_t targetUid{};
};

[[nodiscard]] std::optional<LegacyMachineGunProjectileSpawnPayload>
legacyMachineGunProjectileSpawnPayload(
    const LegacyMachineGunProjectileSpawnInput& input) noexcept;

struct LegacyMachineGunProjectileState final {
    LegacyMachineGunVector3 position{};
    LegacyMachineGunVector3 velocity{};
    float ageSeconds{};
    std::int32_t roomId{};
    std::uint32_t creatorUid{};
    std::uint32_t targetUid{};
    bool active{};
    // NfProjectile's persistent water flag is set by a material-15 terminal
    // surface decision before the subclass surface callback runs.
    bool waterContacted{};

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyMachineGunProjectileState&,
        const LegacyMachineGunProjectileState&) noexcept = default;
};

[[nodiscard]] std::optional<LegacyMachineGunProjectileState>
legacyMachineGunProjectileInitialState(
    const LegacyMachineGunProjectileSpawnPayload& payload,
    const LegacyMachineGunAmmoProfile& profile) noexcept;

struct LegacyMachineGunProjectileFlightStep final {
    LegacyMachineGunProjectileState state{};
    LegacyMachineGunVector3 segmentStart{};
    LegacyMachineGunVector3 segmentEnd{};
    bool deactivatedByLifetime{};
};

// Advances only the unobstructed NfProjectile ballistic path. Collision queries
// remain the responsibility of the room-spatial adapter; a later contact is
// resolved through the actor/surface functions below.
[[nodiscard]] std::optional<LegacyMachineGunProjectileFlightStep>
legacyMachineGunProjectileAdvanceUnobstructed(
    const LegacyMachineGunProjectileState& current,
    const LegacyMachineGunAmmoProfile& profile,
    float deltaSeconds) noexcept;

struct LegacyProjectileCollisionActorOwner final {
    // Nonzero CcObject +0x19C identity. Resolution and gates are supplied by
    // the live actor adapter because retained mission BSP has no actor state.
    std::uint32_t uid{};
    bool projectileIsServer{};
    bool actorResolved{};
    bool projectileActorCollisionsEnabled{};
    bool actorAcceptsProjectileCollision{};
    bool actorActive{};
};

struct LegacyProjectileCollisionHit final {
    float fraction{};
    LegacyMachineGunVector3 normal{};
    // Native owner-backed polygons dereference their material before any
    // portal or actor decision. Absence is accepted only for ownerless hits.
    std::optional<std::int32_t> material;
    bool ownerObjectPresent{};
    // Exact CcObject::GetPortalType domain: -1 nonportal, 0 traversable,
    // 1 reflecting/solid. It is ignored for actor-owned and ownerless hits.
    std::int32_t portalType{-1};
    std::optional<std::int32_t> portalRoomId;
    std::optional<LegacyProjectileCollisionActorOwner> actorOwner;
};

struct LegacyProjectileCollisionStepInput final {
    LegacyMachineGunVector3 segmentStart{};
    LegacyMachineGunVector3 segmentEnd{};
    std::int32_t roomId{};
    std::optional<LegacyProjectileCollisionHit> hit;
};

enum class LegacyProjectileCollisionOutcome : std::uint8_t {
    advanceNoHit,
    advanceMaterialPassThrough,
    advanceActorGate,
    followPortal,
    actorContact,
    surfaceContact,
};

struct LegacyProjectileCollisionDecision final {
    LegacyProjectileCollisionOutcome outcome{
        LegacyProjectileCollisionOutcome::advanceNoHit};
    // Exact semantic equivalents of NfProjectile +0xC0 and +0xCC after the
    // shared transition. For a portal continuation, position is the next
    // trace start and previousPosition still carries the segment endpoint.
    LegacyMachineGunVector3 position{};
    LegacyMachineGunVector3 previousPosition{};
    std::int32_t roomId{};
    float collisionFraction{};
    LegacyMachineGunVector3 normal{};
    std::optional<std::int32_t> material;
    // Present for actorContact and for the surface fallback only when its
    // actor owner was resolved. A failed server actor lookup follows the
    // native surface path without promoting the raw object ID to an actor UID.
    std::optional<std::uint32_t> actorUid;
    // Material 15 sets NfProjectile's water flag before the surface callback.
    bool marksWater{};
};

// Reconstructs the deterministic decision layer of
// NfProjectile::DetectCollisions for one nearest PhLine result. The generic
// LegacyProjectileCollisionLoop repeats a caller-supplied combined
// static/dynamic query after followPortal. Portable input rejects
// non-finite/out-of-segment hits and incomplete owner-backed material/portal
// metadata instead of reproducing unsafe native dereferences.
[[nodiscard]] std::optional<LegacyProjectileCollisionDecision>
legacyProjectileCollisionDecision(
    const LegacyProjectileCollisionStepInput& input) noexcept;

struct LegacyMachineGunDamageCommand final {
    std::uint32_t eventType{legacyMachineGunDamageEvent};
    std::uint32_t targetUid{};
    float damage{};
    std::uint32_t creatorUid{};
    bool deactivateProjectile{true};
};

// Reconstructs the WpMGunAmmo actor-hit override. A zero creator or a hit on
// the creator produces no damage and does not deactivate the projectile.
[[nodiscard]] std::optional<LegacyMachineGunDamageCommand>
legacyMachineGunProjectileActorHit(
    const LegacyMachineGunAmmoProfile& profile,
    std::uint32_t creatorUid,
    std::uint32_t hitActorUid) noexcept;

struct LegacyMachineGunRicochetCommand final {
    std::uint32_t primaryScalarEvent{
        legacyRicochetPrimaryScalarEvent};
    float primaryScalar{legacyRicochetPrimaryScalar};
    std::uint32_t secondaryScalarEvent{
        legacyRicochetSecondaryScalarEvent};
    float secondaryScalar{legacyRicochetSecondaryScalar};
    std::uint32_t normalEvent{legacyRicochetNormalEvent};
    LegacyMachineGunVector3 normal{};
    std::uint32_t materialEvent{legacyRicochetMaterialEvent};
    std::int32_t material{};
    std::uint32_t positionEvent{legacyRicochetPositionEvent};
    LegacyMachineGunVector3 position{};
    std::int32_t roomId{};
};

struct LegacyMachineGunSurfaceContactInput final {
    std::uint32_t creatorUid{};
    std::optional<std::uint32_t> ownerActorUid;
    // These are NfProjectile +0xC0 and +0xCC at entry to the subclass
    // callback. Material 15 interpolates from previousPosition to position.
    LegacyMachineGunVector3 position{};
    LegacyMachineGunVector3 previousPosition{};
    float collisionFraction{};
    LegacyMachineGunVector3 normal{};
    // Ownerless retained-room polygons do not expose owner-backed material.
    // Absence suppresses only the optional ricochet request; contact and
    // deactivation still commit.
    std::optional<std::int32_t> material;
    std::optional<std::int32_t> roomId;
};

struct LegacyMachineGunSurfaceContactResult final {
    LegacyMachineGunVector3 position{};
    bool ignoredCreatorSurface{};
    bool deactivateProjectile{};
    std::optional<LegacyMachineGunRicochetCommand> ricochet;
};

// Resolves the WpMGunAmmo surface callback after the shared projectile
// collision query. The returned ricochet is a request: type lookup/allocation
// and the five native event dispatches belong to the eventual runtime adapter.
[[nodiscard]] std::optional<LegacyMachineGunSurfaceContactResult>
legacyMachineGunProjectileSurfaceContact(
    const LegacyMachineGunSurfaceContactInput& input) noexcept;

} // namespace airfix::simulation
