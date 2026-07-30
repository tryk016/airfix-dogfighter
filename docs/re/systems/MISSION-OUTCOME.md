# Mission outcome boundary

## Scope

This document records the recovered single-player mission-outcome state owned
by `NfMission`, its downstream result-screen decision, and the bounded
campaign-progression directive. It covers the two AFS calls `MissionFail` and
`MissionSuccess`, direct fail/reset effects, one-shot `pause`/`menu`
presentation, the process refresh boundary, failure precedence, and the
`THRD`/`AXMI`/`ALMI` update decision.

It does not implement the AFS VM, trigger-to-process live integration, score or
stat registration, roster chunks or file writes, multiplayer replication, or
either platform's menu.

Primary state evidence is `EV-20260730-005`, recorded by
[`EXP-20260730-070`](../../experiments/EXP-20260730-070-mission-outcome-state.md),
and downstream/scheduler evidence `EV-20260730-006`, recorded by
[`EXP-20260730-075`](../../experiments/EXP-20260730-075-mission-outcome-consumer.md).
AFS bytecode evidence `EV-20260731-002` is recorded by
[`EXP-20260731-080`](../../experiments/EXP-20260731-080-afs-mission-outcome-bytecode.md).
Ghidra 12.1.2 is the canonical static source; Rizin 0.9.1 independently
confirms the selected `AfEngine.dll` function boundaries and instructions and
supplies the compiler-emission evidence. `EV-20260731-001`, recorded by
[`EXP-20260731-079`](../../experiments/EXP-20260731-079-afs-function-order.md),
separately closes the compiled-function Autoexec flag and single-process
declaration-order boundary.

## Native state

The full `NfMission` object owns two independent one-byte fields:

| Full-object offset | Working name | Native readers/writers |
|---:|---|---|
| `+0x498` | `failed` | `Fail`, `HasFailed`, `Reset`, `Call(0x47)` |
| `+0x499` | `accomplished` | `IsAccomplished`, `Reset`, `Call(0x48)` |

`NfMission::Call` is entered through the `AfFunctionCall` subobject at
`NfMission+0x50`. Its instructions consequently address these fields as
`+0x448` and `+0x449`; those are the same full-object bytes after applying the
subobject adjustment.

Constructor, reset, and recovered writers produce only `0` and `1`.
Conditional branches treat any nonzero byte as true. The portable model uses
two `bool` values but does not expose or serialize a native object layout.

The flags must not be collapsed to one outcome enum. A later conflicting call
sets the other flag and leaves both true.

## AFS registration

`AfFunctionCall::SetupAfServerFunctions` registers:

| AFS name | Call identifier |
|---|---:|
| `MissionFail` | `0x47` |
| `MissionSuccess` | `0x48` |

Each registration adds one formal type-`2` parameter, named `dummy`, which
occupies one VM argument word. Cases `0x47` and `0x48` do not read the value
from their argument buffer and both return zero. The payload value therefore
has no semantic effect, but its one-word calling-convention width is part of
the recovered bytecode boundary.

## AFS bytecode boundary

The token/grammar registration maps source literal `true` to compiler AST tag
`0x137`; the recursive compiler emits that tag as two words:

```text
0x1B, 0x00000001
```

Opcode `0x1B` pushes the following immediate word. The recursive compiler then
resolves registered native functions before script functions. After compiling
the argument in source order, it appends the native call triplet:

```text
0x24, 0x26, descriptor-token
```

Opcode `0x24` pushes the current execution object. Opcode `0x26` consumes the
descriptor token, removes the execution word plus the descriptor's argument
words from the VM stack, and invokes the explicit handler with the native
identifier. A script-function call instead uses `0x23, 0x25,
descriptor-token`; that sequence is outside the mission-outcome boundary.

The portable descriptor projection validates a nonzero matching token, an
explicit handler, argument-word count `1`, and identifier `0x47` or `0x48`.
It deliberately does not retain native pointers or model the descriptor's
linked lists.

A private aggregate-only scan of the verified resource working copy found 52
AFS members and 67 outcome calls: 47 fail and 20 success. Every call is a
statement in an `action` body, is terminal in its block, and supplies the
built-in literal `true`. No script text, logical path, or asset is published.
The complete outcome call site is therefore exactly:

```text
0x1B, 0x00000001, 0x24, 0x26, descriptor-token
```

