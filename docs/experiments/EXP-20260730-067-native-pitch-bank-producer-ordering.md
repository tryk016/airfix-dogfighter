# EXP-20260730-067: native pitch/bank producer ordering

**Date:** 2026-07-30
**Evidence ID:** `EV-20260730-003`
**Status:** complete static producer and scheduler report; no runtime code changed
**Decision:** GO for preserving the recovered immediate per-event order at an
already-formed event boundary; NO-GO for a full live-input integration that
adds batching, deduplication, fixed-rate sampling, replay after inactivity, or
an assumed one-sample-per-12-ms clock
**Reference build:** hashes below and `docs/evidence/source-manifest.sha256`

## Result

The native producer path is synchronous and more ordered than the earlier
consumer-only report could prove:

```text
one application-loop iteration
  -> drain all currently obtainable native input events
       -> repeated manager requests, each restarting at joystick
            -> one joystick snapshot: BANK raw event, then PITCH raw event
            -> after snapshot exhaustion: mouse, then one keyboard record
            -> after any returned event: restart again at joystick
       -> each mapped SET calls NfEvent::Process immediately
  -> publish QPC-derived integer milliseconds with NfMain::SetTime
       -> PollRemote
       -> refresh every due time dependant, including zero or more 12 ms
          AirCraft steps
            -> near the end of a vehicle step, AfAI::Refresh may run
                 -> AI control calculation
                 -> PITCH_SET, then BANK_SET, each processed immediately
  -> publish real time
  -> emit the render event
```

This is code order, not a wall-clock-rate claim. Input is drained once per
outer application iteration, before `SetTime`. The time heap may run zero, one,
or several 12 ms vehicle refreshes for that one publication. It therefore does
not sample input once per 12 ms and does not reset or interpolate pitch or
bank between events.

The strongest safe integration decision is narrow:

- **GO** for forwarding each already-formed native SET to the existing
  per-event reducer immediately and in the sequence recovered here;
- **NO-GO** for treating pitch and bank as one atomic sample, sorting them,
  coalescing repeated writes, replaying a producer cache on activation, or
  attaching input ownership to a renderer, scheduler, fixed-point frame, or
  fixed 60 Hz/12 ms pump.

No reducer, executable path, input integration, binary, analysis database, or
private resource was modified by this experiment.

## Sources and method

The analysis used Ghidra 12.1.2 as the primary source and Rizin 0.9.1 as an
independent instruction and function-boundary check. The game and GUI were not
run. Missing function entries were created only in a disposable ignored copy
of the Ghidra project, at entries established by vtables, switch targets, or
direct callers. Rizin automatic discovery is identified separately from an
explicit entry creation below.

| Module | Image base | SHA-256 |
|---|---:|---|
| `Dogfighter.exe` | `0x00400000` | `F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89` |
| `AfEngine.dll` | `0x10000000` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` |
| `AirCraft.type` | `0x10000000` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` |

The committed `ExportAddressReferences.java` script makes the targeted
incoming-reference step repeatable without recording a local project or
source path. The existing Ghidra instruction/decompile exporters and the
hash-verifying Rizin wrapper produced only ignored local reports. Public
conclusions below are concise reconstructions, not copied listings.

## Address and boundary ledger

VA and RVA differ by `0x00400000` for `Dogfighter.exe` and by `0x10000000`
for the two modules loaded at `0x10000000`.

