# EXP-20260730-062: native discrete aircraft-control commands

**Date:** 2026-07-30

**Evidence ID:** `EV-20260730-001`

**Evidence:** static Ghidra 12.1.2 and Rizin 0.9.1 comparison of the private
reference build identified by `docs/evidence/source-manifest.sha256`

**Status:** isolated reducer implemented and synthetically verified; no live
input or scheduler integration

**Decision:** GO for one pure, already-ordered command invocation reducer;
NO-GO for `InputFrame`, Q15, device, repeat-cadence, cross-producer, or timing
integration

## Purpose

Earlier experiments recovered the individual `PITCH_SET`, `BANK_SET`,
`TURN_SET`, and thrust event consumers. This experiment closes the discrete
player-command producer immediately above them. It establishes:

- the eight stored bool command flags;
- the flag-update and opposite-held selection order;
- the exact event type and signed payload emitted by every command;
- repeated-invocation and zero-release behavior; and
- the narrow portable boundary that can be implemented without inventing an
  input clock or device policy.

The original executable was not run. All analysis used read-only copies and
all tests use synthetic states.

## Native callback and stored state

The recovered callback is:

| Function | VA range | RVA range |
|---|---|---|
| user-command callback | `[0x00412EA0, 0x004143C1)` | `[0x00012EA0, 0x000143C1)` |

Its owner constructs an `NfConsoleCallback` subobject at owner offset `+0x18`.
The following eight independent bytes are relative to callback `this`; the
owner-relative address is shown in parentheses:

| Command | Callback byte | Owner byte |
|---|---:|---:|
| `turnleft` | `+0x5C` | `+0x74` |
| `turnright` | `+0x5D` | `+0x75` |
| `thrustinc` | `+0x5E` | `+0x76` |
| `thrustdec` | `+0x5F` | `+0x77` |
| `pitchup` | `+0x60` | `+0x78` |
| `pitchdown` | `+0x61` | `+0x79` |
| `bankleft` | `+0x62` | `+0x7A` |
| `bankright` | `+0x63` | `+0x7B` |

The constructor range `[0x004108F0,0x0041092C)` writes zero to all eight
bytes. No pair shares storage and no cross-axis state is modified by these
branches.

The command registration selectors are `0x13/0x14` for thrust increase and
decrease, `0x15/0x16` for bank left and right, `0x1A/0x1B` for pitch up and
down, and `0x20/0x21` for turn left and right. All eight registrations pass
`true` as the final `AddCommand` argument. Static analysis does not establish
whether that argument enables, suppresses, or otherwise affects operating
system key repeat.

## Exact branch map

Ghidra and Rizin agree on all eight branch bounds and payload selections:

| Command | VA range | RVA range | Active emission | False emission |
|---|---|---|---|---|
| `thrustinc` | `[0x00413773,0x004137FE)` | `[0x00013773,0x000137FE)` | `THRUST_APPLY (0x64), +255` | `-255` if decrease is active, else `0` |
| `thrustdec` | `[0x004137FE,0x00413888)` | `[0x000137FE,0x00013888)` | `THRUST_APPLY (0x64), -255` | `+255` if increase is active, else `0` |
| `pitchup` | `[0x00413888,0x00413910)` | `[0x00013888,0x00013910)` | `PITCH_SET (0x5F), +32` | `-32` if down is active, else `0` |
| `pitchdown` | `[0x00413910,0x00413998)` | `[0x00013910,0x00013998)` | `PITCH_SET (0x5F), -32` | `+32` if up is active, else `0` |
| `bankleft` | `[0x00413998,0x00413A20)` | `[0x00013998,0x00013A20)` | `BANK_SET (0x65), -32` | `+32` if right is active, else `0` |
| `bankright` | `[0x00413A20,0x00413AA8)` | `[0x00013A20,0x00013AA8)` | `BANK_SET (0x65), +32` | `-32` if left is active, else `0` |
| `turnleft` | `[0x00413E5C,0x00413EE4)` | `[0x00013E5C,0x00013EE4)` | `TURN_SET (0x5D), -32` | `+32` if right is active, else `0` |
| `turnright` | `[0x00413EE4,0x00413F6C)` | `[0x00013EE4,0x00013F6C)` | `TURN_SET (0x5D), +32` | `-32` if left is active, else `0` |

Each recognized selector follows the same local order:

1. require exactly one argument;
2. dereference `argv[0]` and convert it with `Str2Bool`;
3. immediately overwrite the selected command byte;
4. construct one integer event with source `0xF0000000`;
5. choose the signed `int32` payload, testing the selected command first and
   the opposite flag only when the new value is false;
6. call `Save(NfMain)`;
7. call `Process(NfMain)`; and
8. destroy the temporary event.

The `Save` result is ignored. The callback has no local null check after the
one-argument test. Argument parsing, pointer validity, save semantics, and
event transport therefore remain outside the portable reducer.

There is no comparison with the old flag and no equality suppression. Every
valid invocation produces exactly one event, including repeated `true`,
repeated `false`, a repeated nonzero payload, and a repeated zero payload.
The result describes callback-invocation behavior; it does not claim which
physical keyboard messages cause the native command system to invoke it.

## Pair behavior

For every pair, let negative be left/down/decrease, positive be
right/up/increase, and magnitude be `32` except for thrust, where it is `255`:

