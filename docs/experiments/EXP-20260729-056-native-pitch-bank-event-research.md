# EXP-20260729-056: native pitch/bank SET event research

**Date:** 2026-07-29
**Evidence ID:** `EV-20260729-007`
**Status:** complete static evidence report; isolated reducer implemented in
[EXP-20260729-060](EXP-20260729-060-native-pitch-bank-event-reducer.md)
**Decision:** GO for an isolated structural reducer and an explicitly labelled
startup-compatible numeric policy; NO-GO for claiming live full-domain numeric
parity, timing, or live-input integration without dynamic evidence
**Reference build:** hashes below and `docs/evidence/source-manifest.sha256`

## Question and boundary

This experiment asks what the original i386 game does with
`PITCH_SET (0x5F)` and `BANK_SET (0x65)`, which producers can emit them, and
which behavior can safely become a small portable reducer contract.

The work used existing Ghidra and Rizin analysis without running the game or
opening a GUI. Missing function entries were created only in a disposable,
ignored copy of the Ghidra project and were checked against Rizin instruction
boundaries, switch targets, vtable references, or direct callers. Raw
disassembly, decompiler output, binaries, analysis databases, and private game
assets are deliberately not part of this report or commit.

The relevant reference modules are:

| Module | Image base | SHA-256 |
|---|---:|---|
| `Dogfighter.exe` | `0x00400000` | `F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89` |
| `AfEngine.dll` | `0x10000000` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` |
| `AirCraft.type` | `0x10000000` | `9745842BDE40406390B46F935E9F12D6626675F89A8FBB303CB135D76CF7DD1E` |

The static cross-check used Ghidra 12.1.2 and Rizin 0.9.1. Tool versions are
pinned in `docs/toolchain/LOCK.md`.

## Result

Both events are active, typed overwrites in `AfVehicle::ProcessEvent`.
Their payload is a signed 32-bit integer. An active event multiplies it by the
exact binary32 constant at `AfEngine.dll` RVA `0x0007DC48`, stores a
binary32 result in one control field, and clears the signed 64-bit rest
duration only when the extended-precision product is nonzero.

The observed producer ranges are independently bounded:

| Producer | Pitch payload | Bank payload | Confidence |
|---|---:|---:|---|
| keyboard/commands | exactly `{-32, 0, +32}` | exactly `{-32, 0, +32}` | high |
| DirectInput analog | `[-32, +32]` | `[-32, +32]` | high, conditional on successful DirectInput range configuration |
| AirCraft AI | `[-32, +32]` | `[-32, +32]` | high |

These producer bounds do not narrow the native handler's accepted domain:
the handler consumes the complete signed `int32` range.

## Native event branches

`NfEvent::GetTypeName` at VA `0x10057110` identifies `0x5F` as
`PITCH_SET`, `0x60` as `PITCH_APPLY`, `0x65` as `BANK_SET`, and `0x66` as
`BANK_APPLY`. In the packed event, the unaligned event type is a DWORD at
offset `+0x05` and the payload is a signed DWORD at offset `+0x11`.

Both SET branches belong to:

| Function | VA range | RVA range | Size |
|---|---|---|---:|
| `AfVehicle::ProcessEvent` | `[0x1001B6C0, 0x1001FA6C)` | `[0x0001B6C0, 0x0001FA6C)` | `17324` bytes |

Ghidra and Rizin agree on the owner boundary and the following local branch
boundaries:

| Event | Local VA range | Local RVA range | Target field |
|---|---|---|---:|
| `BANK_SET (0x65)` | `[0x1001E4D4, 0x1001E4F5)` | `[0x0001E4D4, 0x0001E4F5)` | vehicle `+0x44C` |
| `PITCH_SET (0x5F)` | `[0x1001E4F5, 0x1001E514)` | `[0x0001E4F5, 0x0001E514)` | vehicle `+0x448` |
| shared active tail | `[0x1001E514, 0x1001E536)` | `[0x0001E514, 0x0001E536)` | rest `+0x458/+0x45C` |

The inactive latch is byte `vehicle+0x460`. The relevant instruction order is
exact:

### `BANK_SET`

1. `0x1001E4D4`: read inactive byte `+0x460`.
2. `0x1001E4DE`: if nonzero, jump to the common epilogue at `0x1001F9F7`.
3. `0x1001E4E4`: `FILD dword ptr [event+0x11]`, a signed conversion.
4. `0x1001E4E7`: `FMUL dword ptr [0x1007DC48]`.
5. `0x1001E4ED`: `FST dword ptr [vehicle+0x44C]`, without popping `ST(0)`.
6. `0x1001E4F3`: jump to the shared active tail at `0x1001E514`.

### `PITCH_SET`

1. `0x1001E4F5`: read inactive byte `+0x460`.
2. `0x1001E4FF`: if nonzero, jump to the common epilogue at `0x1001F9F7`.
3. `0x1001E505`: `FILD dword ptr [event+0x11]`, a signed conversion.
4. `0x1001E508`: `FMUL dword ptr [0x1007DC48]`.
5. `0x1001E50E`: `FST dword ptr [vehicle+0x448]`, without popping `ST(0)`.
6. Execution falls through to the shared active tail.

### Shared active tail

1. `0x1001E514`: `FCOMP dword ptr [0x1007C7BC]`, comparing the
   still-extended product with positive zero and popping it.
2. `0x1001E51A`: transfer the x87 status word to `AX`.
3. `0x1001E51C`: test the x87 equality bit.
4. `0x1001E51F`: if equal, jump to the common epilogue.
5. `0x1001E525`: clear the low rest-duration DWORD at `vehicle+0x458`.
6. `0x1001E52B`: clear the high rest-duration DWORD at `vehicle+0x45C`.
7. `0x1001E531`: jump to the common epilogue.

Consequently, the order is:

`inactive gate -> signed conversion -> multiply -> binary32 store ->
extended compare -> conditional rest clear`.

There is no equality check against the previous field value.

## Exact scale and x87 arithmetic

The scale at VA `0x1007DC48`, RVA `0x0007DC48`, has little-endian bytes
`C5 20 30 3D`:

| Representation | Exact value |
|---|---|
| binary32 bits | `0x3D3020C5` |
| C hexadecimal float | `0x1.60418ap-5` |
| rational | `11542725 / 2^28` |
| exact decimal | `0.0430000014603137969970703125` |

The comparison constant at VA `0x1007C7BC` is positive zero, bits
`0x00000000`.

Startup code at Dogfighter VA `[0x00435D5C, 0x00435D6E)` calls
`_controlfp(0x10000, 0x30000)`. This sets x87 precision control to 53 bits.
A full-image Rizin search found no later real `FLDCW` or `FNINIT` in
`Dogfighter.exe` or `AfEngine.dll`; one apparent result at `0x0041BD00` is a
misaligned byte inside the instruction beginning at `0x0041BCFF`.
`AfEngine.dll` does not import `_controlfp`.

The instruction-level path is confirmed:

1. convert the signed payload exactly with `FILD`;
2. multiply by the exact binary32 constant, promoted exactly to x87;
3. apply the live x87 precision-control mode to the product;
4. round to binary32 under the live rounding-control mode on `FST`;
5. compare the retained x87 product to positive zero.

The startup observation supports a compatibility model with precision control
53 and round-to-nearest/even. It does not prove that either control field still
has that value when an event is processed: the cited `_controlfp` call does not
set rounding control, and the static search does not exclude `AirCraft.type`,
another DLL, a runtime library, or a later `_controlfp` call changing precision
or rounding control. The vectors below are exact only under the explicit
condition `PC=53, RC=nearest-even` at the branch. They are not a dynamic
observation of the process-wide live x87 control word.

## Exact synthetic vectors

Under the explicit `PC=53, RC=nearest-even` condition, `Stored bits` are the
DWORD written by `FST`. `x87 SE:SIG` is the sign/exponent word and explicit
64-bit significand retained in `ST(0)` immediately after `FMUL`, after
precision-control-53 rounding. `x87 little-endian` is the exact ten-byte
extended representation for that compatibility model.

| Signed payload | Stored bits | Stored LE bytes | x87 `SE:SIG` | x87 little-endian |
|---:|---:|---|---|---|
| `0` | `0x00000000` | `00 00 00 00` | `0000:0000000000000000` | `00 00 00 00 00 00 00 00 00 00` |
| `+1` | `0x3D3020C5` | `C5 20 30 3D` | `3FFA:B020C50000000000` | `00 00 00 00 00 C5 20 B0 FA 3F` |
| `-1` | `0xBD3020C5` | `C5 20 30 BD` | `BFFA:B020C50000000000` | `00 00 00 00 00 C5 20 B0 FA BF` |
| `+32` | `0x3FB020C5` | `C5 20 B0 3F` | `3FFF:B020C50000000000` | `00 00 00 00 00 C5 20 B0 FF 3F` |
| `-32` | `0xBFB020C5` | `C5 20 B0 BF` | `BFFF:B020C50000000000` | `00 00 00 00 00 C5 20 B0 FF BF` |
| `+3` | `0x3E041894` | `94 18 04 3E` | `3FFC:841893C000000000` | `00 00 00 00 C0 93 18 84 FC 3F` |
| `-3` | `0xBE041894` | `94 18 04 BE` | `BFFC:841893C000000000` | `00 00 00 00 C0 93 18 84 FC BF` |
| `+16777217` | `0x493020C6` | `C6 20 30 49` | `4012:B020C5B020C50000` | `00 00 C5 20 B0 C5 20 B0 12 40` |
| `-16777217` | `0xC93020C6` | `C6 20 30 C9` | `C012:B020C5B020C50000` | `00 00 C5 20 B0 C5 20 B0 12 C0` |
| `+1555145203` | `0x4C7F17F4` | `F4 17 7F 4C` | `4018:FF17F38000000000` | `00 00 00 00 80 F3 17 FF 18 40` |
| `-1555145203` | `0xCC7F17F4` | `F4 17 7F CC` | `C018:FF17F38000000000` | `00 00 00 00 80 F3 17 FF 18 C0` |

The additional payloads distinguish common but incorrect reconstructions:

- `+3`: using decimal double `0.043` and then casting produces
  `0x3E041893`, not the conditional PC53/RN result `0x3E041894`;
- `+16777217`: converting the payload to binary32 before multiplication
  produces `0x493020C5`, not the conditional PC53/RN result `0x493020C6`;
- `+1555145203`: retaining a precision-control-64 product before the store
  produces `0x4C7F17F3`, not the conditional precision-control-53
  `0x4C7F17F4`. The hypothetical PC64 retained value is
  `4018:FF17F37FFFFFFC00`.

The negative counterparts make sign handling independently testable.

## Producer evidence

### Keyboard and command dispatcher

The command owner was recovered from switch targets missed by automatic
function discovery:

| Function | VA range | RVA range |
|---|---|---|
| command dispatcher | `[0x00412EA0, 0x004143C1)` | `[0x00012EA0, 0x000143C1)` |

Rizin switch analysis and a manually created Ghidra function agree on these
branches:

| Command branch | VA range | RVA range | Press/held | Release |
|---|---|---|---:|---:|
| `pitchup` | `[0x00413888, 0x00413910)` | `[0x00013888, 0x00013910)` | `+32` | `-32` if down remains held, else `0` |
| `pitchdown` | `[0x00413910, 0x00413998)` | `[0x00013910, 0x00013998)` | `-32` | `+32` if up remains held, else `0` |
| `bankleft` | `[0x00413998, 0x00413A20)` | `[0x00013998, 0x00013A20)` | `-32` | `+32` if right remains held, else `0` |
| `bankright` | `[0x00413A20, 0x00413AA8)` | `[0x00013A20, 0x00013AA8)` | `+32` | `-32` if left remains held, else `0` |

All four branches emit SET, never APPLY. The exact producer set for each axis
is therefore `{-32, 0, +32}`.

### DirectInput analog path

Three functions establish the upstream range and downstream mapping:

| Role | Owner VA range | Owner RVA range |
|---|---|---|
| joystick setup | `[0x00414DE0, 0x00415092)` | `[0x00014DE0, 0x00015092)` |
| joystick poll/fetch | `[0x00415210, 0x004154BD)` | `[0x00015210, 0x000154BD)` |
| input-event dispatcher | `[0x00411BB0, 0x0041221A)` | `[0x00011BB0, 0x0001221A)` |

The setup supplies a 0x50-byte absolute-axis `DIJOYSTATE` data format. It
sets `DIPROP_RANGE` to `[0, 10000]` with `DIPH_BYOFFSET` for at least offsets
`0` (`lX`) and `4` (`lY`); failure of either required call releases the
device and fails initialization. The exact property structure is
`dwSize=24`, `dwHeaderSize=16`, `dwObj=0` then `4`, `dwHow=1`,
`lMin=0`, `lMax=10000`.

After a successful poll and 0x50-byte state fetch, the poller emits:

| Axis branch | VA range | RVA range | Exact raw expression | Proven raw range |
|---|---|---|---|---:|
| bank input `0x0C` | `[0x004152DF, 0x0041531B)` | `[0x000152DF, 0x0001531B)` | `(lX - 5000) * 255 / 5000` | `[-255, +255]` |
| pitch input `0x0E` | `[0x0041531B, 0x00415358)` | `[0x0001531B, 0x00015358)` | `(5000 - lY) * 255 / 5000` | `[-255, +255]` |

The dispatcher branches are bank
`[0x00412077, 0x0041210C)` / `[0x00012077, 0x0001210C)` and pitch
`[0x0041210C, 0x0041219E)` / `[0x0001210C, 0x0001219E)`. Both first replace
`raw` with zero when `abs(raw) < 20`, then emit only when
`abs(previousRaw - raw) > 9`:

- bank payload: `sign(raw) * min(raw^2 / 200, 32)`;
- pitch payload: `sign(raw) * min(raw^2 / 150, 32)`.

This proves `[-32, +32]` separately for both analog producers; the bound is
not inferred from the native consumer. The code contains no post-read clamp,
so the upstream proof remains conditional on DirectInput honoring the
successfully installed property contract. That API/driver compliance is the
only remaining analog-range caveat.

The emission threshold is also observable sample-and-hold behavior: when no
event is emitted, the vehicle field retains its previous value. A return from
a previously emitted nonzero magnitude to the dead zone normally emits zero
because the previous raw magnitude is at least 20.

### AirCraft AI

The AI producer has distinct configuration and dispatch owners:

| Role | Owner VA range | Owner RVA range |
|---|---|---|
| AirCraft AI constructor | `[0x10009440, 0x1000961E)` | `[0x00009440, 0x0000961E)` |
| AirCraft AI dispatcher | `[0x1000C420, 0x1000C610)` | `[0x0000C420, 0x0000C610)` |

The constructor embeds `AIControls` at AirCraft offset `+0x364`. Its channel-1
pitch range is configured to exact binary32 `[-32, +32]` in
`[0x10009483, 0x10009497)`, and channel-3 bank receives the same endpoints in
`[0x10009497, 0x100094AB)`.

The pitch dispatch branch is `[0x1000C496, 0x1000C4F1)` /
`[0x0000C496, 0x0000C4F1)`; the bank branch is
`[0x1000C4F1, 0x1000C54C)` / `[0x0000C4F1, 0x0000C54C)`. Each checks
`HasChanged`, calls `GetRelative`, converts through `_ftol`, saves the event,
and processes the corresponding SET.

`AIControls::SetValue` and `AddValue` clamp the raw channel to
`[-100,+100]`; `GetRelative` maps that interval into the configured channel
range; `HasChanged` compares mapped binary32 values exactly. Thus pitch and
bank AI payloads are independently bounded to `[-32,+32]`. Which intermediate
integers occur, especially at `_ftol` halfway cases, depends on AI inputs and
the live x87 rounding-control mode; this does not weaken the endpoint bound.

## State semantics

The two target fields are initialized to positive zero by the vehicle
constructor and are independent:

| Scenario | Field write | Rest-duration effect |
|---|---|---|
| inactive latch nonzero | none | none |
| active payload zero | write `+0.0f`, bits `0x00000000` | none |
| active nonzero payload | overwrite selected field | clear `+0x458/+0x45C` |
| repeated active zero | repeat the zero overwrite | none |
| repeated active nonzero | repeat the overwrite | clear rest again |
| multiple SETs for one field | every active SET writes | last processed SET wins |
| interleaved pitch and bank | each writes only its own field | every processed nonzero SET clears shared rest |

The rest clear is synchronous in event processing. A nonzero active SET wakes
the vehicle before the next refresh can take its sleeping branch. Zero does
not clear the accumulator, even though it overwrites the selected field.

The rest accumulator is a signed 64-bit millisecond duration. Construction
sets it to 1999 ms; active refreshes accumulate the signed scheduler delta
only while all five control floats are exactly zero and the other rest
conditions hold. The threshold-crossing refresh at 2000 ms still executes its
active work before resetting rigid-body dynamic state. A later sleeping
refresh skips the active physics branch.

### SET versus APPLY

`PITCH_APPLY (0x60)` and `BANK_APPLY (0x66)` have no explicit cases in the
complete `AirCraft -> AfVehicle -> NfActor -> NfTypeInstance` dispatch chain.
For an AirCraft instance they do not write `+0x448/+0x44C` and do not clear
the rest duration. This is a confirmed AirCraft-chain no-op, not a universal
claim about every actor type.

SET is therefore an absolute overwrite in this chain, not a delta or rate.

## Timing and sample-and-hold limits

Static evidence proves field persistence and per-event order, but does not
prove the following:

- the phase of command, analog, AI, saved, or network event processing
  relative to the nominal 12 ms AirCraft dependant refresh;
- whether producer queues batch, coalesce, reorder, or preserve timestamps
  across sources;
- whether more than one producer write is observed between force steps;
- that one sample is taken per nominal dependant slot;
- the complete target-mask, forward-gap, skip, or catch-up policy;
- that the signed scheduler delta is always 12 ms in wall-clock time;
- runtime x87 precision- or rounding-control changes made outside the
  inspected images;
- exact `_ftol` halfway results if that rounding control changes; or
- network/multiplayer ordering around `Save` and `Process`.

The demonstrated rule is only: fields persist until a later active SET, and
the last event actually processed for an axis supplies the value read later.
There is no per-12-ms reset, interpolation, or resampling in these branches.
The 12 ms value is a nominal dependant interval; event timestamps and a
wall-clock sample-and-hold duration were not recovered. A controlled runtime
trace would be required to close those questions, and was intentionally not
performed in this static experiment.

## Synthetic test plan

No reducer was implemented during this static-evidence experiment. The
following tests formed the acceptance boundary for the later implementation
recorded in
[EXP-20260729-060](EXP-20260729-060-native-pitch-bank-event-reducer.md).

### Native branch tests

Run the same cases for pitch and bank, asserting that the other field remains
bit-identical:

1. inactive plus each of `0`, `+1`, and `-1`: no write and no rest clear;
2. active zero from a nonzero prior field: write bits `0x00000000`, retain
   the exact prior signed 64-bit rest duration;
3. active nonzero: under the selected PC53/RN compatibility policy write the
   table bits, and independently clear both rest DWORDs;
4. two equal nonzero events: two writes and two clear decisions, with no
   equality suppression;
5. nonzero then zero: zero is the last value, while only the first event
   clears rest;
6. pitch and bank interleavings in both orders: independent fields and
   last-processed-write wins per field;
7. APPLY event values: no field or rest mutation for the AirCraft chain.

### Numeric conformance

If the startup-compatible PC53/RN policy is implemented, assert for every
exact-vector row:

- signed `int32` interpretation;
- precision-control-53 product representation;
- stored binary32 bits;
- zero/nonzero rest-clear predicate from the retained product.

Include explicit negative tests against the three wrong reconstructions:
decimal double `0.043`, premature binary32 payload conversion, and
precision-control-64 multiplication.

### Producer tests

- command state table: enumerate press, release, and opposite-held cases and
  prove the set `{-32,0,+32}`;
- analog setup: fail initialization if required X or Y range property setup
  fails;
- analog property tests: enumerate raw endpoints and values around dead-zone
  `19/20` and emission delta `9/10`; assert both payload formulas remain in
  `[-32,+32]`;
- AI property tests: raw endpoints `-100/+100`, clamping outside that range,
  channel endpoints, exact `HasChanged` suppression, and `_ftol` conversion;
- native-domain tests: retain the discriminator payloads outside producer
  ranges as conditional PC53/RN compatibility tests, because the consumer
  accepts full signed `int32` even though its live control word is unproven.

### Timing non-claims

Unit tests may model an already ordered sequence of native events. They must
not manufacture a 12 ms scheduler, assume one input sample per step, or claim
cross-producer timestamp order. Those require a separate, dynamically
evidenced integration contract.

## Implementation recommendation

**GO**, in a later task, for a pure per-event structural reducer whose input is:

- event kind (`PITCH_SET` or `BANK_SET`);
- signed `int32` payload; and
- current inactive state.

Its output should identify the selected field, whether the caller must clear
the rest duration, and either the signed payload or binary32 bits produced by
an explicitly selected numeric policy. The state owner should apply the field
write and optional rest clear atomically in event order.

For an explicitly named startup-compatible `PC=53, RC=nearest-even` policy, a
portable implementation can bit-cast the exact binary32 constant, promote that
value and the signed payload to binary64, multiply in binary64, then convert
the result to binary32. It must not use decimal double `0.043` or convert the
payload to binary32 before multiplication. That policy is deterministic and
matches the startup observation, but it must not be labelled proven live
numeric parity until a controlled trace captures the x87 control word at event
processing.

The proposed reducer boundary deliberately owns no fixed-point input format,
input snapshot, rendering object, clock, or scheduler. It does not infer
producer provenance or restrict the native signed payload domain.

**NO-GO** for hardcoding the conditional PC53/RN vector table as proven live
full-domain parity, or for integrating producer timing, queue ordering, or
nominal 12 ms sampling, until those questions receive separate evidence.

## Confidence and unresolved questions

| Claim | Confidence | Basis |
|---|---|---|
| SET owner, branch bounds, signed payload, target fields, instruction order | high | exact Ghidra/Rizin instruction agreement |
| exact constant | high | raw bytes and exact integer derivation |
| vector outputs conditional on live PC53 round-to-nearest/even | high within condition | exact integer derivation; condition not dynamically observed |
| keyboard producer sets/ranges | high | complete four-branch command state table |
| analog bounds | high with stated API condition | upstream range installation, poll conversion, downstream clamps |
| AI bounds | high | configured ranges plus clamped `AIControls` mapping |
| APPLY no-op on the AirCraft inheritance chain | high | complete chain inspection |
| process-wide live x87 precision and rounding control | low/unknown | startup observation and partial static image search; no branch-time observation |
| cross-producer ordering and 12 ms timing | low/unknown | no static queue/timestamp contract or runtime trace |

Unresolved work is intentionally limited to dynamic precision- and
rounding-control provenance, DirectInput driver conformance, cross-source queue
order, event/refresh phase, and the real clock behavior around the nominal
12 ms dependant interval.

## Related public evidence

- [Portable pitch/bank event reducer](EXP-20260729-060-native-pitch-bank-event-reducer.md)
- [Aircraft flight reconstruction boundary](../re/systems/AIRCRAFT-FLIGHT.md)
- [Vehicle rest/sleep gate](EXP-20260728-026-vehicle-rest-sleep-gate.md)
- [Static Ghidra/Rizin cross-check](EXP-20260727-001-static-tool-crosscheck.md)