| Module and working function | VA range | RVA range | Rizin discovery |
|---|---|---|---|
| `Dogfighter.exe!PollInputAndAdvanceTime` | `[0x00410FA0,0x00410FE3)` | `[0x00010FA0,0x00010FE3)` | explicit vtable entry |
| `Dogfighter.exe!AdvanceMainClock` | `[0x00410DE0,0x00410F99)` | `[0x00010DE0,0x00010F99)` | automatic |
| `Dogfighter.exe!DispatchInputEvents` | `[0x00411BB0,0x0041221A)` | `[0x00011BB0,0x0001221A)` | automatic |
| `Dogfighter.exe!ExecuteMappedInputBinding` | `[0x00412380,0x00412532)` | `[0x00012380,0x00012532)` | Ghidra direct caller/switch target |
| `Dogfighter.exe!DispatchUserCommand` | `[0x00412EA0,0x004143C1)` | `[0x00012EA0,0x000143C1)` | explicit switch entry |
| `Dogfighter.exe!NextInputEvent` | `[0x004146D0,0x004146FE)` | `[0x000146D0,0x000146FE)` | explicit vtable entry |
| `Dogfighter.exe!RegisterInputDevice` | `[0x00414860,0x00414897)` | `[0x00014860,0x00014897)` | automatic |
| `Dogfighter.exe!PollKeyboardInput` | `[0x00414A80,0x00414B38)` | `[0x00014A80,0x00014B38)` | explicit vtable entry |
| `Dogfighter.exe!PollJoystickInput` | `[0x00415210,0x004154BD)` | `[0x00015210,0x000154BD)` | explicit vtable entry |
| `Dogfighter.exe!WindowsApplicationMain` | `[0x00416090,0x00416226)` | `[0x00016090,0x00016226)` | automatic |
| `AfEngine.dll!AfAI::Refresh` | `[0x10036520,0x10036663)` | `[0x00036520,0x00036663)` | automatic |
| `AfEngine.dll!AIControls::HasChanged` | `[0x100370B0,0x100370F2)` | `[0x000370B0,0x000370F2)` | automatic |
| `AfEngine.dll!NfEngine::ProcessEvent` | `[0x10040250,0x1004203B)` | `[0x00040250,0x0004203B)` | automatic |
| `AfEngine.dll!NfMain::SetTime` | `[0x100485A0,0x10048600)` | `[0x000485A0,0x00048600)` | automatic |
| `AfEngine.dll!NfMain::SetRealTime` | `[0x10048600,0x1004874E)` | `[0x00048600,0x0004874E)` | automatic |
| `AfEngine.dll!NfTimeHeap::RefreshDependants` | `[0x1004D750,0x1004D83F)` | `[0x0004D750,0x0004D83F)` | automatic |
| `AfEngine.dll!NfEvent::Process` | `[0x10057910,0x10057A80)` | `[0x00057910,0x00057A80)` | automatic |
| `AirCraft.type!AirCraftAiRefreshStep` | `[0x10009650,0x100096BF)` | `[0x00009650,0x000096BF)` | explicit vtable entry |
| `AirCraft.type!AircraftAiControlStep` | `[0x1000A370,0x1000C41A)` | `[0x0000A370,0x0000C41A)` | automatic |
| `AirCraft.type!DispatchAiControlEvents` | `[0x1000C420,0x1000C610)` | `[0x0000C420,0x0000C610)` | explicit vtable entry |

For all Rizin rows, the normalized first and final instruction agree with
Ghidra. Rizin missed only entries that are valid code but lack an automatic
function seed; creating them at the independently evidenced vtable/switch
address produced the same instruction ranges as Ghidra.

## Synchronous event dispatch

`NfEvent::Process` is a direct dispatcher, not a producer queue:

- target class `0xF0` at `0x10057978` calls `NfPlayer::ProcessEvent`, which
  immediately forwards to the secondary actor at player `+0xDC` when present,
  otherwise the primary actor at `+0xD8`;
- a concrete actor UID, used by AI, is resolved by `NfServer::GetUID` and
  dispatched through the target's event virtual slot at `0x100579DC`;
- target class `0xFF` is delivered to `NfMain::ProcessEvent` at `0x10057A1B`.

The player target used by keyboard and analog events is `0xF0000000`. The AI
producer addresses its controlled actor's concrete UID. These routes can meet
at one actor, but that identity is conditional and is not inferred merely from
the event type.

