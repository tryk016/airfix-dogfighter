# EXP-20260801-107: AirCraft HUD elapsed clock

**Status:** dormant renderer-neutral lifecycle owner implemented; live refresh
and render-event wiring deliberately absent

**Evidence ID:** `EV-20260801-006`
**Decision:** GO for the isolated accumulator, gate, snapshot, and reset
lifecycle; NO-GO for ordinary frame integration or live x87 bit-parity claims

## Question

Can the shared `AirCraft+0x54C` elapsed-time field be reconstructed as a
bounded C++20 lifecycle primitive without assuming one refresh per frame,
inventing the six counter values, or connecting an unverified runtime
scheduler?

## Static method

Ghidra 12.1.2 was the primary source for a fresh headless instruction export
from the hash-verified `AirCraft.type` copy with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.
Rizin 0.9.1 plus rz-ghidra independently reproduced the three exact function
boundaries and the relevant bytes:

| Function | Exact VA range | Clock operation |
|---|---|---|
| `AircraftInstanceConstructor` | `[0x10001EA0,0x10002288)` | `0x100021AE` stores exact `+0` at `AirCraft+0x54C` |
| `AircraftPostCollisionRefresh` | `[0x10002F60,0x10003F1D)` | `0x10003D23/2A/30` perform `FLD delta`, `FADD +0x54C`, `FSTP +0x54C` |
| `AirCraftHudRenderStage` | `[0x10006B00,0x10007321)` | consumers load one field snapshot; `0x1000730F` stores exact `+0` |

Repeatable local commands are:

```powershell
./tools/Invoke-GhidraAnalysis.ps1 `
  -ProgramName 'AirCraft.type' `
  -PostScript 'ExportFunctionInstructions.java' `
  -PostScriptArguments @('10001EA0', '10002F60', '10006B00') `
  -ReportSuffix 'hud-clock-lifecycle'

./tools/Invoke-RizinAnalysis.ps1 `
  -InputFile '<HASH_VERIFIED_WORK_COPY>' `
  -ExpectedSha256 `
    '9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e' `
  -FunctionId 'FN-AIRCRAFT-00002F60' `
  -Rva '0x00002F60' `
  -CreateMissingFunction
```

The same Rizin command is repeated for RVA `0x00001EA0` and `0x00006B00`.
Raw reports, verified binaries, tool databases, and local paths remain outside
Git.

## Recovered lifecycle

The constructor first creates six retained rolling-number states at
`AirCraft+0x528..+0x53C`, then initializes the shared clock at `+0x54C` to
positive zero. Every completed slot-44 post-collision refresh adds its supplied
binary32 delta to this field. The evidence does not support collapsing this to
one nominal 12-ms update per rendered frame: the scheduler can execute zero,
one, or multiple refreshes before a HUD event.

The HUD stage has five static loads of `+0x54C`, at VAs `0x10006C9A`,
`0x10006D07`, `0x10006DFC`, `0x1000722F`, and `0x100072CF`. One load occurs in
the primary/selected-secondary weapon loop, so six retained digit states
consume the same accumulated value. There is no intervening clock write. The
stage then stores exact positive zero at `0x1000730F`.

Four native early exits branch past that reset, in this exact precedence:

1. an application window/overlay is active;
2. the aircraft has no camera at stage entry;
3. the type-level HUD byte is disabled; or
4. the repeated camera read fails after layout calculation.

A failed gate therefore retains accumulated time for a later eligible stage.
Once all gates pass, the reset executes even when an individual optional HUD
element or texture is absent.

## Portable boundary

`LegacyAircraftHudElapsedClockState` is single-thread-confined, bounded,
allocation-free, and disconnected from applications and render backends. It
provides three explicit lifecycle operations:

- `advance(delta)` accepts one already-scheduled non-negative finite refresh
  delta and accumulates it;
- `beginHudStage(gates)` returns one immutable elapsed snapshot shared by the
  six known rolling-number consumers;
- `commitHudStage(snapshot)` clears the accumulator exactly once, while
  `abortHudStage(snapshot)` preserves it for a retry.

Tokens and exact snapshot bits prevent stale or forged stage values from
partially committing. Refreshes are rejected while a stage transaction is
open, avoiding a mixed snapshot in this dormant portable boundary. Rejected
gates, invalid deltas, overflow, stale commits, and aborted stages leave the
authoritative accumulator unchanged.

The native `FADD` executes under process-wide x87 state that has not been
dynamically accepted. The portable implementation uses binary64 staging before
the binary32 store to model the documented startup-compatible PC53/RNE policy.
This is an explicit reconstruction policy, not a claim that the live process
cannot change its x87 control word.

## Validation

Synthetic tests cover:

- exact positive zero after construction and after one successful commit;
- zero/one/multiple refresh accumulation without a one-frame assumption;
- all four early gates retaining accumulated time;
- one immutable snapshot read by all six consumers;
- rejection of refresh during an active stage;
- one-time commit, abort-and-retry, stale token, and mismatched snapshot paths;
- fail-closed NaN, infinity, negative delta, and overflow handling.

The new owner is not linked to the Windows or iOS frame path. Live scheduler
calls, render event `0x06`, real camera/gate publication, the six counter value
producers, and physical-device acceptance remain later evidence gates.

## Catalog correction

The same independent instruction reports expose two prior size-note errors.
`AircraftHudRollingDigitsInitialize` ends after the three-byte return at
`0x10009267`, so its exact range is `[0x10009210,0x1000926A)` (90 bytes).
`AircraftHudRollingDigitsDraw` ends after the return at `0x1000943D`, so its
exact range is `[0x100093A0,0x10009440)` (160 bytes). No executable behavior
changed; only the catalog and earlier experiment wording are corrected.
