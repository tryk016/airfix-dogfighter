# Controlled AirCraft/x87 runtime capture

## Status and purpose

The public validator is implemented. No real game capture has been performed
or committed.

The remaining flight-model blocker is no longer another static source scan.
It is a bounded observation of the live x87 control/status words, MXCSR,
thread identity, event stores, rigid-body discriminator vectors, and the
local-input/time-dependent ordering at the recovered consumer sites. The
validator answers whether a normalized capture is internally complete and
consistent. It does not run the game, attach a debugger, or implement the
rigid-body kernel.

## Safety boundary

Perform any later capture only under these rules:

- use a disposable working copy of the original installation;
- run the game and x32dbg as an ordinary user, never as administrator;
- disconnect analysis tools from cloud services and disable automatic symbol,
  plugin, update, crash-report, and external-URL traffic;
- prefer hardware execution breakpoints; do not use software breakpoints that
  replace code bytes;
- do not patch memory, files, imports, or control flow;
- capture only the fields listed here, not arbitrary process memory;
- do not save modified executables or original assets;
- keep raw debugger workspaces, dumps, traces, and normalized capture JSONL
  outside Git and CI.

The `.gitignore` and public-boundary scanner reject `*.dd32`, `*.trace32`,
`*.aircraft-capture.jsonl`, their backups, and all files under local trace or
dump directories.

## Exact supported build

The validator accepts only the three reference modules authenticated by the
public evidence manifest:

- `Dogfighter.exe`;
- `AfEngine.dll`; and
- `Cc.dll`.

The capture header must contain their exact SHA-256 identities. A different
build is rejected rather than guessed. Full paths are neither accepted nor
printed.

## Capture sites

Every sample binds one stable site ID to one exact module and RVA.

| Site ID | Module | RVA | Purpose |
|---|---|---:|---|
| `startup.before_controlfp` | `Dogfighter.exe` | `0x00035C3D` | environment immediately before the startup request |
| `startup.after_controlfp` | `Dogfighter.exe` | `0x00035C42` | environment immediately after the request |
| `loop.input_drain_begin` | `Dogfighter.exe` | `0x00010FA7` | outer-loop local-input boundary |
| `clock.poll_remote` | `AfEngine.dll` | `0x000485AC` | remote-input boundary inside `SetTime` |
| `clock.refresh_dependants` | `AfEngine.dll` | `0x000485E0` | time-dependent dispatch boundary |
| `scheduler.aircraft_refresh` | `AfEngine.dll` | `0x00040392` | observed AirCraft refresh dispatch |
| `loop.before_render` | `Dogfighter.exe` | `0x00010FDC` | post-time, pre-render outer-loop boundary |
| `event.bank.fild` | `AfEngine.dll` | `0x0001E4E4` | BANK signed-payload load |
| `event.pitch.fild` | `AfEngine.dll` | `0x0001E505` | PITCH signed-payload load |
| `event.shared.compare` | `AfEngine.dll` | `0x0001E514` | retained x87 comparison after the field store |
| `rigid.euler.entry` | `Cc.dll` | `0x0002A420` | Euler input state and V1/V2/V6 anchor |
| `rigid.derive.entry` | `Cc.dll` | `0x0002A8A0` | derivative input and V3/V5 anchor |
| `rigid.normalize.entry` | `Cc.dll` | `0x0002C2C0` | quaternion V4 anchor |
| `rigid.postode.entry` | `Cc.dll` | `0x0002A890` | post-Euler phase boundary |

x86 exposes four hardware execution-breakpoint slots. Use them in phases in
one process: capture startup, pause and replace those breakpoints, then capture
event, ordering, and rigid-body phases. Do not merge observations from
different processes merely because their values look similar. The acceptance
profile requires one stable thread identity throughout.

## Normalized JSONL contract

The first line is a header. Every later line is one sample with a consecutive
zero-based `sequence`. Unknown fields, duplicate JSON keys, unknown modules,
wrong RVAs, symbolic paths, invalid word widths, and oversized inputs are
rejected.

A minimal non-accepting observation has this shape; hashes are represented by
placeholders here and must come from the authenticated reference manifest:

