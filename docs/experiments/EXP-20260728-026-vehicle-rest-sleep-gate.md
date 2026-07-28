# AfVehicle rest/sleep gate

**Date:** 2026-07-28

**Evidence:** `EV-20260728-011`

**Scenario:** `SCN-FLIGHT-001`, `SCN-COLLISION-001`

**Status:** signed 64-bit rest accumulator, thresholds, and refresh transitions
confirmed by Ghidra and independently checked with Rizin

## Question

What do the two apparent `AfVehicle` fields at `+0x458` and `+0x45C` mean,
how do they gate the aircraft physics path, and what establishes the ground
predicate stored at `+0x464`?

## Sources and safety

The analysis used only the hash-verified `AfEngine.dll` working copy recorded
in the source manifest:

| Module | SHA-256 | Image base |
|---|---|---:|
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |

Ghidra 12.1.2 supplied the canonical MSVC symbols, typed context,
cross-references, and control-flow interpretation. Rizin 0.9.1 independently
exported normalized instructions for the constructor, refresh dispatcher, and
ground predicate.

No original file was modified or executed. No binary, analysis database, or
raw report was uploaded. Hash-verified copies and generated reports remain in
the ignored local artifact tree.

## Recovered representation

The values at `AfVehicle +0x458` and `+0x45C` are respectively the low and high
DWORDs of one signed 64-bit rest-duration accumulator:

```text
restDurationMilliseconds = s64[vehicle+0x458]
```

`AfVehicle::AfVehicle` initializes the low DWORD to `1999` (`0x7CF`) and the
high DWORD to zero. The refresh path adds the signed 64-bit scheduler delta
with an x86 `ADD`/`ADC` pair and compares the result with `2000` (`0x7D0`).
The unit is therefore the same integer millisecond unit as the dependant
scheduler payload.

The exported symbol
`?IsOnGround@AfVehicle@@QAE_NXZ` identifies
`AfVehicle::IsOnGround` at RVA `0x00020FC0`. Its complete body returns the byte
at `this + 0x464`, proving the `onGround` meaning and width.

## Refresh transition

For refresh events `0x76`, `0x77`, and `0x78`,
`AfVehicle::ProcessEvent` performs this state transition:

```text
restMs = s64[vehicle+0x458]

if restMs < 2000:
    run active physics, force, collision, and room work

    controlsIdle =
        f32[vehicle+0x444] == 0 &&
        f32[vehicle+0x440] == 0 &&
        f32[vehicle+0x450] == 0 &&
        f32[vehicle+0x44C] == 0 &&
        f32[vehicle+0x448] == 0

    resting =
        controlsIdle &&
        ((IsOnGround() && velocitySquared < 0.03f) ||
         (IsWaterUnit() && velocitySquared < 0.08f))

    restMs = resting ? restMs + deltaMilliseconds : 0

    if restMs >= 2000:
        ResetForceAndTorque()
        clear six rigid-body dynamic-state floats
else:
    skip active physics, force, collision, and room work

invoke the post-collision/refresh virtual slot in both branches
```

Both speed comparisons are strict. Their exact binary32 constants are:

| Predicate | Decimal | Bits |
|---|---:|---:|
| ground | `velocitySquared < 0.03f` | `0x3CF5C28F` |
| water | `velocitySquared < 0.08f` | `0x3DA3D70A` |

The threshold-crossing refresh still runs the active path, then resets force
and torque and clears the six floats at `+0x294..+0x29C` and
`+0x2A0..+0x2A8`. A subsequent refresh whose entry duration is already at
least 2000 ms skips the active path and does not repeat that clearing.

Because the constructor starts at 1999 ms, the first qualifying low-motion
ground or water refresh can cross the sleep threshold immediately. Control,
damage, ownership, and synchronization event paths separately reset the
accumulator to zero. A successful nonzero control write therefore wakes the
vehicle before a later refresh; refresh input alone must not impersonate that
separate event transition.

AirCraft routes its water-unit predicate to the already recovered shared
zero-return implementation. Its rest accumulation therefore uses only the
on-ground leg, although the portable `AfVehicle` contract retains both legs.

