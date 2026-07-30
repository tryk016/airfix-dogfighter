# EXP-20260730-075 — mission outcome consumer

## Decision

`EV-20260730-006` gives GO for a pure result/progression consumer downstream of
the already implemented two-flag mission outcome. It remains NO-GO for live
AFS wiring, score/stat mutation, roster chunks, file writes, and platform UI
execution.

All claims are static-only and capped at confidence `2/medium`.

## Question

How does the native single-player result screen resolve conflicting outcome
flags, choose Continue or Retry, and decide whether to update campaign
progress?

## Method

Ghidra 12.1.2 was the primary source. Rizin 0.9.1 independently matched the
selected function boundaries and relevant instructions in hashed read-only
working copies. No game executable, GUI, original script, or installer was
run.

The audit covered:

- result-screen constructor `[0x00405A90, 0x0040690A)`;
- result-screen callback `[0x00406CE0, 0x00406E14)`;
- `NfMission::ProcessEvent` and `NfMission::Refresh`;
- database process creation/global refresh;
- one process constructor/refresh; and
- the AFS interpreter boundary.

## Findings

The result predicate appears three times and is exact:

```text
success = missionExists && accomplished && !failed
```

Failure therefore wins when both independent flags are true.

Success reads `mission_number` as signed 32-bit `int`, uses x86 `INC`, maps a
case-insensitive `"allied"` thread to `1` and all other/missing values to `0`,
always replaces `THRD`, and then adds or conditionally replaces `ALMI`/`AXMI`.
An existing maximum is retained when signed `old >= candidate`.

The portable consumer accepts the side as an already classified typed value.
It deliberately rejects `INT32_MAX`, whose native `INC` wraps, so it cannot
emit a corrupt modern progression directive. It otherwise preserves signed
comparison behavior, including `-1 -> 0`.

Failure emits Retry and no progression. Success emits Continue, always emits a
thread directive, and emits one of add/replace/none for the selected maximum.
Unknown side values fail closed without UI or progression directives.

Ordinary mission refresh executes active AFS processes before polling trigger
refresh, so a newly polled trigger state is first visible on the next mission
refresh. Immediate `Spawned` events are a distinct update-then-call path. The
remaining compiler/bytecode/global-order gaps still forbid live AFS wiring.

## Implementation

The allocation-free `LegacyMissionOutcomeConsumer` contains only typed values:

- result: failure or success;
- primary action: Retry, Continue, or unavailable on invalid metadata;
- thread side: Axis or Allied;
- maximum directive: none, add, or replace; and
- next signed mission number.

It owns no strings, trigger/AFS process, event queue, FourCC, roster, score,
stats, disk, console, UIKit, Win32, renderer, or audio state.

## Synthetic tests

Tests cover:

- all four outcome flag states with and without a mission object;
- both-flags failure precedence;
- failure ignoring otherwise invalid/overflowing unused metadata;
- Axis and Allied thread directives;
- absent maximum add;
- signed old-less/equal/greater maximum decisions;
- negative-to-zero and `INT32_MAX - 1 -> INT32_MAX` boundaries;
- thread replacement even when the maximum is unchanged;
- unknown side and `INT32_MAX` fail-closed behavior; and
- deterministic, trivially copyable value results.

## Validation

- the complete portable GCC/Ninja build passes 116/116 CTests;
- the complete MSVC 19.51/Ninja Windows product build links
  `AirfixDogfighter.exe` and passes 126/126 CTests, including both D3D11
  product smokes;
- Clang 22 compiles the focused test with warnings as errors and the test
  passes;
- clang-format warnings-as-errors and `git diff --check` pass;
- synthetic public-boundary tests and the 600-file repository scan pass; and
- the 322-row, 14-column function catalogue remains unique and has only its
  two known historical missing references.
- independent review reproduces the GCC Release build and all 116 CTests,
  checks the code against the raw Ghidra evidence, and returns GO with no
  P0-P3 finding.

Hosted Windows/Linux/macOS/iOS Actions remain the publication gate.

Controlled runtime comparison is still required before raising any confidence
above `2/medium` or wiring a live mission flow.
