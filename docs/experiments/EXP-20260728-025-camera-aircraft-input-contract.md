# Gameplay-camera AirCraft input contract

**Date:** 2026-07-28

**Evidence:** `EV-20260728-010`

**Scenario:** `SCN-CAMERA-001`, `SCN-FLIGHT-001`

**Status:** live geometry sources and both factor-recovery gates named by
Ghidra and independently confirmed by Rizin

## Question

Which live AirCraft fields feed the already implemented gameplay-camera step,
and what do the two previously unnamed factor-recovery gates at instance
offsets `+0x98` and `+0x460` mean?

## Sources and safety

The analysis used only hash-verified working copies recorded in the source
manifest:

| Module | SHA-256 | Image base |
|---|---|---:|
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |
| `AirCraft.type` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | `0x10000000` |

Ghidra 12.1.2 supplied the canonical MSVC symbols, typed decompilation, event
context, and cross-references. Rizin 0.9.1 independently exported normalized
instruction reports for the exact function boundaries below.

No original file was modified or executed. No binary, analysis database, or
raw report was uploaded. Hash-verified copies and generated reports remain in
the ignored local artifact tree.

## Confirmed live inputs

| Portable input | Native source | Confirming behavior |
|---|---:|---|
| chase world position | `AfVehicle +0x278..+0x280` | read by the chase recurrence; event `0x6D` writes the position |
| vehicle world anchor | `AfVehicle +0x1A4..+0x1AC` | read separately by look-at and line queries; event `0x6D` also writes it |
| vehicle world rotation | quaternion `(w,x,y,z)` at `AfVehicle +0x284..+0x290` | events `0x70` and `0x71` update it; `CcMatrixRot::FromQuat` supplies the recovered matrix |
| vehicle health | `NfActor +0x98` | exported `NfActor::GetHealth()` returns this float; health-set event `0x84` writes it |
| vehicle inactive | byte at `AfVehicle +0x460` | `Activate()` clears it; `Deactivate()` and kill paths set it |
| refresh delta | scheduler event delta converted from milliseconds to seconds | both slot-`+0xB0` call paths pass the same binary32 value; nominal AirCraft cadence is `0.012f` |

The two position triples remain distinct in the portable contract. The binary
updates them together on one event path but reads them for different purposes
in steady state; substituting either one for the other would invent an
invariant.

## Ghidra evidence

The exported symbol `?GetHealth@NfActor@@QAEMXZ` identifies
`NfActor::GetHealth` at `AfEngine.dll` RVA `0x00026380`. Its complete body
returns the float at `this + 0x98`. `NfActorType::GetHealth` returns type field
`+0x48`, and instance construction copies that value to `+0x98`. Event
`EVENT_HEALTH_SET` (`0x84`) writes the same instance field.

The exported vehicle lifecycle methods establish the byte at `+0x460`:

| RVA | Symbol | Relevant writes |
|---:|---|---|
| `0x0001FF80` | `AfVehicle::Activate()` | active byte `+0x49 = 1`; inactive latch `+0x460 = 0` |
| `0x0001FF90` | `AfVehicle::Deactivate()` | active byte `+0x49 = 0`; adjacent lifecycle byte `+0x461 = 0`; inactive latch `+0x460 = 1` |

Multiple death paths set `+0x460` immediately before calling
`NfActor::Kill`. `AfVehicle::ProcessEvent` also uses the byte to gate controls,
damage, and the AirCraft camera-factor recovery. The evidence therefore
supports the broader name `vehicleInactive`; `vehicleKilled` or
`vehicleDeactivated` would both be too narrow.

In `AirCraft.type` RVA `0x00002F60`, factor recovery first compares the health
float at `+0x98` with zero and then tests the inactive byte at `+0x460`.
Recovery proceeds only while health is positive and the vehicle is active.

## Independent Rizin comparison

Rizin confirms the instruction boundaries and field accesses without relying
on Ghidra's decompiler:

```text
NfActor::GetHealth [0x10026380, 0x10026387)
    fld  dword [ecx+0x98]
    ret

AfVehicle::Activate [0x1001FF80, 0x1001FF8C)
    mov  byte [ecx+0x49], 0x01
    mov  byte [ecx+0x460], 0x00

AfVehicle::Deactivate [0x1001FF90, 0x1001FFA3)
    mov  byte [ecx+0x49], al
    mov  byte [ecx+0x461], al
    mov  byte [ecx+0x460], 0x01
```

For the AirCraft slot target `[0x10002F60, 0x10003F1D)`, Rizin reports:

```text
0x10003DAA  fld  dword [ebp+0x98]
             compare health with zero
0x10003DC1  mov  al, byte [ebp+0x460]
             test inactive latch and branch
0x10003DCF  begin per-axis factor recovery
```

Rizin also confirms the known `AfVehicle::ProcessEvent` boundary
`[0x1001B6C0, 0x1001FA6C)` and its repeated reads of the same fields. Its
calling-convention and argument inference is less reliable for this MSVC x86
binary, so Ghidra remains canonical for typed signatures while Rizin is the
independent instruction-level check.

## Reconstruction decision

The portable API now uses `vehicleHealth` and `vehicleInactive` instead of
offset-derived placeholder names. The helper preserves the recovered rule:

```text
if vehicleHealth <= 0 or vehicleInactive:
    factors = (0, 0, 0)
else:
    factors[i] = clamp(factors[i] + refreshDeltaSeconds * 0.25, 0, 1)
                  only when factors[i] < 1
```

This naming result does not activate the native iOS producer. The current
portable simulation owns input intentions and an authenticated frozen spawn,
but it does not yet publish live pose, health, lifecycle, or the 12 ms
scheduler event. The weak mission-runtime endpoint must remain inactive until
one producer can provide the complete, same-step input set without substituting
the separate 60 Hz input-pump clock.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 223
steps and pass all 68 portable tests. Clangd 22.1.8 reports zero errors in the
two changed production and three changed test translation units.

The three reverse-engineering wrapper suites, 12 Rizin report-normalization
tests, public-boundary tests and a 342-file public scan pass. The 227-entry,
14-column function catalogue has unique IDs and module/RVA pairs. `actionlint`,
`git diff --check`, and a local-path scan are clean. Both unsigned iOS variants
remain mandatory publication gates in GitHub Actions.

No proprietary data is required by the portable build or tests.

## Confidence

Confidence is **3/3** for:

- the health meaning and float type of `NfActor +0x98`;
- the active/inactive meaning and byte type of `AfVehicle +0x460`;
- the activate, deactivate, and death-path transitions;
- the live chase-position, world-anchor, quaternion, and scheduler sources;
  and
- the portable `vehicleHealth` and `vehicleInactive` names.

Confidence remains **2/3** for complete live-producer parity. Controlled
runtime traces and a changing reconstructed aircraft simulation are still
required before connecting the endpoint.

## Related material

- [Aircraft flight boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Aircraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Gameplay-camera step coordinator](EXP-20260728-021-camera-step-coordinator.md)
- [Camera refresh time contract](EXP-20260728-023-camera-refresh-time-contract.md)
- [Gameplay-camera mission runtime](EXP-20260728-024-camera-mission-runtime.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
