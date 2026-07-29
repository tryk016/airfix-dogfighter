# Weapon reconstruction

This note records only behavior confirmed in the original PE32/x86 modules.
Working names are descriptive and do not claim recovered source identifiers.
The current portable boundary covers primary `WpMGun` timing, the complete
semantic projectile payload, unobstructed projectile motion, and the isolated
actor/surface contact reactions. Private type allocation, room collision
queries, event dispatch, visual instances, and secondary weapons remain
separate work.

## Evidence boundary

The current join uses hash-verified, read-only working copies of:

- `Game/Types/AirCraft.type`, SHA-256
  `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E`;
- `Game/Types/Projectiles.type`, SHA-256
  `639D9F07DD954457CED364E0F1ED2732309819B493422FDAE827D00DC1D76B9F`;
  and
- the previously imported `AfEngine.dll`, which supplies the exported weapon,
  event, actor, and type-instance interfaces.

Ghidra 12.1.2 is canonical for the MSVC class/import context. Rizin 0.9.1
independently confirms the selected function boundaries and instructions.
Vtable pointers supplied guarded entries where either analyzer initially
missed a function. Generated reports, tool projects, binaries, and working
copies remain ignored.

## Attack-event dispatch

`AfVehicle::ProcessEvent` dispatches `0xB9 EVENT_PRIMARY_ATTACK` through the
vehicle vtable offset `+0xC0` and `0xBA EVENT_SECONDARY_ATTACK` through
`+0xC4`. The AirCraft vtable maps those slots to:

| Module RVA | Working name | Behavior |
|---:|---|---|
| `0x00007430` | `AircraftSetPrimaryAttack` | obtains/activates `WpMgun` if needed, rejects firing in water, sets its firing state, and propagates authoritative ammo/attack events |
| `0x000075B0` | `AircraftSetSecondaryAttack` | rejects a missing weapon or water state, then sets the selected secondary weapon's firing state |

The primary and selected-secondary pointers are respectively `AirCraft+0x490`
and `AirCraft+0x494`. Both handlers call weapon vtable offset `+0x34`; the
exported `AfWeapon::Fire(bool)` stores the value in the weapon firing byte.
An AirCraft flag at `+0x208` causes either handler to call
`AfWeapon::SetAmmo(100)` before changing the firing state. Its broader
gameplay meaning is not named yet.

The primary handler also:

1. refreshes the authoritative ammo value through event `0xDC`;
2. broadcasts primary attack event `0xB9` when it is the server; or
3. sends `0xB9` server-only on the relevant client/type path.

The input event therefore changes persistent weapon state. It does not create
a projectile directly.

## WpMGun type and instance

`Projectiles.type::typeCreate` registers `WpMGun` and the five private
ammunition types `WpMGunAmmoTech0` through `WpMGunAmmoTech4`.

The `WpMGun` type factory at RVA `0x0000A5E0` allocates `0x210` bytes and calls
the instance constructor at `0x0000A6E0`. That constructor:

- constructs the `AfWeapon` base;
- installs the primary vtable at virtual address `0x10013904`;
- installs a time-dependant subobject vtable at `WpMGun+0x18`;
- retains the weapon type at `+0x208`;
- applies technology level zero; and
- loads the separate `FxRicochet` type.

The time-dependant vtable identifies RVA `0x0000A910` as the `WpMGun` refresh.
Its `this` value addresses the subobject at `WpMGun+0x18`. Adding that
adjustment produces these instance fields:

| Instance offset | Working meaning |
|---:|---|
| `+0x84` | attached parent vehicle |
| `+0x88` | accumulated shot time in seconds |
| `+0x8C` | current shot interval in seconds |
| `+0x90` | selected private ammunition type |
| `+0x94` | firing flag inherited from `AfWeapon` |
| `+0x98` | ammunition count |
| `+0xA8` | number of muzzle/barrel attachment positions |
| `+0xB0 + 12*n` | local muzzle position `n` |
| `+0x1FC` | next muzzle/barrel index |
| `+0x200` | projectile speed |