Keyboard commands and AI call `NfEvent::Save` before `Process`; the analog
branches call `Process` without `Save`. This difference establishes a
save-before-local-dispatch order for the first two paths. Static code does not
justify a stronger claim about the network/log meaning of `Save`.

## Device-manager order

The application constructor creates the input manager and then keyboard,
mouse, and joystick devices in that order. Every device constructor calls
`RegisterInputDevice`. That function inserts at the head:

1. read manager head at `0x00414881`;
2. store the old head as the new device's next link at `0x00414886`;
3. repair the old head's back link at `0x0041488B` when non-null;
4. store the new device as manager head at `0x00414891`.

The resulting list is therefore:

```text
joystick -> mouse -> keyboard
```

`NextInputEvent` starts at manager `+0x10` on every call. It invokes device
virtual slot `+0x18` at `0x004146E2`, returns immediately after the first
device that supplies one event, and follows the device `+0x04` next link at
`0x004146E9` otherwise. `DispatchInputEvents` requests and processes one event
at a time until the manager returns false. Each new request starts at the
joystick again.

This establishes source priority among locally polled devices. It does not
establish how the DirectInput driver internally orders buffered records.

## Keyboard and command producer

`PollKeyboardInput` requests one buffered DirectInput record per manager call.
It maps the record state byte's `0x80` bit to input type 1 for press and type 3
for release. The raw record's key code and timestamp are copied into the
intermediate input record; the later SET construction does not copy or compare
that timestamp.

`DispatchInputEvents` first gives the raw event to `NfMain::Input`. If gameplay
mapping remains eligible, it calls `ExecuteMappedInputBinding`, which parses a
binding string and calls `NfConsole::Execute` synchronously in textual order.
The registered command callback reaches the four branches in
`DispatchUserCommand`:

| Command branch | VA / RVA range | Press or true | Release or false |
|---|---|---:|---:|
| `pitchup` | `[0x00413888,0x00413910)` / `[0x00013888,0x00013910)` | `PITCH_SET +32` | `-32` if down remains held, else `0` |
| `pitchdown` | `[0x00413910,0x00413998)` / `[0x00013910,0x00013998)` | `PITCH_SET -32` | `+32` if up remains held, else `0` |
| `bankleft` | `[0x00413998,0x00413A20)` / `[0x00013998,0x00013A20)` | `BANK_SET -32` | `+32` if right remains held, else `0` |
| `bankright` | `[0x00413A20,0x00413AA8)` / `[0x00013A20,0x00013AA8)` | `BANK_SET +32` | `-32` if left remains held, else `0` |

Each callback first overwrites its held byte, then constructs one SET, calls
`Save`, calls `Process`, and destroys the event. There is no equality
suppression: a repeated identical callback emits another identical SET. Held
state by itself is not re-polled into a SET on every outer loop.

Consequences:

- the separately proven keyboard payload set for each axis is exactly
  `{-32,0,+32}`;
- there is no fixed keyboard PITCH-versus-BANK order; it follows buffered
  record order and binding-string execution order;
- successive keyboard records can be separated by a fresh joystick poll
  because every manager request restarts at the joystick;
- if a command SET reaches an inactive actor, the held byte has already
  changed, but the consumer ignores the field write; activation alone does not
  replay the held value.

## DirectInput joystick producer

The upstream proof remains producer-specific. Successful device setup installs
`DIPROP_RANGE [0,10000]` for `lX` and `lY`, failing initialization if either
required property call fails. `PollJoystickInput` then obtains one 0x50-byte
state snapshot only when its persistent axis index at device `+0x1C` is zero.

That one snapshot is exposed over successive manager requests:

| Persistent index | Raw event | VA / RVA branch | Expression |
|---:|---|---|---|
| 0 | bank type `0x0C` | `[0x004152DF,0x0041531B)` / `[0x000152DF,0x0001531B)` | `(lX-5000)*255/5000` |
| 1 | pitch type `0x0E` | `[0x0041531B,0x00415358)` / `[0x0001531B,0x00015358)` | `(5000-lY)*255/5000` |
| 2-5 | other axes | later branches | not pitch/bank |
| exhausted | changed buttons, then false | `[0x0041547E,0x004154BD)` | reset index to zero |

