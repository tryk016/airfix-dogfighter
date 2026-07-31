# EXP-20260730-078: process x87 policy static audit

**Date:** 2026-07-30

**Evidence ID:** `EV-20260730-008`

**Status:** complete game-module static audit; controlled branch-time capture
still required

**Decision:** GO for retaining the explicitly labelled startup-compatible
PC53/nearest-even oracle; NO-GO for claiming live native numeric parity or
implementing the bit-parity `CcRigidBody` kernel

**Reference build:** all SHA-256 identities in
`docs/evidence/source-manifest.sha256`

## Question and boundary

The pitch/bank event and rigid-body reports recovered exact x87 instruction
schedules but left the live precision-control, rounding-control, exception,
and non-finite policy unknown. This audit asks a narrower static question:

> After the executable requests 53-bit x87 precision, can any supplied game
> module visibly replace or reset the floating-point environment before the
> recovered aircraft code runs?

This is not a dynamic observation. It cannot prove what a Windows system DLL,
DirectX runtime, hardware driver, or the process's inherited environment does
on the live gameplay thread.

No executable, plugin, GUI, debugger, or original resource was run. Analysis
used only hash-verified read-only working copies. Generated reports, binaries,
tool databases, and local paths remain outside Git.

## Inputs and method

The complete supplied native-code set is:

- `Dogfighter.exe`;
- `AfEngine.dll`, `Cc.dll`, `UdsPack.dll`, `gtDirect3D.dll`, and
  `gt3DFX.dll`;
- seven `Game/Types/*.type` modules;
- two `Game/Modes/*.mode` modules; and
- the protected historical `Dogfighter.icd`, which the recovered v1.01
  startup path does not execute.

Ghidra 12.1.2 remains the canonical database for the already recovered CRT
entry, pitch/bank branches, and rigid-body functions. Rizin 0.9.1 independently
performed this new module-wide pass:

1. `rz-bin -ij` enumerated imports for every image;
2. `aaa` and `aflj` established the independently discovered function set;
3. `pdfj` decoded each discovered function from its actual entry and CFG;
4. exact instruction mnemonics were checked for `FLDCW`, `FLDENV`, `FRSTOR`,
   `FSTENV/FNSTENV`, `FSAVE/FNSAVE`, `FINIT/FNINIT`, `FCLEX/FNCLEX`,
   `FXRSTOR`, `LDMXCSR`, and `XRSTOR`; and
5. imports were checked for `_controlfp`, `_control87`, `_fpreset`, and the
   standard `fenv` mutation family.

This function-aware pass deliberately replaced a raw byte-pattern search.
For example, bytes at `Dogfighter.exe` VA `0x0041BD00` decode as a false
`FLDCW` only when disassembly starts one byte late; the real instruction begins
at `0x0041BCFF` and is `mov ebx, ecx`.

A new deterministic Ghidra post-script,
`ExportFloatingPointEnvironmentWrites.java`, expresses the same canonical
query. The current managed sandbox prevented a fresh headless execution
because Ghidra could not use its normal per-user cache. Therefore the
module-wide zero-result below is a Rizin result, not falsely presented as a
fresh two-tool whole-image agreement. Existing Ghidra reports still provide
the canonical instruction evidence at every consumer and at the executable
startup helper.

## Complete function-aware result

| Module | Rizin functions | FP-environment instructions | FP-environment imports |
|---|---:|---:|---|
| `AfEngine.dll` | 1,924 | 0 | 0 |
| `AfFX.type` | 110 | 0 | 0 |
| `AirCraft.type` | 34 | 0 | 0 |
| `Cc.dll` | 1,128 | 0 | 0 |
| `Dogfight.mode` | 17 | 0 | 0 |
| `Dogfighter.exe` | 441 | 0 | `_controlfp` only |
| `GroundUnit.type` | 78 | 0 | 0 |
| `gt3DFX.dll` | 87 | 0 | 0 |
| `gtDirect3D.dll` | 39 | 0 | 0 |
| `Interactive.type` | 26 | 0 | 0 |
| `Pickups.type` | 120 | 0 | 0 |
| `Projectiles.type` | 176 | 0 | 0 |
| `Singleplayer.mode` | 10 | 0 | 0 |
| `UdsPack.dll` | 50 | 0 | 0 |
| `WaterUnit.type` | 37 | 0 | 0 |
| runtime-module total | **4,277** | **0** | **1** |
| protected `Dogfighter.icd` | 2 discovered | 0 discovered | 0 |

The protected `.icd` row is not a completeness claim: only two functions are
discoverable in that high-entropy historical image. It is also not part of the
recovered execution path. The 4,277-function runtime-module total covers the
ordinary EXE, its supplied DLLs, and every dynamically discoverable type/mode
plugin.

## Exact executable startup order

Rizin identifies the CRT process entry as
`Dogfighter.exe` VA range `[0x00435BBA,0x00435D0C)`. At call site
`0x00435C3D` it invokes the helper
`[0x00435D5C,0x00435D6E)`, which performs:

```text
push 0x00030000          ; _MCW_PC mask
push 0x00010000          ; _PC_53
call MSVCRT!_controlfp
pop ecx
pop ecx
ret
```

The call is therefore `_controlfp(0x00010000, 0x00030000)`: it selects the
MSVCRT 53-bit precision mode and does not select a rounding mode.

The CRT entry then calls both `_initterm` ranges before calling the game's
WinMain-style function at `0x00435CE9`. The four range endpoints at
`0x00441000`, `0x00441004`, `0x00441008`, and `0x0044100C` are all null, so
the executable contributes no C/C++ initializer callback after `_controlfp`.
Later game startup loads the supplied render, type, and mode modules already
covered by the table above.

