# EXP-20260731-079: AFS function activation and execution order

**Date:** 2026-07-31

**Evidence:** `EV-20260731-001`

**Status:** static compiler-to-process chain recovered and represented by an
isolated C++20 schedule oracle; no AFS bytecode VM or live mission wiring

**Decision:** GO for classifying `event` as explicit-call and `action`/`timer`
as Autoexec, and GO for constructing one process's initial Autoexec queue in
reverse function-declaration order. NO-GO for executing bytecode, selecting
between mission `Load`/`Start` lifecycle paths, or wiring outcomes to a live
dispatcher.

## Question

What does compiled-function byte `+0x20` mean, which AFS declarations set it,
and in what order does one newly created process execute its automatically
started functions?

## Sources and safety boundary

- `AfEngine.dll` matches its SHA-256 entry in
  [`source-manifest.sha256`](../evidence/source-manifest.sha256).
- The owner-local `Resource.up` independently re-matched its public manifest
  digest immediately before the aggregate AFS scan.
- Ghidra 12.1.2 remains the canonical decompiler source. Rizin 0.9.1 with
  rzpipe 0.6.2 independently supplied normalized PE32/i386 boundaries and
  instructions.
- Analysis was static and offline. No original executable or script was run,
  patched, uploaded, or written.
- Original binaries and resources were read only. Working copies, generated
  reports, tool databases, helper executables, AFS text, logical paths, and
  per-file results remain ignored and local.

Confidence is capped at `2/medium` because the result has no controlled
running-original trace.

## Procedure

1. Recheck the immutable PE32 working copy against the public source manifest.
2. Export the compiler, function-record, process, lexer, and parser functions
   with the repository's pinned Rizin wrapper.
3. Compare instruction boundaries, calls, data accesses, and control flow
   against the existing deterministic Ghidra reports.
4. Follow source text through forward lexing, token-list insertion, grammar
   matching, AST child storage, recursive object compilation, compiled-record
   insertion, process construction, and process refresh.
5. Cross-reference every direct call to `AfDatabase::NewProcess` and inspect
   supplied-module PE import reports for external importers.
6. Enumerate all owner-authenticated local `.afs` entries through the bounded
   UDSP reader. Tokenize without retaining output, skipping line comments,
   block comments, and string literals; count only declarations at object
   brace depth one.
7. Encode only the proven activation/order rule in a synthetic, data-less
   C++20 module and test empty, event-only, mixed, repeated, and forged inputs.

## Static observations

Selected boundaries use image virtual addresses and exclusive ends:

| Function | Range `[start, end)` | Relevant observation |
|---|---:|---|
| compiler entry | `0x10060C60–0x10060E15` | lexes/parses then enters the recursive compiler |
| recursive compiler | `0x100612C0–0x10066A15` | dispatches object children and function kinds |
| compiled-record prepend | `0x10069340–0x10069360` | makes the new record the object-list head |
| compiled-record constructor | `0x10069360–0x10069393` | initializes Autoexec byte `+0x20` to zero |
| process constructor | `0x10069BC0–0x10069C73` | filters Autoexec records and appends execution nodes |
| named function invocation | `0x10069CF0–0x10069D95` | accepts only a record whose Autoexec byte is zero |
| process refresh | `0x10069DA0–0x10069DE6` | walks execution nodes from head through `+0x28` |
| execution-node constructor | `0x10069DF0–0x10069E98` | initializes null queue links before append |
| forward lexer | `0x1007A800–0x1007AA3C` | consumes source forward and emits tokens |
| token append | `0x1007AA70–0x1007AACF` | appends a token at the list tail |
| grammar/AST parser | `0x1007AEF0–0x1007B069` | visits grammar elements forward and stores children contiguously |

### Source and AST order

The token-list owner stores head at `+0x00` and tail at `+0x04`.
`[0x1007AA70, 0x1007AACF)` links the old tail's `+0x0C` to the new token,
stores the old tail at new token `+0x10`, clears new token `+0x0C`, and then
updates the tail. The lexer calls this helper while advancing through the
source.

The parser at `[0x1007AEF0, 0x1007B069)` follows grammar elements through
`+0x08`, writes successful child results to an increasing pointer, and creates
the containing AST node with that count and array. In the object path,
`0x10064DD0–0x10064E1D` starts at child index one and recursively compiles
successive children in ascending index order. Function declarations therefore
reach record construction in source order.

### Function kind and Autoexec

At `0x10064F7C`, the compiler dispatches three consecutive AST type values:

| AST type | Compiler path | Diagnostic corroboration | Autoexec |
|---:|---|---|---|
| `0x385` | `0x100657B2` | `== EVENT %s ==` | off |
| `0x386` | `0x100652F6` | action condition/duplicate diagnostics | on |
| `0x387` | `0x10064F9B` | timer time-missing diagnostic | on |

Every record starts with byte `+0x20 == 0` at `0x10069387`. The action path
writes one at `0x100653AA`; the timer path writes one at `0x1006503F`; the
event path reaches common insertion without changing the byte. Compiler debug
strings explicitly label this field `Autoexec: %s` with `(on)` and `(off)`.

