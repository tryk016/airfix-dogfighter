# Gameplay-camera mission runtime

**Date:** 2026-07-28

**Scenario:** `SCN-CAMERA-001`

**Status:** mission-lifetime ownership, bounded workspaces, weak iOS producer
endpoint, and Metal consumption implemented; live AirCraft producer inputs
remain gated

## Question

Can the recovered camera coordinator and packet exchange be installed as one
mission-lifetime object without borrowing a movable room member, copying the
potentially large BSP arena, allocating in the frame path, or allowing a stale
simulation endpoint to access a replacement mission?

## Ownership contract

`LegacyGameplayCameraMissionRuntime` is one non-copyable, non-moving portable
owner for:

- the authenticated `MissionWorldSpatialArena`;
- the mission runtime basis;
- one sphere-candidate record per retained polygon;
- one constraint-plane slot per retained polygon;
- the producer-side `LegacyGameplayCameraStepCoordinator`; and
- the two-slot `LegacyGameplayCameraPacketExchange`.

Metal preparation first validates the complete `LoadedMissionWorldRoom`. It
then moves the arena into the runtime and finally moves the remaining room
payload into the render snapshot. No arena vector is copied. The loader had
already charged the arena payload to the room's published CPU accounting, so
the runtime reports only its additional logical bytes: both workspaces and the
packet exchange.

The portable default admits at most 2,000,000 candidate records, 2,000,000
constraint planes, and 128 MiB of additional storage. The iOS preparation path
tightens the byte limit to the exact remainder of its existing 128 MiB private
CPU-packed budget after model and actor-pose admission. Checked multiplication
and addition run before either workspace is allocated.

The source-to-runtime basis is preflighted with the same finite,
orthonormal-axis, positive uniform-scale, inverse, and `1e-5` tolerance
requirements used by the retained static sphere adapter. An incomplete arena,
invalid basis or initial room, exceeded limit, failed initialization/exchange,
or allocation failure publishes no runtime and leaves the previous render
snapshot untouched. The loaded-room publication gate remains responsible for
full arena topology authentication before this ownership transfer.

## Producer and render behavior

Creation performs every allocation, initializes camera0 through the
coordinator, constructs the exchange from that complete packet, and reacquires
generation one as a consistency check. After handoff:

- one simulation producer may call `tryAdvance`;
- one Metal consumer may call `tryAcquire`;
- both operations are bounded, `noexcept`, and allocation-free;
- coordinator failure never reaches the exchange; and
- exchange `busy` means that the authoritative camera state advanced but this
  visual packet was dropped, allowing a later non-contiguous camera generation
  to publish.

Any transport rejection other than `busy` is surfaced separately from a
coordinator rejection. The runtime never rolls back a coordinator state that
was already committed before an exchange publication attempt.

## Weak endpoint and replacement safety

The Objective-C mission snapshot strongly owns the runtime. Before the
main-thread room commit, the renderer validates and returns a
`weak_ptr<LegacyGameplayCameraMissionRuntime>`. The controller installs that
weak endpoint beside the render snapshot, spawn pose, actor-pose endpoint, and
fresh deterministic input state without dispatching a callback between
validation and commit.

Replacing the mission releases the old snapshot's strong owner and makes its
weak endpoint expire. If the producer had already locked the weak endpoint, the
temporary strong reference remains safe because the runtime itself owns the
arena and basis. A Metal packet lease has a narrower guarantee: it retains the
exchange storage and its exact immutable packet even after the runtime facade
is destroyed.

## Deliberate producer gate

The iOS controller stores and validates the endpoint but does not yet advance
it from the 60 Hz input consumer. The current deterministic aircraft state
retains input intentions only and cannot supply all of:

- the distinct live AirCraft chase position;
- the distinct live vehicle world anchor;
- the live vehicle rotation;
- recovered vehicle field `+0x98`;
- recovered vehicle flag `+0x460`; and
- the AirCraft scheduler delta in seconds.

Using the authenticated spawn pose for both positions, inventing gate values,
or substituting `1/60` for the confirmed 12 ms scheduler clock would create
visible behavior without evidence. Metal therefore continues to render the
explicit camera0 bootstrap until the flight runtime supplies the complete
input contract.

## Validation

Synthetic portable tests cover:

- exact workspace and additional-byte admission;
- incomplete arena, invalid basis or room, count limits, and byte limit;
- coherent camera0 initialization;
- complete coordinator-to-exchange publication;
- mode input reaching the published packet generation;
- two retained packet slots and an accepted state with a dropped busy packet;
- a later non-contiguous generation after that busy result;
- coordinator rejection preserving both state and published packet;
- weak endpoint expiration while an acquired packet lease remains readable;
  and
- 4,096 successful advance/acquire cycles with zero counted steady-state
  allocations.

A local Release Ninja/GCC 15.2 build passes all 68 portable tests. The
regenerated code-intelligence build compiles the new target, and clangd 22
reports zero errors in the runtime and its test. Objective-C++ ownership and
both unsigned iOS variants remain mandatory gates in the repository's Apple CI
matrix. No original game content is required.

## Result

The ownership and replacement boundary is complete. The next camera task is no
longer infrastructure: it is to feed the existing weak endpoint from a
recovered live aircraft runtime, using the confirmed seconds-based scheduler
delta without resampling the input pump. Dynamic-object BSP, transparent
collision portals, runtime traces, and physical-device acceptance remain
separate parity work.

## Confidence

Confidence is **3/3** for mission ownership, workspace admission, weak endpoint
expiration, SPSC lifetime, busy-packet semantics, and allocation-free portable
operation.

Confidence remains **2/3** for complete gameplay-camera parity because the
producer is deliberately inactive until the missing live aircraft fields are
available.

## Related material

- [Gameplay-camera step coordinator](EXP-20260728-021-camera-step-coordinator.md)
- [Gameplay-camera packet exchange](EXP-20260728-022-camera-packet-exchange.md)
- [Camera refresh time contract](EXP-20260728-023-camera-refresh-time-contract.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