Thus a successful snapshot produces the bank raw event before pitch. Each is
processed synchronously before the manager asks for the next event. Only after
the snapshot's axes and changed buttons are exhausted can the same manager
pass reach mouse or keyboard. A later manager request begins another joystick
poll.

The two raw values are independently proven in `[-255,+255]`, conditional on
the DirectInput device honoring the successfully installed range. No
downstream clamp can be substituted for this upstream condition.

The dispatcher applies:

- bank branch `[0x00412077,0x0041210C)`: dead zone `abs(raw)<20`, emit only
  when `abs(previousRaw-raw)>=10`, payload
  `sign(raw)*min(raw*raw/200,32)`;
- pitch branch `[0x0041210C,0x0041219E)`: the same dead zone and threshold,
  payload `sign(raw)*min(raw*raw/150,32)`.

The corresponding previous-raw global is updated before `Process`. Therefore:

- the analog SET payload for each axis is separately bounded to `[-32,+32]`;
- an identical or sub-threshold later raw sample emits no SET and the vehicle
  field holds its last processed value;
- if an emitted analog event reaches an inactive actor, the producer cache has
  already advanced, so the same raw value after activation is not replayed;
  another mapped raw change of at least ten is required.

The joystick assigns a current `GetTickCount` value to each raw axis record.
The generated SET does not carry or compare that timestamp, so it cannot be
used to reconstruct a cross-source time order.

## AI producer and its cadence

The AirCraft vehicle refresh calls `AfAI::Refresh` near its end, after the
force step, collision work, primary vtable slot 44, and cannon refresh.
`AfAI::Refresh` adds the supplied binary32 `dt` to accumulator `+0x344`,
stores it, and returns while the result is less than interval `+0x340`.
AirCraft configures that interval to exact binary32 `0.1`
(`0x3DCCCCCD`).

At the threshold, `AfAI::Refresh` invokes derived virtual slot 1 with the
accumulated value and then resets the accumulator to positive zero. It does
not subtract the interval, so overshoot is discarded. With uninterrupted
12 ms vehicle deltas, eight additions reach approximately 0.096 and the ninth
approximately 0.108 triggers the AI step: nominally every ninth vehicle
refresh, or 108 ms, not exactly every 100 ms.

`AirCraftAiRefreshStep` has this exact local order:

1. call `AircraftAiControlStep` at `0x1000969E`;
2. call primary virtual slot `+0x2C`, resolving to
   `DispatchAiControlEvents`, at `0x100096A7`;
3. increment its local cadence counter.

The control calculation writes current `AIControls` channel values.
`DispatchAiControlEvents` tests channels in order 0, 1, 3, 5, 6. For the two
axes:

| Branch | VA / RVA range | Ordered operation |
|---|---|---|
| pitch channel 1 | `[0x1000C496,0x1000C4F1)` / `[0x0000C496,0x0000C4F1)` | `HasChanged -> construct PITCH_SET -> GetRelative -> _ftol -> Save -> Process -> destroy` |
| bank channel 3 | `[0x1000C4F1,0x1000C54C)` / `[0x0000C4F1,0x0000C54C)` | `HasChanged -> construct BANK_SET -> GetRelative -> _ftol -> Save -> Process -> destroy` |

If both mapped values changed, PITCH is completely processed before BANK.
Internal channel writes inside the preceding large control calculation do not
alter this event-dispatch order.

`HasChanged` recomputes the mapped binary32 value and compares it exactly with
the channel cache at `+0x60`; it does not update the cache. `GetRelative`
updates that cache before `_ftol`, `Save`, and `Process`. `SetValue` clamps raw
AI channels to `[-100,+100]`, while the AirCraft constructor separately
configures channel 1 and channel 3 endpoints to exact binary32 `[-32,+32]`.
The AI payload bound for each axis is therefore independently proven as
`[-32,+32]`.

