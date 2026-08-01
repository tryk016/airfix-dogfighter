# EXP-20260801-098: aircraft health-gauge plan

**Status:** implemented from static evidence; backend consumption pending

**Evidence ID:** `EV-20260801-001`
**Decision:** GO for the renderer-neutral armour/health-gauge command plan;
NO-GO for claiming complete AirCraft HUD parity

## Question

Can one complete, independently useful subsection of the 2,081-byte AirCraft
HUD stage be reconstructed without guessing the remaining instruments, weapon
state, mission status, or old DirectX behavior?

## Method

Ghidra 12.1.2 decompiled the function containing VA `0x10006B00` and exported
its complete decoded instruction listing. Targeted memory reads recovered the
four-byte constants and the eight-byte initial-angle constant. The vehicle
constructor and `AfVehicleType::LoadHUDGraphics` establish the instance/type
fields and the two texture roles.

Rizin 0.9.1 plus rzpipe 0.6.2 independently reports RVA `0x00006B00`, file
offset `0x00006B00`, section `.text`, and exact boundary
`[0x10006B00,0x10007321)` with size 2,081. Its 26 direct/IAT call sites agree
with the Ghidra instruction stream, including the maximum-health call and the
screen calls surrounding the mask fan.

Repeatable local commands are:

```powershell
./tools/Invoke-GhidraAnalysis.ps1 `
  -ProgramName 'AirCraft.type' `
  -PostScript 'ExportAddressFunctions.java' `
  -PostScriptArguments @('10006B00') `
  -ReportSuffix 'aircraft-hud-stage'

./tools/Invoke-GhidraAnalysis.ps1 `
  -ProgramName 'AirCraft.type' `
  -PostScript 'ExportFunctionInstructions.java' `
  -PostScriptArguments @('10006B00') `
  -ReportSuffix 'aircraft-hud-stage-instructions'

./tools/Invoke-RizinAnalysis.ps1 `
  -InputFile '<HASH_VERIFIED_WORK_COPY>' `
  -ExpectedSha256 `
    '9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e' `
  -FunctionId 'FN-AIRCRAFT-00006B00' `
  -Rva '0x00006B00'
```

All raw reports, Ghidra databases, Rizin reports, and the verified binary copy
remain ignored local evidence.

## Result

The gauge is a three-stage ordered composition: optional `armour_meter`, an
unconditional opaque-black annular mask fan, then optional `armour`. Both
textures start at `(8, top)`, where screens narrower than 640 use `top=6` and
all others use `screenHeight-136`.

The mask centre is `(72, top+64)`, its radii are 62 and 44, and it starts at
`-pi/4`. Smoothed displayed health `+0x544` and `GetMaxHealth()` produce a
damage sweep from zero at full health to pi at zero health. Native stepping is
binary32 `0.1`; the final residual segment is always drawn. Empty health
produces 32 mask quads, giving a fixed maximum of 34 commands including both
optional textures.

`LegacyAircraftHealthGaugePlan` preserves the native gate precedence, two
camera observations, layer order, constants, quad topology, and fixed
capacity. It rejects invalid inputs atomically, allocates no memory, and
publishes only logical UI coordinates. The caller remains responsible for
authenticated texture binding and mapping to the native-resolution UI output.

## Numeric boundary

The original uses x87 `FSIN`/`FCOS`; the live process precision/rounding state
has not yet been dynamically accepted. Portable trigonometry therefore retains
the recovered inputs and topology but does not claim bit-identical last-bit
coordinates across Windows and ARM64. This is isolated presentation data and
cannot change deterministic simulation.

## Validation

The focused C++ test verifies native gate order, both screen-anchor branches,
full/half/zero health, the degenerate full-health final quad, the 32-quad
maximum, optional texture omission, representative vertices, invalid-value
rejection, bounds-safe lookup, and 1,024 allocation-free builds.

A complete code-intelligence Ninja build passes all 129 CTests and publishes
265 compilation-database entries with the new source present and no Apple-only
source. A fresh MSVC 19.51 HostX64/Ninja product build compiles 705 steps and
passes all 141 CTests, including D3D11 renderer and both product smokes. Public
boundary synthetic tests and the 680-file scan, function-catalog shape,
identity and source validation, changed-source formatting, and `git diff
--check` pass. Hosted iOS builds remain the publication gate.

## Decision

**GO** for the bounded renderer-neutral armour/health-gauge plan and later
common D3D11/Metal consumption.

**NO-GO** for ordinary-frame HUD drawing until authenticated texture ownership,
modern UI mapping, and backend submission are joined. The remaining clock,
digits, weapon, aircraft/team, and mission-status portions of the enclosing
function remain separate reconstruction work.
