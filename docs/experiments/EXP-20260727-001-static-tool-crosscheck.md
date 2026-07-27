# EXP-20260727-001 — representative static-tool cross-check

**Question:** Do Ghidra 12.1.2 and Rizin 0.9.1 independently agree on the
function boundaries and material behavior of one rendering, one input, and one
aircraft-physics function, and what remains for Binary Ninja Free to verify?
**Source build:** Airfix Dogfighter v1.01 modules identified by the public
SHA-256 manifest
**Working copies:** hash-identical explicit copies below the ignored
`analysis/work-copies` boundary
**Environment:** ordinary-user, local/offline static analysis; no execution,
debugging, write mode, patching, symbol download, or cloud service
**Tools:** Ghidra 12.1.2; Rizin 0.9.1 commit
`c3a90e9226d977f58f4e9c75f78fa6b07afe13c7`; `rzpipe` 0.6.2; Cutter 2.5.0
bundled `rz-ghidra` pseudocode; Binary Ninja Free 5.3 R2 installed with
offline configuration and manual comparison pending
**Evidence:** `EV-20260727-002`
**Related functions:** `FN-CC-0001FDD0`,
`FN-DOGFIGHTER-00011BB0`, `FN-AIRCRAFT-00003F40`

## Prediction

Raw boundaries, return cleanup, direct call sites, and literal global addresses
should agree. Ghidra should retain better recovered MSVC symbols and types.
Rizin should independently expose omissions or overly confident names. Static
agreement must not raise the aircraft force law from confidence 2 to 3.

## Reproducible procedure

1. Prepare only `Cc.dll`, `Dogfighter.exe`, and
   `Game/Types/AirCraft.type` through
   `tools/Prepare-ReWorkingCopy.ps1`; require the public manifest hashes before
   and after copying.
2. Reuse the locked local Ghidra project by literal program name. Export both
   `ExportAddressFunctions.java` and `ExportFunctionInstructions.java` for the
   exact requested VA.
3. Run `tools/Invoke-RizinAnalysis.ps1` on the corresponding verified copy and
   RVA. Use Cutter's verified local Sleigh directory only for optional
   `rz-ghidra` pseudocode. Do not import Ghidra names or types into the Rizin
   analysis.
4. Preserve Rizin's automatic function-discovery result. If a target is not
   found, record the miss before explicitly creating a function at the known
   requested RVA for a bounded comparison.
5. After confirming Binary Ninja Free's network and crash-report settings are
   disabled, open the same copies locally and fill the pending manual column
   without importing either earlier database.

Raw Ghidra and Rizin reports remain ignored. The commands, normalized schema,
hash identities, and concise conclusions are reproducible through the committed
scripts.

## Input identity

| Function | Module SHA-256 | Image base | Requested RVA / VA |
|---|---|---:|---:|
| `FN-CC-0001FDD0` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | `0x10000000` | `0x0001FDD0` / `0x1001FDD0` |
| `FN-DOGFIGHTER-00011BB0` | `F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89` | `0x00400000` | `0x00011BB0` / `0x00411BB0` |
| `FN-AIRCRAFT-00003F40` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | `0x10000000` | `0x00003F40` / `0x10003F40` |

## Rendering — `FN-CC-0001FDD0`

### Comparison

