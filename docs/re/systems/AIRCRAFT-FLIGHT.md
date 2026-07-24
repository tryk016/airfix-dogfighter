# Aircraft flight reconstruction boundary

**Status:** observed, not yet specified or implemented for parity
**Evidence:** `EV-20260724-001`, `EV-20260724-002`,
`EV-20260724-003`, `EV-20260724-004`, `EV-20260724-005`
**Reference build:** SHA-256 values in `docs/evidence/source-manifest.sha256`

This note records the current clean-room boundary around
`Game/Types/AirCraft.type`. It deliberately separates recovered executable
facts from working names and hypotheses. Raw disassembly and decompiler output
remain in the ignored `artifacts/` tree.

The complete static equation, constructor-field, and 12 ms timing contract is
maintained separately in `AIRCRAFT-FLIGHT-LAW.md`. This file retains the wider
system boundary, event joins, and implementation policy.

The player spawn, primary-actor, mission start-room, and selected-skin
hierarchy contract is maintained in `PLAYER-SPAWN.md`. That static identity
does not yet provide the dynamic actor-to-instance publisher needed by the
renderer.

## Method

The plugin is a PE32/i386 module with the public `nfVersion` and `typeCreate`
ABI already described in `STARTUP-PLUGINS.md`. The targeted pass used:

1. the plugin import table to identify control and rigid-body dependencies;
2. the vtable at virtual address `0x1000D4D0` as executable evidence for
   function entry points;
3. `CreateFunctionsAtAddresses.java` to create only those vtable-confirmed
   functions that Ghidra's automatic analysis missed;
4. `ExportCallersOfNamedFunctions.java` to enumerate callers of selected
   imported APIs; and
5. address-selected decompilation for local inspection.

The table has 63 slots, numbered 0 through 62. The manual entry-point step
matters: before the vtable pointers were applied, 16 real functions in this
class were not recognized as functions. Conclusions based on the earlier
automatic function map would therefore have been incomplete.

## Confirmed executable facts

### Type registration

`typeCreate` at `0x10001000` registers 17 named aircraft records through one
constructor at `0x100017F0`:

`AcMustang`, `AcHellcat`, `AcDauntless`, `AcZero`, `AcStuka`, `AcBf109`,
`AcFw190`, `AcMe262`, `AcFiatG50`, `AcSpitfire`, `AcHurricane`, `AcTyphoon`,
`AcBlackWidow`, `AcKomet`, `AuJunker86`, `AuLancaster`, and `AuB17`.

Each registration supplies the name plus the same ten-field numeric tuple.
The tuple values differ by aircraft, but their field meanings are not yet
proven. They must not be named as speed, mass, maneuverability, or another
gameplay property merely because the values appear plausible.

### Instance and rigid-body construction

The instance constructor at `0x10001EA0`:

- calls the `AfVehicle` constructor;
- installs the aircraft vtable;
- initializes a `CcRigidBody` subobject at instance offset `0x238`;
- adds two massive blocks and one massive particle;
- inverts the resulting 3x3 matrix; and
- calls `CcRigidBody::ResetForceAndTorque` at `0x1000203F`.

This establishes rigid-body ownership and construction. It does not establish
the per-frame reset/integration schedule.

### Force-producing aircraft method

Vtable slot 45 points directly to `0x10003F40`; its bounded code range is
`[0x10003F40, 0x100065A3)`. The recovered ABI is
`void __thiscall(AirCraft*, float dt)`. The function:

- derives a matrix from the instance quaternion;
- reads velocity and multiple aircraft-type fields;
- makes numerous calls to `CcRigidBody::ApplyForce`;
- makes multiple calls to `CcRigidBody::ApplyTorqueOnly`;
- has an `ApplyForceOnly` path;
- contains ground, water, and height-dependent branches; and
- multiplies `dt` into propeller animation and timer accumulators.

The durable working name is `AircraftFlightForceStep`. This name describes
observed effects only. The argument is the scheduler-derived per-step delta
after conversion to seconds. AirCraft uses a 12 ms dependant interval and
`AfVehicle::ProcessEvent` applies a `0.001f` millisecond-to-second scale, giving
a nominal `dt` of `0.012f`. The method is not `NfActor::Refresh(__int64)`: the
two ABIs have different argument sizes and stack cleanup.

Selected direct call sites:

