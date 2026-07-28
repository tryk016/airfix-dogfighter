# AirCraft engine-audio state and modulation

**Date:** 2026-07-28

**Evidence:** `EV-20260728-013`

**Scenario:** `SCN-FLIGHT-001`, `SCN-AUDIO-001`

**Status:** cadence, phase transitions, sound IDs, command order, and parameter
equations confirmed by Ghidra and independently checked with Rizin

## Question

What is the complete engine-audio subset of AirCraft primary vtable slot 44,
how often does it run, and what portable contract can preserve it without
depending on the original mixer or sound files?

## Sources and safety

The analysis used only the hash-verified `AirCraft.type` working copy recorded
in the source manifest:

| Module | SHA-256 | Image base |
|---|---|---:|
| `AirCraft.type` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` | `0x10000000` |

Ghidra 12.1.2 supplied the canonical MSVC class, vtable, imported sound-method,
dependency-string, and pseudocode context. Rizin 0.9.1 independently supplied
the exact instructions and branches for the constructor, flight-force step,
and slot-44 target.

No original file was modified or executed. A read-only PE section lookup was
used to confirm the five null-terminated dependency strings. No sound, binary,
database, or generated report is committed or required by the portable build.

## State and cadence

The constructor initializes:

```text
f32[A+0x558] = 0
f32[A+0x55C] = 0
f32[A+0x560] = 0
u8 [A+0x564] = 0
u8 [A+0x565] = 0
i32[A+0x5E8] = 0
```

The durable working names are:

```text
A+0x558  smoothedOrientationM01
A+0x55C  engineStartElapsedSeconds
A+0x560  smoothedThrust
A+0x564  engineStartTransitionActive
A+0x565  engineRunning
A+0x5E8  engineAudioCadenceCounter
```

Slot 44 first updates the timer on every invocation:

```text
if engineStartTransitionActive:
    engineStartElapsedSeconds += dt
else:
    engineStartElapsedSeconds = 0
```

It then implements a five-call cadence:

```text
if engineAudioCadenceCounter < 4:
    engineAudioCadenceCounter += 1
    skip engine-audio update
else:
    engineAudioCadenceCounter = 0
    run engine-audio update
```

Rizin places the timer branch at `0x100035B4..0x10003620`, the signed counter
comparison at `0x10003626..0x1000362F`, the skipped-path increment at
`0x100039BD..0x100039BE`, and the completed-path jump over that increment at
`0x100039BB`. Starting from the constructor's zero, calls one through four
increment the counter and call five performs the update and resets it.

The timer is therefore accumulated at the scheduler cadence, but phase changes
are observed only on an audio-update call. A four-second crossing on a skipped
call waits until the next fifth-call boundary.

## Sound IDs and phase transitions

The type constructor creates five dependencies whose read-only PE strings are
the engine-on, engine-idle, engine-start, engine-stop, and engine-turn WAV
roles. Their observed consumers establish this sound-ID map:

| Sound ID | Role | Evidence in slot 44 |
|---:|---|---|
| `0x1A` | engine-on layer | started after completed startup; load volume/pitch |
| `0x1B` | engine-idle layer | started after completed startup; complementary volume |
| `0x1C` | engine-turn layer | started after completed startup; orientation-modulated volume |
| `0x1D` | engine-start | played on startup and stopped on completion/shutdown |
| `0x1E` | engine-stop | played on shutdown |

The imported `NfActor` vtable methods identify `+0x80` as `PlaySound`, `+0x84`
as `StopSound`, `+0x8C` as `UpdateSounds`, and `+0x90` as `SetSoundParam`.

The strict transition contract is:

```text
if smoothedThrust < 0.001 and (starting or running):
    stop 0x1D, 0x1C, 0x1A, 0x1B
    play 0x1E
    set model parameter 1 to 0
    smoothedThrust = 0
    running = false
    starting = false

if smoothedThrust > 0.001 and !starting and !running:
    play 0x1D
    set model parameter 1 to smoothedThrust
    starting = true
    engineStartElapsedSeconds = 0

if starting and !running and engineStartElapsedSeconds > 4:
    stop 0x1D
    play 0x1B, 0x1A, 0x1C
    set model parameter 1 to smoothedThrust
    running = true
    starting = false
