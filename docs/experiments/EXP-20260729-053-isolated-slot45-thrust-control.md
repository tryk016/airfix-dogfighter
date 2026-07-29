# EXP-20260729-053: isolated slot-45 thrust-control prefix

- Date: 2026-07-29
- Status: implemented as an unwired pure transition
- Scope: AirCraft target/apply/clamp/smoothing order inside an executed primary
  vtable slot 45

## Question

Can the locally confirmed scalar prefix of one AirCraft force step be preserved
in portable C++20 without implying that the project has reconstructed the
12 ms scheduler, Q15 event timing, rigid-body integration, or flight behavior?

## Evidence boundary

Static Ghidra and Rizin analysis agrees on these AirCraft fields and operations:

- `+0x440` is the persistent value written by `EVENT_THRUST_APPLY`;
- `+0x444` is the target written by `EVENT_THRUST_SET`;
- positive health updates `+0x444` as
  `clamp(target + apply, 0, 1)` without using `dt`;
- non-positive health skips that target update;
- `+0x560` is smoothed on every executed slot-45 step, after the target branch;
- inactive engine-start byte `+0x564` selects
  `(x + 0.3) * (target - x) * 0.02`;
- active `+0x564` selects `(target - x) * 0.0004`;
- neither smoothing branch applies a final clamp.

The vehicle rest/sleep gate is outside this function. A sleeping refresh does
not invoke slot 45. The engine-start flag is owned and later updated by slot 44.

## Portable contract

`LegacyAircraftThrustControlState` contains only the three scalar fields:

```text
thrustApply     persistent +0x440
targetThrust    +0x444
smoothedThrust  +0x560
```

`legacyAircraftAdvanceThrustControl` means that the caller has already selected
one active slot-45 force step. It:

1. rejects non-finite health, target, or smoothed state;
2. for positive health, validates apply, performs the addition, rejects
   non-finite arithmetic, and clamps the target to `[0, 1]`;
3. for non-positive health, retains target and apply without inspecting apply;
4. delegates the unconditional second phase to the existing
   `legacyAircraftAdvanceSmoothedThrust`;
5. returns one complete next state or no state.

This arrangement keeps the recovered operation order in one place and avoids a
second copy of the smoothing equations.

## Deliberate exclusions

The transition has no:

- scheduler, catch-up, or wall-clock policy;
- `dt` parameter;
- sleep boolean or rest-duration ownership;
- Q15 conversion;
- `EVENT_THRUST_SET/APPLY` decoder;
- input sample-and-hold policy;
- pose, velocity, force, torque, collision, camera, audio, or renderer output.

The next event reducer may accept an already formed signed native payload and
produce typed target/apply writes plus the proven rest-clear decision. It must
remain separate from this 12 ms transition. Connecting either layer to the
60 Hz `InputFrame` requires controlled timing traces.

## Tests

Synthetic tests verify:

- target update occurs before smoothing in the same step;
- apply persists and advances consecutive active steps;
- upper and lower target clamps;
- normal and engine-start smoothing use the updated target;
- zero and negative health skip apply but continue smoothing;
- an invalid skipped apply is retained, while later positive health rejects it;
- non-finite health/target/smoothed input and overflowing active addition fail
  closed;
- exact portable binary32 vectors agree under GCC and MSVC for normal,
  engine-start, dead-health, and slot-45-to-slot-44 ordering; and
- a rest-duration `1999 -> 2011` threshold crossing still executes the current
  slot 45 because the entry decision is `integratePhysics`; the following
  refresh skips it even though `sleeping()` became true on the prior step.

The existing independent smoothing, collision damage, recovery, and constant
tests continue to run in the same executable. No original executable, resource,
logical name, checksum, trace, or local path is needed.

## Acceptance rule

This experiment accepts only the isolated scalar transition and its order. It
does not raise the flight model above `observed`, does not claim x87 bit parity,
does not move the rendered aircraft, and does not make the Windows or iOS build
playable.

## Related

- [AirCraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
- [AirCraft controls and scheduler](../re/systems/AIRCRAFT-FLIGHT.md)
- [Vehicle rest/sleep gate](EXP-20260728-026-vehicle-rest-sleep-gate.md)
- [Thrust integrity](EXP-20260728-027-aircraft-thrust-integrity.md)
