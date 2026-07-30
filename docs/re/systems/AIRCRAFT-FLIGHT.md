# Aircraft flight reconstruction boundary

**Status:** observed, not yet specified or implemented for parity
**Evidence:** `EV-20260724-001`, `EV-20260724-002`,
`EV-20260724-003`, `EV-20260724-004`, `EV-20260724-005`,
`EV-20260728-010`, `EV-20260728-011`, `EV-20260728-012`,
`EV-20260728-013`, `EV-20260728-014`, `EV-20260729-007`,
`EV-20260730-001`, `EV-20260730-003`, `EV-20260730-004`,
`EV-20260730-007`, and `EV-20260730-008`
**Reference build:** SHA-256 values in `docs/evidence/source-manifest.sha256`

This note records the current clean-room boundary around
`Game/Types/AirCraft.type`. It deliberately separates recovered executable
facts from working names and hypotheses. Raw disassembly and decompiler output
remain in the ignored `artifacts/` tree.

The complete static equation, constructor-field, and 12 ms timing contract is
maintained separately in `AIRCRAFT-FLIGHT-LAW.md`. This file retains the wider
system boundary, event joins, and implementation policy.
The Ghidra/Rizin cross-check that names actor health, the vehicle inactive
latch, and the complete gameplay-camera live-input set is recorded in
[EXP-20260728-025](../../experiments/EXP-20260728-025-camera-aircraft-input-contract.md).
The signed 64-bit vehicle rest accumulator, exact two-second sleep gate, and
exported `IsOnGround` field are recorded in
[EXP-20260728-026](../../experiments/EXP-20260728-026-vehicle-rest-sleep-gate.md).
The complete engine phase, five-call audio cadence, sound roles, and modulation
contract are recorded in
[EXP-20260728-028](../../experiments/EXP-20260728-028-aircraft-engine-audio-state.md).
The separate health-gated `enginedive` state and vertical-speed volume
contract are recorded in
[EXP-20260728-029](../../experiments/EXP-20260728-029-aircraft-destroyed-dive-audio.md).
The exact `PITCH_SET`/`BANK_SET` branch boundaries, signed payload, x87
vectors, producer-specific bounds, and timing limits are recorded in
[EXP-20260729-056](../../experiments/EXP-20260729-056-native-pitch-bank-event-research.md).
The follow-up producer, device-priority, synchronous-dispatch, AI-cadence, and
time-heap ordering proof is recorded in
[EXP-20260730-067](../../experiments/EXP-20260730-067-native-pitch-bank-producer-ordering.md).
The exact `CcRigidBody` 13-float layout, Euler/derivative/quaternion equations,
x87 spill schedule, matrix convention, exact-bit vectors, and implementation
NO-GO are recorded in
[EXP-20260730-068](../../experiments/EXP-20260730-068-rigid-body-integration-kernel.md)
and the durable [CcRigidBody boundary](CC-RIGID-BODY.md).
The sibling `TURN_SET` field, discrete producer, and transactional
five-control state owner are recorded in
[EXP-20260730-061](../../experiments/EXP-20260730-061-native-turn-event-control-state.md).
The complete supplied-module search for later x87 environment writers and its
hardware-breakpoint-only dynamic follow-up are recorded in
[EXP-20260730-078](../../experiments/EXP-20260730-078-x87-runtime-policy-static-audit.md).

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
`LegacyAircraftTypeCatalog` now preserves the complete registry order and
every tuple member as its exact source `uint32_t` word. Typed float accessors
use `bit_cast` and perform no arithmetic, so this catalogue does not select a
floating-point policy. The authenticated player descriptor resolves a type
only from the exact canonical
`Game\Objects\AirCrafts\<Ac-name>.object` or
`Game\Objects\Units\<Au-name>.object` identity; generic MODL-root visuals
retain no aircraft type. A name in the wrong directory is rejected. The
publication boundary rejects a missing or forged association. The catalogue
and cross-tool evidence are recorded in
[EXP-20260730-077](../../experiments/EXP-20260730-077-aircraft-runtime-prerequisites.md).

