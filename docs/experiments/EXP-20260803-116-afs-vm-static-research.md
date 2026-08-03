# EXP-20260803-116 — AFS mission scripting VM static reconstruction

**Status:** statically recovered and independently reviewed; no open
P0–P3 finding

**Question:** What are the complete AFS mission-script lifecycle, parser and
compiler boundary, runtime instruction set, process/execution state,
scheduler order, gameplay dispatch, failure behavior, and safe-port gates?

**Source build/hash:** `AfEngine.dll`
`A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E`

**Working-copy hash:** byte-identical to the source hash above

**Git start snapshot:** `origin/main` at
`1ec27ace154607a99359f39ffa82390e8bc5ab35`, fetched at
`2026-08-03T15:13:13+01:00`; branch divergence at the start of the experiment
was `0/0`. The remote later advanced to an out-of-scope campaign merge, after
this isolated study had started; it was not imported into this branch.

**Environment:** isolated Windows worktree; static analysis only

**Tool versions:** Ghidra Headless 12.1.2 primary; Rizin 0.9.1 with rz-ghidra
and rzpipe 0.6.2 independent cross-check

**Offline/no-cloud confirmed:** yes

**Memory/file patching:** disabled; no game, script, executable, debugger, or
GUI was run

**Safety boundary:** documentation only; no runtime C++20, original binary,
AFS source, original asset, decompiler database, host path, PR, or merge is
part of this experiment

**Related IDs:** `EV-20260803-003`, `EV-20260724-005`–`007`,
`EV-20260730-005`, `EV-20260730-006`, `EV-20260731-001`,
`EV-20260731-002`, `EV-20260803-001`, `EV-20260803-002`

## Prediction

The falsifiable prediction was that the AFS runtime is a wordcode interpreter
with explicit process and execution queues, and that two independent static
tools would agree on a closed chain:

```text
source read -> lexer/parser -> compiler -> object/function records
            -> process -> execution -> one interpreter step
            -> native mission action -> trigger/world/outcome
            -> reset/destruction
```

Distinguishing results were:

- no centralized dispatch or an indirect format not recoverable as bounded
  words would contradict the wordcode prediction;
- a register machine would contradict the expected stack/process state model;
- automatic event startup or source-order action startup would contradict the
  earlier Autoexec evidence;
- trigger polling before the ordinary process pass would contradict the
  earlier scheduler seam;
- mismatched raw instruction bytes, unresolved central addresses, or an
  unreconciled boundary/opcode result between Ghidra and Rizin would prevent
  publication; and
- safe code/stack/index/descriptor gates in every handler would contradict the
  prediction that malformed native behavior is unsuitable for a port.

## Prior-evidence review

