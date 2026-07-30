# EXP-20260730-077: aircraft runtime prerequisites

**Date:** 2026-07-30

**Evidence:** `EV-20260730-007` for the `AirCraft.type` registry, composed
with the previously recovered control-state and vehicle-sleep contracts in
`EV-20260730-003`, `EXP-20260730-061`, `EXP-20260730-063`, and
`EXP-20260728-026`

**Status:** exact public type catalogue and isolated control-to-sleep refresh
gate implemented; no live producer, scheduler, rigid-body integration, flight
law, camera, audio, or renderer wiring

**Decision:** GO for carrying an authenticated player aircraft identity and
its exact raw type tuple into the mission snapshot, and GO for advancing the
already-recovered vehicle sleep gate after caller-ordered control events.
NO-GO for a live 12 ms aircraft runtime.

## Purpose

The mission products can authenticate, load, and render the selected player
aircraft, while the portable simulation already owns exact isolated control
writes and the signed vehicle rest/sleep transition. Two narrow prerequisites
were still absent:

1. a public, bit-preserving representation of all 17 records registered by
   `AirCraft.type`; and
2. one composition boundary from the committed five-control state to the
   recovered sleep/physics entry decision.

This slice adds only those prerequisites. It does not infer physical units,
choose a floating-point integration policy, or connect the 60 Hz semantic
input pump to the native 12 ms scheduler.

## Static cross-check

Ghidra 12.1.2 remains the canonical static-analysis source. Its bounded
`typeCreate` report identifies:

- entry `VA 0x10001000`;
- end-exclusive boundary `VA 0x100017E8`;
- 17 calls to the common constructor at `VA 0x100017F0`;
- the exact registration order and name strings; and
- ten immediate 32-bit tuple values for every record.

A repeatable Rizin 0.9.1 analysis of the same verified PE32/i386 working copy
independently reproduced the 2024-byte function boundary, all 17 constructor
call sites, immediate words, and string data references. The generated
Ghidra/Rizin reports and working copy remain ignored local evidence. No
original binary or resource is committed.

The registry order is:

```text
AcMustang, AcHellcat, AcDauntless, AcZero, AcStuka, AcBf109,
AcFw190, AcMe262, AcFiatG50, AcSpitfire, AcHurricane, AcTyphoon,
AcBlackWidow, AcKomet, AuJunker86, AuLancaster, AuB17
```

## Exact catalogue contract

`LegacyAircraftTypeCatalog` stores every tuple member as its source
`uint32_t` word. Named float accessors use `std::bit_cast`; they perform no
arithmetic and therefore cannot silently choose a host rounding policy.
The ten stored fields correspond to the already-recovered constructor join:

| Source value | Portable field | Native constructor role |
|---:|---|---|
| T1 | `instanceExtentBits` | direct float; instance extent input |
| T2 | `thrustPercentBits` | constructor later applies `0.01` |
| T3 | `forceDampingPercentBits` | constructor later applies `0.01` |
| T4 | `rigidBodyMassScaleBits` | copied to instance mass scale |
| T5 | `initialHealthBits` | copied to initial/current health |
| T6 | `inheritedInstanceParameterBits` | copied to inherited fields |
| T7 | `unusedTupleValueBits` | not stored by the native constructor |
| T8 | `kindCode` | direct integer, observed as 1 or 3 |
| T9 | `parameter9Bits` | direct float bits; meaning not assigned |
| T10 | `parameter10Bits` | direct float bits; meaning not assigned |

Compile-time validation fixes the record count and order, rejects duplicate
names, and verifies only the facts common to all recovered records. Tests
independently repeat the complete 17-by-10 source-word table, check lookup
identity, verify every typed view round-trips to the same bits, and require
unknown identifiers and non-canonical paths to fail closed.

The path resolver accepts only the two exact authenticated archive forms
verified by the owner-resource inventory: the 14 `Ac*` records under
`Game\Objects\AirCrafts` and the three `Au*` records under
`Game\Objects\Units`. A registered name in the wrong directory is rejected.
The resolver allocates nothing, does not normalize or retain the caller's
value, and uses case-sensitive registry identifiers. Tests exercise the
canonical path for all 17 records and both cross-directory failure cases.

`MissionPlayerVisualDescriptor` now carries an optional typed aircraft ID.
A canonical registered object must carry the matching ID, while a generic
MODL-root player visual must carry no ID. Both manifest construction and the
native publication boundary enforce this relationship. No original object
definition, private path, display string, or resource checksum enters the
catalogue.

## Control-to-sleep refresh gate

`legacyAircraftAdvanceVehicleRefreshGate` accepts:

- one complete, already-committed `LegacyAircraftControlCommandStepState`;
- caller-sampled squared linear velocity;
- caller-sampled ground and water predicates; and
- one signed refresh delta in milliseconds.

It obtains the five wake-control values and shared rest duration from the
committed control-event state, invokes the existing
`legacyVehicleAdvanceSleepStep`, and on success commits only the returned rest
duration. Command flags and all control fields remain bit-identical.

If the active-path sleep helper rejects non-finite data or signed overflow,
the complete input state is returned unchanged. A state already at the sleep
threshold preserves the recovered early gate and does not inspect values that
belong only to the skipped active path.

Synthetic tests cover:

- a committed pitch command keeping physics active;
- an active zero write allowing rest accumulation;
- the one-time 1999-to-2000 ms dynamics-clear transition;
- subsequent sleeping refreshes skipping integration and clearing;
- skipped-path NaN, infinity, and maximum-delta values;
- atomic rejection of active-path non-finite values and overflow;
- signed negative deltas; and
- prefix/suffix replay composition without hidden state.

## Current validation

- The complete Windows GCC 15.2/Ninja build passes all 119 CTests.
- A focused Clang 22 C++20 build with
  `-Wall -Wextra -Wpedantic -Werror` passes for the exact catalogue and its
  independent full-table test; the refresh gate has the same warnings-clean
  focused result.
- Manifest and publication tests cover both a correctly typed canonical
  aircraft and fail-closed generic, missing, and forged associations.
- Synthetic public-boundary tests and the 611-file repository scan pass.
- The 322-row, 14-column function catalogue has unique IDs; changed-document
  links and `git diff --check` pass.
- Independent review found that the first resolver covered only the 14
  `AirCrafts` records and omitted the three `Units` records. The two-layout
  fix and all-17 path test close that finding; final re-review reports GO with
  no remaining P0-P3 finding.

Hosted Linux, macOS, Windows x64 product, iPhoneOS, and iPhoneSimulator builds
remain publication gates. The new source is portable C++20 and no platform
backend is changed.

## Deliberate exclusions

This slice does not add:

- an input producer, event batch, queue, sort, deduplication, retry, or replay;
- Q15-to-native payload conversion or producer-cache ownership;
- a clock, time heap, catch-up loop, or assumed 12 ms cadence;
- the slot-45 force law, `CcRigidBody` Euler integration, or an x87 policy;
- mutation of the authenticated spawn pose;
- camera, collision, weapon, audio, or render publication; or
- private/original resource dependencies in source, tests, CMake, Git, or CI.

## Final recommendation

**GO for these two isolated prerequisites.**

The next live-runtime proposal must still preserve immediate producer order,
separate the 60 Hz input sampling clock from the 12 ms catch-up scheduler,
select and document a numeric policy, and keep rejected refreshes atomic.
Until those gates are closed, the player remains intentionally fixed at the
authenticated spawn pose.