Constructor transformations and behavior-backed roles are recorded in
`AIRCRAFT-FLIGHT-LAW.md`. Unproven members remain neutral `parameter9` and
`parameter10`; they must not be renamed as speed, maneuverability, or another
gameplay property merely because their values appear plausible.

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

`AfVehicle+0x450` is a fifth binary32 control initialized to positive zero and
written by `EVENT_TURN_SET` with the same exact scale as pitch and bank. It is
checked by the vehicle sleep gate, but no AirCraft force-law consumer has been
found. It therefore remains the event-backed `turn` control rather than being
renamed to yaw or assigned a physical axis.

The `+0x444` update is inside a branch gated by current `NfActor` health at
`+0x98`. The exported `NfActor::GetHealth` returns this exact float.
`EVENT_THRUST_APPLY` stores a persistent delta/rate in `+0x440`; the
flight step does not clear it. A release event must write zero (or the opposite
held direction) to stop or reverse the change. The exported
`AfVehicle::GetThrottle` returns `+0x444`; that symbol proves the field name,
but does not make the separately named `EVENT_THROTTLE_*` events active for an
aircraft.

The smoothed thrust at `+0x560` has two exact recurrences. In normal operation
it advances by `(x + 0.3f) * (target - x) * 0.02f`. While byte `+0x564` is set
it advances only by `(target - x) * 0.0004f`. The slot-44 engine-sound state
machine proves that this byte means an active engine-start transition: it sets
the byte when smoothed thrust becomes strictly greater than `0.001f`, maintains
the seconds timer at `+0x55C`, and clears it after the timer becomes strictly
greater than `4.0f`.

Slot 44 evaluates phase changes and the engine parameter stream every fifth
call through the counter at `+0x5E8`; the start timer itself advances on every
call. Its five engine roles are start, stop, idle, running, and turn. Running
and turn pitch depend on smoothed thrust plus speed, idle volume complements
running volume, and turn volume is additionally modulated by the absolute
low-pass orientation `m01` value at `+0x558`. A pure bounded C++20 helper now
preserves this engine-only command contract without owning sound files or a
playback backend.

On the same fifth-call pass, health `<= 0` starts or retains sound ID `0x20`,
registered by the type loader as `enginedive`. Its parameter 1 remains one and
parameter 0 is `clamp(-0.15 * velocity.y - 0.2, 0, 1)`. Positive health stops
an active sample. The original state byte at `+0x566` is not initialized by
the constructor; the separate portable helper deliberately defaults its
caller-owned state to false.

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

This slot also degrades the AirCraft float at `+0x568`. For every collision
response whose normal/velocity dot is strictly greater than `0.9f` and whose
dot times the local collision scalar is strictly greater than `2.0f`, it
accumulates the fourth power of the dot. After the loop it subtracts one half
of the average qualified fourth power from `+0x568`, without clamping.

The constructor initializes `+0x568` to `1.0f`, and the live-health thrust
equation multiplies by it. Slot 44 later adds
`rand() * dt * bit_cast<float>(0x38000100)` while the factor is below one and
then clamps it to `[0,1]`. This establishes the behavior-backed working name
`thrustIntegrityFactor`; it is distinct from current actor health at `+0x98`.

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

This order also fixes thrust-integrity timing. Slot 45 prepares force from the
factor at refresh entry, slot 30 can reduce it after that force is prepared,
and slot 44 slightly recovers and clamps it. The resulting factor is first
consumed by the next force step. A sleeping refresh skips both force and
collision but still performs the slot-44 recovery.

