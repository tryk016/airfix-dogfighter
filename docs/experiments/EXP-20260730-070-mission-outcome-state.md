# EXP-20260730-070 — mission outcome state

## Decision

`EV-20260730-005` gives conditional GO for a pure, one-call-at-a-time
`LegacyMissionOutcomeState`.

It gives NO-GO for wiring that state to AFS, `NfTrigger`, campaign
progression, save data, multiplayer, or either platform's menu until those
owners and their ordering are recovered separately.

## Question

What is the smallest exact state transition behind the native `MissionFail`
and `MissionSuccess` functions, and can it be implemented without guessing
trigger, scheduler, script, or frontend behavior?

## Method

Ghidra 12.1.2 was the primary source. Reproducible headless reports exported
decompilation and exact instructions for sixteen selected `AfEngine.dll`
functions:

- `NfMission::Load`, `Fail`, `HasFailed`, `IsAccomplished`, `Reset`, and
  `Call`;
- six `NfTrigger` constructors, `Refresh`, and `Event`;
- `AfFunctionCall::SetupAfFunctions` and `SetupAfServerFunctions`.

Rizin 0.9.1 independently rediscovered and exported all sixteen function
boundaries from the same hashed read-only working copy. A separate Ghidra
memory report recovered the exact command bytes at the two addresses consumed
by the terminal path. No executable, mode, game script, or GUI was run.

The selected `Singleplayer.mode` factory, constructor, destructors, and helper
were also inspected. In-memory Rizin analysis was used only to classify the
candidate at RVA `0x12D0`; it is a cheat callback, not an outcome override.

## Findings

The full mission owns independent bytes at `+0x498` and `+0x499`.
`NfMission::Call` enters through a subobject at `+0x50`, so its apparent
`+0x448/+0x449` accesses resolve to those same full-object offsets.

`SetupAfServerFunctions` maps:

| Name | Identifier |
|---|---:|
| `MissionFail` | `0x47` |
| `MissionSuccess` | `0x48` |

Neither case reads its argument buffer. Each tests both bytes, sets its own
byte to one, and returns zero. Only the first transition from `00` executes
`pause` and then `menu`. A repeated or conflicting call still writes its
selected byte, but does not repeat presentation.

Direct `NfMission::Fail` sets only the failed byte and has no presentation
effect. `NfMission::Reset` clears both bytes at `0x1002CC9D` and
`0x1002CCA3`; the rest of its 520-byte function is outside the portable
outcome projection.

The native model is not a first-result-wins enum. These sequences are exact:

| Sequence after reset | Final `(failed, accomplished)` | Presentation |
|---|---|---|
| fail, fail | `10` | once, on first fail |
| success, success | `01` | once, on first success |
| fail, success | `11` | once, on fail |
| success, fail | `11` | once, on success |
| direct fail, success | `11` | none |
| fail, reset, success | `01` | twice, separated by reset |

The complete address ledger and durable trigger boundary are maintained in
[`MISSION-OUTCOME.md`](../re/systems/MISSION-OUTCOME.md).

## Implementation

The new allocation-free C++20 boundary contains:

- `LegacyMissionOutcomeState { failed, accomplished }`;
- exact call identifiers `0x47` and `0x48`;
- one `legacyMissionApplyOutcomeCall` invocation per already-ordered call;
- a `requestPauseThenMenu` data directive;
- `legacyMissionMarkFailed`;
- `legacyMissionResetOutcomeState`;
- fail-closed state preservation for unsupported identifiers.

It owns no trigger, AFS process, event queue, batch, ordering policy, console,
pause state, menu, mission loader, save, campaign, renderer, audio, or platform
adapter.

Synthetic tests cover all four reachable two-flag starting states crossed with
both calls, repeated calls, both conflict orders, direct fail, reset, exact
identifier values, presentation order as one typed directive, and unsupported
identifiers.

## Confidence

Medium (`2`) and static-only:

- all sixteen selected `AfEngine.dll` boundaries;
- identifiers `0x47/0x48`;
- field offsets and independent writes;
- repeat and conflict behavior;
- direct fail and reset projection;
- first-terminal presentation predicate;
- `pause` before `menu`;
- exclusion of `Singleplayer.mode` from this pure kernel;
- `NfTrigger` as a condition producer rather than a direct outcome caller.

The agreeing Ghidra and Rizin instruction reports make this a strong static
contract, but project policy caps it at confidence `2` until controlled
dynamic comparison against the original process exists.

Unknown and intentionally excluded:

- ordering among multiple AFS processes;
- trigger-to-process scheduling;
- priority when downstream code sees both flags true;
- campaign/save consumption;
- multiplayer/network behavior;
- owner-private script behavior;
- platform presentation implementation.

## Validation

- fresh complete Windows GCC 15.2/Ninja build;
- `108/108` GCC CTests;
- fresh complete Windows MSVC 19.51/Ninja build;
- `108/108` MSVC CTests;
- clang-format check for all new C++ sources;
- Clang 22.1.8 warnings-as-errors syntax check;
- Ghidra reports for all sixteen selected `AfEngine.dll` functions with no
  unresolved address;
- sixteen matching Rizin 0.9.1 boundaries;
- all three reverse-engineering wrapper suites;
- `12/12` Rizin exporter tests;
- synthetic public-boundary tests and the `561`-file repository scan;
- `313` unique function-catalog rows with fourteen columns and only the two
  known historical missing references;
- changed-document links, changed-addition local-path scanning, reserved-ID
  scanning, and `git diff --check`;
- no original executable, script, asset, local path, or tool database added to
  Git.

Independent review's sole P2 confidence overclaim was corrected by capping all
static-only `EV-20260730-005` claims at medium/`2`; final re-review reports GO
with no remaining P0-P3 finding. Hosted Windows/Linux/macOS/iOS publication
gates remain required before merge.
