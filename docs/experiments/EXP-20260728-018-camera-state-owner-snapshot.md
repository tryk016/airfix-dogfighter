# Gameplay-camera state owner and frame snapshot

**Date:** 2026-07-28

**Scenario:** `SCN-CAMERA-001`

**Status:** implemented as a portable SPSC publication boundary; Metal camera
consumption remains separate

## Question

Can the complete static camera proposal be made authoritative and transferred
from simulation to rendering without exposing torn position, room, axis-factor,
step, or generation fields?

## Claim boundary

This experiment adds a reconstruction architecture decision, not a claim about
the original game's threading implementation. The recovered behavior ends at
the all-or-nothing static proposal documented in
[EXP-20260728-017](EXP-20260728-017-camera-static-state-integration.md).

The port still needs a safe owner because simulation will mutate camera state
while Metal consumes a stable frame. The existing dynamic actor-pose exchange
provides a validated project-local SPSC pattern, but camera snapshots are small
fixed values and do not need a retained span or heap-backed lease.

## Published value

`LegacyGameplayCameraFrameSnapshot` contains exactly:

```text
simulationStep
publicationGeneration
state.roomState.runtimeWorldPosition
state.roomState.worldRoomIndex
state.axisFactors.x/y/z
```

Initialization publishes generation one. Every later successful commit
requires a strictly increasing simulation step and advances the generation by
one. Generations never wrap.

The producer's current snapshot is the sole authoritative camera state. A
commit accepts `LegacyGameplayCameraStaticCollisionResult`, so a failed result
or a nominally valid result without its complete proposed state cannot be
published. Position, room, and all three factors are copied as one value.

## Ownership and thread contract

`LegacyGameplayCameraStateOwner` has three phases:

1. construct an uninitialized owner;
2. initialize it once before concurrent use; and
3. hand it to exactly one simulation/camera producer and one render consumer.

After handoff:

- only the producer calls `currentSnapshot` and `tryCommit`;
- only the consumer calls `tryAcquire`;
- destruction waits until both sides are quiescent; and
- no scene rebinding or second producer is permitted.

This explicit contract avoids suggesting that arbitrary concurrent mutation is
safe. The class is neither copyable nor movable, so its synchronization
identity and slot addresses remain stable.

## Publication protocol

Two fixed slots alternate by publication generation. Each slot carries one
atomic access word:

- the high bit is an exclusive writer claim;
- the remaining values are transient reader claims; and
- zero means reusable.

The producer validates the entire candidate and step before claiming the next
slot. It then writes the complete snapshot, updates its producer-only current
value, release-publishes the new generation, and releases the slot. If the
target slot is being copied, the producer returns `busy` and changes nothing.

The consumer:

1. acquire-loads the published generation;
2. claims its selected slot unless a writer owns it;
3. acquire-loads the generation again;
4. copies the complete snapshot only if the generation and slot tag still
   agree; and
5. releases the claim before returning the owning value.

A publication race therefore returns an empty optional for that render
iteration. The returned snapshot has no reference, span, or pointer into the
owner and remains immutable after the claim is released.

This is bounded behavior with no explicit mutex, not a promise of lock-free or
wait-free progress on every C++ implementation: contention is reported rather
than spun on internally.

## Failure contract

None of the following changes the current or published snapshot:

- use before initialization;
- a failed integrated proposal;
- a missing proposed state;
- non-finite position or factors;
- negative axis factors;
- duplicate or backward simulation steps;
- exhausted publication generation; or
- a busy target slot.

An invalid initialization leaves the owner reusable for a later valid
initialization. A second successful initialization is rejected.

## Validation

Synthetic public tests cover:

- empty state before initialization;
- one complete generation-one initialization;
- invalid position and factor inputs;
- rejection of a second initialization;
- valid no-transition and transition commits;
- duplicate/backward steps and forward step gaps;
- failed, missing, and malformed proposal state;
- preservation of the exact prior frame after every rejection;
- 20,000 concurrent producer/consumer frames with cross-field coherence and
  monotonic generation checks, repeated in 100 focused stress runs; and
- 4,096 commit/acquire/current operations under allocation counting.

The fixtures contain no original game data. A fresh Release/Ninja build
completed all 208 steps and the complete 63-test suite passes on Windows. The
code-intelligence build and its same 63 tests also pass. An isolated Linux GCC
13 ThreadSanitizer build passes the focused concurrent test with
`halt_on_error`; address randomization was disabled only for that test process
to avoid WSL2's TSan shadow-memory mapping conflict. Clean multi-platform and
unsigned iOS builds are required before merge.

## Result and next boundary

The static gameplay-camera state now has one portable owner and one immutable
simulation-to-render snapshot. Metal can later consume a single acquired value
without observing a corrected position paired with an old room or factor set.

The next camera boundary is a complete gameplay-camera pose snapshot: combine
the already recovered look-at/world-to-view and projection contracts with this
state without guessing the still-missing dynamic-object collision behavior.
Actual Metal matrix/depth packaging and physical-device visual acceptance
remain separate work.

## Confidence

Confidence is **3/3** for the SPSC ownership, all-or-nothing commit, immutable
copy, bounded contention, and tested cross-field coherence.

This experiment does not change the existing **2/3** confidence for complete
native event-5 parity because dynamic-object BSP and controlled executable
multi-room traces are still pending.

## Related material

- [Camera static state integration](EXP-20260728-017-camera-static-state-integration.md)
- [Dynamic actor-pose transport](../progress/STATUS.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
