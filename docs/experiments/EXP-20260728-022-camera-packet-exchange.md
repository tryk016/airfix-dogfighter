# Gameplay-camera packet exchange

**Date:** 2026-07-28

**Evidence:** `EV-20260728-008`

**Scenario:** `SCN-CAMERA-001`

**Status:** bounded SPSC exchange and Metal consumption implemented; the
simulation producer endpoint and recovered aircraft inputs remain separate

## Question

Can the simulation eventually publish a complete gameplay-camera packet to
Metal without exposing a torn combination of state, anchor, transform, and
projection, allocating in the frame path, or making a retained render lease
depend on a destroyed mission facade?

## Transport boundary

`LegacyGameplayCameraStateOwner` remains the producer's internal authoritative
state transaction. Its small frame snapshot is not sufficient for rendering:
Metal also needs the matching vehicle anchor, look-at transform, scalar
projection, logical canvas, and clip conversion.

`LegacyGameplayCameraPacketExchange` therefore transports the already built,
owning `LegacyGameplayCameraClipPacket`. It never reconstructs camera math and
does not accept separate fields that could come from different generations.
Construction publishes the initial packet and performs the exchange's only
allocation.

Two fixed slots alternate under a contiguous exchange generation. Each slot
uses one atomic access word:

- the high bit is an exclusive producer claim;
- the remaining bits count active render leases;
- the producer never overwrites a leased slot; and
- the consumer confirms the front generation after claiming its slot.

An acquire race returns an empty optional and the renderer skips that frame.
No partial packet or mutable view is exposed.

## Camera and exchange generations

The packet already contains its recovered camera publication generation and
simulation step. Both must strictly increase for every accepted publication.
The exchange also owns a separate contiguous generation used only to select
and confirm its two slots.

Camera generations need not be contiguous at this transport boundary. If a
retained render lease makes the target slot busy, the producer may drop that
render packet and later publish a newer complete camera generation. Requiring
the next exact camera generation would permanently wedge the exchange after a
single legitimate busy result because the camera coordinator has already
committed its state.

Rejected stale steps, stale camera generations, exhausted exchange generation,
and busy slots leave the published packet and the exchange's last-accepted
metadata unchanged.

## Lifetime

The exchange facade owns private shared storage. A move-only lease retains that
storage until it releases its reader claim, so a lease remains readable even
if it outlives the facade. Creating and destroying the facade remain lifecycle
operations requiring producer/consumer quiescence; normal publication and
acquisition are bounded SPSC operations.

The iOS mission resource snapshot now strongly owns one exchange instead of a
standalone optional bootstrap packet. Snapshot preparation:

1. builds the same validated camera0 bootstrap;
2. accounts the complete two-slot exchange storage under the private retained
   CPU ceiling;
3. constructs the exchange and reacquires its initial packet as a consistency
   check; and
4. commits the exchange with the rest of the immutable mission resources.

For each private-room draw, Metal acquires one lease before reading any camera
field and retains it through command encoding. Snapshot replacement keeps the
old exchange alive for an in-flight draw. The current simulation does not yet
receive a producer endpoint, so the only packet published on iOS remains the
explicit camera0 bootstrap.

## Validation

Portable tests cover:

- immediate acquisition of the initial packet and retained-storage accounting;
- strict simulation-step and camera-generation monotonicity;
- preservation of the previous packet after rejected publications;
- two simultaneously retained slots and a fail-closed busy result;
- publication of a later non-contiguous camera generation after a dropped busy
  packet;
- move-only lease behavior and a lease outliving the exchange facade;
- 4,096 packet build/publish/acquire cycles with zero counted hot-path
  allocations; and
- 20,000 concurrent SPSC publications with no torn, regressed, or lost final
  packet.

Release and regenerated code-intelligence builds pass all 67 portable tests.
Focused clangd 22 checks report zero errors in the exchange and its test.
Objective-C++/Metal compilation requires the unsigned Apple CI matrix.

## Remaining join

This step deliberately does not publish guessed camera movement. The next
producer boundary still needs:

- mission-lifetime ownership of the camera coordinator, immutable spatial
  arena reference, runtime basis, and preallocated collision workspaces;
- a weak producer endpoint installed in the same atomic mission commit as the
  render snapshot;
- the distinct live AirCraft chase position and vehicle anchor;
- the two recovered AirCraft factor gates; and
- an evidence-backed physical meaning for the raw factor-refresh argument.

Only after those values exist may the simulation advance the coordinator and
offer its successful packet to this exchange.

## Confidence

Confidence is **3/3** for the transport's packet atomicity, SPSC ownership,
lease lifetime, generation policy, bounded behavior, allocation-free hot path,
and current Metal consumption.

Confidence remains **2/3** for the complete runtime camera join because its
producer inputs and physical-device acceptance are not yet available.

## Related material

- [Camera state owner](EXP-20260728-018-camera-state-owner-snapshot.md)
- [Retained-static pose snapshot](EXP-20260728-019-camera-retained-static-pose-snapshot.md)
- [Metal gameplay-camera packet](EXP-20260728-020-metal-gameplay-camera-packet.md)
- [Camera step coordinator](EXP-20260728-021-camera-step-coordinator.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
