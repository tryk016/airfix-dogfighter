# Aircraft flight reconstruction boundary

**Status:** observed, not yet specified or implemented for parity
**Evidence:** `EV-20260724-001`
**Reference build:** SHA-256 values in `docs/evidence/source-manifest.sha256`

This note records the current clean-room boundary around
`Game/Types/AirCraft.type`. It deliberately separates recovered executable
facts from working names and hypotheses. Raw disassembly and decompiler output
remain in the ignored `artifacts/` tree.

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
observed effects only. The argument is a per-step time delta; its unit and the
scheduler that supplies it remain unknown. The method is not
`NfActor::Refresh(__int64)`: the two ABIs have different argument sizes and
stack cleanup.

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

### Collision/auxiliary method

Vtable slot 30 points to `[0x10007920, 0x1000851D)`. It consumes a
`PhCollidedPolyList`, constructs collision constraints, attempts movement,
handles static collision, and calls `CcRigidBody::CalcAuxiliary` at
`0x10007AE6` and `0x10008045`. The durable working name is
`AircraftStaticCollisionStep`. The exact scheduler relationship between it,
`AircraftFlightForceStep`, and the core rigid-body integrator is still unknown.

### AI control surface

A separate routine at `0x1000A370` accepts one `float` argument and operates on
`AIControls`. Its constructor and dispatcher establish these numeric
contracts:

| Control index | Configured range | Event |
|---:|---:|---:|
| 0 | `[-255, +255]` | `0x63` |
| 1 | `[-32, +32]` | `0x5F` |
| 3 | `[-32, +32]` | `0x65` |
| 5 | `[0, 1]` | `0xB9` |
| 6 | `[0, 1]` | `0xBA` |

This routine is AI logic. Its indices cannot yet be assigned directly to the
portable player's bank, pitch, or throttle actions. Event `0xB9` is confirmed
as the primary `WpMgun` firing state. Event `0xBA` is symmetrically associated
with the second weapon channel, but its symbolic name remains inferred. The
axis names and signs of indices 0, 1, and 3 remain unknown.

The executable also contains separate `BANK`, `PITCH`, and `THROTTLE`
`SET`/`APPLY` event names. This supports an absolute-versus-relative
distinction but does not yet join those names to indices 0, 1, and 3.

## Inferred, not yet contractual

- `AircraftFlightForceStep` is likely one stage of the aircraft update rather
  than the complete scheduler-visible update.
- Player and AI controls likely converge on fields read by the force step.
- `AircraftStaticCollisionStep` likely runs after a movement/integration stage,
  but its exact place in the frame is not proven.

None of these inferences may be used as parity claims or encoded as flight
constants without another confirming source.

## Still unknown

- the scheduler-visible entry point and complete update order;
- the unit and scheduler source of each float time argument;
- where player `SET`/`APPLY` events become aircraft state;
- `AICONTROL` numeric meanings, ranges, dead zones, signs, and clamps;
- the ten constructor-tuple field meanings;
- world distance, mass, force, and torque units;
- lift, drag, thrust, gravity, stall, and stabilization equations;
- local aerodynamic axes and dynamic orientation composition;
- rigid-body reset, derive, integrate, post-ODE, and collision order; and
- the point at which fire intent spawns a projectile.

## Portable implementation boundary

Until those unknowns are resolved, the portable simulation may:

- accept the immutable 60 Hz `InputFrame`;
- preserve bank and pitch as uninterpreted Q15 intentions;
- preserve primary-fire held state and exact edge counts;
- maintain its own completed-step count;
- reject invalid or non-increasing input atomically; and
- produce a canonical cross-compiler diagnostic hash.

It must not yet move or rotate the aircraft, apply throttle, spawn a weapon,
or label provisional constants as original behavior. That narrow state bridge
provides a deterministic integration seam for the recovered flight law without
creating false parity.
