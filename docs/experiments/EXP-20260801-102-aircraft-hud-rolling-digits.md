# EXP-20260801-102: AirCraft HUD rolling digits

**Status:** renderer-neutral state and draw plan implemented; authenticated
texture ownership and native consumers remain separate work

**Evidence ID:** `EV-20260801-002`
**Decision:** GO for the isolated four-digit presentation helper; NO-GO for
ordinary HUD publication or live x87 bit-parity claims

## Question

Can the repeated numeric-display helper inside the AirCraft HUD be recovered
as a bounded C++20 presentation primitive without guessing its texture,
backend ownership, live value producers, or process-wide floating-point state?

## Static method

Ghidra 12.1.2 was the primary source for the hash-verified `AirCraft.type`
copy with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.
Fresh headless exports cover functions, instructions, constants, double
constants, and references for these exact ranges:

| Function | Exact VA range | Size | Recovered role |
|---|---|---:|---|
| `0x10009210` | `[0x10009210,0x1000926A)` | 90 | initialize four retained digit positions |
| `0x10009280` | `[0x10009280,0x1000939F)` | 287 | update/animate toward a formatted value |
| `0x100093A0` | `[0x100093A0,0x10009440)` | 160 | draw four vertical-atlas windows |

Rizin 0.9.1 plus rz-ghidra independently reproduced all three boundaries and
their instruction streams. The draw target was separately checked in the
hash-verified `Cc.dll` copy with SHA-256
`18002a3af1405d932579c6d6888256ce26b8a479735093d33441f10fc27aa8af`:
`GtScreen::Blit` is exact RVA `0x00044D90`, range
`[0x10044D90,0x10044DD6)`. It forwards source width and height derived from the
supplied source rectangle through virtual slot `+0x58`.

`AfVehicleType::LoadHUDGraphics` at AfEngine RVA `0x0001A440` registers the
`digits` texture role under `Graphics\Ingame\HUD`. The corresponding
hash-verified AfEngine copy has SHA-256
`a4ccc7e59e4119488322e80077803a5bb9922731d84ce1a420fd27516d45711e`.
Read-only owner-local archive inventory confirms one logical `digits.gti`
entry with CRC `3d74811a`, dimensions `16x128`, formats 4 and 8, and one mip.
No image, archive payload, binary, local path, or analysis database enters the
repository.

Repeatable wrapper commands are:

```powershell
./tools/Invoke-GhidraAnalysis.ps1 `
  -ProgramName 'AirCraft.type' `
  -PostScript 'ExportAddressFunctions.java' `
  -PostScriptArguments @('10009210', '10009280', '100093A0') `
  -ReportSuffix 'hud-number-helper-functions'

./tools/Invoke-RizinAnalysis.ps1 `
  -InputFile '<HASH_VERIFIED_WORK_COPY>' `
  -ExpectedSha256 `
    '9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e' `
  -FunctionId 'FN-AIRCRAFT-00009280' `
  -Rva '0x00009280'
```

Raw reports remain ignored local evidence.

## Recovered state and update contract

The initializer formats its signed input with `%04d`, converts the first four
characters with `character - '0'`, stores them at offsets `+4,+8,+C,+10`,
stores the texture pointer at `+0`, and clears the cached elapsed value at
`+0x14`. The native constructor does not establish `+0x18`; the portable type
uses an explicit validity bit so it never reproduces an uninitialized read.

The update first converts its incoming float through MSVC `_ftol`. That
conversion remains outside the portable primitive: its input is an
already-quantized signed 32-bit value. After `%04d`, each retained position
uses the following recovered rule:

```text
target = character - '0'
if abs(current - target) > 5:
    target += target < 5 ? 10 : -10

factor = float(pow(0.0005, accumulatedElapsedSeconds))
result = float(factor*current + (1-factor)*target)
if result < 0:  result += 10
else if result >= 10: result -= 10
if result is not in [0,10): result = +0
```

The factor is recomputed only when the accumulated elapsed value differs from
the cached value. The enclosing HUD shares that elapsed accumulator across the
displays and clears it at the end of the stage. The exact constants are double
`0.0005`, double `5.0`, and cycle `10.0`.

## Recovered atlas draw contract

For valid retained position `p` in slot `i`, the draw helper emits:

```text
destination = {x + i*8, y + 1, width 7, height 9}
source      = {left 0, top p*11 + 1, right 7, bottom p*11 + 10}
```

Fractional `p` values smoothly roll the source window between vertically
stacked glyphs. Positions outside `[0,10)` and non-finite positions are
skipped independently. Tint passes through unchanged. The recovered 16x128
GTI dimensions bound every valid source rectangle.

Six helper instances are constructed with initial values `0`, `100`, `0`,
`0`, `100`, and `0`. Five static update/draw call sites occur in the enclosing
HUD; one call site selects between two weapon counters. Their live semantic
labels are not assigned until the producer fields and texture ownership are
separately authenticated.

## Portable boundary and numeric policy

`LegacyAircraftHudRollingDigitsState` and
`LegacyAircraftHudRollingDigitsPlan` preserve the recovered operation order,
binary32 retained state, fixed four-command capacity, and per-slot filtering.
They allocate no memory and contain no asset, renderer, simulation, or timing
dependency.

Double staging follows a PC53-compatible visual policy. It does not claim
bit-identical equivalence to every live x87 precision/rounding configuration.
The dynamic Phase A capture remains NO-GO because the original executable
could not initialize its legacy Direct3D driver before `AirCraft.type` loaded;
there were zero accepted observations. No runtime claim is derived from that
attempt.

## Validation

A fresh GCC 15.2/Ninja build compiles the complete portable graph. The focused
test proves `%04d` initialization for zero, leading zeros, over-width and
negative values; cached retention; the forward 9-to-0 path; strict distance-5
handling; wrap and non-finite reset; exact integral and fractional atlas
rectangles; independent invalid-slot suppression; atomic invalid-input
rejection; bounds-safe command lookup; and 1,024 allocation-free steady-state
iterations. The complete portable suite passes 131/131 CTests.

A fresh MSVC 19.51 HostX64/Ninja build compiles the complete Windows x64
product. Its 142 native executable tests pass in one run; the Python capture
validator is blocked only by the default command sandbox and passes separately
outside that restriction, completing 143/143 tests. Both Windows product
smokes and the D3D11 renderer tests are included. The public-boundary synthetic
suite and 693-file repository scan, all 12 Rizin exporter tests, both Ghidra
and Rizin wrapper suites, and `git diff --check` pass.

## Decision

**GO** for the isolated renderer-neutral rolling-number state and exact draw
plan, and for later use by authenticated D3D11/Metal submissions.

**NO-GO** for connecting ordinary frames until the `digits` texture set,
native-output mapping, backend resources, live producer meanings, and elapsed
clock ownership are joined. **NO-GO** for asserting live `_ftol` or x87
bit-parity until a controlled runtime environment can initialize the original
legacy renderer and produce an accepted capture.
