# Runtime/source-world spatial adapter

**Date:** 2026-07-27

**Scenario:** `SCN-CAMERA-001`

**Status:** implemented portable boundary; state proposal completed separately

## Question

How can runtime camera segments query the retained source-world BSP without
changing the recovered traversal arithmetic, losing reflected/sheared basis
semantics, or allowing a legacy epsilon crossing outside the requested segment
to partially publish a room transition?

## Scope

This experiment covers only:

- runtime point conversion into the source-world BSP coordinate system;
- source hit-point and plane-normal conversion back to runtime space;
- static and portal line-query result adaptation;
- fail-closed validation of every portal hop before its room change.

It does not infer or implement sphere/contact resolution, dynamic-object
collision, camera smoothing orchestration, mutable room ownership, a Metal
camera matrix, or an iOS frame-publication endpoint.

## Coordinate contract

Let `B` be `BasisTransform::sourceToRuntime` and let `s` be the positive
`runtimeUnitsPerSourceUnit`. The existing geometry path establishes:

```text
runtimePoint = s * B * sourcePoint
sourcePoint  = inverse(B) * (runtimePoint / s)
```

The segment parameter remains invariant under this affine linear mapping. The
adapter therefore passes converted endpoints to the existing source tracer and
preserves its returned `legacyFraction`.

A plane normal is a covector, not a point or tangent. Its runtime form is:

```text
runtimePlaneNormal = transpose(inverse(B)) * sourcePlaneNormal
```

The positive uniform factor `1/s` is omitted because plane normals are
homogeneous. The adapter deliberately does not normalize this vector and names
it `runtimePlaneNormal`; a future contact solver must choose and document its
own unit/contact-facing normal policy. A reflected basis needs no extra sign
flip, and a sheared basis must not use `B * normal`.

All inputs, inverses, intermediate values, and outputs must remain finite.
Non-positive scale and singular/non-finite bases fail before the arena query.
The implementation is read-only, `noexcept`, and allocation-free.

## Epsilon-fraction policy

The recovered BSP polygon endpoint tolerance can produce a finite fraction
slightly below zero or above one. The source line tracer retains this behavior
and does not clamp. That is evidence, not a safe state-publication guarantee.

The runtime line boundary:

- transforms the authored hit point directly instead of recomputing it from a
  clamped fraction;
- retains the raw fraction and a `withinRequestedSegment` diagnostic;
- returns the typed `outOfSegmentHit` status when the fraction is outside
  `[0,1]`.

The source portal-chain API now has an opt-in
`requireHitsWithinSegment` policy. Its default remains false so the recovered
source behavior does not silently change. The runtime portal boundary always
enables it. Each local hit is checked before the mesh/type/visibility gate,
before advancing the segment start, and before changing the current room.
Failure returns no target room. A hit and the number of already completed hops
remain diagnostic only and must not be treated as partially committed state.

This check cannot be performed only on the final portal result: an earlier
invalid hop could otherwise be hidden by a later valid hit.

## Validation

Synthetic tests cover:

- identity basis and a normal in the default coordinate system;
- non-unit scale plus a sheared basis that distinguishes inverse transpose
  from direct normal transformation;
- reflected basis without a second normal flip;
- valid portal transition through a permuted/reflected basis;
- singular basis and non-finite runtime input;
- a real epsilon hit above `1.0`, retained diagnostically but rejected;
- rejection of an out-of-segment portal hit before any room change;
- rejection of a later invalid hop after an earlier valid transition, without
  publishing the intermediate room;
- preservation of the raw source portal behavior when strict mode is off;
- zero heap allocations across line and portal hot-path calls.

The tests use only synthetic geometry and require no original game data.

## Result and next boundary

The retained BSP can now be queried safely from runtime coordinates without
moving basis conversion into `assets` or introducing a dependency cycle.
`render` remains the correct layer because it already depends on `assets` and
owns `BasisTransform`.

The follow-up
[camera position and room-state proposal](EXP-20260728-013-camera-room-state-proposal.md)
now returns a complete candidate position and resolved room only after portal
resolution succeeds. It deliberately remains a pure value boundary: the
current simulation state contains input intentions but no mutable world
pose/room owner, while the iOS mission payload already owns the basis and
arena. Sphere/contact resolution and synchronized runtime publication remain
separate, unimplemented boundaries.

## Confidence

Confidence is **3/3** for the transformation mathematics and fail-closed
adapter contract: it follows the established geometry convention, is covered
by shear/reflection tests, and preserves the underlying tracer result.

Confidence remains **2/3** for the exact endpoint tolerance as gameplay
behavior until controlled executable traces cover pathological epsilon cases.
The strict portable policy is intentionally a safety boundary, not a claim
that the original game rejected those hits.
