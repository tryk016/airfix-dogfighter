# AirCraft engine-start and thrust-integrity state

**Date:** 2026-07-28

**Evidence:** `EV-20260728-012`

**Scenario:** `SCN-FLIGHT-001`, `SCN-COLLISION-001`, `SCN-AUDIO-001`

**Status:** field lifecycles, gates, constants, and scheduler order confirmed by
Ghidra and independently checked with Rizin

## Question

What do the remaining AirCraft fields at `+0x564` and `+0x568` represent, who
writes them, and how do they affect the recovered flight-force step?

## Sources and safety

The analysis used only the hash-verified `AirCraft.type` working copy recorded
in the source manifest:

| Module | SHA-256 | Image base |
|---|---|---:|
| `AirCraft.type` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | `0x10000000` |

Ghidra 12.1.2 supplied the canonical MSVC context, vtable targets, imported
sound-method symbols, cross-references, and pseudocode. Rizin 0.9.1
independently exported normalized instructions for the constructor, force,
collision, and post-collision refresh functions.

No original file was modified or executed. No binary, database, or raw report
was uploaded. Working copies and generated Ghidra/Rizin reports remain in the
ignored local artifact tree.

## Constructor state

The AirCraft instance constructor establishes:

```text
f32[A+0x55C] = 0
f32[A+0x560] = 0
u8 [A+0x564] = 0
u8 [A+0x565] = 0
f32[A+0x568] = 1
```

Rizin places the writes at `0x100021E6..0x10002207`, including exact
`0x3F800000` initialization for `+0x568`.

## `+0x564`: engine-start transition

Primary vtable slot 44, the working `AircraftPostCollisionRefresh`, owns an
engine-sound state machine. Exported `NfActor` symbols identify vtable offsets
`+0x80` and `+0x84` as play/stop-sound operations. The observed sound IDs and
field transitions establish:

```text
if smoothedThrust > 0.001 and !starting and !running:
    play sound 0x1D
    starting = true
    engineStartElapsedSeconds = 0

if starting:
    engineStartElapsedSeconds += dt

if starting and !running and engineStartElapsedSeconds > 4:
    stop sound 0x1D
    play sounds 0x1B, 0x1A, and 0x1C
    running = true
    starting = false

if smoothedThrust < 0.001 and (starting or running):
    stop sounds 0x1D, 0x1C, 0x1A, and 0x1B
    play sound 0x1E
    smoothedThrust = 0
    starting = false
    running = false
```

Here:

```text
engineStartElapsedSeconds = f32[A+0x55C]
smoothedThrust            = f32[A+0x560]
starting                  = u8[A+0x564]
running                   = u8[A+0x565]
```

The `0.001f` and `4.0f` comparisons are strict at the transition points shown.
Rizin confirms the `starting` set at `0x10003701`, timer reset at
`0x10003708`, completed-start transition at `0x10003722..0x10003781`, and
shutdown clears at `0x100036A8..0x100036B5`.

The force step independently consumes `starting`:

```text
x = smoothedThrust
t = targetThrust

if !starting:
    x += (x + 0.3f) * (t - x) * 0.02f
else:
    x += (t - x) * 0.0004f
```

Rizin matches the complete sequence at `0x10004396..0x100043CE`. The native
function applies no final clamp to `x`. The durable working member name is
`engineStartTransitionActive`; it describes proven behavior and is not a
recovered original label.

## `+0x568`: thrust-integrity factor

The live-health thrust path in the force step is:

```text
Q = 2 * f32[A+0x568] * smoothedThrust * f32[T+0x78] * B
```

Rizin confirms the `+0x568` read at `0x100045D1`, multiplication by
`smoothedThrust` at `0x100045DD`, and the later type/instance factors.

Primary slot 30, `AircraftStaticCollisionStep`, maintains two locals across
its collision loop:

```text
qualifiedCount = 0
fourthPowerSum = 0

normalVelocityDot = dot(collisionNormal, currentVelocity)

if normalVelocityDot > 0.9f and
   normalVelocityDot * collisionScalar > 2.0f:
    qualifiedCount += 1
    fourthPowerSum += normalVelocityDot^4
```

The comparisons are strict. Rizin places them at
`0x10008091..0x100080B5`, followed by the three multiplications and spilled
binary32 sum at `0x100080B7..0x100080D6`.

After the loop:

```text
if qualifiedCount != 0:
    f32[A+0x568] -=
        (fourthPowerSum / qualifiedCount) * 0.5f
```