## What the static evidence proves

With medium static confidence:

- the executable explicitly requests 53-bit precision before entering the
  game-owned WinMain-style shell;
- it does not explicitly select an RC mode in that call;
- no recognized function in any supplied runtime module contains an
  instruction that loads, restores, initializes, saves-and-resets, or otherwise
  writes the x87 environment;
- no supplied runtime module except the executable imports a known CRT/fenv
  environment mutator; and
- every supplied plugin loaded after process startup is included in that
  import and instruction audit.

This closes the earlier open possibility that `AirCraft.type`, another
supplied type/mode module, or one of the two supplied renderer adapters has an
obvious later writer.

## What remains unproved

Static absence is not a branch-time measurement:

- RC is inherited because the observed `_controlfp` mask changes only PC;
- MSVCRT implementations and imported conversion/math helpers are outside the
  supplied image set and may temporarily manipulate the environment;
- DirectInput, DirectSound/DirectX, COM, Windows, and hardware-driver code is
  outside the supplied image set;
- an external call can change the current thread's environment without leaving
  a game-module import named like `_controlfp`;
- thread identity and any environment inheritance at thread creation remain
  unobserved; and
- exception masks, pending status, NaN payload/trap behavior, and actual CW/SW
  at the pitch/bank and rigid-body instructions remain unknown.

Consequently this audit strengthens PC53 from a two-module startup hypothesis
to a complete supplied-runtime-module result, but it does not elevate
PC53/nearest-even to observed live parity.

## Minimal controlled dynamic capture

The fail-closed normalized contract and synthetic validator for this step are
now documented in
[EXP-20260731-097](EXP-20260731-097-aircraft-runtime-capture-validator.md) and
the [capture runbook](../toolchain/AIRCRAFT-RUNTIME-CAPTURE.md). No real capture
has yet been performed.

The next numeric experiment should use the verified x32dbg environment as an
ordinary user, offline from analysis services, on working copies only.
It must not patch files, inject code, save modified executables, or use
software breakpoints that replace code bytes. Use one or more hardware
execution-breakpoint runs and record:

1. raw x87 CW and SW, MXCSR, and thread ID immediately before
   `Dogfighter.exe+0x00035C3D` and immediately after at
   `Dogfighter.exe+0x00035C42`;
2. the same values at the actual `BANK_SET` `FILD`
   (`AfEngine.dll+0x0001E4E4`), `PITCH_SET` `FILD`
   (`AfEngine.dll+0x0001E505`), and shared comparison
   (`AfEngine.dll+0x0001E514`);
3. the same values at `CcODE::EulerODE`
   (`Cc.dll+0x0002A420`), `CcRigidBody::Derive`
   (`Cc.dll+0x0002A8A0`), `CcQuaternion::Normalize`
   (`Cc.dll+0x0002C2C0`), and `CcRigidBody::PostODE`
   (`Cc.dll+0x0002A890`); and
4. the exact relevant state/input DWORDs before and after each bounded
   operation, without collecting unrelated owner content.

Decode the raw x87 word explicitly: exception masks are bits 0-5, precision
control is bits 8-9, and rounding control is bits 10-11. Repeat enough runs to
cover keyboard, analog, AI, one free-flight rigid-body step, and one
ground/contact step. A stable expected value at startup alone is insufficient;
the consumer-site values and thread identity are the acceptance evidence.

The capture is a GO only if consumer-site policy is stable and the exact
synthetic discriminator outputs agree with the matching PC/RC oracle. A
mismatch selects a newly documented per-phase policy or a deliberately
portable non-bit-parity policy; it must not be rounded into the existing
PC53/nearest-even label.

## Impact on reconstruction

- Keep the current turn/pitch/bank reducers explicitly named
  **startup-compatible PC53/nearest-even**.
- Do not wire those reducers into the live producer/scheduler on the strength
  of this static audit alone.
- Keep `CcRigidBody` at NO-GO for a bit-parity C++20 implementation and runtime
  integration.
- The static game-owned-writer search is complete; the remaining question is a
  small consumer-site dynamic capture, not another speculative source scan.

## Validation

- Rizin 0.9.1 plus `rzpipe` 0.6.2 completed the function-aware scan for all
  16 supplied PE32/i386 images.
- The ordinary runtime module set contains 4,277 independently recognized
  functions; all have zero matching environment-write instructions.
- Import enumeration finds `_controlfp` only in `Dogfighter.exe`.
- The exact startup helper, caller order, empty initializer ranges, false
  `0x0041BD00` byte hit, and consumer addresses were checked against the
  existing canonical Ghidra reports and targeted Rizin listings.
- Both RE wrapper suites, all 12 Rizin exporter tests, synthetic
  public-boundary tests, the 613-file repository scan, the
  324-row/14-column unique function catalogue, changed-document links,
  changed-scope local-path scanning, and `git diff --check` pass. The catalogue
  retains only its two known historical missing references.
- No game, GUI, debugger, installer, or original resource was executed.

## Confidence

| Claim | Confidence | Basis |
|---|---|---|
| `_controlfp(_PC_53, _MCW_PC)` helper and call order | 2/medium | canonical Ghidra consumer/startup evidence plus targeted Rizin function listing |
| supplied runtime modules have no recognized later environment writer/import | 2/medium | complete import inventory and 4,277-function Rizin scan |
| protected `.icd` has no writer | unknown | only two functions are discoverable and it is outside the recovered path |
| live PC at event/rigid-body consumers is 53 | unknown | no branch-time capture |
| live RC is nearest/even | unknown | startup call does not select RC |
| exception, NaN, and trap policy | unknown | no live CW/SW or exception-state capture |