Attachment/type loading supplies the barrel count and positions. The portable
code keeps the count explicit and does not guess that data.

## Technology profiles

The technology setter at RVA `0x0000A7E0` caps an integral level above the
supported range to level four, initializes the accumulated time to the chosen
interval, and writes:

| Level | Shot interval | Binary32 bits | Projectile speed |
|---:|---:|---:|---:|
| 0 | `0.12` s | `0x3DF5C28F` | `40` |
| 1 | `0.11` s | `0x3DE147AE` | `55` |
| 2 | `0.10` s | `0x3DCCCCCD` | `70` |
| 3 | `0.09` s | `0x3DB851EC` | `85` |
| 4 | `0.08` s | `0x3DA3D70A` | `100` |

Primary-vtable offset `+0x48` returns `250`; offset `+0x4C` returns `500`.
These are retained as the recovered initial and maximum ammunition constants.

## Refresh and spawn contract

RVA `0x0000A910` accepts the scheduler's signed 64-bit millisecond delta and
multiplies it by exact binary32 `0.001f` (`0x3A83126F`). A zero delta returns
immediately. Otherwise it adds seconds to the accumulator and proceeds only
when a parent vehicle is attached.

Let:

```text
period = shotInterval * (ammunition == 0 ? 10 : 1)
```

The native state transition is:

```text
if !firing:
    if accumulated > period:
        accumulated = period
    return

if accumulated < period:
    return

accumulated -= period
barrel = nextBarrel
nextBarrel = (nextBarrel + 1) modulo barrelCount
if ammunition != 0:
    ammunition -= 1

projectile = selected WpMGunAmmoTechN.CreatePrivateInstance(parent.player)
if projectile creation succeeds:
    build NfEventProjectile(projectile.uid, 0xE2)
    fill room, muzzle world position, direction, speed, owner and target data
    process the event
```

Only one projectile can be requested by one refresh, even when the retained
accumulator spans several periods. A zero ammunition count is preserved and
uses the tenfold period while still reaching the private-instance creation
path; no stronger semantic label such as “empty” or “infinite” is assigned
without runtime evidence.

## Ammunition type and instance

`Projectiles.type::typeCreate` allocates a `0x90`-byte `AfAmmoType` subclass
for each `WpMGunAmmoTech0..4`. Its constructor at RVA `0x0000ADB0` installs
the vtable at virtual address `0x1001396C`, initializes the shared visual-type
helper, and writes:

| Technology | Impact damage at type `+0x48` |
|---:|---:|
| 0 | `3` |
| 1 | `4` |
| 2 | `5` |
| 3 | `6` |
| 4 | `7` |

The five aircraft types pass a zero broad-hit radius. The separate
`WpGroundMGunAmmo` passes `0.07`, which the constructor squares. Consequently,
the subclass event scan at RVA `0x0000B650` is disabled for every aircraft
`WpMGunAmmoTechN`; aircraft hits use the shared projectile collision path.

The type factory at RVA `0x0000AEC0` allocates a `0x198`-byte ammo instance and
calls RVA `0x0000B090`. The constructor:

- constructs `AfAmmo`/`NfProjectile`;
- installs the primary, time-dependant, and visual-helper vtables;
- stores the type pointer;
- sets maximum lifetime to exact `4.0f`;
- sets acceleration to exact `{0, 0xBF96D5CF, 0}`, whose middle component is
  approximately `-1.1784f`; and
- optionally creates the local `mguntracer` visual.

The exact acceleration bits are retained in portable code rather than relying
on decimal-literal rounding.

## Complete event `0xE2`

`NfEventProjectile` is a packed 53-byte event. `WpMGun` fills these semantic
fields before processing it:

| Event offset | Meaning |
|---:|---|
| `+0x11..+0x19` | world muzzle position |
| `+0x1D..+0x25` | projectile velocity |
| `+0x29` | room ID |
| `+0x2D` | creator vehicle UID |
| `+0x31` | target UID, written as zero by this producer |

