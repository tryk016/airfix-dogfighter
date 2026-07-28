# Gameplay-camera refresh time contract

**Date:** 2026-07-28

**Evidence:** `EV-20260728-009`

**Scenario:** `SCN-CAMERA-001`, `SCN-FLIGHT-001`

**Status:** scheduler-to-AirCraft factor-recovery time join confirmed by
Ghidra and Rizin

## Question

What is the physical unit of the argument used by `AirCraft.type` RVA
`0x00002F60` to recover the three gameplay-camera collision factors, and is it
the aircraft's existing scheduler delta or an unrelated refresh value?

## Sources and method

The analysis used the hash-verified working copies recorded in the source
manifest:

| Module | SHA-256 | Image base |
|---|---|---:|
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |
| `AirCraft.type` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | `0x10000000` |

Ghidra 12.1.2 supplied the canonical MSVC symbol, event, vtable, and
decompilation context. Rizin 0.9.1 independently exported normalized
instruction reports for `AfVehicle::ProcessEvent` and the manually created,
vtable-confirmed AirCraft function at RVA `0x00002F60`.

No binary was executed, patched, uploaded, or modified. Raw reports remain in
the ignored local artifact tree.

## Confirmed data flow

For the refresh-event path, `AfVehicle::ProcessEvent` performs:

```text
0x1001B9E4  load event payload low  dword from event +0x11
0x1001B9E7  load event payload high dword from event +0x15
0x1001B9F8  FILD signed qword
0x1001B9FC  multiply by f32[0x1007C7C4] = 0.001f
0x1001BA02  store binary32 at local [esp+0x10]
```

The event payload is the signed 64-bit millisecond delta already established
by the `NfTimeHeap -> NfActor::Refresh -> AfVehicle::ProcessEvent` scheduler
chain. The multiplication therefore produces seconds.

Both branches that invoke primary vtable slot `+0xB0` push that exact local
binary32 value:

```text
full physics/collision path:
0x1001C1B4  mov eax, [esp+0x10]
0x1001C1BB  push eax
0x1001C1BE  call dword [vtable+0xB0]

physics/collision skipped path:
0x1001C3BC  mov ecx, [esp+0x10]
0x1001C3C3  push ecx
0x1001C3C6  call dword [vtable+0xB0]
```

The AirCraft vtable maps slot `+0xB0` to VA `0x10002F60`. Its prologue
subtracts `0x6C` bytes and saves four registers, so the caller's sole
four-byte argument is at the later `[esp+0x80]`. Rizin independently reports
that operand at each factor-recovery site:

```text
0x10003DE2  fld  dword [esp+0x80]
0x10003DE9  fmul dword [0x1000D4B4]  ; 0.25f
0x10003DEF  fadd dword [AirCraft+0x42C]

0x10003E42  fld  dword [esp+0x80]
0x10003E49  fmul dword [0x1000D4B4]
0x10003E4F  fadd dword [AirCraft+0x430]

0x10003EA2  fld  dword [esp+0x80]
0x10003EA9  fmul dword [0x1000D4B4]
0x10003EAF  fadd dword [AirCraft+0x434]
```

The same AirCraft method also adds this parameter to independent timers. This
is consistent with a seconds delta and rules out a camera-only frame fraction.

## Result

The factor-recovery argument is the scheduler-derived refresh delta in
seconds:

```text
refreshDeltaSeconds =
    binary32(signedEventDeltaMilliseconds * 0.001f)

if AirCraft+0x98 <= 0 or AirCraft+0x460 != 0:
    factors = (0, 0, 0)
else for each factor:
    if factor < 1:
        factor += refreshDeltaSeconds * 0.25f
    factor = clamp(factor, 0, 1)
```

AirCraft selects a 12 ms dependant interval. Its nominal binary32 delta is
therefore `0.012f` (bits `0x3C449BA6`), and one unobstructed nominal recovery
step adds `0.003f`.

This delta belongs to the aircraft scheduler, not the renderer and not the
portable 60 Hz input pump. A future runtime must preserve that distinction.
It may use the recovered nominal fixed interval while reconstructing the
legacy scheduler, but it must not substitute `1/60` merely because input is
sampled at 60 Hz.

## Tool comparison

| Check | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| `AfVehicle::ProcessEvent` boundary | `[0x1001B6C0,0x1001FA6C)` | exact agreement | accepted |
| millisecond payload and `0.001f` conversion | typed event context plus instructions | same `FILD`, multiply, and store | accepted |
| both slot `+0xB0` call sites | structured control flow plus instructions | same indirect calls and pushed local | accepted |
| AirCraft slot target | vtable-confirmed `0x10002F60` | manual function at the confirmed RVA | accepted |
| AirCraft boundary | `[0x10002F60,0x10003F1D)` | exact agreement | accepted |
| factor argument and three consumers | `float` parameter in decompilation | same `[esp+0x80]` operand at all sites | accepted |
| calling convention | MSVC `__thiscall`, `ret 4` | incorrectly inferred `cdecl` with excessive arguments | Ghidra preferred |

Rizin remains an independent instruction and boundary check, not the
authority for the recovered C++ signature.

## Reconstruction changes

The portable API now names the value `refreshDeltaSeconds`, exposes the exact
nominal AirCraft value as
`legacyAircraftNominalRefreshDeltaSeconds`, and tests the nominal `0.003f`
factor increment. The narrow algebraic helper continues to preserve the
native finite-negative arithmetic and clamp behavior; rejecting backwards
scheduler time belongs at the future scheduler boundary, not inside this
recovered function.

The step coordinator still requires the caller to supply the delta and both
vehicle gates. Confirming the unit does not invent the missing dynamic
aircraft-state producer.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds compile all 220
steps and pass all 67 portable tests. Focused clangd 22 checks report zero
errors in the changed implementation and test translation units. The three
reverse-engineering wrapper suites, 12 Rizin report-normalization tests,
public-boundary tests and a 337-file public scan pass. The 224-entry function
catalogue remains unique; `actionlint` and `git diff --check` are clean.

No proprietary input is required by the implementation or its synthetic
tests. The regenerated Rizin report and both clean build trees remain ignored
local artifacts.

## Confidence

Confidence is **3/3** for:

- the argument's unit as seconds;
- the exact millisecond-to-seconds conversion;
- reuse of the same local delta on both slot-44 call paths;
- the 12 ms nominal AirCraft interval and `0.012f` nominal value; and
- use of that argument by all three factor-recovery expressions.

Controlled runtime traces remain necessary for end-to-end camera feel,
collision tolerances, and scheduler gap behavior, but not for naming this
argument or using seconds in the portable contract.

## Related material

- [Aircraft flight boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Aircraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Gameplay-camera step coordinator](EXP-20260728-021-camera-step-coordinator.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