Repeated equal mapped binary32 values emit nothing. An inactive consumer does
not cause replay because the AI cache has already changed. Exact `_ftol`
halfway behavior remains conditional on the live x87 rounding mode, but cannot
exceed the proven endpoints.

Because AI dispatch runs after the current refresh's force calculation, an AI
SET cannot alter forces already accumulated by that same step. Its field write
and possible rest clear occur before the outer refresh returns and can affect a
later force step. If local player input and AI resolve to the same actor, a
changed AI axis can overwrite the earlier local field in that iteration;
whether they target the same actor is runtime state, not a static default.

## Application loop, scheduler, and the 12 ms interval

The application vtable at `0x004393FC` resolves outer-loop slots:

| Slot | Function | Evidence |
|---:|---|---|
| `+0x10` | Windows-message drain/continue | called at `0x004161C1` |
| `+0x14` | `PollInputAndAdvanceTime` | called at `0x004161D0` |
| `+0x18` | render-event producer | called at `0x004161DB` |

`PollInputAndAdvanceTime` calls `DispatchInputEvents` at `0x00410FA7`, performs
pending console work, and then calls `AdvanceMainClock` at `0x00410FDC`.
The clock converts QPC to integer milliseconds, calls `NfMain::SetTime`, then
`NfMain::SetRealTime`. The render-event producer runs only afterward. There is
no explicit sleep, 12 ms wait, or 60 Hz limiter in this outer loop.

`NfMain::SetTime` has the following exact order:

1. `NfServer::PollRemote` at `0x100485AC`;
2. construct target-`0xFC` event `0x4D`;
3. `Save` at `0x100485D6`;
4. `Process` at `0x100485E0`;
5. destroy the event;
6. `NfServer::Flush`.

The target-`0xFC` time event reaches `NfEngine::ProcessEvent`. Its event-`0x4D`
path calls `NfTimeHeap::RefreshDependants` at `0x10040392` for an allowed
publication. A nonnegative forward difference is skipped when it is at least
`0x2BB`, or 699 ms; smaller forward differences reach the heap. A separate
event-`0x4C` branch also refreshes at `0x100403D3`. These are exact local
branches, not a universal assertion about every target/time path.

AirCraft's `NfTimeDependant` subobject is at vehicle `+0x18`. The vehicle
constructor sets its periodic byte at dependant `+0x08` and, because AirCraft
is neither a ground nor water vehicle, writes interval 12 ms at dependant
`+0x10/+0x14`.

For a periodic dependant, `NfTimeHeap::RefreshDependants` repeatedly:

1. selects a due dependant;
2. calls secondary virtual slot 1 with its configured interval;
3. advances the dependant deadline by that interval;
4. searches again until no dependant is due.

One `SetTime` can consequently produce zero, one, or multiple 12 ms AirCraft
refreshes. A long but accepted publication catches up in 12 ms steps; a
forward gap of 699 ms or more on the shown event-`0x4D` path skips this heap
call. Multiple catch-up refreshes may occur within one wall-clock application
iteration.

The statically proven local order is:

```text
local keyboard/analog SETs
  -> PollRemote
  -> zero or more due 12 ms vehicle refreshes
       -> optional AI PITCH_SET then BANK_SET near each eligible AI step
  -> SetRealTime
  -> render event
```

Remote events processed inside `PollRemote` can occur between local input and
the scheduler. Their internal network order and relationship to `Save`/`Flush`
remain unresolved.

## Sample-and-hold, inactivity, and last-write behavior

The consumer branch contract remains the one independently established in
[EXP-20260729-056](EXP-20260729-056-native-pitch-bank-event-research.md):

