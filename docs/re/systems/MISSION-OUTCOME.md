# Mission outcome boundary

## Scope

This document records the recovered single-player mission-outcome state owned
by `NfMission`. It covers the two AFS calls `MissionFail` and
`MissionSuccess`, the direct `NfMission::Fail` method, the outcome projection
of `NfMission::Reset`, and the one-shot `pause`/`menu` presentation request.

It does not reconstruct trigger evaluation, AFS scheduling, campaign
progression, save data, multiplayer replication, or either platform's menu
implementation.

Primary evidence is `EV-20260730-005`, recorded by
[`EXP-20260730-070`](../../experiments/EXP-20260730-070-mission-outcome-state.md).
Ghidra 12.1.2 is the canonical static source; Rizin 0.9.1 independently
confirms the sixteen selected `AfEngine.dll` function boundaries and
instructions.

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

Cases `0x47` and `0x48` do not read their argument buffer and both return
zero. The absence of a payload is part of the portable boundary.

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
| `AfFunctionCall::SetupAfFunctions` | `0x100683F0–0x100686C0` | 2 |
| `AfFunctionCall::SetupAfServerFunctions` | `0x100686C0–0x1006916B` | 2 |

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

## Portable implementation

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

- What orders multiple simultaneously runnable AFS processes?
- How does a satisfied `NfTrigger` schedule its attached process?
- Which result does campaign/UI code prefer if both flags are true?
- How are mission results persisted and consumed by campaign progression?
- What are the network and multiplayer semantics?
- What do the owner-private AFS scripts request in real missions?

These questions block live integration, not the isolated two-flag transition.
