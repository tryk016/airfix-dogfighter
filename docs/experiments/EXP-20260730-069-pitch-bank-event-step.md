# EXP-20260730-069: one-event pitch/bank state step

**Date:** 2026-07-30

**Evidence:** composition of the statically recovered consumer and producer
contracts recorded by `EV-20260730-003`, `EXP-20260730-067`,
`EXP-20260729-060`, and `EXP-20260730-061`

**Status:** isolated one-event composition implemented and synthetically
verified; no producer, live input, queue, replay, scheduler, physics, or
renderer integration

**Decision:** GO for one caller-ordered, already-formed `PITCH_SET` or
`BANK_SET` through the existing decoder and control-state owner; NO-GO for
batching, producer identity, Q15 conversion, sample-and-hold, timing, or live
flight integration

## Purpose

The portable reconstruction already provided:

1. an exact typed decoder for one already-formed `PITCH_SET (0x5F)` or
   `BANK_SET (0x65)`;
2. a simulation-thread-confined owner that transactionally commits a selected
   pitch/bank write and its optional shared-rest clear; and
3. static producer evidence establishing immediate order for joystick, AI,
   and keyboard paths.

Production code did not yet expose one canonical entry point for an event
already emitted by the analog or AI producer. The discrete command step is not
that boundary: it begins with a keyboard-style bool command and updates the
separate eight-flag callback state first.

This experiment composes the existing pitch/bank decoder and state owner
without adding another interpretation of the payload or inferring a producer.
No original executable or private resource is run or read by its tests.

## Contract

`legacyAircraftAdvancePitchBankEventStep` accepts:

- one complete caller-owned `LegacyAircraftControlEventState`; and
- one `LegacyAircraftNativePitchBankEventInput`.

It performs exactly:

```text
one already-formed PITCH_SET or BANK_SET
    -> existing exact typed decoder
    -> ignored/rejected, or one typed write
    -> existing transactional state owner
    -> complete next control-event snapshot
```

The result reports one of four explicit outcomes:

| Status | State | Write |
|---|---|---|
| `committed` | complete next snapshot | exactly one typed pitch/bank write |
| `ignoredInactive` | input snapshot unchanged | absent |
| `decodeRejected` | input snapshot unchanged | absent |
| `commitRejected` | input snapshot unchanged | absent |

The defensive commit-rejection result is retained even though every write
currently emitted by the typed decoder satisfies the owner contract. This
keeps validation and mutation separate and makes a future decoder/owner
contract drift fail closed.

## Ordering and lifecycle invariants

The step has no collection API. Observable order is solely the caller's call
order:

- joystick code may call it for BANK and then PITCH;
- AI code may call it for PITCH and then BANK;
- keyboard/binding code may call it in record/text execution order; and
- the last processed SET wins independently for each axis.

The step never sorts, batches, deduplicates, interpolates, retries, or replays.
Equal repeated values still commit. It has no producer cache, so an inactive
event that is accepted as a no-op cannot reappear automatically after
activation.

The existing decoder continues to own the native rules:

- the inactive gate precedes numeric-policy validation;
- active zero writes positive `+0` and preserves the signed rest duration;
- active nonzero writes clear the signed rest duration with the field commit;
- only the selected pitch or bank field changes;
- the complete signed `int32` consumer domain remains supported; and
- exact bits use the explicitly conditional startup-compatible PC53/RNE
  policy.

The new step does not strengthen that policy into a claim about the live
process-wide x87 control word.

## Synthetic verification

`LegacyAircraftPitchBankEventStepTests` covers:

- exact isolated `+32` PITCH and `-32` BANK results;
- bit-identical preservation of thrust, turn, smoothed thrust, and the
  unselected axis;
- joystick BANK-before-PITCH and AI PITCH-before-BANK intermediate states;
- last-processed SET behavior and prefix/suffix composition;
- active positive zero and retained rest;
- equal repeated nonzero writes and repeated rest clear;
- inactive-before-policy behavior and absence of replay;
- unsupported event and numeric-policy rejection without partial mutation;
- `noexcept` and trivially copyable result storage.

The full exact-bit numeric vector set remains in the lower-level pitch/bank
decoder tests. This composition test deliberately does not duplicate that
decoder proof.

## Validation

- Fresh Windows GCC 15.2/Ninja and MSVC 19.51/Ninja builds compile the complete
  portable repository and each pass all 107 CTests.
- An independent clean GCC build and Clang 22 warnings-enabled syntax check
  pass. Independent review reports GO with no P0-P3 findings.
- Clang-format, `git diff --check`, all three reverse-engineering wrapper
  suites, all 12 Rizin exporter tests, changed-document links, and the
  changed-scope local-path scan pass.
- The public-boundary scanner accepts all 553 public/pending files. No private
  game data, original-derived report, analyzer database, or local path is
  included.

Hosted Linux, macOS, Windows product, iPhoneOS, and iPhoneSimulator builds
remain publication gates. The portable step has no platform-specific source.

## Deliberate exclusions

This slice does not add:

- an event array, span, queue, timestamp, sequence number, producer tag, or
  replay format;
- DirectInput, keyboard, mouse, AI, or remote-network adapters;
- `InputFrame` or Q15 conversion;
- producer-cache ownership or device cadence;
- a 12 ms scheduler or a join to the separate 60 Hz input pump;
- slot-45 force-law, rigid-body, player-state, camera, audio, or renderer
  wiring; or
- runtime parity claims.

The unresolved live gates remain the x87 control word, producer-side `_ftol`,
DirectInput range conformance, remote input ordering, wall-clock cadence, and
the sample-and-hold relationship between input and vehicle refresh.

## Final recommendation

**GO for this isolated one-event boundary.**

**NO-GO for connecting it to live input or physics.** A later producer adapter
must call it immediately once per already-formed event and preserve the order
proved by `EXP-20260730-067`. Any proposal that first collects events into a
batch, frame, sort key, or replay buffer requires separate evidence and review.

## Related evidence

- [Producer ordering and 12 ms phase](EXP-20260730-067-native-pitch-bank-producer-ordering.md)
- [Pitch/bank event reducer](EXP-20260729-060-native-pitch-bank-event-reducer.md)
- [Staged discrete command step](EXP-20260730-063-native-control-command-step.md)
- [Aircraft flight-law boundary](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