Active refreshes also maintain a signed 64-bit rest duration whose low and
high DWORDs occupy `+0x458` and `+0x45C`. It starts at 1999 ms. When all five
control floats are exactly zero, the vehicle is on the ground with squared
speed below `0.03f`, or a water unit with squared speed below `0.08f`, the
native signed scheduler delta is accumulated. Other active states reset it to
zero. The threshold-crossing step at 2000 ms still runs physics, then resets
force/torque and clears six rigid-body dynamic-state floats; later sleeping
refreshes skip the active path while still invoking slot 44. AirCraft's water
predicate is false, so only its on-ground leg applies. The exported
`AfVehicle::IsOnGround` returns byte `+0x464`.

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
choice. The input/time pump drains all currently obtainable local input before
publishing this QPC-derived time. `NfMain::SetTime` polls remote input before
emitting target-`0xFC` time event `0x4D`; its `NfEngine` branch reaches
`NfTimeHeap::RefreshDependants` for forward differences below 699 ms and skips
that heap call at 699 ms or more. The periodic heap repeatedly advances a due
deadline by 12 ms, so one time publication can invoke zero, one, or multiple
AirCraft refreshes. This is catch-up scheduling, not one input sample per
12 ms. The full target-mask and remote-network policies remain outside the
portable timing contract.

### Event names and vehicle dispatch

`NfEvent::GetTypeName` at `AfEngine.dll` virtual address `0x10057110`
establishes the relevant event names:

| Event | Symbolic name | Effect in the AirCraft inheritance chain |
|---:|---|---|
| `0x5D` | `EVENT_TURN_SET` | writes `AfVehicle+0x450` |
| `0x5E` | `EVENT_TURN_APPLY` | no control-field write |
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
32-bit integer at event offset `+0x11`. The turn, pitch, bank, and thrust cases are
skipped while the inactive latch at vehicle offset `+0x460` is nonzero.
`AfVehicle::Activate` clears this byte; `Deactivate` and death paths set it. A
successful nonzero control write also clears the signed 64-bit rest duration
whose low and high DWORDs occupy `+0x458` and `+0x45C`.

The two thrust cases now have a separate portable typed-write decoder. It
accepts only an already-formed signed native payload and returns a write for
exactly `+0x444` or `+0x440`, plus the proven rest-clear directive. The native
wire type is a full signed `int32`. Recovered command bindings and the AI map
establish `[-255, 255]`; the analog formula has no local clamp and its upstream
raw-axis domain remains unproven. The portable boundary conservatively admits
that range and fails closed outside it. This added validation must not be
misreported as a native dispatcher or universal producer check. A recognized
inactive event is accepted before payload validation, matching the native
gate.

Pitch and bank SET now have a separate, still-unwired portable typed-write
decoder. It accepts the complete native signed `int32` domain and returns
only the selected semantic field, exact binary32 bits under the explicitly
named startup-compatible PC53/nearest-even policy, and the proven rest-clear
directive. Its integer implementation models both rounding stages without
depending on the host floating-point environment. This is deterministic port
behavior, not a claim that the native live x87 control word has been observed.
The decoder owns no producer, ordering, scheduler, or state mutation;
[EXP-20260729-060](../../experiments/EXP-20260729-060-native-pitch-bank-event-reducer.md)
records the implementation boundary.

The follow-up process audit closes the obvious supplied-module mutation gap:
the executable requests PC53 before its game shell, its local initializer
ranges are empty, only it imports `_controlfp`, and 4,277 Rizin-recognized
functions across all 15 supplied runtime modules contain no decoded
floating-point-environment writer. This strengthens the startup-compatible
label but does not observe RC, exception state, external system/driver code, or
the live consumer thread. The numeric label and integration NO-GO therefore
remain unchanged.

`TURN_SET` now has the equivalent separate reducer. It shares the exact
host-FP-independent PC53/nearest-even arithmetic with pitch and bank but
returns only the `turn` write for `+0x450`. A simulation-thread-confined
transactional owner accepts all three angular writes and the two thrust
writes, committing the selected field and explicit shared-rest clear
together. It exposes the sleep controls in exact native order without owning
producer ordering, timing, sleep-result persistence, or rigid-body state.
[EXP-20260730-061](../../experiments/EXP-20260730-061-native-turn-event-control-state.md)
records that boundary.

