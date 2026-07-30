# EXP-20260729-060: native pitch/bank event reducer

**Date:** 2026-07-29
**Evidence:** `EV-20260729-007` and
[EXP-20260729-056](EXP-20260729-056-native-pitch-bank-event-research.md)
**Status:** implemented and locally verified; native live-control-word
observation remains pending
**Decision:** GO for the isolated reducer; NO-GO for producer, scheduler, or
live flight-state integration

## Purpose and evidence boundary

This experiment transfers the confirmed `PITCH_SET (0x5F)` and
`BANK_SET (0x65)` branch semantics into portable C++20 without inventing an
input clock or a flight-control scheduler.

The reducer accepts one event that the caller has already ordered:

- the SET event kind;
- its complete signed 32-bit native payload at event offset `+0x11`;
- the current vehicle-inactive state; and
- an explicitly selected numeric compatibility policy.

It returns either no mutation or one semantic typed write containing the
selected field, exact final binary32 bits, and the proven shared-rest-clear
directive. It does not own vehicle memory, producer provenance, event queues,
Q15 conversion, timing, physics, or rendering.

## Portable contract

The implementation is:

- `src/airfix/simulation/LegacyAircraftPitchBankEventReducer.hpp`;
- `src/airfix/simulation/LegacyAircraftPitchBankEventReducer.cpp`; and
- `tests/LegacyAircraftPitchBankEventReducerTests.cpp`.

`PITCH_SET` selects the pitch field corresponding to native vehicle `+0x448`.
`BANK_SET` selects the bank field corresponding to `+0x44C`. The offsets stay
in evidence documentation and do not leak into the portable API.

The ordering is preserved:

1. reject a type outside the reducer's two SET events;
2. accept a recognized inactive event as a no-op before inspecting its
   numeric policy;
3. reject an unsupported policy for an active event;
4. decode every active signed `int32` payload;
5. emit one overwrite and request rest clear exactly when payload is nonzero.

The full native signed domain is admitted. Producer observations such as the
keyboard and AI `[-32,+32]` bounds are not consumer validation rules.
`PITCH_APPLY (0x60)` and `BANK_APPLY (0x66)` are deliberately absent from the
public event enum; raw values for them fail as unsupported inputs. Their
confirmed AirCraft-chain no-op remains a dispatch fact outside this reducer.

## Numeric policy

The sole policy is named
`startupPc53RoundToNearestEven`. It models the original startup-compatible
x87 precision-control 53 and nearest-even rounding condition. The API returns
binary32 bits rather than an unlabelled host `float`.

The implementation uses the exact scale rational
`11542725 / 2^28`, whose binary32 bits are `0x3D3020C5`. Unsigned integer
arithmetic performs two explicit nearest-even steps:

1. exact signed-payload magnitude times the scale significand, rounded to a
   53-bit significand;
2. that retained value rounded to a 24-bit binary32 significand.

The sign and exponent are then assembled into the stored binary32 word. This
is independent of the host floating-point environment and preserves the
native `FILD -> FMUL -> FST` compatibility model, including its PC53
double-rounding discriminator.

This policy is deterministic reconstruction behavior. It is not labelled
proven live full-domain parity because no controlled runtime trace has yet
captured the process-wide x87 precision and rounding control at the event
branch.

## Verified semantics

The dedicated test executable covers:

- event values, exact scale bits, `noexcept`, and trivially copyable output;
- all 11 confirmed positive, negative, zero, producer-range, and full-domain
  discriminator vectors for both axes;
- `INT32_MIN -> 0xCCB020C5` and
  `INT32_MAX -> 0x4CB020C5`;
- explicit rejection of decimal-double `0.043`, premature payload-to-float,
  and retained-PC64 alternative results;
- inactive no-write behavior across zero, signs, and signed-domain endpoints;
- unsupported event and numeric-policy failures;
- active positive-zero overwrite without rest clear;
- repeated nonzero writes without equality suppression;
- nonzero-to-zero and interleaved pitch/bank sequences, with last processed
  SET winning independently per axis.

The state-application helper exists only in the test. Production code cannot
partially mutate a vehicle or rest counter.

## Integration gate

This reducer may be consumed only after a future state owner can apply the
typed field write and shared rest clear atomically in already-established
event order. It must remain unwired from live player, keyboard, controller,
analog, AI, and network sources until controlled evidence establishes:

- live x87 control state or an accepted portability policy;
- Q15-to-native payload conversion and event ordering;
- cross-producer ordering and queue behavior; and
- event phase relative to the nominal 12 ms AirCraft refresh.

No nominal 12 ms sample-and-hold behavior is inferred here.

## Validation

- A clean native Windows GCC 15.2/Ninja build passes all 100 CTests.
- An isolated WSL Ubuntu GCC 13.3/Ninja build passes all 100 CTests.
- A fresh local Clang 22.1.8 build passes the dedicated reducer test.
- The public-boundary scanner passes 509 files and `git diff --check` passes.
- An independent exhaustive audit compared all `2^32` signed payloads with a
  hardware binary64 PC53/nearest-even path; every result matched. A separate
  exact-product oracle confirmed that `+1555145203` and its negative
  counterpart are the only sign-symmetric full-domain discriminator pair
  between this PC53 model and direct PC64/exact-product-to-binary32 rounding.
- Independent code review initially rejected validation precedence for an
  inactive event carrying an unsupported numeric-policy value. Moving the
  native inactive gate before active-policy validation and adding the exact
  regression resolved it. Final re-review reports GO with no P0-P2 findings.

All seven hosted checks pass: portable, native Windows product, macOS, and
clangd jobs in run `30507391649`, plus unsigned iPhoneOS and iPhoneSimulator
jobs in run `30507391659`.