The position is:

```text
creatorPosition + rotateByCreatorSrt(localMuzzleOffset)
```

The initial direction is the weapon's creator-relative aim point minus the
rotated muzzle offset. If the selected target UID resolves to an active actor,
the weapon adds:

```text
targetVelocity *
    (distance(targetPosition, muzzleWorldPosition) / projectileSpeed)
```

It then uses exact zero-length fallback `{0, 1, 0}`, normalizes when the
length-squared value is not exactly one, and multiplies by projectile speed.
The selected target affects lead only; the event target field remains zero.

`AfAmmo::ProcessEvent` receives `0xE2`, copies all five payload groups into the
shared `NfProjectile` state, resolves the room ID, aligns/places the optional
visual node, and activates the projectile.

## Room identity bridge

`CcWorld` is its root room with ID zero and initializes a monotonically
increasing ordinary-room counter at one. Each ordinary `CcRoom` is prepended to
the world's linked list and receives the current counter before it increments.
`CcWorld::GetRoomById` checks root first, then scans that list newest-first.

`MissionWorldRoomCatalog` already preserves the identical successful-load
snapshot: root at index zero and ordinary rooms newest-created first. Therefore
for a non-root room:

```text
legacyRoomId = catalog.rooms.size() - worldRoomIndex
```

The inverse uses the same subtraction. Allocation-free helpers now implement
both directions and reject incomplete catalogues, negative/out-of-range IDs,
or counts outside the native signed 32-bit domain. This supplies the room
identity needed by event `0xE2` and the retained BSP arena without retaining
native pointers or looking rooms up by name.

## Flight and lifetime

The shared `NfProjectile::ProcessEvent` handles time-step events. For seconds
`dt`, while the projectile is active and its accumulated age is at most four:

```text
segmentEnd =
    position + velocity * dt + acceleration * dt * dt * 0.5
velocity += acceleration * dt
```

The shared collision routine traces the segment before committing the velocity.
Age greater than four deactivates the projectile without advancing it. The
subclass refresh at RVA `0x0000B5D0` synchronizes the optional tracer position
and then invokes the shared projectile refresh.

## Shared collision decision

`PhLine::GetBspCollision` at RVA `0x00038BD0` traces room static BSP first and
then active dynamic-object BSP through one shared strict-nearest fraction.
Visible type-zero portals recursively continue from the contact point into the
target room and combine the inner and outer fractions. This ordering means a
static-only query cannot safely stand in for the full native result: a moving
actor may be nearer.

After each returned hit, `NfProjectile::DetectCollisions` applies:

```text
ownerless                         -> surface
owner material 8                  -> commit endpoint
actor object on non-server        -> commit endpoint
server actor lookup failure       -> surface
disabled projectile/actor gates   -> commit endpoint
eligible actor                    -> actor contact
nonportal/type-1 scene object     -> surface
type-0 scene object               -> change room and query again
```

Actor and surface paths normalize a non-unit normal. Material `15` swaps the
full endpoint into the callback state, sets the projectile water flag, and
leaves fraction interpolation to the subclass; other surfaces interpolate in
the shared layer.

`legacyProjectileCollisionDecision` now preserves this deterministic
single-hit state machine as an allocation-free seam. It exposes endpoint,
portal, actor, and surface outcomes and composes with the existing WpMGun
contact functions.

`buildLegacyDynamicBsp` now constructs the native-style object-local collision
tree from converted mesh triangles. It preserves source triangle/material
provenance, the standard dynamic-polygon cross product, deterministic
splitter scoring, prepend order, negative-A/nonnegative-B children,
`+/-0.000001` classification, `0.0001` fragment rejection, and local bounding
radius. `traceMissionWorldRuntimeCombinedLine` then lets retained room-static
geometry and caller-ordered active object instances compete through one
strict-nearest fraction. Static geometry and earlier objects retain exact
ties; object-local hits return transformed normals and actor/material
provenance without allocating.