| Owner/branch | VA range | RVA range | Target |
|---|---|---|---|
| `AfVehicle::ProcessEvent` | `[0x1001B6C0,0x1001FA6C)` | `[0x0001B6C0,0x0001FA6C)` | branch owner |
| `BANK_SET (0x65)` | `[0x1001E4D4,0x1001E4F5)` | `[0x0001E4D4,0x0001E4F5)` | vehicle `+0x44C` |
| `PITCH_SET (0x5F)` | `[0x1001E4F5,0x1001E514)` | `[0x0001E4F5,0x0001E514)` | vehicle `+0x448` |
| shared active tail | `[0x1001E514,0x1001E536)` | `[0x0001E514,0x0001E536)` | rest `+0x458/+0x45C` |

- packed event type DWORD at `+0x05`, signed payload DWORD at `+0x11`;
- inactive byte at vehicle `+0x460`;
- `PITCH_SET` writes `+0x448`; `BANK_SET` writes `+0x44C`;
- exact order is inactive gate, signed `FILD`, multiply, binary32 `FST`,
  extended-product zero comparison, and conditional clear of signed rest
  duration at `+0x458/+0x45C`;
- an inactive SET changes neither field nor rest;
- active zero writes positive zero and does not clear rest;
- active nonzero overwrites its selected field and clears rest;
- there is no previous-field equality test, so every processed repeated SET
  writes again;
- later SETs win independently per field;
- `PITCH_APPLY (0x60)` and `BANK_APPLY (0x66)` are no-ops along the complete
  AirCraft inheritance chain.

Fields hold their last processed SET across any number of outer iterations and
vehicle refreshes. Producer-side suppression is distinct from consumer-side
repetition:

| Producer | Repeat rule | Inactive consequence |
|---|---|---|
| keyboard command callback | every callback emits, including an equal bool value | held byte advances; no automatic replay |
| analog | emit only for mapped raw delta at least ten after dead zone | previous-raw cache advances before ignored consumer |
| AI | emit only when mapped binary32 differs exactly | relative-value cache advances before ignored consumer |

There is no per-refresh rest of pitch/bank to zero. “Rest” here means the
vehicle sleep-duration accumulator, not a control recenter operation.

## Numeric contract retained from the consumer study

Both SET branches multiply by the exact binary32 constant at
`AfEngine.dll` VA `0x1007DC48`, RVA `0x0007DC48`:

| Property | Exact value |
|---|---|
| bytes | `C5 20 30 3D` |
| binary32 bits | `0x3D3020C5` |
| hexadecimal float | `0x1.60418ap-5` |
| rational | `11542725 / 2^28` |
| decimal | `0.0430000014603137969970703125` |

The native order is signed integer conversion, x87 multiply, binary32 store,
then compare of the retained extended product. Under the explicitly
conditional startup-compatible `PC=53, RC=nearest-even` model, the exact
vectors are:

| Payload | Stored binary32 | x87 `SE:SIG` after multiply |
|---:|---:|---|
| `0` | `0x00000000` | `0000:0000000000000000` |
| `+1` | `0x3D3020C5` | `3FFA:B020C50000000000` |
| `-1` | `0xBD3020C5` | `BFFA:B020C50000000000` |
| `+32` | `0x3FB020C5` | `3FFF:B020C50000000000` |
| `-32` | `0xBFB020C5` | `BFFF:B020C50000000000` |
| `+3` | `0x3E041894` | `3FFC:841893C000000000` |
| `-3` | `0xBE041894` | `BFFC:841893C000000000` |
| `+16777217` | `0x493020C6` | `4012:B020C5B020C50000` |
| `-16777217` | `0xC93020C6` | `C012:B020C5B020C50000` |
| `+1555145203` | `0x4C7F17F4` | `4018:FF17F38000000000` |
| `-1555145203` | `0xCC7F17F4` | `C018:FF17F38000000000` |

The added vectors distinguish decimal-double `0.043`, premature conversion of
the payload to binary32, and a PC64 retained product. They remain conditional
because startup `_controlfp` evidence does not prove the live branch-time x87
control word.

## Ghidra/Rizin comparison