| Field | Ghidra 12.1.2 | Rizin 0.9.1 / `rz-ghidra` | Binary Ninja Free |
|---|---|---|---|
| Automatic discovery | Export symbol and complete function found | Complete function found automatically | Pending owner acceptance |
| Boundary | `[0x1001FDD0, 0x10020AFA)`, final `RET 0xC` | Exact same range, size `0xD2A`; `.text`, file offset `0x0001FDD0`; 838 instructions | Pending |
| Signature / ABI | `void __thiscall CcObject::AddVisiblePolygonsToRenderList(CcCamera*, CcRenderList*, bool)` | Export text preserves the `__thiscall` declaration, but Rizin proposes `cdecl` and three explicit stack arguments; this is contradicted by ECX receiver use and `RET 0xC` | Pending |
| Direct calls | `operator new(0xC)`, `CcLight::GetCameraRelation` twice, `_CIacos`, `fsin`, `ftol`, and local allocation/construction helpers at RVAs `0x3980`/`0x39D0` | Seven direct call sites at the same raw addresses; several import names are not recovered | Pending |
| Global/data access | Ghidra names references at RVAs `0x4A0D0`, `0x4A0D4`, `0x4A0F4`, `0x4A23C`, and `0x4A314` | The current automated data-reference set is empty even though the instructions/pseudocode contain the literal accesses | Pending |
| Pseudocode summary | Transforms the linked mesh vertices, walks polygons, applies the recovered reverse-cross facing test, optionally computes lighting/lens-flare contributions, then queues visible polygon/material records | Same broad loop, facing, light, flare, allocation, and queue behavior; local-variable recovery is substantially noisier and the receiver type is lost | Pending |

### Resolution

The boundary and direct raw calls independently agree. Ghidra is the most
reliable result for this function because the PE export, ECX use, stack cleanup,
recovered class types, and named globals are internally consistent. Rizin is a
valuable boundary/call corroboration and exposes its own ABI/data-reference
limitations; its `cdecl` signature is rejected.

No behavior change is promoted from this cross-check. The existing reverse-cross
and draw-contract conclusions remain unchanged.

## Input — `FN-DOGFIGHTER-00011BB0`

### Comparison

| Field | Ghidra 12.1.2 | Rizin 0.9.1 / `rz-ghidra` | Binary Ninja Free |
|---|---|---|---|
| Automatic discovery | Complete function found | Complete function found automatically | Pending owner acceptance |
| Boundary | `[0x00411BB0, 0x0041221A)`, final `RET 0x4` | Exact same range, size `0x66A`; `.text`, file offset `0x00011BB0`; 510 instructions | Pending |
| Signature / ABI | `void __thiscall (dispatcher*, input-source*)`; Ghidra initially types the explicit parameter as `int*` | Proposes `cdecl` with two explicit arguments; ECX receiver use and `RET 0x4` instead support the Ghidra `__thiscall` shape | Pending |
| Direct calls | 37 direct calls covering input iteration, active-window/console dispatch, command routes, event construction/processing/destruction, and local helpers | The same 37 raw direct call sites are listed; most imported/local names remain unresolved | Pending |
| Global/data access | Reads/writes raw analog-history globals at `0x00445948`, `0x0044594C`, and `0x00445950` | Independently lists both read and write sites for all three addresses, plus eight dispatcher/jump/import data references | Pending |
| Pseudocode summary | Iterates input events, routes console/window/key/button cases, then handles bank, pitch, and thrust analog cases with the documented dead zones, change thresholds, curves, and event IDs | Preserves the same event loop and switch, including the three analog-history globals and constants; types/names are less reliable | Pending |

### Resolution

The old working name `DispatchAnalogFlightInput` was too narrow. Analog flight
mapping is only cases `0x0C`, `0x0E`, and `0x10` inside a broader input-event
dispatcher. The durable working name becomes `DispatchInputEvents`; the stable
ID does not change.

Ghidra is most reliable for the ABI and behavioral structure. Rizin is equally
strong on the raw boundary and exact analog global sites, and its independent
result supports the rename. Existing analog curve claims remain unchanged.

## Aircraft physics — `FN-AIRCRAFT-00003F40`

### Comparison