| Address | Callee |
|---:|---|
| `0x100042B5` | `CcRigidBody::ApplyForce` |
| `0x100049AF` | `CcRigidBody::ApplyTorqueOnly` |
| `0x10005028` | `CcRigidBody::ApplyForceOnly` |
| `0x10005E66` | `CcRigidBody::ApplyTorqueOnly` |
| `0x100061AC` | `CcRigidBody::ApplyForce` |

There are additional calls between these sites. The function is
branch-dependent, so address order alone is not a claim that every call runs
on every update. Across the complete method there are 15 `ApplyForce`, five
`ApplyTorqueOnly`, and one `ApplyForceOnly` call sites. There is no
`ResetForceAndTorque` or `CalcAuxiliary` call in this method.

The player/AI event pass establishes four control fields that this method
consumes:

| Instance offset | Confirmed write source | Confirmed use |
|---:|---|---|
| `+0x440` | `EVENT_THRUST_APPLY`: `payload * 0.02 * (1/255)` | added to `+0x444` during the gated thrust update |
| `+0x444` | `EVENT_THRUST_SET`: `payload * (1/255)` | clamped to `[0,1]`, smoothed through `+0x560`, and used by force calculations |
| `+0x448` | `EVENT_PITCH_SET`: `payload * 0.043` | read by multiple torque paths |
| `+0x44C` | `EVENT_BANK_SET`: `payload * 0.043` | read by multiple torque and force paths |

The `+0x444` update is inside a branch gated by an as-yet unnamed field at
`+0x98`. `EVENT_THRUST_APPLY` stores a persistent delta/rate in `+0x440`; the
flight step does not clear it. A release event must write zero (or the opposite
held direction) to stop or reverse the change. The exported
`AfVehicle::GetThrottle` returns `+0x444`; that symbol proves the field name,
but does not make the separately named `EVENT_THROTTLE_*` events active for an
aircraft.

### Collision/auxiliary method

Vtable slot 30 points to `[0x10007920, 0x1000851D)`. It consumes a
`PhCollidedPolyList`, constructs collision constraints, attempts movement,
handles static collision, and calls `CcRigidBody::CalcAuxiliary` at
`0x10007AE6` and `0x10008045`. The durable working name is
`AircraftStaticCollisionStep`.

The caller at `NfActor::DetectCollisions` confirms the ABI as
`void __thiscall(AirCraft*, PhCollidedPolyList*)`. It disables BSP participation,
queries static/BSP collisions, invokes primary vtable slot 30 when a list is
present, and then enables BSP participation again. The normal slot-30 path:

1. calls `CalcAuxiliary`;
2. copies the current OBB/position and filters collided polygons;
3. builds a `CcConstraint`;
4. searches for a best free move and adds collision planes;
5. calls `CalcAuxiliary` again;
6. calls `CcRigidBody::StaticCollision` with contact point, negated normal,
   and scalar `0.008`; and
7. writes the corrected position and handles damage/sound branches.

`StaticCollision` performs another `CalcAuxiliary` before computing and
applying its impulse. This slot is the per-vehicle static/BSP collision-list
resolver. Primary vtable slot 29 remains the separate inherited
`AfVehicle::ActorCollision(NfActor*)` hook.

### Scheduler and force lifecycle

The active aircraft update path is now confirmed. `Dogfighter.exe` supplies
QPC-derived time through `NfMain::SetTime`/`SetRealTime`.
`NfTimeHeap::RefreshDependants` selects due dependants and invokes secondary
vtable slot 1 as `Refresh(__int64 delta)`. AirCraft does not override that
secondary slot: an adjustor thunk reaches `NfActor::Refresh`, which emits one
of events `0x76`, `0x77`, or `0x78` with the exact 64-bit time payload.

`AfVehicle::ProcessEvent` converts that millisecond payload to seconds with an
exact `0.001f` global scale. Its active physics path is:

1. `CcODE::EulerODE(body+0x238, dt)`;
2. `CcRigidBody::ResetForceAndTorque`;
3. `CcRigidBody::CalcAuxiliary`;
4. primary vtable slot 45, `AircraftFlightForceStep(dt)`;
5. collision and portal work, including
   `NfActor::DetectCollisions -> AircraftStaticCollisionStep(list)`;