Before any new export, the reports covering AFS, mission loading, mission
outcome, result/campaign flow, schedulers, and triggers were reviewed. The exact
reuse ledger is in
[EV-20260803-003](../re/systems/AFS-VM.md#earlier-evidence-reused).

The experiment reused mission ownership and load order from
`EV-20260724-005`–`007`; outcome bytes and native IDs from
`EV-20260730-005`; process-before-trigger and immediate-`Spawned` seams from
`EV-20260730-006`; declaration/Autoexec/list order and the three process
creation sites from `EV-20260731-001`; and the exact five-word outcome call
from `EV-20260731-002`. It did not rescan the private AFS corpus or reproduce
the already-confirmed mission/result/campaign investigation.

The AI/physics cycle in `EV-20260803-001` and campaign/frontend flow in
`EV-20260803-002` were used only as external boundaries. Neither is evidence
for a global AFS-versus-aircraft scheduler order.

## Procedure

1. Fetch current remote state, scan all current and active local/remote refs,
   verify that `EXP-20260803-116` and `EV-20260803-003` are unused, and reserve
   them on an isolated branch based on current `origin/main`.
2. Prepare a read-only-derived working copy with the repository wrapper and
   verify its SHA-256 against the public source manifest.
3. Create a fresh Ghidra 12.1.2 PE32/i386 project through the repository
   headless wrapper. Export full decompilation, selected decompilation,
   instructions, xrefs, helper bodies, and both dispatch tables. Require zero
   unresolved selected addresses.
4. Follow source input through `LoadScript`, compiler entry, lexer, grammar/AST,
   recursive emission, object/function insertion, process construction,
   immediate named calls, refresh, interpreter dispatch, reset, and database
   destruction.
5. Decode all 70 jump-table entries and record instruction width, operand,
   stack effect, condition, next IP, references/calls, and exceptional paths.
6. Independently export 35 explicit functions through the Rizin 0.9.1 wrapper
   without seeding Ghidra names. Compare boundaries, instructions, calls,
   xrefs, list offsets, jump-table targets, and malformed paths.
7. Resolve every discrepancy from raw bytes and explicit SP/IP simulation.
   Keep facts, conditional inferences, hypotheses, and unknowns separate.
8. Publish the format note, evidence report, opcode/process/scheduler maps,
   gameplay diagram, portability requirements, and separate GO/NO-GO gates;
   update function/module/parity/progress ledgers only.
9. Run both wrappers' tests, Rizin normalized-export tests, documentation
   integrity/link checks, public-boundary tests and scan, path/ID checks, and
   `git diff --check`. Submit the closed diff for independent review and
   resolve all P0–P3 findings.

## Raw observations

### Tool agreement

Ghidra freshly imported the verified PE32/i386 image and identified 1,645
entry functions. The selected exports contain no unresolved address or
decompilation-failure marker. Rizin emitted 35 normalized reports. The tools
agree on all 35 selected end-exclusive function boundaries and on the central
direct calls/xrefs listed in
[the evidence call graph](../re/systems/AFS-VM.md#static-architecture-map).

Selected central bodies are:

| Function | Exact VA range | Bytes / instructions | Cross-check |
|---|---:|---:|---|
| compiler entry | `0x10060C60–0x10060E15` | 437 / 150 | Ghidra + Rizin |
| recursive compiler | `0x100612C0–0x10066A15` | 22,357 / 7,089 | Ghidra + Rizin |
| `NfMission::Refresh` | `0x1002C830–0x1002CB41` | 785 / 211 | Ghidra + Rizin |
| `AfDatabase::RefreshAll` | `0x100681A0–0x100681C1` | 33 / 15 | Ghidra + Rizin |
| process constructor | `0x10069BC0–0x10069C73` | 179 / 73 | Ghidra + Rizin |
| named invocation | `0x10069CF0–0x10069D95` | 165 / 65 | Ghidra + Rizin |
| process refresh | `0x10069DA0–0x10069DE6` | 70 / 32 | Ghidra + Rizin |
| execution constructor | `0x10069DF0–0x10069E98` | 168 / 56 | Ghidra + Rizin |
| interpreter | `0x10069F10–0x1006AC84` | 3,444 / 1,212 | Ghidra + Rizin |

The word-vector helper's exact executable boundary is
`[0x10061220,0x100612A3)`. Padding follows, then a separate identity helper at
`[0x100612B0,0x100612B7)`. This corrects the older aggregate boundary ending at
`0x100612B8`; the compiler and outcome conclusions that used the append helper
are unchanged.

### Reconciled opcode analysis

The interpreter reads a 32-bit opcode, increments IP, compares unsigned
`opcode - 1` with `0x45`, and indexes the 70-dword table at `0x1006AC84`.
Valid runtime IDs are exactly `0x01–0x46`. The complete public table is
[here](../re/systems/AFS-VM.md#complete-opcode-table).

One independent reading initially classified opcode `0x37` as five inputs to
three with a retained word. Replaying the common `ESI=-4`, `EBX=+4` SP updates
in raw instructions `[0x1006A8DC,0x1006A948)` found the missed SP stores at
`0x1006A903` and `0x1006A914`. Both tools then agree on:

```text
[w0,w1,w2,w3,w4,w5] -> [w3/w0,w4/w1,w5/w2]
stack delta = -3 words
```

The rejected reading is not retained as an open finding. Separate raw review
confirmed that null input to opcodes `0x3C` and `0x3E` is popped without a
replacement, so those paths have a different stack height.

Opcode zero or greater than `0x46` is not rejected. Its word has already been
consumed when the interpreter silently returns. The invalid opcode itself
does not alter active/link state. An otherwise-active process keeps the
execution linked and resumes at the following word when the scheduler next
reaches it—normally on the next refresh, but a newly appended child can be
reached later in the same process pass. If an earlier opcode `0x46` in that
call already cleared active, normal wrapper teardown destroys the process
instead.

### Lifecycle and scheduler

`NfMission` embeds an `AfDatabase` at `+0x4C0`. Both `Load` and `Start` can
compile, prepend a mission process, and immediately interpret named setup
events with `dt=0`. Autoexec action/timer nodes are queued but first run on
`RefreshAll`. Reset destroys processes only; final database destruction also
destroys descriptors, objects/wordcode, and compiler state.

An ordinary mission refresh runs effects/message, then every AFS process
newest-first and each execution FIFO, then polls ordinary triggers. Immediate
event types `0x6B`, `0x6C`, and `0x92` update triggers first and call named
`Spawned` synchronously. New process and execution nodes interact with cached
next pointers exactly as detailed in the evidence report.

No AI/physics body is called from `NfMission::Refresh`; global cross-dependant
order is unresolved.

### Structure and gameplay boundary

The recovered owner sizes are: database `0x18`, object `0x30`, compiled
function `0x30`, process `0x28`, execution `0x30`, native descriptor `0x2C`,
parameter record `0x10`, and retained text/reference object `0x0C`. Field maps
and ownership are published in
[EV-20260803-003](../re/systems/AFS-VM.md#runtime-state-and-ownership).

Registered native IDs cover messages/console, level/script loads,
actor/effect/pickup creation, pickup visibility/spawn, AI activation and task
state, doors, actor/team/squad/rank/health, triggers, and mission outcome. Six
registered names are confirmed default-return-zero no-ops in this handler:
`KillProcess`, `GetTeam`, `GetSquad`, `Restore`, `TurnOffLight`, and
`TurnOnLight`. No general objective or actor-despawn descriptor was found.

### Parser and malformed behavior

Source ingress is a NUL-terminated pointer without a length. The loader does
not require an exact read, does not bound `size + 1`, and returns success for a
valid package member even when compilation records an error. Lexer, AST-child,
compiler-vector, instruction, stack, slot, branch, descriptor, call-width, and
allocation bounds are absent or incomplete. Compile mutation is not visibly
transactional. Native text handling contains fixed unchecked buffers, and the
diagnostic writer treats compiler output as a format string.

The temporary source allocation is released on every ordinary post-compile
path, including reported compiler errors; invalid members return before that
allocation. A normal short read still reaches deletion but compiles incomplete
or uninitialized contents. Overflow, unusable/null allocation, fault/exception
cleanup before the join, and one conditional diagnostic-file close path remain
unsafe or unknown.

Missing-reference behavior is deliberately not normalized in the original.
Undeclared functions and inherited objects mark compiler errors, but a valid
package member still makes `LoadScript` return true; `Load` and `Start` then
handle a missing selected object differently. Missing named events/processes
and native ID `0x04` object/function targets are skipped silently, whereas
malformed descriptor tokens in opcodes `0x25`–`0x27` are dereferenced without
a guard. Exact branches, diagnostics, cleanup, and return sites are recorded in
the evidence report; partial database residue remains unknown because no
rollback call was found.

These observations support a new fail-closed policy, not reproduction of
unsafe malformed behavior.

## Interpretation

The stack/process prediction is supported. AFS is text compiled to 32-bit
database/object-owned wordcode; there are no VM registers. Process/execution-
local mutable state consists of delay, IP, SP/stack, two variable arrays,
handler/record/process references, and queue links.

The local scheduler prediction is supported: new processes are prepended,
initial Autoexec functions are reverse source order, executions are FIFO, and
named calls can take an immediate first step. Mutation semantics depend on
pre-cached next pointers and re-entrant execution, so an implementation still
requires dedicated safe-lifetime oracles.

The safety prediction is also supported. Native behavior trusts compiled
pointers, indices, control flow, stack effects, allocation, and time values.
It is not an acceptable parser/VM security policy. Static confidence remains
2 because no game was executed and no numerical or wall-clock trace exists.

## Reproduction artifacts

All raw reports, working copies, and tool databases remain ignored and local.
Logical Ghidra report hashes are:

| Logical report | SHA-256 |
|---|---|
| `AfEngine.dll.fresh-full-decomp.txt` | `17B1228D94E012FD2D9BAFB8B7BEF0C4FDAA2FFC6B5099C9CEE28D5031A50259` |
| `AfEngine.dll.target-decomp.txt` | `62407B39CEA1EE65583E33141DAA70A4ED8B078F204F99264D8C6A1266CEFED2` |
| `AfEngine.dll.target-insns.txt` | `65E7E9F3F23C167D509C3E67DABDE451D1B5E8DD7D3B2D25C74800C00C86A095` |
| `AfEngine.dll.target-xrefs.txt` | `2D521F968694A2BBF81C939E5826BA20C309CB7FA8BD7E9A5DB384ABA00C4073` |
| `AfEngine.dll.helpers-decomp.txt` | `BB92FE9BE6DB58020515E6DE283432B40216EA52F7804681CBA29E06F68B1D3E` |
| `AfEngine.dll.helpers-insns.txt` | `4E334477A892BA96C2ACF306BF1B1715B24FD2639BCD3DED2CF5C20E982FCC66` |
| `AfEngine.dll.dispatch-tables.txt` | `2AD8E19B429E8592CFF22CE0AD4C0F7E2B44A29CA427D0C73E0576217C51C0AC` |

The 35 normalized Rizin JSON reports use schema
`airfix.re.rizin-function.v1`. The SHA-256 of the sorted `name:sha256` aggregate
(lowercase per-report digests, LF-separated, no final LF) is
`2FC068F1D9291511622355E69440E2034B74CED67B2CBA6406C6388956CD289D`.
No raw report or per-function hash ledger is committed.

## Integrity checks

- Source and working-copy hashes match the public manifest: **pass**.
- Fresh Ghidra import and selected-address exports: **pass**; 1,645 functions,
  zero unresolved selected addresses, zero path hits.
- Rizin wrapper and normalized exports: **pass**; 35 path-free reports with
  unique function IDs.
- Ghidra/Rizin selected end-exclusive boundaries: **35/35 exact**; key direct
  call-graph edges: **17/17 exact**, with no unresolved disagreement; opcode
  `0x37` semantic reading reconciled from raw instructions.
- Repository wrapper tests: **pass** for Ghidra analysis, Rizin analysis, and
  working-copy preparation.
- Rizin exporter unit tests: **12/12 pass**.
- Documentation integrity and links: **pass**; 116 experiments and 408
  function-catalogue rows.
- Public-boundary tests and full scan: **pass**; 779 files scanned.
- Reserved-ID scan: **pass**; 232 current refs contained zero prior occurrence
  of either reserved ID before commit.
- Opcode enumeration **70/70**, VA/RVA arithmetic **50 ranges**, strict UTF-8,
  changed-scope local-path scan, docs-only scope, and `git diff --check`:
  **pass**.
- Original files, game, AFS, and derived content publication: **none**.
- Independent Ghidra, Rizin, and final closed-diff reviews: **APPROVE / PASS**;
  open findings `P0=0`, `P1=0`, `P2=0`, `P3=0`.

## Decision

| Area | Decision |
|---|---|
| fail-closed parser policy | **GO**; implementation/legacy compatibility remains **NO-GO** |
| process/structure model | static **GO**; safe mutation implementation **CONDITIONAL GO** |
| local process scheduler | static **GO**; re-entrant lifetime implementation **CONDITIONAL GO** |
| each opcode | width/raw effect **GO** at confidence 2; execution gate is recorded per row in `EV-20260803-003` |
| full interpreter | **NO-GO** |
| global AI/physics order | **NO-GO** |
| numerical and temporal compatibility | **NO-GO** |

## Follow-up

- Specify bounded source/AST/IR limits and synthetic valid/malformed fixtures.
- Build data-less stack/CFG and scheduler-mutation oracles before any runtime
  proposal.
- Recover the outer `Load`/`Start`/`Reset` and pause-owner call sequence in a
  separate evidence ID.
- Obtain controlled running-original numerical/time traces only under a future
  explicitly authorized dynamic experiment.
- Implement no runtime behavior from this documentation-only experiment.