The compiler's `action` case creates an auto-executed function with a
0.25-second yield/condition loop, sets an action-state slot when the condition
passes, executes the body, and emits the execution terminator. This is a
bounded structural envelope, not a complete VM or scheduling contract.

## Exact transition

For either AFS call, `NfMission::Call` first tests both flags. It then sets the
selected flag unconditionally. Only a transition from `(false, false)` also
executes the two console commands, in this order:

1. `pause`
2. `menu`

Repeated and conflicting calls still perform their idempotent byte write, but
they do not request those commands again.

```text
MissionFail:
    firstTerminal = !failed && !accomplished
    failed = true
    if firstTerminal:
        request pause, then menu

MissionSuccess:
    firstTerminal = !failed && !accomplished
    accomplished = true
    if firstTerminal:
        request pause, then menu
```

The direct virtual `NfMission::Fail` method only sets `failed`. It does not
execute `pause` or `menu`. `NfMission::Reset` clears both flags, along with
substantial unrelated mission state which remains outside this reconstruction.

## Address ledger

All ranges are image virtual addresses and use an exclusive end.

| Function | Range `[start, end)` | Confidence |
|---|---:|---:|
| `NfMission::Load` | `0x1002ABB0–0x1002B0B7` | 2 |
| `NfMission::Fail` | `0x1002CB50–0x1002CB58` | 2 |
| `NfMission::HasFailed` | `0x1002CB60–0x1002CB67` | 2 |
| `NfMission::IsAccomplished` | `0x1002CB70–0x1002CB77` | 2 |
| `NfMission::Reset` | `0x1002CB90–0x1002CD98` | 2 |
| `NfMission::Call` | `0x1002EC80–0x100315FE` | 2 |
| `NfTrigger` constructor 0 | `0x10033F70–0x1003412A` | 2 |
| `NfTrigger` constructor 1 | `0x10034130–0x100341EA` | 2 |
| `NfTrigger` constructor 2 | `0x100341F0–0x100342CA` | 2 |
| `NfTrigger` constructor 3 | `0x100342D0–0x10034397` | 2 |
| `NfTrigger` constructor 4 | `0x100343A0–0x10034454` | 2 |
| `NfTrigger` constructor 5 | `0x10034460–0x1003450D` | 2 |
| `NfTrigger::Refresh` | `0x10034590–0x100349C5` | 2 |
| `NfTrigger::Event` | `0x10034A20–0x10034EBB` | 2 |
| AFS token/grammar registration | `0x1005D0A0–0x10060AB7` | 2 |
| recursive AFS compiler | `0x100612C0–0x10066A15` | 2 |
| `AfDatabase::NewFunctionType` | `0x10068310–0x10068345` | 2 |
| `AfFunctionCall::SetupAfFunctions` | `0x100683F0–0x100686C0` | 2 |
| `AfFunctionCall::SetupAfServerFunctions` | `0x100686C0–0x1006916B` | 2 |
| native descriptor constructor | `0x100698D0–0x10069929` | 2 |
| add-parameter helper | `0x100699C0–0x10069A2B` | 2 |
| AFS interpreter | `0x10069F10–0x1006AC84` | 2 |

All confidence values are capped at `2` because this is static-only evidence.
Agreement between Ghidra and Rizin does not replace controlled comparison with
the running original.

The outcome-specific instructions are:

| Operation | Address |
|---|---:|
| direct `Fail` write | `0x1002CB50` |
| `Reset` failed clear | `0x1002CC9D` |
| `Reset` accomplished clear | `0x1002CCA3` |
| `Call(0x47)` entry | `0x1003136E` |
| first fail write | `0x10031382` |
| repeated/conflicting fail write | `0x1003138B` |
| `Call(0x48)` entry | `0x100313A1` |
| first success write | `0x100313B5` |
| shared `pause` then `menu` path | `0x100313BC` |
| repeated/conflicting success write | `0x100313F7` |

## Trigger boundary

`NfTrigger::Refresh` and `NfTrigger::Event` maintain trigger-condition state.
They do not directly call `MissionFail` or `MissionSuccess`. The safe portable
join is therefore:

```text
trigger / scheduler / AFS
    -> one already-ordered call identifier
    -> LegacyMissionOutcomeState
```

The outcome transition must not evaluate triggers, run AFS processes, sort or
batch calls, remove duplicates, replay dropped calls, or invent ordering
between script processes.

### Recovered process refresh order