6. position/portal/room update;
7. primary vtable slot 44 at `0x10002F60`, accepting `float dt`;
8. `AfCannon::Refresh(dt)`; and
9. `AfAI::Refresh(dt)`.

The branch that skips physics/collision still invokes slot 44, but not slot 45.
The original C++ names of slots 44 and 45 are unavailable. Timer labels support
the working roles “Physics” for slot 45 and “Refresh” for slot 44; those labels
are semantic descriptions rather than recovered symbols.

`CcODE::EulerODE` itself performs:

1. virtual slot 0 `Derive(state, derivative)`;
2. `state += dt * derivative`; and
3. virtual slot 1 `PostODE()`.

For `CcRigidBody`, `Derive` reads the accumulated linear force and torque,
while `PostODE` normalizes the quaternion. The following reset clears those
accumulators before slot 45 runs. Therefore slot 45 accumulates forces and
torques for the *next* `EulerODE` call; it is not a force-before-integrate
stage in the same update. Collision impulses after slot 45 modify current
momentum/state independently of those next-step accumulators.

The QPC clock is integer milliseconds. `NfTimeDependant` initially writes a
1000 ms interval, then `AfVehicle::AfVehicle` selects 25 ms for a ground unit,
33 ms for a water unit, or 12 ms for other vehicle types. Both AirCraft type
predicates route to the shared zero-return implementation, proving the 12 ms
choice. The dependant scheduler still skips large forward gaps in observed
layers; those gap and target-mask policies are not yet a portable timing
contract.

### Event names and vehicle dispatch

`NfEvent::GetTypeName` at `AfEngine.dll` virtual address `0x10057110`
establishes the relevant event names:

| Event | Symbolic name | Effect in the AirCraft inheritance chain |
|---:|---|---|
| `0x5F` | `EVENT_PITCH_SET` | writes `AfVehicle+0x448` |
| `0x60` | `EVENT_PITCH_APPLY` | no control-field write |
| `0x61` | `EVENT_THROTTLE_SET` | no control-field write |
| `0x62` | `EVENT_THROTTLE_APPLY` | no control-field write |
| `0x63` | `EVENT_THRUST_SET` | writes `AfVehicle+0x444` |
| `0x64` | `EVENT_THRUST_APPLY` | writes `AfVehicle+0x440` |
| `0x65` | `EVENT_BANK_SET` | writes `AfVehicle+0x44C` |
| `0x66` | `EVENT_BANK_APPLY` | no control-field write |
| `0x67` | `EVENT_YAW_SET` | no control-field write |
| `0x68` | `EVENT_YAW_APPLY` | no control-field write |
| `0xB9` | `EVENT_PRIMARY_ATTACK` | primary weapon firing state |
| `0xBA` | `EVENT_SECONDARY_ATTACK` | secondary weapon firing state |

The naming trap is material: aircraft thrust is controlled by
`EVENT_THRUST_SET/APPLY`, not `EVENT_THROTTLE_SET/APPLY`.

`AirCraft::ProcessEvent` at `0x10008530` handles only two unrelated event
values locally and delegates these control events to
`AfVehicle::ProcessEvent` at `0x1001B6C0`. The packed payload is a signed
32-bit integer at event offset `+0x11`. The pitch, bank, and thrust cases are
skipped while an unnamed byte at vehicle offset `+0x460` is nonzero. A
successful nonzero control write also clears two still-unnamed fields at
`+0x458` and `+0x45C`.

The apparent `APPLY` alternatives for pitch/bank and both `THROTTLE` events
fall through the complete `AirCraft -> AfVehicle -> NfActor ->
NfTypeInstance` dispatch chain without modifying the control fields. This is
a confirmed no-op for this inheritance path, not a claim that no other actor
type can consume those enum values.

### Player command and analog mapping

The user-command dispatcher at `Dogfighter.exe` virtual address `0x00412EA0`
joins named commands to the vehicle events:

| Command | Press/held payload | Release behavior |
|---|---:|---|
| `thrustinc` | `EVENT_THRUST_APPLY`, `+255` | `-255` if decrease remains held, otherwise `0` |
| `thrustdec` | `EVENT_THRUST_APPLY`, `-255` | `+255` if increase remains held, otherwise `0` |
| `bankleft` | `EVENT_BANK_SET`, `-32` | `+32` if right remains held, otherwise `0` |
| `bankright` | `EVENT_BANK_SET`, `+32` | `-32` if left remains held, otherwise `0` |
| `pitchup` | `EVENT_PITCH_SET`, `+32` | `-32` if down remains held, otherwise `0` |
| `pitchdown` | `EVENT_PITCH_SET`, `-32` | `+32` if up remains held, otherwise `0` |