The conversion preserves the recovered wider-intermediate store vectors:
`THRUST_APPLY(249)` is `0x3C9FFC25` and `THRUST_APPLY(255)` is
`0x3CA3D70B`. The decoder owns no state, scheduler, Q15 conversion, or slot-45
call and remains unwired. See
[EXP-20260729-054](../../experiments/EXP-20260729-054-native-thrust-event-reducer.md).

The apparent `APPLY` alternatives for turn/pitch/bank and both `THROTTLE` events
fall through the complete `AirCraft -> AfVehicle -> NfActor ->
NfTypeInstance` dispatch chain without modifying the control fields. This is
a confirmed no-op for this inheritance path, not a claim that no other actor
type can consume those enum values.

The two attack events are now joined beyond this dispatcher. AirCraft vtable
offsets `+0xC0/+0xC4` set the persistent `AfWeapon::Fire(bool)` state for the
primary pointer at `+0x490` and selected secondary pointer at `+0x494`.
`WpMGun` then consumes the primary firing state in its separate
time-dependant refresh, where it applies technology cadence, selects a barrel,
updates ammunition, creates a private `WpMGunAmmoTechN` instance, and
processes projectile event `0xE2`. See
[Weapon reconstruction](WEAPONS.md) and
[EXP-20260728-030](../../experiments/EXP-20260728-030-primary-machine-gun-spawn.md).

### Player command and analog mapping

The user-command dispatcher at `Dogfighter.exe` virtual address `0x00412EA0`
joins named commands to the vehicle events:

| Command | Press/held payload | Release behavior |
|---|---:|---|
| `turnleft` | `EVENT_TURN_SET`, `-32` | `+32` if right remains held, otherwise `0` |
| `turnright` | `EVENT_TURN_SET`, `+32` | `-32` if left remains held, otherwise `0` |
| `thrustinc` | `EVENT_THRUST_APPLY`, `+255` | `-255` if decrease remains held, otherwise `0` |
| `thrustdec` | `EVENT_THRUST_APPLY`, `-255` | `+255` if increase remains held, otherwise `0` |
| `bankleft` | `EVENT_BANK_SET`, `-32` | `+32` if right remains held, otherwise `0` |
| `bankright` | `EVENT_BANK_SET`, `+32` | `-32` if left remains held, otherwise `0` |
| `pitchup` | `EVENT_PITCH_SET`, `+32` | `-32` if down remains held, otherwise `0` |
| `pitchdown` | `EVENT_PITCH_SET`, `-32` | `+32` if up remains held, otherwise `0` |

This confirms signs relative to the original command labels: positive pitch is
`pitchup`, positive bank is `bankright`, positive turn is `turnright`, and
positive thrust apply is increase. It does not by itself define the physical
world-axis handedness.

The callback stores eight independent bool bytes, initially zero. A valid
one-argument invocation first overwrites its own byte, then emits exactly one
event. A true value always selects its own direction; a false value selects the
opposite direction only if that command remains active, otherwise zero.
Neither repeated values nor equal payloads are suppressed. The isolated
`LegacyAircraftControlCommandReducer` implements this already-ordered local
transition and returns an existing typed native-event input. It does not accept
`InputFrame`: the final Q15 axis snapshot cannot preserve opposing-command
order, an in-tick tap, or repeated callback invocations. See
[EXP-20260730-062](../../experiments/EXP-20260730-062-native-control-command-reducer.md).

`LegacyAircraftControlCommandStep` now supplies the single production
composition point from that command transition through the matching existing
native-event decoder and the control-event state owner. It advances exactly one
caller-ordered invocation. The callback flag update is retained when an
inactive event is ignored or a port-side numeric policy is rejected; vehicle
control state changes only after a decoded typed write commits. This staged
behavior does not claim atomicity across the native callback and
`ProcessEvent`, and it adds no queue, producer, Q15, or clock. See
[EXP-20260730-063](../../experiments/EXP-20260730-063-native-control-command-step.md).