`NfMission::Load` creates the `mission` process after loading the selected AFS,
then explicitly invokes `SetupLevelData` and conditionally `SetupServer`. A
process constructor walks the compiled-function list and appends one execution
for each function whose `+0x20` Autoexec flag is set. The compiler maps
`event` to Autoexec off and `action`/`timer` to Autoexec on. It visits object
declarations in source order but prepends every
new compiled record, so initial action/timer executions are queued in reverse
source declaration order. Executions within a process are refreshed FIFO;
newer processes are placed at the head of the global process list.

The separate named invocation accepts only Autoexec-off records. Events are
therefore explicit named calls rather than initial process executions.
`LegacyAfsFunctionSchedule` implements only this classification and initial
single-process order as a synthetic, fail-closed C++20 boundary.

Each ordinary `NfMission::Refresh` calls `AfDatabase::RefreshAll` before it
polls `NfTrigger::Refresh`. Consequently an ordinary trigger condition set by
that later poll is first visible to an action during the next mission refresh.
Events `0x6B`, `0x6C`, and `0x92` are a separate exception:
`NfMission::ProcessEvent` first updates all triggers and then immediately calls
the AFS function `Spawned`.

This evidence is not yet a live AFS integration contract. The compiler's
`action` case, the complete five-word outcome call site, and the initial
single-process schedule are now bounded, but the live relationship between the
two mission creation paths, all additional process owners, mutation during
refresh, custom-script dynamic process operations, and global
dispatcher/mission/presentation order remain unproven.

Selected scheduler ranges are image virtual addresses with exclusive ends:

| Function | Range `[start, end)` | Confidence |
|---|---:|---:|
| `NfMission::ProcessEvent` | `0x1002B310–0x1002C083` | 2 |
| `NfMission::Refresh` | `0x1002C830–0x1002CB41` | 2 |
| AFS compiler entry | `0x10060C60–0x10060E15` | 2 |
| recursive AFS compiler | `0x100612C0–0x10066A15` | 2 |
| `AfDatabase::NewProcess` | `0x100680F0–0x10068125` | 2 |
| `AfDatabase::RefreshAll` | `0x100681A0–0x100681C1` | 2 |
| compiled-record prepend | `0x10069340–0x10069360` | 2 |
| compiled-record constructor | `0x10069360–0x10069393` | 2 |
| mission-process constructor | `0x10069BC0–0x10069C73` | 2 |
| named function invocation | `0x10069CF0–0x10069D95` | 2 |
| mission-process refresh | `0x10069DA0–0x10069DE6` | 2 |
| execution-node constructor | `0x10069DF0–0x10069E98` | 2 |
| AFS interpreter | `0x10069F10–0x1006AC84` | 2 |
| forward lexer | `0x1007A800–0x1007AA3C` | 2 |
| token-list append | `0x1007AA70–0x1007AACF` | 2 |
| grammar/AST parser | `0x1007AEF0–0x1007B069` | 2 |

The authenticated local corpus contains 52 AFS objects with 114 events, 328
actions, and no timers. It contains 47 `MissionFail` and 20 `MissionSuccess`
calls, but no dynamic `Call`, `KillProcess`, or `LoadScript` use. These are
aggregate compatibility observations only; no script text, logical path, or
per-file result is public.

## Downstream result and progression

The single-player result-screen constructor at
`[0x00405A90, 0x0040690A)` applies this predicate at all three relevant
branches:

```text
success = missionExists && accomplished && !failed
```

Therefore failure wins when both native flags are true. No mission object,
neither flag, failed-only, and both-flags states all select failure/retry.
Only accomplished-only with an existing mission selects success/continue.

On success, the native code reads `mission_number` as signed 32-bit `int`,
increments it with x86 `INC`, classifies case-insensitive
`mission_thread == "allied"` as side `1` and every other/missing string as
side `0`, then:

1. always removes any existing `THRD` and adds a new four-byte side marker;
2. selects `ALMI` for Allied or `AXMI` for Axis;
3. adds the four-byte candidate when that maximum is absent; or
4. replaces it only when the existing signed `int32` maximum is less than the
   candidate.

An equal or greater maximum remains unchanged, but the preceding `THRD`
replacement still occurs. Failure performs none of these progression updates.

The original `INC` wraps `INT32_MAX` to `INT32_MIN`, and the following
comparison is signed. The portable model intentionally rejects that one input
instead of copying wraparound into a modern save directive. Other signed
values, including `-1 -> 0`, remain defined and testable.