`traceMissionWorldRuntimeCombinedPortalLine` now consumes a flat native-order
object publication plus one range per retained room and performs the automatic
visible type-zero tail of `PhLine::GetBspCollision`. It iteratively repeats the
complete static/dynamic competition, replaces portal provenance with a later
hit, composes the whole-segment fraction, returns no collision when no later
hit exists, and stops cycles at an explicit maximum of 256 transitions. The
hot path remains allocation-free.

A first authenticated live boundary now exists for the primary player.
`PlayerActorCollisionAssembly` builds immutable per-mesh colliders from the
same verified CCF and actor-local visual used by the mission loader; its
allocation-free frame adapter publishes the supplied player pose, object ID,
active flag, and current room in exact hierarchy-instance order.

Serialized placed-scene colliders now have a separate immutable boundary.
`MissionPlacedDynamicBspAssembly` binds typed `0x4101` roots to physical mesh
triangles/materials, reproduces first-use mesh caching and per-room object
prepend order, converts unit-scale authored F050 world transforms, and emits
flat ranges directly consumable by the combined portal-line query. It
preserves the observed native quirk that a later cache-reusing `0x4101` object
is not relinked into the room dynamic list.

`LoadedMissionWorldRoom` owns and budgets that placed assembly through the
authenticated publication boundary. Metal preparation then moves it, the
optional player colliders, and the static arena into one non-moving
mission-lifetime runtime. Exact-size reusable object/range buffers are admitted
against the remaining iOS CPU budget. The allocation-free frame publisher
prepends the live player instances in their current room and exposes both mesh
owners as one segmented logical index space, so the combined portal query
copies neither mesh records nor nested arenas.

A live producer must still provide changing player pose/room/ID/active state
and the two recovered projectile-collision gates, publish other actors, and
dispatch callbacks. The separate projectile-level `followPortal` repetition
is a bounded generic coordinator. Its authenticated adapter now consumes the
published mission trace, maps signed room IDs and exact collision materials,
and resolves the primary player's gates from the same complete runtime
generation as its collider. An explicit callback remains for future
non-player actor producers. See
[EXP-20260728-034](../../experiments/EXP-20260728-034-projectile-collision-decision.md),
[EXP-20260728-035](../../experiments/EXP-20260728-035-dynamic-bsp-line-adapter.md),
[EXP-20260728-036](../../experiments/EXP-20260728-036-combined-line-portal-continuation.md),
and
[EXP-20260728-037](../../experiments/EXP-20260728-037-authenticated-player-collision-publication.md),
plus
[EXP-20260728-039](../../experiments/EXP-20260728-039-placed-dynamic-bsp-assembly.md)
and
[EXP-20260729-040](../../experiments/EXP-20260729-040-mission-dynamic-collision-composition.md).
Runtime ownership is recorded in
[EXP-20260729-041](../../experiments/EXP-20260729-041-mission-runtime-dynamic-collision-ownership.md).
The repeated projectile query is recorded in
[EXP-20260729-042](../../experiments/EXP-20260729-042-projectile-collision-portal-loop.md);
its runtime adapter is recorded in
[EXP-20260729-043](../../experiments/EXP-20260729-043-projectile-runtime-query-adapter.md);
the concrete primary-player resolver is recorded in
[EXP-20260729-044](../../experiments/EXP-20260729-044-projectile-player-actor-resolver.md);
and terminal machine-gun state/command reduction is recorded in
[EXP-20260729-045](../../experiments/EXP-20260729-045-projectile-terminal-collision-commit.md).
Creator BSP bracketing is recorded in
[EXP-20260729-046](../../experiments/EXP-20260729-046-projectile-creator-bsp-guard.md).

## Actor damage and surface reaction