```json
{"record":"header","schema":"airfix-aircraft-runtime-capture/v1","profile":"observation","modules":[{"name":"Dogfighter.exe","sha256":"<SHA-256>"},{"name":"AfEngine.dll","sha256":"<SHA-256>"},{"name":"Cc.dll","sha256":"<SHA-256>"}]}
{"record":"sample","sequence":0,"site_id":"startup.after_controlfp","module":"Dogfighter.exe","rva":"0x00035C42","thread_id":1,"x87_control_word":"0x027F","x87_status_word":"0x0000","mxcsr":"0x00001F80"}
```

All rigid-body samples carry exactly 13 DWORD strings in `state_words`, in
this order:

```text
position[3], quaternion(w,x,y,z), linearMomentum[3], angularMomentum[3]
```

Event samples carry `axis`, signed `payload_s32`, and
`observed_store_bits`. Ordering samples carry `iteration`; only
`scheduler.aircraft_refresh` also carries `scheduled_time_ms` in the bounded
non-negative signed-64-bit range.
V1-V6 samples carry `vector_id` and the exact bounded `observed` fields
defined by `EXP-20260730-068`. No owner-object dump is allowed.

Input limits are 4 MiB total, 10,000 samples, and 64 KiB per line. Symlinks,
directories, empty files, invalid UTF-8, embedded NUL, and non-object records
fail closed.

## x87 and MXCSR decoding

The validator decodes the raw words rather than accepting a textual policy:

- x87 exception masks: control-word bits 0-5;
- x87 precision control: bits 8-9 (`PC24`, reserved, `PC53`, `PC64`);
- x87 rounding control: bits 10-11 (nearest/even, down, up, toward zero);
- x87 pending exception flags: status-word bits 0-5;
- MXCSR flags: bits 0-5; and
- MXCSR exception masks: bits 7-12.

The startup-before sample may differ. Every later sample must retain exactly
one x87 control word, one MXCSR control policy, and one thread ID. Sticky
x87 SW and MXCSR exception flags may accumulate and are recorded, but the
MXCSR status bits are excluded from the policy-stability comparison.
Acceptance requires all exception masks; it does not require the inexact flag
to remain clear.

## Profiles and decision

`observation` validates structure, addresses, bounded values, any provided
event result, and any provided oracle vector. Its decision is always
`NO-GO`; missing sites and vectors remain explicit counts in the safe summary.

`acceptance` additionally requires:

- all listed sites, with startup-before/startup-after as the first two samples;
- a stable PC53/nearest-even policy after startup, stable MXCSR control bits,
  and one thread ID;
- all x87/MXCSR exception masks set, while retaining sticky status flags as
  evidence;
- outer-loop observations with zero, one, and multiple due AirCraft refreshes;
- each multi-refresh series advancing by exactly 12 ms;
- exact event-store results for zero, signs, ordinary endpoints, and the
  `+/-3`, `+/-16777217`, and `+/-1555145203` discriminators; and
- exact V1-V6 results, including V5 spill asymmetry and the V6 PC/RC
  discriminator.

The public oracle supports V6 under PC24/PC53/PC64 and all four rounding
modes, and V5 under all three precision modes with nearest/even. V1-V5 under
a directed rounding mode remain unsupported and therefore cannot be promoted
to acceptance by this tool.

## Running the validator

```powershell
python tools/re/validate_aircraft_runtime_capture.py `
  --input <private.aircraft-capture.jsonl> `
  --json-summary
```

Use `--require-go` when the caller must reject a structurally valid partial
observation. Output contains only the schema, profile, GO/NO-GO decision,
sample count, decoded policy, and missing-count totals. It never echoes the
input filename, path, module hashes, payloads, state words, or debugger data.

The synthetic regression suite is public and uses no game data:

```powershell
python tests/AircraftRuntimeCaptureValidatorTests.py
```

## Reconstruction gate

A validator `GO` is necessary but not sufficient for live flight. It permits
a separately reviewed, policy-conditioned `LegacyCcRigidBodyStep` oracle. It
does not authorize a 12 ms scheduler, Q15/input wiring, force-law assumptions,
collision behavior, production pose movement, or bit-parity claims outside
the captured policy and vectors.

Related evidence:

- [native producer ordering](../experiments/EXP-20260730-067-native-pitch-bank-producer-ordering.md);
- [rigid-body integration kernel](../experiments/EXP-20260730-068-rigid-body-integration-kernel.md); and
- [static x87 policy audit](../experiments/EXP-20260730-078-x87-runtime-policy-static-audit.md).