FourCC and payload evidence:

| Chunk | FourCC little-endian | Payload |
|---|---:|---|
| `THRD` | `0x44524854` | signed `int32`, Axis `0` or Allied `1` |
| `AXMI` | `0x494D5841` | signed `int32` next mission number |
| `ALMI` | `0x494D4C41` | signed `int32` next mission number |

The result screen also computes/registers local-player statistics, updates
`SCOR`, and calls `AfChunkContainer::Write(nullptr)`. Those effects are
deliberately excluded: the remembered-path roster lifecycle, corruption
recovery, score contract, and safe modern save format are not recovered.

The callback at `[0x00406CE0, 0x00406E14)` confirms Continue after success,
Retry after failure, and a separate Exit route. The portable consumer returns
only the primary continue/retry choice as data; it executes no command.

## Portable implementation

`src/airfix/simulation/LegacyAfsMissionOutcomeCall.*` implements only:

- the proven `0x1B, 1, 0x24, 0x26, descriptor-token` recognition;
- a pointer-free descriptor projection;
- validation of the exact one-word outcome descriptor shape;
- typed conversion of identifiers `0x47` and `0x48`; and
- fail-closed zero-word consumption for every mismatch.

It recognizes but does not execute the immediate argument producer. It owns no
VM stack, bytecode scheduler, trigger, execution object, native pointer, or
mission state.

`src/airfix/simulation/LegacyMissionOutcomeState.*` implements only:

- the two independent flags;
- one already-formed `0x47` or `0x48` call;
- the ordered `requestPauseThenMenu` directive on the first terminal call;
- direct `NfMission::Fail`;
- the two-flag projection of `NfMission::Reset`;
- fail-closed handling of unsupported call identifiers.

The command directive is data, not an executed console command. A future
runtime adapter must translate it through the shared pause/menu ownership
boundary without embedding the original console or AFS runtime.

`src/airfix/simulation/LegacyMissionOutcomeConsumer.*` separately implements:

- the exact mission-exists/accomplished/not-failed result predicate;
- failure/retry and success/continue value directives;
- typed Axis/Allied thread selection already classified by a caller;
- always-set-thread behavior on success;
- signed add/replace/keep maximum planning;
- fail-closed unknown side and `INT32_MAX` handling; and
- no progression directive on failure or invalid success metadata.

It does not parse `mission_thread`, serialize FourCC chunks, register stats,
calculate score, write a roster, or wire the earlier outcome state to AFS.

`src/airfix/script/LegacyAfsFunctionSchedule.*` separately implements:

- `event` as explicit-call and `action`/`timer` as Autoexec;
- reverse-source initial Autoexec order for one process;
- preservation of repeated declarations and source indices; and
- atomic rejection of forged function kinds.

It accepts already-classified declarations and does not parse source, compile
bytecode, resolve names, execute scripts, own process state, or perform live
mission integration.

## `Singleplayer.mode` exclusion

The selected mode creates a derived `NfMission`, but no outcome-contract
override was found. RVA `0x12D0`, previously a candidate, is a cheat callback
for the subobject at mission offset `+0x548`; it handles weapon, time-scale,
medal, and autopilot cheats and is unrelated to mission outcome.

Confirmed selected mode boundaries are:

| Function | Range `[start, end)` |
|---|---:|
| `missionCreate` | `0x10001000–0x10001026` |
| derived constructor | `0x10001030–0x100011B8` |
| deleting destructor | `0x100011C0–0x100011DE` |
| destructor | `0x100011E0–0x100012C2` |
| vehicle helper | `0x10001900–0x100019B2` |

## Open questions

- What is the complete bytecode of representative success/failure actions,
  including each condition and surrounding control flow?
- How do `NfMission::Load` and `NfMission::Start` replace or coexist with
  database/process state in the live mission lifecycle?
- What is the complete dispatcher/process/presentation order when several
  processes, dynamic process operations, and immediate `Spawned` calls
  interact?
- What is the safe roster lifecycle, schema, corruption recovery, and atomic
  replacement contract behind `AfChunkContainer::Read/Write`?
- How are score and registered player statistics calculated and migrated?
- What are the network and multiplayer semantics?

These questions block live integration and persistence, not the isolated
two-flag transition, value-only result/progression decision, or initial
single-process function schedule.