| Question | Ghidra 12.1.2 | Rizin 0.9.1 | Result |
|---|---|---|---|
| outer input/time/render call order | vtable and decompile join | exact instructions and automatic outer-loop boundary | agree |
| input list order | constructor/data flow and decompile | exact insert/traversal instructions | agree |
| joystick BANK then PITCH | switch reconstruction and state-index flow | explicit-entry function and jump-table instructions | agree |
| command per-callback processing | switch branches and call chain | explicit-entry switch plus direct calls | agree |
| synchronous target dispatch | decompile and target-mask switch | automatic function and calls | agree |
| scheduler catch-up and 699 ms gate | decompile, data flow, constructor join | automatic boundaries and exact compare/loop | agree |
| AI threshold and dispatch order | vtable, decompile, channel data flow | automatic/explicit-entry instruction paths | agree |

No instruction, boundary, branch target, or call-order contradiction remains
for the claims in this report. The Rizin explicit-entry cases are not
independent automatic function discovery, so confidence comes from agreement
at entries independently established by vtables, callers, and switch targets,
not from the function name itself.

## Synthetic test plan

No runtime implementation was added. A later integration must first pass a
path-free deterministic model with these cases:

1. Reconstruct device insertion `keyboard -> mouse -> joystick` and assert
   traversal `joystick -> mouse -> keyboard`; assert every manager request
   restarts at the joystick.
2. Feed one joystick snapshot and assert raw BANK before raw PITCH, then later
   axes/buttons, with only one device event returned per manager call.
3. Enumerate analog endpoints and values around dead zone `19/20` and delta
   `9/10`; assert cache-before-process, no identical repeat, and no replay
   after an inactive drop.
4. Enumerate the four command bool transitions over all opposing-held states;
   assert exactly one SET per callback, repeated-equal emission, and binding
   textual order without an invented global PITCH/BANK order.
5. Change both AI channels and assert complete PITCH
   `HasChanged/GetRelative/_ftol/Save/Process/destruct` before complete BANK;
   assert exact-equality suppression and cache-before-process.
6. Apply consumer cases for both fields: inactive, zero, nonzero, repeated
   equal write, both interleavings, last-write-wins, rest-clear decisions, and
   APPLY no-ops.
7. Model QPC publications with no due deadline, one due deadline, and five due
   12 ms deadlines; assert input fields hold and the latter invokes five
   sequential vehicle refreshes rather than one 60 ms refresh.
8. Assert an event-`0x4D` forward difference of 698 reaches the heap and 699
   does not on the recovered `NfEngine` path.
9. Feed AI `dt=0.012f` repeatedly: eight calls do not reach `0.1f`, the ninth
   triggers one derived step, and the accumulator resets rather than retaining
   overshoot.
10. Record sequence markers
    `local input -> PollRemote -> due refresh/AI -> SetRealTime -> render`;
    allow unresolved remote events only inside the explicit `PollRemote`
    interval.
11. Verify call counts: command/AI `Save` before `Process`; analog `Process`
    without `Save`; no synthetic queue, sort, merge, timestamp arbitration, or
    producer retry.
12. Re-run every exact numeric vector under the named PC53/nearest-even policy
    and negative-test decimal `0.043`, premature binary32 input conversion, and
    PC64 multiplication.

The test API must consume already ordered native event observations and
caller-owned producer state. It must not introduce Q15, `InputFrame`, a
renderer, a scheduler, or a fixed-rate input snapshot.

## Hard evidence, limits, and minimal later trace