This confirms signs relative to the original command labels: positive pitch is
`pitchup`, positive bank is `bankright`, and positive thrust apply is
increase. It does not by itself define the physical world-axis handedness.

The analog dispatcher at `0x00411BB0` uses only `SET` events:

- bank: raw dead zone `abs(raw) < 20`, emission threshold
  `abs(previous - raw) > 9`, payload
  `sign(raw) * min(raw^2 / 200, 32)`;
- pitch: the same dead zone and emission threshold, payload
  `sign(raw) * min(raw^2 / 150, 32)`; and
- thrust: emission threshold `abs(previous - raw) > 9`, payload
  `255 - (raw + 255) / 2`, sent as `EVENT_THRUST_SET`.

No explicit local clamp was found on the thrust analog payload. The downstream
flight step clamps the resulting `+0x444` value to `[0,1]` in its gated update.

### AI control surface

`AIControls` owns eight channels and four arrays of eight floats. `SetValue`
and `AddValue` clamp the raw value to `[-100,+100]`. `GetRelative` maps that
raw interval linearly into the configured per-channel range and caches the
mapped value. `HasChanged` compares the cached and newly mapped floats by exact
equality; there is no epsilon or dead zone in this layer.

The AirCraft AI instance contains this object at `+0x364`. Its constructor and
dispatcher at `0x1000C420` establish these exact routes:

| Control index | Configured range | Emitted event |
|---:|---:|---|
| 0 | `[-255, +255]` | `0x63 EVENT_THRUST_SET` |
| 1 | `[-32, +32]` | `0x5F EVENT_PITCH_SET` |
| 3 | `[-32, +32]` | `0x65 EVENT_BANK_SET` |
| 5 | `[0, 1]` | `0xB9 EVENT_PRIMARY_ATTACK` |
| 6 | `[0, 1]` | `0xBA EVENT_SECONDARY_ATTACK` |

The mapped float is converted through `_ftol` before becoming the signed event
payload. The numeric route is confirmed, but the symbolic names of the
`AICONTROL` indices themselves remain unknown and are not reconstructed from
their consumers.

## Inferred, not yet contractual

- The working labels `AircraftFlightForceStep`,
  `AircraftStaticCollisionStep`, and “post-collision refresh” describe
  confirmed callers/effects but are not recovered original C++ symbols.
- `EVENT_THROTTLE_*` and the unused pitch/bank `APPLY` values may be reserved
  for other actor types or an unused control path; their global producer set
  has not been exhaustively classified.

None of these inferences may be used as parity claims or encoded as flight
constants without another confirming source.

## Still unknown

- the full scheduler target-mask and large-gap policy;
- the complete actor-vs-actor slot-29 order outside the shown static/BSP path;
- the semantic meaning of the `AICONTROL` index enum names;
- the meaning of AirCraft/AfVehicle fields `+0x98`, `+0x458`, `+0x45C`, and
  `+0x460`;
- the gameplay meanings of several constructor fields even though every tuple
  value is now joined to its exact storage and immediate consumers;
- world distance, mass, force, and torque units;
- semantic aerodynamic labels for the now-recovered force, torque, contact,
  throttle, and damping equations;
- the collision scalar's exact range and meaning;
- the point at which fire intent spawns a projectile.

## Portable implementation boundary

Until those unknowns are resolved, the portable simulation may:

- accept the immutable 60 Hz `InputFrame`;
- preserve flight, absolute/relative thrust, camera, combat, rear-view,
  weapon-selection, mission-status, and pause intentions;
- preserve held states and exact discrete edge counts;
- maintain its own completed-step count;
- reject invalid or non-increasing input atomically; and
- produce a canonical cross-compiler diagnostic hash.

It must not yet move or rotate the aircraft, apply throttle, spawn a weapon,
or label provisional constants as original behavior. That narrow state bridge
provides a deterministic integration seam for the recovered flight law without
creating false parity. The current 60 Hz input publication and recovered
nominal 12 ms physics cadence are separate clocks and must remain separate in
the eventual implementation.
