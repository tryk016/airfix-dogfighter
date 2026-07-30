# EXP-20260730-061: native TURN_SET and control-event state owner

**Date:** 2026-07-30

**Evidence:** static Ghidra 12.1.2 and Rizin 0.9.1 comparison of the
private reference build identified by `docs/evidence/source-manifest.sha256`

**Status:** implemented and locally verified; producer, downstream force-law
use, and live event timing remain unproven

**Decision:** GO for the isolated `TURN_SET` reducer and simulation-thread
transactional state owner; NO-GO for live producer or scheduler wiring

## Purpose

The sleep gate previously exposed a fifth exact-zero control at
`AfVehicle+0x450`, but its initialization and write source were unknown.
Targeted static analysis now establishes that this field:

- is a binary32 control initialized to positive zero by
  `AfVehicle::AfVehicle`;
- is written by `EVENT_TURN_SET (0x5D)`;
- receives the complete signed `int32` event payload at packed offset `+0x11`
  multiplied by the same exact binary32 scale `0x3D3020C5` as pitch and bank;
- is protected by the vehicle-inactive gate at `+0x460`;
- participates in the same active-nonzero shared rest-duration clear; and
- is the third control in the native sleep-gate order.

The native event name supports the portable field name `turn`. It does not
prove that the field means world-axis yaw, that AirCraft consumes it in its
force law, or that its behavior can be reconstructed from the event name.

## Static evidence

The relevant function is:

```text
AfVehicle::ProcessEvent
VA  [0x1001B6C0, 0x1001FA6C)
RVA [0x0001B6C0, 0x0001FA6C)
```

Confirmed instruction anchors are:

| Fact | VA | RVA |
|---|---:|---:|
| constructor positive-zero write to `+0x450` | `0x1001AC3A` | `0x0001AC3A` |
| `TURN_SET` final binary32 store to `+0x450` | `0x1001E4CC` | `0x0001E4CC` |
| sleep-gate read of `+0x450` | `0x1001C1FD` | `0x0001C1FD` |

The event branch has the same ordered numeric shape already independently
established for `PITCH_SET` and `BANK_SET`:

```text
inactive gate
FILD signed dword [event+0x11]
FMUL m32 0x3D3020C5
FST m32 [vehicle+0x450]
compare retained x87 value with +0
if nonzero: clear s64 [vehicle+0x458/+0x45C]
```

The constructor, event store, exact-zero wake read, field width, and event
name agree between Ghidra and Rizin. The recovered command dispatcher emits
exactly `-32` for `turnleft`, `+32` for `turnright`, the opposite held value
on release, or zero when neither direction remains held. No `TURN_SET`
producer was found in the inspected analog or AirCraft/GroundUnit/WaterUnit
AI paths. No consumer was found in those classes' flight/vehicle laws outside
the sleep gate. `TURN_APPLY (0x5E)` does not write the field in the inspected
inheritance chains. These negative search results do not prove that no other
plugin can use the event.

## Portable TURN_SET reducer

`LegacyAircraftTurnEventReducer` accepts one already-ordered event:

- only `TURN_SET (0x5D)`;
- the complete signed `int32` native payload;
- the current vehicle-inactive state; and
- an explicitly selected numeric compatibility policy.

It returns either an ignored/failed result or one exact binary32 write plus
the proven rest-clear directive. Unsupported event types fail before the
inactive gate. A recognized inactive `TURN_SET` is ignored before numeric
policy validation, matching the native branch. Active zero writes positive
zero without clearing rest; every active nonzero payload requests a rest
clear. Equal writes are not suppressed.

The common internal arithmetic for turn, pitch, and bank now lives in
`detail/LegacyAircraftAngularSetDecode`. It performs the explicit
PC53/nearest-even retained-product rounding followed by binary32 store
rounding using unsigned integer arithmetic. Moving the already-tested pitch
and bank path to this shared helper does not change its output.

The policy is a deterministic startup-compatible reconstruction choice. It
is not a claim that the live process-wide x87 control word has been observed
at the three event branches.

## Transactional control-event state

`LegacyAircraftControlEventStateOwner` owns a caller-supplied complete
snapshot containing:

- `thrustApply`, `targetThrust`, and the separately advanced
  `smoothedThrust`;
- exact binary32 bits for `turn`, `pitch`, and `bank`; and
- the signed 64-bit rest duration.

It accepts already-decoded `LegacyAircraftThrustWrite`,
`LegacyAircraftPitchBankWrite`, and `LegacyAircraftTurnWrite` values. Each
operation validates the complete write, prepares a local next snapshot,
updates only the selected field, applies the write's explicit rest-clear
directive, and then commits the complete snapshot once. Invalid field tags
and non-finite forged values leave the state unchanged.

This is all-or-nothing transactional behavior on one simulation thread. It is
not a `std::atomic`, lock-free publication, or multi-producer ordering claim.
The owner intentionally trusts the reducer's rest-clear directive instead of
recomputing it from the destination field or suppressing an equal overwrite.

The wake view is exposed in the confirmed native order:

```text
targetThrust (+0x444)
thrustApply  (+0x440)
turn         (+0x450)
bank         (+0x44C)
pitch        (+0x448)
```

This array can be supplied to the separate `LegacyVehicleSleepState` helper
only after the event write has committed. The owner does not commit the later
sleep result or clear rigid-body dynamics.

## Validation

Synthetic tests cover:

- all 11 independently established PC53/nearest-even vectors plus signed
  `int32` endpoints for `TURN_SET`;
- inactive, unsupported-event, unsupported-policy, positive-zero, nonzero,
  and repeated-write behavior;
- exact wake-array order and bit patterns;
- isolation of every thrust, turn, pitch, and bank write;
- preservation of `smoothedThrust` and all unselected controls;
- directive-controlled rest clearing, including equal nonzero writes;
- fail-closed rollback for invalid tags and non-finite forged writes;
- already-ordered interleaving with last processed write winning per field;
  and
- event-owner-to-sleep composition for nonzero wake and zero release.

Clean Windows GCC 15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds
compile the complete repository and pass all 102 CTests. A fresh local
Clang 22.1.8 build passes the three directly affected reducer/owner tests.
Synthetic public-boundary tests, all 12 Rizin normalization tests, and the
519-file repository boundary scan pass. The Ghidra, working-copy, and Rizin
PowerShell wrapper suites also pass. All tests use synthetic state and contain
no private asset, executable, path, checksum, or original resource.

## Deliberate exclusions

This stage does not implement or infer:

- controller, analog, AI, network, or replay producers for `TURN_SET` beyond
  the confirmed discrete keyboard commands;
- Q15 conversion, event timestamps, queue ownership, cross-producer ordering,
  or sample-and-hold;
- a native 12 ms scheduler or a join to the separate 60 Hz input pump;
- an AirCraft force, torque, yaw, camera, or animation consumer for `turn`;
- sleep-result persistence, threshold dynamics clearing, or rigid-body state;
- multi-threaded mutation/publication; or
- live `PlayerAircraftState` integration.

The next live integration gate remains controlled trace evidence for numeric
mode, producer conversion, event ordering, and event phase relative to the
native refresh.