| Claim | Confidence | Limit |
|---|---|---|
| local device priority and joystick BANK-before-PITCH | medium | DirectInput driver's internal buffer order is outside the image |
| keyboard callback and binding execution order | medium | physical key-repeat cadence is OS/driver/runtime behavior |
| AI PITCH-before-BANK and change suppression | medium | `_ftol` halfway result depends on live x87 control |
| producer-specific payload bounds | medium, analog conditional on installed range being honored | no downstream assumption replaces driver compliance |
| local-input-before-SetTime and render-after-time | medium | wall-clock iteration frequency is not encoded |
| zero/one/many 12 ms refreshes per SetTime | medium | full target-mask/network policy is not exhaustively classified |
| effective nominal AI trigger after nine 12 ms steps | medium for uninterrupted `dt=0.012f` | catch-up and skipped publications can compress or delay wall time |
| global order including remote/network events | unknown | `PollRemote` internals and transport arrival are unresolved |
| branch-time x87 control word | unknown | no runtime observation was made |

Under the project confidence rubric, independent agreement between Ghidra and
Rizin supports medium confidence because both are static analyses. Static
evidence is sufficient for immediate local call order but not for
wall-clock, device-driver, network, or live floating-environment claims. The
smallest later dynamic experiment, if those claims become required, is an
unpatched breakpoint/log trace that captures:

- outer-loop sites `0x00410FA7` and `0x00410FDC`;
- raw joystick state after `0x00415289`, input type/value/timestamp, and
  dispatcher entries;
- `PollRemote`/time dispatch at `0x100485AC` and `0x100485E0`;
- heap/vehicle refresh at `0x10040392` and within
  `NfTimeHeap::RefreshDependants`;
- consumer entry plus event target/type/payload and inactive/rest fields;
- AI calls at `0x1000969E`, `0x100096A7`, and the pitch/bank process sites;
- QPC-derived integer time, thread identity, and x87 control word.

The trace should exercise a simultaneous stick deflection, ordered key
press/release pairs, inactivity/reactivation, an AI-controlled aircraft, and a
deliberate accepted catch-up gap. It needs no patch, renderer instrumentation,
or private asset publication. This experiment intentionally did not run it.

## Validation

- The targeted Ghidra reference exporter compiled and ran headlessly against a
  valid mapped address. A real out-of-range request wrote deterministic
  `unresolvedAddresses` details and was rejected by the wrapper; the dedicated
  fake-headless regression covers the same fail-closed contract. Forty-three
  selected local Ghidra reports have no unresolved-address marker.
- All 19 normalized Rizin function boundaries occur byte-for-byte in the
  public address ledger; the separate 12-case Rizin normalizer suite passes.
- The Ghidra, verified-working-copy, and Rizin PowerShell wrapper suites pass.
- The function catalog has 287 rows, 14 columns, unique stable IDs, and unique
  module/RVA pairs; only its two pre-existing missing CC references remain.
- Synthetic public-boundary tests and the 549-file repository scan pass.
- Changed Markdown links, the changed-scope local-path scan, reserved-ID scan,
  and `git diff --check` pass.

No product source changed, so a product compile would not exercise this
evidence report or exporter beyond the dedicated checks above.

## Final recommendation

**NO-GO for full live-input integration at this point.** Static evidence is
strong enough to preserve immediate producer-to-consumer order, but a full
integration would also need an explicit decision about remote ordering,
driver behavior, wall-clock cadence, and the live x87 environment. Until that
boundary is made narrow, the safe next action is only a producer adapter that:

- forwards one already-formed SET at a time;
- preserves joystick BANK-before-PITCH and AI PITCH-before-BANK;
- preserves keyboard/binding arrival order;
- never batches, sorts, deduplicates, interpolates, or replays;
- leaves producer caches and inactive behavior with their native owners; and
- owns no input frame, renderer, scheduler, or 12 ms clock.

That adapter is a conditional **GO** only as a separately reviewed task. This
experiment does not implement it.

## Related public evidence

- [Native pitch/bank SET consumer report](EXP-20260729-056-native-pitch-bank-event-research.md)
- [Portable per-event pitch/bank reducer](EXP-20260729-060-native-pitch-bank-event-reducer.md)
- [Native discrete command producer](EXP-20260730-062-native-control-command-reducer.md)
- [Aircraft flight reconstruction boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Reverse-engineering workflow](../RE-WORKFLOW.md)