`LegacyAircraftVehicleRefreshGate` now supplies the next isolated composition
boundary. After the caller has applied every already-ordered control event for
one refresh, it passes the committed five controls, shared signed rest
duration, sampled movement/ground predicates, and signed delta through the
existing sleep helper. Success commits only the returned rest duration;
rejection retains the complete input state. The returned integrate/clear
directives remain unwired from rigid-body state. This helper owns no producer,
event order, clock, scheduler, force law, or render publication; see
[EXP-20260730-077](../../experiments/EXP-20260730-077-aircraft-runtime-prerequisites.md).

The native keyboard path obtains one buffered DirectInput record per device
request and executes its mapped binding synchronously. It has no fixed
PITCH-versus-BANK order beyond DirectInput record order and textual binding
order. Its command held byte changes before `NfEvent::Process`; an event
ignored by an inactive vehicle is not automatically replayed after activation.

Input devices are registered at the head of the manager list. Construction in
keyboard, mouse, joystick order therefore yields traversal in joystick, mouse,
keyboard order. Every request restarts at joystick. One successful joystick
state snapshot is exposed over successive requests in fixed axis-index order:
bank first, pitch second, then other axes and changed buttons. Each returned
raw event is mapped and its SET is processed before the next manager request.

The broader input-event dispatcher at `0x00411BB0`
(`FN-DOGFIGHTER-00011BB0`, working name `DispatchInputEvents`) uses only `SET`
events in its three analog cases:

- bank: raw dead zone `abs(raw) < 20`, emission threshold
  `abs(previous - raw) > 9`, payload
  `sign(raw) * min(raw^2 / 200, 32)`;
- pitch: the same dead zone and emission threshold, payload
  `sign(raw) * min(raw^2 / 150, 32)`; and
- thrust: emission threshold `abs(previous - raw) > 9`, payload
  `255 - (raw + 255) / 2`, sent as `EVENT_THRUST_SET`.

The joystick setup at `[0x00414DE0,0x00415092)` installs an absolute-axis
`DIJOYSTATE` format and a required `DIPROP_RANGE` of `[0,10000]` for the X
and Y offsets. The poller at `[0x00415210,0x004154BD)` consequently maps X to
bank raw `(lX-5000)*255/5000` and Y to pitch raw
`(5000-lY)*255/5000`. Successful setup therefore proves a separate upstream
raw range of `[-255,+255]` and a final pitch/bank payload range of
`[-32,+32]`; it is not assumed from the consumer. There is no post-read
clamp, so the proof remains conditional on DirectInput honoring the
successfully installed range property.

Each analog history global is updated before `NfEvent::Process`. An equal or
sub-threshold later sample emits no SET, and a SET ignored by an inactive
vehicle is not replayed merely because it becomes active. The field therefore
sample-and-holds until a later mapped raw change of at least ten. Raw
`GetTickCount` timestamps are not copied into the generated SET and do not
define cross-source ordering.

No explicit local clamp was found on the thrust analog payload. The downstream
flight step clamps the resulting `+0x444` value to `[0,1]` in its gated update.
No `TURN_SET` case was found in this analog dispatcher.
Ghidra and Rizin independently agree on this function's exact range and all
three analog-history globals; see
[EXP-20260727-001](../../experiments/EXP-20260727-001-static-tool-crosscheck.md).

### AI control surface

`AIControls` owns eight channels and four arrays of eight floats. `SetValue`
and `AddValue` clamp the raw value to `[-100,+100]`. `GetRelative` maps that
raw interval linearly into the configured per-channel range and caches the
mapped value. `HasChanged` compares the cached and newly mapped floats by exact
equality; there is no epsilon or dead zone in this layer.

The AirCraft AI instance contains this object at `+0x364`. Its constructor at
`0x10009440` and dispatcher at `0x1000C420` establish these exact routes:

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