| Prior pair state | Ordered invocation | Next pair state | Emission |
|---|---|---|---:|
| neutral | negative `true` | negative | `-magnitude` |
| neutral | positive `true` | positive | `+magnitude` |
| negative | positive `true` | both | `+magnitude` |
| positive | negative `true` | both | `-magnitude` |
| both | negative `false` | positive | `+magnitude` |
| both | positive `false` | negative | `-magnitude` |
| negative | negative `false` | neutral | `0` |
| positive | positive `false` | neutral | `0` |

When both flags are true, their two bools do not encode which event most
recently won. That result exists only in the already-ordered event stream.
Reconstructing it later from a final held mask would therefore lose evidence.

## Portable reducer

`LegacyAircraftControlCommandReducer` is a pure transition over:

- an eight-bit caller-owned command snapshot;
- one already-ordered command enum;
- its already-parsed bool value;
- the vehicle-inactive value to forward to the native event decoder; and
- the explicitly selected angular numeric policy to forward unchanged.

It returns the complete next command snapshot and exactly one variant of the
existing typed native-event inputs. A forged command enum fails before any bit
change and returns no event. The transition allocates no memory and owns no
mutable global state.

The separation is:

```text
ordered bool command invocation
    -> pure command-state transition and typed event
    -> existing native event decoder
    -> transactional control-event state owner
```

The reducer does not produce a destination-field write directly. This keeps
the native vehicle-inactive gate, exact angular PC53/RNE compatibility policy,
thrust arithmetic, positive-zero write, and shared-rest clear decision in the
already tested native event decoders. It also means a command flag can update
while an inactive vehicle ignores the downstream field mutation, matching the
observed ownership boundary without inventing a later replay.

## Why this is not an InputFrame integration

The present `InputFrame` is an immutable per-tick semantic snapshot. Its flight
bank, pitch, and throttle-delta values are final Q15 axes; it carries no
per-axis press/release edges, no turn action, and no ordered history of two
opposing commands within one tick. The router also applies multi-source
ownership, hysteresis, lifecycle neutral gates, and a separate 60 Hz
publication policy.

Consequently, an `InputFrame` cannot reproduce:

- press and release of one command inside the same sampled tick;
- which opposing command was invoked last while both remain held;
- the native callback's repeated-invocation emission; or
- command ordering relative to analog, AI, saved, or network producers.

No `InputFrame`, `InputRouter`, desktop/controller bridge, Q15 conversion, or
`PlayerAircraftState` type appears in the reducer API. A future live adapter
requires an explicit ordered semantic-command stream before aggregation, or a
clearly documented new portable policy that does not claim native ordering.

## Synthetic validation

The dedicated test suite covers:

- all eight active command-to-event mappings and raw event IDs;
- every command with both bool values across all 256 possible flag snapshots;
- negative-then-positive and positive-then-negative pair ordering;
- opposite-held restoration and final zero release for all four pairs;
- repeated true, repeated false, repeated nonzero, and repeated zero
  invocations without suppression;
- cross-pair isolation and an ordered press-plus-release tap;
- forged command failure with bit-identical rollback;
- forwarding of inactive state and supported or forged angular numeric policy;
- inactive-before-policy behavior in the existing angular decoder; and
- complete typed-event-to-decoder-to-owner composition for turn, pitch, bank,
  and thrust, including exact binary32 bits, equal nonzero rest clear, and
  zero write without rest clear.

The exact command outputs compose to angular bits
`0xBFB020C5/0x3FB020C5`, thrust-apply bits
`0xBCA3D70B/0x3CA3D70B`, and positive zero `0x00000000` under the selected
existing decoder policies.

Clean Windows GCC 15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds
compile the complete repository and pass all 103 CTests. A fresh Clang 22.1.8
build passes the dedicated reducer test. The public-boundary unit tests,
Ghidra/working-copy/Rizin PowerShell wrapper suites, all 12 Rizin export tests,
the 523-file public scan, the 268-row/14-column function catalogue,
changed-document links, changed-scope local-path scan, and `git diff --check`
pass.

Independent final review found one P2 documentation overclaim: the first
catalogue update marked the complete multi-command callback implemented even
though this slice implements only its eight flight-control branches. The row
now remains `in_progress` and names the implemented subset. Re-review reports
GO with no open P0-P3 findings; an additional GCC 15.2
`-Wall -Wextra -Wpedantic -Werror` build and dedicated test pass.

## Deliberate exclusions

This stage does not implement or infer:

- command-line argument parsing or invalid-pointer behavior;
- whether native `AddCommand(true)` causes operating system key repeat;
- a portable duplicate-edge suppression or focus-loss reset policy;
- per-source cancellation or arbitration;
- Q15-to-native payload conversion;
- analog, AI, network, replay, or saved-event ordering;
- timestamp preservation, batching, coalescing, or queue ownership;
- a relationship between the 60 Hz input pump and nominal 12 ms AirCraft
  refresh;
- force-law consumption of `TURN_SET`, yaw semantics, or physical units; or
- live player-state, physics, renderer, or audio wiring.

Focus-loss neutralization remains a deliberate safety policy of the portable
input layer, not a recovered behavior of this native callback. If a later
adapter needs a bounded all-command reset, its cross-source and event-order
semantics must be specified independently rather than added to this reducer.
