# Camera position and room-state proposal

**Date:** 2026-07-28

**Scenario:** `SCN-CAMERA-001`

**Status:** implemented portable portal-only boundary; static collision
integration completed in a follow-up and the runtime owner remains deferred

## Question

How can a future simulation/camera owner apply a candidate runtime position and
the room selected by portal traversal as one all-or-nothing state change,
without publishing an intermediate room after a later traversal failure?

## Scope

This experiment covers only the semantic join between:

- the current runtime camera position and world-room index;
- a candidate runtime position produced by camera movement;
- the strict runtime/source-world portal traversal; and
- one complete proposed `{position, worldRoomIndex}` value.

It does not itself implement the legacy sphere/contact solver, dynamic-object
collision, camera orientation, synchronization between threads, a mutable
simulation owner, Metal matrices, or native frame publication. The separate
integrated static proposal composes this narrow primitive after sphere
correction.

## Contract

`LegacyGameplayCameraRoomState` contains the two values that must remain
consistent:

```text
runtimeWorldPosition
worldRoomIndex
```

`proposeLegacyGameplayCameraRoomState` traces the complete segment from the
current position to the candidate position through the current room. It uses
the mission `BasisTransform` and the strict per-hop fraction policy already
implemented by `MissionWorldRuntimeSpatialTrace`.

A valid result is one of:

- `noTransition`: propose the candidate position in the current room; or
- `transition`: propose the candidate position in the final target room.

A completed portal cycle that returns to the starting room is a valid
`noTransition` result even when its diagnostic transition count is nonzero.
A blocked portal also keeps the current room while accepting the candidate
position, matching the recovered portal gate.

Every invalid arena, room, input, basis, traversal-depth, transition-limit, or
out-of-segment result contains no proposed state. A hit and the count of
already completed transitions may remain available only as diagnostics. In
particular, a valid first hop followed by an invalid second hop never publishes
the intermediate room.

The function is read-only, allocation-free, and `noexcept`. Its atomicity is
semantic rather than a C++ memory-order guarantee: it produces one complete
value or none. A future single-writer runtime owner must apply the returned
state under its own synchronization/publication protocol; callers must not copy
the position and room through separate observable writes.

## Portable safety policy

The caller chooses a finite transition limit no greater than the hard retained
BSP ceiling of 256. A zero limit is legal but rejects the first enabled portal
before changing rooms. Values above the hard ceiling are invalid input.

The state boundary does not clamp a portal fraction or reconstruct a hit point.
It inherits the runtime adapter's exact `[0,1]` check on every local hop. Exact
endpoint hits remain valid, while a finite legacy epsilon hit outside the
requested segment fails with `outOfSegmentHit`.

## Validation

Synthetic tests cover:

- no portal hit, a blocked first portal, and a blocked later portal that keeps
  the last completed room rather than the diagnostic hit's target;
- a one-hop transition;
- a reflected/permuted basis with non-unit scale;
- a two-hop cycle returning to the current room;
- singular basis, non-finite candidate, invalid room, and a limit above 256;
- first-hop and later-hop out-of-segment failures;
- self-portal failure after a bounded number of transitions;
- a zero transition limit failing before the first room change; and
- 4,096 allocation-counted proposal calls.

The tests retain the caller's input state and require the complete
`proposedState` to be absent on every failure. They use only synthetic geometry
and require no original game data.

## Result and next boundary

The runtime portal adapter is joined to a portable camera-position/room state
value without inventing a contact algorithm or a shared mutable owner. The
static sphere solver and axis-factor response are now composed with this
primitive by
[EXP-20260728-017](EXP-20260728-017-camera-static-state-integration.md).
The next step is to place that complete proposal behind the actual simulation
owner and its frame-publication synchronization.

Controlled executable traces are still required before declaring multi-room
camera movement parity.

## Confidence

Confidence is **3/3** for the portable all-or-nothing proposal contract and its
use of the existing strict portal adapter.

Confidence remains **2/3** for gameplay parity of pathological endpoint and
cyclic portal cases until controlled runtime traces exist. The finite limits
and strict fraction rejection are documented portable safety policies.

## Related evidence

- [Runtime/source-world spatial adapter](EXP-20260727-012-runtime-spatial-adapter.md)
- [Portal BSP line trace](EXP-20260727-011-portal-bsp-line-trace.md)
- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Static collision state integration](EXP-20260728-017-camera-static-state-integration.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