`DispatchAiControlEvents` tests channel 1 before channel 3. When both mapped
values changed, the complete PITCH
`HasChanged -> GetRelative -> _ftol -> Save -> Process` sequence finishes
before the equivalent BANK sequence. `GetRelative` updates the cache before
processing, so an inactive drop is not replayed until the mapped value changes
again.

The vehicle's 12 ms refresh invokes `AfAI::Refresh` near its end. AirCraft
configures the AI interval to exact binary32 `0.1`; the accumulator adds each
binary32 `dt`, triggers while no longer below that interval, and resets to
zero rather than subtracting the interval. With uninterrupted `0.012f` steps,
the ninth refresh triggers after approximately 0.108 seconds. The derived
step computes controls and then dispatches PITCH before BANK. Those events
cannot affect force work already completed in the current vehicle refresh.

No `TURN_SET` AI producer was found in the inspected AirCraft, GroundUnit, or
WaterUnit paths. No control-law consumer of `+0x450` was found in those
classes outside the shared sleep gate. These bounded negative results are why
the portable reducer remains unwired from physics.

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

- the full scheduler target-mask and remote-network ordering outside the
  recovered local input-before-time path;
- the physical meaning and any missing downstream consumer of
  `EVENT_TURN_SET`/`AfVehicle+0x450`;
- the complete actor-vs-actor slot-29 order outside the shown static/BSP path;
- the semantic meaning of the `AICONTROL` index enum names;
- the gameplay meanings of several constructor fields even though every tuple
  value is now joined to its exact storage and immediate consumers;
- world distance, mass, force, and torque units;
- semantic aerodynamic labels for the now-recovered force, torque, contact,
  throttle, and damping equations;
- the collision scalar's exact range and meaning;
- deterministic replacement and exact sequence ownership for the native global
  random source used by thrust-integrity recovery;
- the complete projectile event payload ownership after the now-recovered
  `WpMGun` spawn request, plus projectile motion, impact, and damage;

## Portable implementation boundary

Until those unknowns are resolved, the portable simulation may:

- accept the immutable 60 Hz `InputFrame`;
- preserve flight, absolute/relative thrust, camera, combat, rear-view,
  weapon-selection, mission-status, and pause intentions;
- preserve held states and exact discrete edge counts;
- maintain its own completed-step count;
- reject invalid or non-increasing input atomically; and
- produce a canonical cross-compiler diagnostic hash.

Separate pure helpers may also preserve already confirmed local contracts for
the vehicle sleep gate; the typed already-formed native thrust-event write;
the eight-flag already-ordered discrete control-command transition and its
single-invocation typed-event/decode/field-commit composition;
the ordered slot-45 target/apply/clamp/smoothing prefix; collision-driven
thrust-integrity degradation; later
recovery/clamping; and the bounded engine-only audio command stream plus the
destroyed-dive sample state. The thrust-control transition treats invocation
as the already-decided active slot-45 call and owns no scheduler, `dt`, Q15
conversion, or event timing. The command reducer owns neither the 60 Hz
producer nor 12 ms sample-and-hold policy and returns only a typed native-event
input. The per-invocation step retains that separation while composing the
event decoder and field owner; it is not a producer or batch runner. The event
decoders share that timing boundary and return only a typed write plus a
rest-clear directive. The primary `WpMGun` helper may also
preserve its
recovered technology profiles, shot accumulator, trigger, barrel, ammunition,
and one-projectile-request-per-refresh transition. These helpers remain
unwired until a
runtime owns the native 12 ms scheduler, rigid-body/collision state, engine
phase, deterministic random sequence, and audio backend. The eventual audio
coordinator must place destroyed-dive commands after phase transitions and
before common engine modulation on the shared fifth-call pass.

It must not yet move or rotate the aircraft, apply throttle, instantiate a
projectile, or label provisional constants as original behavior. That narrow
state bridge provides a deterministic integration seam for the recovered
flight law without creating false parity. The current 60 Hz input publication
and recovered nominal 12 ms physics cadence are separate clocks and must remain
separate in the eventual implementation.
