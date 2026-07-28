# Gameplay-camera static collision state integration

**Date:** 2026-07-28

**Evidence:** `EV-20260728-004`

**Scenario:** `SCN-CAMERA-001`

**Status:** implemented for retained static room BSP; mutable runtime owner and
dynamic-object BSP remain separate

## Question

Can the recovered sphere resolver, collision-factor response, and portal-room
proposal be joined without ever exposing a corrected position, reduced factor,
or intermediate room by itself?

## Evidence boundary

The native `AfVehicle::ProcessEvent` event-5 path establishes this order:

1. place a sphere at the smoothed camera candidate;
2. set its radius to `nearClipping * 1.1F`;
3. resolve room and enabled dynamic-object contacts;
4. reduce each persistent axis factor by twice the absolute correction,
   clamping only the lower bound to zero;
5. trace the old camera position to the corrected position through portals;
6. publish the corrected position and resulting room before later line
   occlusion and look-at work.

The static contact implementation is already closed by
[EXP-20260728-016](EXP-20260728-016-camera-static-sphere-resolver.md).
The axis-factor arithmetic and raw post-collision portal-call argument are
recorded in
[EXP-20260727-010](EXP-20260727-010-gameplay-camera-modes.md). The third portal
float is unused by the recovered room backend, so the portable trace API
intentionally does not transport it.

The retained arena does not yet contain moving-object BSP state. This
integration therefore claims the confirmed ordering only for static room
geometry.

## Portable state and proposal

`LegacyGameplayCameraStaticCollisionState` joins:

```text
roomState.runtimeWorldPosition
roomState.worldRoomIndex
axisFactors.x/y/z
```

`proposeLegacyGameplayCameraStaticCollisionState` accepts the current complete
state, a smoothed runtime-space candidate, near clipping, the mission basis,
and caller-owned candidate/constraint workspaces. It then:

1. derives the exact binary32 legacy sphere radius;
2. resolves the candidate against retained static BSP in the current room and
   every visible type-zero sphere portal room reached within the configured
   bound;
3. reduces the current axis factors from the difference between the original
   candidate and corrected centre after removing the mission's uniform runtime
   unit scale;
4. traces the segment from the current published position to that corrected
   centre through the portal BSP; and
5. returns one proposed state containing the corrected centre, final room, and
   all three reduced factors.

The existing `proposeLegacyGameplayCameraRoomState` remains available as the
narrow portal-only primitive and is not behaviorally changed.

## Atomic failure contract

The integrated result exposes a proposed state only when every stage
succeeds. In particular:

- sphere candidate or constraint workspace exhaustion publishes nothing;
- an invalid arena, room, basis, near value, factor, or intermediate result
  publishes nothing;
- sphere portal-room, BSP traversal, or portal transition limit failure
  publishes nothing;
- an out-of-segment portal hit publishes nothing; and
- a later portal failure cannot leak the factor reduction or corrected centre.

Optional nested sphere and room results are diagnostics showing which stages
were reached. They are not independently publishable state. The input object
and arena remain read-only. Atomicity at this layer is semantic: the
single-writer owner documented in
[EXP-20260728-018](EXP-20260728-018-camera-state-owner-snapshot.md) copies the
complete returned value under its synchronization protocol.

## Ordering consequence

Portal tracing deliberately consumes `sphereCollision.resolvedCenter`, not the
uncorrected candidate. A correction can therefore keep an endpoint on the
current side of a portal that the raw candidate crossed, or it can still
permit a transition while publishing the corrected endpoint in the target
room. Synthetic fixtures cover both cases.

This is also why collision resolution cannot be bolted on after the existing
room proposal: doing so would select the room from the wrong segment.

Axis factors retain runtime axis order so they remain compatible with the
portable chase recurrence. Their reduction is nevertheless measured in legacy
world-distance units: the integration divides the runtime correction by
`runtimeUnitsPerSourceUnit` before applying the recovered formula. A coordinate
unit conversion therefore cannot change the dimensionless response.

## Safety policy

The operation is allocation-free and `noexcept`. It retains two independent
bounds:

- at most 256 sphere-discovered rooms, including the starting room; and
- at most 256 completed line-portal transitions.

The caller selects lower operational values through
`LegacyGameplayCameraStaticCollisionOptions`. Workspace exhaustion and limit
violations are typed failures with no proposal. The sphere stage continues to
require an orthonormal basis plus positive uniform unit scale; a basis that
would transform the sphere into an ellipsoid fails closed.

## Validation

Public synthetic tests cover:

- static face correction before room proposal;
- exact use of `nearClipping * 1.1F`;
- all-three-axis factor publication with a lower-clamped corrected axis;
- factor invariance under a non-unit runtime/source unit scale;
- a correction that prevents the raw candidate's portal transition;
- a correction followed by a valid transition into the target room;
- candidate and constraint workspace exhaustion;
- portal transition-limit failure after a successful sphere stage;
- invalid factor and near input with stage-presence diagnostics;
- preservation of the caller's complete input state; and
- 4,096 allocation-counted integrated proposals.

The fixtures require no original game data.

A fresh Release/Ninja build completed all 205 steps and the complete 62-test
portable suite passes. The code-intelligence preset and its same 62-test suite
also pass. Cross-platform CI and unsigned iOS validation are performed before
this stage is merged.

## Result and next boundary

The project now has one portable all-or-nothing proposal for the recovered
static portion of event-5 camera collision. A single-writer simulation/camera
owner now applies this complete state and publishes an immutable frame
snapshot. The next boundary is to assemble a complete gameplay-camera pose for
one acquired snapshot before Metal consumes it.

Dynamic-object BSP collision and controlled executable traces remain required
before full gameplay parity can be claimed.

## Confidence

Confidence is **3/3** for the portable stage ordering, all-or-nothing proposal,
static solver/factor/portal composition, and typed resource failures.

Confidence remains **2/3** for complete event-5 gameplay parity because moving
object BSP queries and controlled native multi-room traces are still pending.

## Related evidence

- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Camera room-state proposal](EXP-20260728-013-camera-room-state-proposal.md)
- [Sphere-contact recovery](EXP-20260728-014-camera-sphere-contact-recovery.md)
- [Constraint solver](EXP-20260728-015-camera-constraint-solver.md)
- [Static sphere resolver](EXP-20260728-016-camera-static-sphere-resolver.md)
- [Camera state owner and snapshot](EXP-20260728-018-camera-state-owner-snapshot.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
