# AirCraft destroyed-dive audio state

**Date:** 2026-07-28

**Evidence:** `EV-20260728-014`

**Scenario:** `SCN-FLIGHT-001`, `SCN-AUDIO-001`

**Status:** sample identity, state transitions, command order, and volume
equation confirmed by Ghidra and independently checked with Rizin

## Question

What does sound ID `0x20` represent in AirCraft primary vtable slot 44, which
state controls it, and what portable contract can preserve the behavior
without depending on an original sound file or mixer?

## Sources and safety

The pass used only hash-verified working copies recorded in the source
manifest:

| Source | SHA-256 | Role |
|---|---|---|
| `Game/Types/AirCraft.type` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | executable control flow and sample registration |
| `Resource.up` | `11F08660E42B5D7EFAF91AEDA60E9F9FD0355A3D9BF56276E63154E461E86557` | read-only logical archive listing |

Ghidra 12.1.2 supplied the canonical MSVC vtable, imported actor/sound method,
and pseudocode context. Rizin 0.9.1 independently supplied exact instruction
boundaries, branches, field accesses, and x87 operations. The local UDSP
listing tool inspected only archive metadata and established that the logical
entry is `Sound\Ingame\Models\Ac\enginedive.wav`.

No original file was modified or executed. No sound, binary, database, local
path, or generated analysis report is committed or required by the portable
build.

## Sample identity

The AirCraft type vtable routes its load entry to
`[0x100019C0, 0x10001BD7)`. Ghidra automatic analysis did not initially create
that exact function, so its vtable entry and the independently bounded Rizin
report were used as the guarded function-entry evidence.

The function first calls the inherited `AfVehicleType::Load`, creates the
sample bank/search paths, and registers the aircraft samples. At
`0x10001B6C..0x10001BA0` it constructs an `AfActorSample` whose:

```text
sound ID = 0x20
name     = "enginedive"
```

The separate read-only archive listing contains the matching
`enginedive.wav` entry. Together, the code string, ID registration, and archive
name establish the behavior-backed role `engineDive`; the portable code does
not load or redistribute that file.

## Slot-44 state and branches

The complete block is `0x10003788..0x1000384E`, between the engine phase
transitions and common engine modulation. It is reached only when slot 44's
shared five-call audio cadence expires.

Let:

```text
health     = f32[A+0x98]
diveActive = u8[A+0x566]
velocityY  = GetSpeed().y
```

The exported `NfActor::GetHealth` joins `health` to the exact field. The native
state machine is:

```text
if health <= 0:
    if !diveActive:
        PlaySound(0x20)
        diveActive = true

    volume = clamp(-0.15 * velocityY - 0.2, 0, 1)
    SetSoundParam(0x20, 1, 1)
    SetSoundParam(0x20, 0, volume)
else:
    if diveActive:
        StopSound(0x20)
        diveActive = false
```

Health zero follows the destroyed branch. The live branch performs no velocity
query. In the destroyed branch, playback starts before either parameter call;
retained updates omit playback but repeat both parameter calls. Recovery to
positive health emits only the stop command.

The scalar constants have exact binary32 encodings:

| Meaning | Value | Bits |
|---|---:|---:|
| vertical-velocity scale | `-0.15f` | `0xBE19999A` |
| volume offset | `0.2f` | `0x3E4CCCCD` |

The descending-speed examples are therefore:

```text
velocityY = -4  -> volume = 0.4
velocityY = -8  -> volume = 1.0
velocityY >= -4/3 -> volume = 0
```

The last threshold is descriptive only; the implementation retains the
recovered multiply, subtract, and ordered clamp instead of replacing them with
an algebraically folded comparison.

## Original initialization defect

The AirCraft instance constructor at `0x10001EA0` explicitly writes:

```text
u8 [A+0x564] = 0
u8 [A+0x565] = 0
f32[A+0x568] = 1
```

It does not write byte `+0x566`. The allocation path uses the ordinary MSVCRT
operator `new`, which does not guarantee zero-filled storage. The first native
read of `diveActive` is therefore indeterminate.

For a normally live aircraft, slot 44 eventually normalizes a stale nonzero
byte by issuing a harmless stop and clearing it. Preserving an uninitialized
byte would be undefined behavior in portable C++ and would not provide useful
parity. `LegacyAircraftDestroyedDiveAudioState` deliberately defaults
`soundActive` to `false`; this is a documented deterministic correctness
deviation from the original constructor.

## Tool comparison

| Property | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| Slot-44 ABI and sound methods | typed MSVC/vtable and imported API context | exact numeric call sites | Ghidra canonical |
| Dive block boundary | structured branch context | exact `0x10003788..0x1000384E` range | agree |
| State and health fields | actor-health symbol plus decompilation | exact byte/float accesses | agree |
| Volume equation | typed float pseudocode and scalar export | exact x87 multiply/subtract/clamp order | agree |
| Type-load sample registration | vtable identifies the entry, automatic function creation incomplete | exact `[0x100019C0, 0x10001BD7)` after guarded creation | combined |
| Sample file identity | embedded `enginedive` registration string | same immediate/string references | archive listing closes the join |

Ghidra remains the strongest source for ABI and imported method meaning. Rizin
is strongest for the missed loader boundary and instruction-order cross-check.
Neither alone proves the archive filename; the independent UDSP metadata
listing supplies that final join.

## Reconstruction decision

The portable C++20 layer now exposes a pure, allocation-free transition with:

- caller-owned active state;
- caller-supplied health and vertical velocity;
- exact sound ID `0x20`, command ordering, constants, and clamp;
- at most three commands in fixed storage; and
- explicit rejection of non-finite values only when their branch consumes
  them.

Positive health does not inspect vertical velocity, matching the native branch
dependency. Non-finite health is rejected instead of reproducing x87 unordered
comparison behavior. The helper contains no decoded audio, asset path, mixer,
global state, allocation, or platform API.

The helper remains unwired. Its eventual caller must run it only on the shared
fifth-call audio pass and splice its commands after engine phase transitions
but before the common engine parameter stream. The current engine-only helper
intentionally does not claim ownership of that larger scheduler or audio
backend.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 235
steps and pass all 72 portable tests. Clangd 22.1.8 reports zero errors in the
new production and test translation units.

The Ghidra, working-copy, and Rizin wrapper suites pass, as do all 12 Rizin
report-normalization tests. Public-boundary tests and the 358-file public scan
pass. The 229-entry, 14-column function catalogue retains unique IDs and
module/RVA pairs. `actionlint`, the local-path scan, and `git diff --check` are
clean. GitHub Actions remains a mandatory publication gate.

No proprietary data is required by the build or tests; generated analysis
reports remain ignored.

## Confidence

Confidence is **3/3** for:

- sound ID `0x20` registration as `enginedive`;
- health and active-state branches;
- command order, scalar bits, volume equation, and ordered clamp;
- absence of the `+0x566` constructor write; and
- the portable finite-input transition plus deliberate false initialization.

Confidence is **2/3** for descriptive member names and exact cross-architecture
x87-versus-ARM rounding until controlled runtime traces establish tolerances.

## Related material

- [Engine-audio state](EXP-20260728-028-aircraft-engine-audio-state.md)
- [Aircraft flight boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Aircraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