The decrement is at `0x100081D0..0x100081FE`. This function does not clamp the
result.

Later in the same vehicle refresh, slot 44 performs:

```text
if factor < 1:
    factor += rand() * dt * 0x1.0002p-15f

if factor > 1:
    factor = 1
else if factor < 0:
    factor = 0
```

The exact recovery-scale bits are `0x38000100`
(`0.000030518509...f`, approximately `1/32767`). Rizin confirms the imported
random call and recovery at `0x10003D36..0x10003D66`, then the ordered clamp at
`0x10003D6C..0x10003D9F`.

The scheduler order is force step, collision, then slot-44 refresh. Collision
damage therefore changes the factor after the current force was prepared; the
degraded and slightly recovered/clamped value affects the next force step.
The sleeping vehicle branch skips force and collision but still invokes slot
44, so factor recovery continues while vehicle physics is asleep.

The durable working name is `thrustIntegrityFactor`. It is narrower and better
supported than “engine health”: actor health is a separate exported field at
`NfActor +0x98`, while `+0x568` scales only the live-health thrust equation in
the recovered consumers.

## Tool comparison

| Property | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| Constructor boundary and writes | Typed vtable context | Exact `[0x10001EA0, 0x10002288)` and writes | agree |
| Force-step boundary | Vtable target and MSVC context | Exact `[0x10003F40, 0x100065A3)` after guarded creation | agree |
| Collision boundary | Vtable target and collided-list context | Exact `[0x10007920, 0x1000851D)` after guarded creation | agree |
| Slot-44 boundary | Vtable target and sound symbols | Exact `[0x10002F60, 0x10003F1D)` | agree |
| Field widths/constants | Strong typed pseudocode and scalar export | Exact byte/DWORD instructions and data references | agree |
| Calling convention/semantic types | Most reliable | Generic cdecl/zero-argument guesses on manual functions | Ghidra canonical |

Ghidra remains the most reliable result because it retains the MSVC symbol and
class context. Rizin materially strengthens confidence in boundaries, exact
branches, constants, and field accesses without sharing Ghidra's analysis
database.

## Reconstruction decision

The portable C++20 layer now contains three isolated, allocation-free helpers:

- smoothed-thrust advancement with the exact `starting` branch;
- strict collision qualification and averaged fourth-power degradation; and
- random-sample recovery followed by the native ordered clamp.

They fail closed on non-finite input reached by the corresponding native branch
and on arithmetic overflow instead of propagating invalid floating-point
state. Inputs behind rejected/skipped gates remain uninspected. The recovery
helper accepts a caller-supplied integer random sample, so the simulation does
not import a platform-dependent global PRNG. A factor at or above one ignores
`dt` and the sample, matching the native skipped branch.

The helpers remain separate from `PlayerAircraftSimulation`: that intentionally
frozen intent transport does not yet own the native rigid body, collision
samples, sound state, 12 ms scheduler, or deterministic replacement PRNG.
Portable fourth-power and recurrence tests use explicit tolerances because the
required x87-versus-ARM floating-point parity policy remains open.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 229
steps and pass all 70 portable tests. Clangd 22.1.8 reports zero errors in the
new production and test translation units.

The Ghidra, working-copy, and Rizin wrapper suites pass, as do all 12 Rizin
report-normalization tests. Public-boundary tests and the 350-file public scan
pass. The 228-entry, 14-column function catalogue retains unique IDs and
module/RVA pairs. `actionlint`, the local-path scan, and `git diff --check` are
clean. No proprietary data is required by the build or tests; generated RE
reports remain ignored.

Unsigned `iphoneos` and `iphonesimulator` builds remain mandatory publication
gates in GitHub Actions.

## Confidence

Confidence is **3/3** for:

- constructor widths and initial values;
- `+0x564` being active only during the engine-start transition;
- both smoothing branches and their exact constants;
- `+0x568` scaling live-health thrust;
- collision qualification, fourth-power averaging, and `0.5f` degradation;
- random recovery, ordered clamp, and scheduler order.

Confidence is **2/3** for the descriptive member names, because original member
labels are not exported, and for cross-architecture numeric parity until
controlled x87 traces establish tolerances and PRNG sequencing.

## Related material

- [Aircraft flight boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Aircraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
- [Static tool cross-check](EXP-20260727-001-static-tool-crosscheck.md)
- [Confirmed aircraft scheduler boundary](../re/MODULE-MAP.md#confirmed-aircraft-boundary)
- [Vehicle rest/sleep gate](EXP-20260728-026-vehicle-rest-sleep-gate.md)