The actor-hit override at RVA `0x0000B2C0` emits damage event `0x7D` only when
the creator UID is nonzero and differs from the hit actor UID. Its payload is
the technology damage and creator UID. A valid hit then deactivates the
projectile; a zero creator or self-hit does neither.

The surface callback at RVA `0x0000B330` first ignores a collision whose owner
resolves to the creator actor. Every other surface contact deactivates the
projectile. Material code `15` recomputes the impact position by interpolating
the collision fraction from the prior point to the current point.

When the projectile has a room and `FxRicochet` can be created, the callback
processes this exact ordered parameter set:

| Event | Payload |
|---:|---|
| `0xA0` | scalar `1.0f` |
| `0xA2` | scalar `0.2f` |
| `0xB7` | collision normal |
| `0xB6` | material code |
| `0xA6` | room ID and impact position |

The scalar event meanings are not named more strongly without the effect-type
consumer. The portable result preserves event IDs, values, order-bearing
fields, and the effect-creation request without owning an effect runtime.

## Portable boundary

`LegacyMachineGunFireState` implements:

- the five exact technology profiles;
- initial/maximum ammunition constants;
- accumulated-time and trigger-state behavior;
- zero-ammunition cadence;
- one-shot-per-refresh behavior;
- barrel selection/wrap and nonzero-ammunition decrement; and
- an allocation-free projectile request carrying event `0xE2`, barrel index,
  and projectile speed.

`LegacyMachineGunProjectile` adds:

- the five exact damage profiles, lifetime, acceleration bits, and disabled
  aircraft broad-hit radius;
- complete semantic `0xE2` position, velocity, room, creator, and target data;
- target-velocity lead and the exact zero-direction fallback;
- an unobstructed ballistic/lifetime step;
- the creator/self actor-hit gate and damage command; and
- surface interpolation, deactivation, and a bounded ricochet event request;
  and
- the shared material/portal/actor/surface decision order for one already
  selected nearest collision; and
- a fail-closed terminal commit that updates endpoint, room, activity, and the
  recovered material-15 water flag while returning bounded damage or
  surface/ricochet command data; and
- a synchronous creator `DisableBsp`/conditional `EnableBsp` transaction that
  brackets the complete runtime query, projectile-level portal loop, and
  terminal reducer while preserving missing and already-disabled actors.

`LegacyMachineGunShotCoordinator` composes the timing request with the selected
already-rotated muzzle, capped technology ammo profile, and complete payload.
It returns the already-advanced fire state alongside the prepared shot because
the native refresh cycles the barrel and decrements nonzero ammunition before
private allocation. A later allocation failure must not roll that state back.

The helpers accept seconds because the owning scheduler boundary already
performs the recovered millisecond conversion. They reject non-finite or unsafe
consumed inputs instead of reproducing x87 unordered behavior or invalid
attachment memory. The retained mission query is now wired through the bounded
automatic and projectile-level portal paths, including the atomic
primary-player resolver and the terminal machine-gun state/command reducer.
Ownerless retained-room contacts deactivate but deliberately suppress the
optional ricochet request when no owner-backed material provenance exists.
The remaining live transaction must resolve private types and authored muzzle
transforms, publish and resolve other actor geometry/state, provide the
concrete creator BSP callbacks, dispatch the reduced terminal events/effects,
and honor allocation failure without inventing a projectile or effect.

## Remaining work

- Consume the prepared-shot transaction through private type allocation and
  event dispatch without rolling back fire state on allocation failure.
- Feed the implemented player collider and concrete resolver from the changing
  actor producer, extend the same authenticated publication/resolution to
  other live actors and dynamic portal objects, then connect the implemented
  creator BSP guard and terminal command data to private live actor/effect
  adapters.
- Recreate the optional `mguntracer` and `FxRicochet` visual/effect adapters.
- Trace sample/effect commands associated with a shot.
- Recover secondary weapon selection and each secondary projectile family.
- Confirm ammunition-zero semantics and scheduler/x87 tolerance with controlled
  runtime traces.