| Field | Ghidra 12.1.2 | Rizin 0.9.1 / `rz-ghidra` | Binary Ninja Free |
|---|---|---|---|
| Automatic discovery | Function recovered at the exact vtable target | `aaa` did **not** create a function at the requested RVA; explicit read-only `af @ 0x10003F40` is required for the bounded comparison | Pending owner acceptance |
| Boundary | `[0x10003F40, 0x100065A3)`, final `RET 0x4` | After the explicit `af`, exact same range, size `0x2663`; `.text`, file offset `0x00003F40`; 2,227 instructions | Pending |
| Signature / ABI | `void __thiscall AircraftFlightForceStep(AirCraft*, float dt)`; Ghidra's raw type remains the base `AfVehicle*` | Proposes `cdecl` with 19 arguments derived incorrectly from receiver-relative field offsets; ECX use, the single stack `float`, and `RET 0x4` contradict it | Pending |
| Direct calls | Local vector helpers; 15 `ApplyForce`, five `ApplyTorqueOnly`, one `ApplyForceOnly`; relation, BSP collision, room/mesh, height/water, notification, and vtable BSP enable/disable paths | Lists 63 raw direct call sites including three calls to RVA `0x3F20`, 29 calls to RVA `0x65B0`, import thunks, and other helpers; without the Ghidra import/type recovery it does not classify the 15/5/1 force-family split | Pending |
| Global/data access | Direct coefficient table references at RVAs `0xD490`, `0xD4A0`, `0xD4AC`, `0xD5F0`, `0xD5FC`, `0xD600`, and `0xD610–0xD664` | Emits only five explicit data references, principally import-table slots; the coefficient loads are visible in instructions/pseudocode but are not normalized as data references | Pending |
| Pseudocode summary | Entry gate; basis/velocity transforms; throttle smoothing; ordered force/torque accumulation; cadence-gated BSP probes; height/water branches; final damping | Recovers the same broad branches, transforms, repeated force-like calls, probes, and damping after explicit creation, but receiver fields become false arguments and callees/constants remain mostly unnamed | Pending |

### Resolution

Rizin's automatic miss is itself a useful independent result: the aircraft
vtable-target recovery performed in the Ghidra project is necessary and must
remain reproducible. The guarded explicit creation then independently matches
the exact bytes and end boundary, but Ghidra is substantially more reliable for
the ABI, call roles, globals, and readable behavior. This result cannot be used
to raise confidence.

## Cross-function conclusions

- Ghidra currently produces the most reliable complete result. Its strength is
  not visual pseudocode quality alone: it combines recovered exports/vtables,
  exact instructions, MSVC stack cleanup, renamed imports, types, and the
  established project evidence graph.
- Rizin independently matches both automatically discovered boundaries exactly,
  matches direct calls, and is particularly useful for deterministic global
  sites in the input dispatcher. It also reveals real weaknesses: a wrong
  `cdecl` proposal, missing render globals, and no automatic aircraft-function
  discovery.
- Cutter's `rz-ghidra` output is useful for manual navigation but shares the
  Rizin analysis database and is not counted as an independent third tool.
- Binary Ninja Free remains the required manual independent decompiler check.
  No claim below can say "three tools agree" until that local review is
  completed.
- No static-only agreement changes confidence 3 policy. In particular,
  `FN-AIRCRAFT-00003F40` remains confidence 2 pending runtime/parity evidence.

## Integrity checks

- All three source/copy SHA-256 pairs match the public manifest.
- The copy script hashes the source before and after the copy and never opens a
  destination for overwrite.
- Normalized Rizin JSON contains module hashes and tool versions but no input,
  tool, repository, user-profile, or report path.
- Original files were not executed, debugged, patched, or modified.
- Binary Ninja Cloud and all other upload services were not used.
- Portable unit tests and the public-boundary scanner use only synthetic data.

## Pending manual setup and analysis

The owner accepted the Binary Ninja Free license, confirmed the official
installer hash, and installed build `5.3.9757`; the installed executable has a
valid Vector 35 signature. The remaining step before opening a working copy is
to confirm that network and crash-report settings are disabled and restart the
application. This does not block Ghidra/Rizin automation, documentation,
portable builds, or CI.