## Independent Rizin comparison

Rizin confirms the constructor writes:

```text
AfVehicle::AfVehicle [0x1001A950, 0x1001AFAA)
0x1001AA5B  mov dword [esi+0x458], 0x7cf
0x1001AA65  mov dword [esi+0x45c], ebx
0x1001AA7E  mov byte  [esi+0x464], bl
```

It also confirms the complete ground predicate:

```text
AfVehicle::IsOnGround [0x10020FC0, 0x10020FC7)
    mov al, byte [ecx+0x464]
    ret
```

Within `AfVehicle::ProcessEvent [0x1001B6C0, 0x1001FA6C)`, the independent
instruction report places the five exact-zero control tests at
`0x1001C1CF..0x1001C23C`, the on-ground read at `0x1001C242`, the ground
threshold comparison at `0x1001C26E`, the water virtual predicate at
`0x1001C286`, and the water threshold comparison at `0x1001C2B2`.

The low-DWORD `ADD` sequence is at `0x1001C2C5..0x1001C2D1`; the high-DWORD
`ADC` sequence is at `0x1001C2D7..0x1001C2E3`. The reset writes are at
`0x1001C2EB` and `0x1001C2F1`, followed by the signed-high/unsigned-low
comparison with `0x7D0` at `0x1001C2F7..0x1001C30B`. Dynamic-state clearing
begins at `0x1001C30D`.

Rizin agrees on field widths, constants, branches, and boundaries. Its
automatic type and calling-convention inference remains weaker for this MSVC
x86 binary, so Ghidra remains canonical for symbols and typed context.

## Reconstruction decision

The portable C++20 layer now contains an isolated, allocation-free
`LegacyVehicleSleepState` transition. It accepts the already-derived
`velocitySquared` rather than claiming that a portable three-component sum has
the final x87 parity tolerance. It preserves:

- the signed millisecond accumulator;
- the 1999 ms constructor value and 2000 ms sleep threshold;
- exact-zero tests for all five wake controls;
- strict ground and water speed-squared thresholds;
- active execution on the threshold-crossing step;
- skipped integration on later sleeping refreshes; and
- one-shot dynamic-state clearing on threshold crossing.

Signed overflow returns no result instead of reproducing native two's-complement
wraparound. This is an explicit safe portable-boundary deviation for an
impossible-duration state. Finite negative scheduler deltas remain supported
because the recovered native payload and addition are signed.

The helper is not yet wired into `PlayerAircraftSimulation`. That simulation
does not own the recovered rigid-body state or the native 12 ms scheduler.
Control-event wake remains a separate caller transition and is not guessed
from values supplied to an already sleeping refresh.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 226
steps and pass all 69 portable tests. Clangd 22.1.8 reports zero errors in the
new production and test translation units.

The Ghidra, working-copy, and Rizin wrapper suites pass, as do all 12 Rizin
report-normalization tests. The public-boundary test and 346-file scan pass.
The 228-entry, 14-column function catalogue has unique IDs and module/RVA
pairs. `actionlint`, `git diff --check`, and the local-path scan are clean.
Both unsigned iOS variants remain mandatory publication gates in GitHub
Actions.

No proprietary data is required by the portable build or tests.

## Confidence

Confidence is **3/3** for:

- the signed 64-bit representation and low/high DWORD layout;
- the millisecond unit, 1999 ms initialization, and 2000 ms threshold;
- both strict speed-squared constants;
- the five exact-zero control gates;
- the active, threshold-crossing, and sleeping branches; and
- `AfVehicle::IsOnGround` returning byte `+0x464`.

Confidence is **2/3** for the descriptive member name
`restDurationMilliseconds`, because the original member label is not exported,
and for complete portable flight parity, which still needs controlled runtime
traces and x87 tolerance work.

## Related material

- [Aircraft flight boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Aircraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
- [Static tool cross-check](EXP-20260727-001-static-tool-crosscheck.md)
- [Aircraft scheduler ABI](EXP-20260724-003-aircraft-scheduler-abi.md)
- [Camera refresh time contract](EXP-20260728-023-camera-refresh-time-contract.md)