Named invocation at `0x10069D15–0x10069D1A` rejects any selected record whose
byte is nonzero. This independently agrees with events remaining explicit
named calls while actions and timers start with a process.

### Compiled list and process queue

All three function paths converge on the call at `0x100659F4`. The insertion
helper stores the old object head in new record `+0x24`, back-links the former
head through `+0x28`, and replaces object `+0x1C` with the new record.
Consequently the compiled-record traversal order is the reverse of compiler
visit and source declaration order.

The process constructor starts at object `+0x1C`, advances through record
`+0x24`, and skips records with Autoexec off. For every accepted record it
appends an execution node after process tail `+0x18`, using node `+0x28` as
next and `+0x2C` as previous. `AfProcess::Refresh` starts at process `+0x14`,
prefetches node `+0x28`, and interprets in that FIFO order.

The complete single-process result is therefore:

```text
source function declarations
    -> compile in source order
    -> prepend each compiled record
    -> traverse records in reverse source order
    -> retain action/timer (Autoexec on)
    -> append execution nodes FIFO
    -> execute action/timer in reverse source order
```

Repeated declarations are not sorted or deduplicated by this chain. Events are
omitted from initial execution but remain available to the separate named-call
path.

### Process creation boundary

Rizin finds exactly three direct `AfDatabase::NewProcess` calls in
`AfEngine.dll`:

| Call site | Owner | Purpose |
|---:|---|---|
| `0x1002ACA1` | `NfMission::Load` | creates the selected mission process |
| `0x1002CE78` | `NfMission::Start` | creates the selected mission process |
| `0x1002F0C6` | `NfMission::Call` case `4` | implements native AFS `Call(object,event)` |

The supplied-module PE reports contain no external import of this exported
method. New processes are prepended to the database list, and
`AfDatabase::RefreshAll` traverses that list newest first. This establishes the
container rule but not whether the `Load` and `Start` paths coexist in one live
mission lifecycle.

## Authenticated local corpus observation

The comment/string-aware structural pass covered every owner-local AFS entry:

| Aggregate | Count |
|---|---:|
| AFS entries / object declarations | 52 / 52 |
| event declarations | 114 |
| action declarations | 328 |
| timer declarations | 0 |
| entries containing actions | 20 |
| maximum events in one entry | 7 |
| maximum actions in one entry | 51 |
| `MissionFail(...)` calls | 47 |
| `MissionSuccess(...)` calls | 20 |
| dynamic `Call(...)` uses | 0 |
| `KillProcess(...)` uses | 0 |
| `LoadScript(...)` uses | 0 |

This is aggregate compatibility evidence only. No logical name, source
fragment, per-file count, offset, or resource-derived file is committed. The
absence of timers and dynamic process calls describes this authenticated
corpus; it does not remove those statically supported language features from
the reconstruction.

## Portable implementation

`LegacyAfsFunctionSchedule` accepts already-classified function kinds in
source order. It:

- classifies events as explicit-call and actions/timers as Autoexec;
- emits only Autoexec entries in reverse source order;
- preserves repeated declarations;
- retains each source index and kind; and
- rejects a forged kind atomically with no partial schedule.

The implementation does not parse AFS, compile bytecode, hold function names,
execute an instruction, resolve an event, create a process, choose a mission
lifecycle path, or own a clock/dispatcher. Tests are wholly synthetic and have
no dependency on original or private files.

## Interpretation and gates

The earlier `+0x20` flag and function-order questions are closed for the static
single-process boundary. A future AFS loader may use the portable oracle after
it has safely parsed and classified declarations.

Live mission-outcome integration remains NO-GO because the following are still
unproven or unimplemented:

- exact bytecode and operand semantics for representative actions;
- a bounded source/bytecode parser and validated VM;
- the live choice and reset relationship between `NfMission::Load` and
  `NfMission::Start`;
- process creation/removal during a refresh, including custom scripts that use
  `Call`, `KillProcess`, or `LoadScript`;
- global dispatcher, trigger, immediate-event, presentation, and save ordering;
  and
- controlled running-original traces.

## Reproduction artifacts

Ignored local reports use the function IDs in the boundary table and the
`airfix.re.rizin-function.v1` schema. The local AFS inspector and its output
remain untracked. The public reproduction inputs are:

- `tools/Invoke-GhidraAnalysis.ps1`;
- `tools/Invoke-RizinAnalysis.ps1`;
- `docs/evidence/source-manifest.sha256`;
- `src/airfix/script/LegacyAfsFunctionSchedule.*`; and
- `tests/LegacyAfsFunctionScheduleTests.cpp`.

## Integrity checks

- Source and copy hashes match the public manifest: yes.
- Reports contain no local paths: yes.
- Original files were not modified: yes; the source digest was rechecked.
- Original code or scripts executed: no.
- Public tests depend on private/original data: no.
- Public-boundary and full build/test results: pass and are recorded in the
  progress log after final validation.
- Hosted Actions remain the publication gate.