```

Exactly `0.001f` neither starts nor stops the engine. Exactly `4.0f` does not
complete startup. Rizin confirms shutdown at `0x10003635..0x100036B5`, startup
at `0x100036BC..0x10003708`, and completion at
`0x1000370E..0x10003781`.

Shutdown does not immediately clear `+0x55C`; because it clears `starting`,
the next slot-44 invocation takes the timer-zero branch. The portable contract
retains this otherwise short-lived ordering detail.

## Load, pitch, and volume

The flight-force step builds orientation matrix `M` from the rigid-body
quaternion. While `onGround == 0`, it updates:

```text
A[0x558] += 0.05 * (M.m01 - A[0x558])
```

This proves `+0x558` is a low-pass orientation-matrix component. Slot 44 is
its confirmed consumer: the absolute value modulates engine-turn volume.

Let:

```text
T = smoothedThrust
S = length(current velocity)
R = smoothedOrientationM01
L = (0.2*T + 0.3)*S + T
V = min(L, 1)
P = 0.2*L + 1
```

Slot 44 emits these ordered parameter calls:

```text
SetSoundParam(0x1A, 1, P)
SetSoundParam(0x1A, 0, V)
SetSoundParam(0x1C, 1, P)
SetSoundParam(0x1C, 0, abs(R)*V)
SetSoundParam(0x1B, 1, 0.5*L + 1)
SetSoundParam(0x1B, 0, max(1 - V, 0))
UpdateSounds(dt)
set model parameter 1 to T
```

The running volume has an upper clamp only. No lower clamp is present before
the engine-on and engine-turn parameter calls. Rizin confirms the load and
upper clamp at `0x1000385B..0x100038C9`, engine-on calls at
`0x100038C9..0x100038FE`, engine-turn calls at
`0x10003904..0x1000392C`, idle calls at `0x10003932..0x10003991`, and the
final updates at `0x10003997..0x100039B8`.

Slot 44 has a separate sound-`0x20` state between phase transitions and common
engine modulation. The follow-up
[EXP-20260728-029](EXP-20260728-029-aircraft-destroyed-dive-audio.md) resolves
it as the health-gated `enginedive` sample and documents byte `+0x566`.
Keeping that state in a separate helper does not change the relative order of
the five confirmed engine roles.

## Tool comparison

| Property | Ghidra 12.1.2 | Rizin 0.9.1 | Decision |
|---|---|---|---|
| Slot-44 boundary and ABI | Vtable and MSVC class context | Exact `[0x10002F60, 0x10003F1D)` | Ghidra canonical |
| Sound methods and dependencies | Exported symbols and strings | Numeric call sites and IDs | combined |
| Cadence and phase branches | Readable structured pseudocode | Exact compare/jump order | agree |
| Parameter equations | Typed float data flow | Exact x87 instruction sequence | agree |
| `+0x558` producer | Quaternion-matrix context | Exact read/write addresses | agree |

Ghidra remains more reliable for calling convention and symbolic sound API
roles. Rizin materially strengthens the exact counter, branch, command, and
floating-point-operation ordering without sharing Ghidra's database.

## Reconstruction decision

The portable C++20 layer now provides one pure, allocation-free transition:

- caller-owned engine phase/timer/cadence state;
- caller-supplied `dt`, smoothed thrust, speed magnitude, and smoothed
  orientation component;
- returned smoothed thrust, next state, and at most 14 ordered commands; and
- explicit play, stop, parameter, mixer-update, and model-parameter operations.

The helper contains no mixer, file path, decoded audio, global state, or
platform API. Non-finite values and arithmetic overflow fail closed when the
native branch would consume them. Inputs behind a skipped cadence remain
uninspected except that `dt` is consumed whenever the active start timer is
advancing. A negative caller-supplied speed magnitude is rejected because it
cannot be produced by the native length calculation.

The helper remains unwired from `PlayerAircraftSimulation`: that frozen intent
transport does not own the 12 ms aircraft scheduler, rigid-body velocity,
orientation, smoothed thrust, or a future iOS audio backend.

## Validation

Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all 232
steps and pass all 71 portable tests. Clangd 22.1.8 reports zero errors in the
new production and test translation units.

The Ghidra, working-copy, and Rizin wrapper suites pass, as do all 12 Rizin
report-normalization tests. Public-boundary tests and the 354-file public scan
pass. The 228-entry, 14-column function catalogue retains unique IDs and
module/RVA pairs. `actionlint`, the local-path scan, and `git diff --check` are
clean. No proprietary data is required by the build or tests; generated RE
reports remain ignored.

Unsigned `iphoneos` and `iphonesimulator` builds remain mandatory publication
gates in GitHub Actions.

## Confidence

Confidence is **3/3** for:

- all field widths, initial values, cadence behavior, and strict thresholds;
- phase-transition command order and five sound-ID roles;
- load, pitch, volume, and model-parameter equations;
- `+0x558` as the low-pass orientation `m01` producer/consumer join; and
- the portable helper matching the confirmed finite-input engine-only subset.

Confidence is **2/3** for the descriptive member and command names because
original member labels are unavailable, and for cross-architecture
floating-point parity until x87-versus-ARM traces establish tolerances.

## Related material

- [Aircraft flight boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Aircraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
- [Engine-start and thrust-integrity state](EXP-20260728-027-aircraft-thrust-integrity.md)
