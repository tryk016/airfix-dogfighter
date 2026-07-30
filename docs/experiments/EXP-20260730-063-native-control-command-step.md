# EXP-20260730-063: staged native control-command step

**Date:** 2026-07-30

**Evidence:** composition of the statically recovered command and event
contracts recorded by `EV-20260730-001`, `EXP-20260730-062`,
`EXP-20260730-061`, `EXP-20260729-060`, and `EXP-20260729-054`

**Status:** isolated per-invocation composition implemented and synthetically
verified; no live input, queue, replay format, or scheduler integration

**Decision:** GO for one caller-ordered command invocation through the existing
typed event decoder and control-state owner; NO-GO for batching, Q15,
producer ordering, sample-and-hold, or timing

## Purpose

The previous slices implemented and tested three separate boundaries:

1. one bool command invocation updates the recovered eight-flag callback state
   and emits one typed native event;
2. the native event decoder either produces one typed field write, accepts an
   inactive no-op, or rejects an unsupported reconstruction input; and
3. the simulation-thread-confined owner commits one decoded field write and
   its optional shared-rest clear.

Tests composed those boundaries manually, but production code did not yet
provide one canonical entry point for that exact sequence. This experiment
adds that entry point without adding a producer, a clock, or a collection of
events.

No new native behavior is inferred here. The original executable was not run,
and every test uses synthetic state.

## Contract

`LegacyAircraftControlCommandStepState` contains two deliberately separate
snapshots:

- `commandState`: the eight callback-owned bool flags; and
- `controlEventState`: the five recovered control fields plus the shared
  signed rest duration.

`legacyAircraftAdvanceControlCommandStep` accepts that complete state and one
already-ordered `LegacyAircraftControlCommandInput`. It executes:

```text
one caller-ordered bool invocation
    -> LegacyAircraftControlCommandReducer
    -> one typed native event
    -> matching turn, pitch/bank, or thrust decoder
    -> one typed write
    -> LegacyAircraftControlEventStateOwner
```

The result retains the next complete state, the typed event when one was
emitted, the typed write when one was decoded, and a path-free status.
It allocates no memory and owns no mutable global state.

## Staged mutation, not invented atomicity

The recovered callback stores its selected bool byte before it calls
`Process`. The portable composition therefore does not turn the command flag
and downstream vehicle field into one artificial all-or-nothing transaction.

| Result | Command flags | Typed event | Typed write | Control-event state |
|---|---|---|---|---|
| `committed` | selected flag updated | present | present | selected field committed; rest cleared only when requested |
| `ignoredInactive` | selected flag updated | present | absent | unchanged |
| `decodeRejected` | selected flag updated | present | absent | unchanged |
| `unsupportedCommand` | unchanged | absent | absent | unchanged |
| `commitRejected` | selected flag updated | present | present | unchanged |

`commitRejected` is a defensive contract-drift guard. Every write produced by
the current closed set of decoders is valid for the owner, so exhaustive
recognized-command tests must not reach it.

An unsupported angular numeric policy is a port-side reconstruction failure,
not a claimed native runtime state. If it is supplied to an active turn,
pitch, or bank invocation, the decoder rejects it after the earlier command
flag update. If the vehicle is inactive, the proven inactive gate accepts the
event before policy validation. Thrust commands do not carry or interpret an
angular policy.

## Exact retained behavior

The composition preserves all previously established local behavior:

- every recognized invocation emits exactly one typed event, including
  repeated `true`, repeated `false`, equal nonzero writes, and zero releases;
- each invocation changes only its own one of eight command flags;
- false restores the signed opposite payload only while the opposite flag
  remains active;
- recognized inactive events change no vehicle control field;
- active zero writes positive binary32 zero without clearing rest;
- active nonzero writes clear the shared rest duration even when their value
  equals the existing field;
- already-ordered last write wins independently for turn, pitch, bank, and
  thrust apply; and
- exact angular and thrust-apply output bits remain
  `0xBFB020C5/0x3FB020C5` and
  `0xBCA3D70B/0x3CA3D70B` for the recovered discrete payloads.

The supported angular result remains conditional on the explicitly named
startup-compatible PC53/round-to-nearest-even policy. This composition adds no
evidence about the live process-wide x87 control word.

## Why there is no production replay runner

The test suite folds an explicit array of invocations to prove determinism and
prefix/suffix composition. That local test helper is not a runtime queue or a
file format.

A production batch API would need policies for limits, partial progress,
error stopping, producer identity, cross-source ordering, timestamps, and
lifecycle resets. None of those policies follows from the per-invocation
callback evidence. Calling a synthetic list a captured native trace would also
overstate the evidence.

The public API therefore advances exactly one invocation. A later controlled
trace can call it repeatedly in the observed order without changing this
local contract.

## Synthetic validation

The dedicated test covers:

- both bool values for every command over all 256 command-flag snapshots;
- exact event IDs, signed payloads, write variants, destination fields, and
  binary32 bits;
- preservation of every unselected command and control field;
- nonzero rest clear and zero rest retention;
- inactive-before-policy behavior and command-only partial progress;
- active unsupported-policy rejection after the command flag update;
- invalid command rollback before event production;
- repeated equal nonzero writes without suppression;
- deterministic replay of an explicit mixed-axis sequence; and
- equality of full replay with prefix-then-suffix composition.

The replay helper carries no timestamp, tick, source, or hidden order. Its
array order is the complete synthetic input.

Fresh Windows GCC 15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds
compile all 336 steps and pass all 104 CTests. A fresh Clang 22.1.8 build
passes the dedicated test. Three independent reviewers report GO with no
P0-P3 findings; one review additionally compiles the new slice with both GCC
and Clang warnings-as-errors.

The public-boundary unit tests, 12 Rizin export tests, all three
reverse-engineering PowerShell wrapper suites, the 527-file public scan,
changed-document links, changed-scope local-path scan, clang-format, and
`git diff --check` pass. No original content, private data, generated trace,
or local path is added.

## Deliberate exclusions

This stage does not implement or infer:

- `InputFrame` or Q15 conversion;
- keyboard, touch, controller, analog, AI, network, or saved-event producers;
- cross-producer ordering or arbitration;
- a queue, batch, serializer, trace file, coalescing, or deduplication;
- operating-system repeat cadence or focus-loss behavior;
- sample-and-hold between the 60 Hz input pump and AirCraft refresh;
- a 12 ms scheduler, timestamp, or physics delta;
- live `PlayerAircraftState`, flight-law, camera, audio, or renderer wiring;
  or
- physical meaning or units for the `TURN_SET` destination.

These remain evidence gates rather than missing implementation details that
may be filled with plausible behavior.
