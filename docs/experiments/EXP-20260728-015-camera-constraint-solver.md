# Gameplay-camera constraint solver

**Date:** 2026-07-28

**Evidence:** `EV-20260728-002`

**Scenario:** `SCN-CAMERA-001`

**Status:** constraint-plane admission, override selection, and movement
projection implemented; sphere contact generation remains separate

## Question

What are the exact missing predicate and thresholds used after the gameplay
camera chooses a penetrating sphere contact, and can that bounded stage be
transferred without guessing the still-incomplete sphere-versus-triangle
selector?

## Addressed evidence

Fresh Ghidra 12.1.2 Headless reports were generated for:

| Function | RVA | Native role |
|---|---:|---|
| `CcConstraint::AddPlane` | `0x00025ED0` | suppress a near-duplicate normal or prepend a new plane |
| `CcConstraint::AttemptMove` | `0x00025F40` | select active constraints and project the requested correction |
| `CcConstraintPlane::Overrides` | `0x00026230` | compare two opposing planes for the current movement |

The reports also exported the exact bytes at the four constants used by this
stage:

| Use | Native value | Exact bits |
|---|---:|---:|
| vector normalization numerator | `1.0F` | `0x3F800000` |
| opposition boundary | `0.0F` | `0x00000000` |
| two-plane cross-length-squared cutoff | `0.0001` (`double`) | `0x3F1A36E2EB1C432D` |
| duplicate-plane dot cutoff | `0.999` (`double`) | `0x3FEFF7CED916872B` |

Rizin 0.9.1/rzpipe 0.6.2 independently found
`CcConstraintPlane::Overrides` at RVA `0x00026230`, with the exact automatic
range `[0x10026230, 0x100262AF)`, 127 bytes, no calls, two explicit stack
arguments, and `RET 8`. Rizin again labels the exported member function
`cdecl`; the decorated symbol, ECX receiver, and stack cleanup establish the
Ghidra `__thiscall` signature.

No original binary, generated report, project database, or local path is
committed.

## Plane admission

`CcConstraint::AddPlane` walks the linked list in head-first order. It returns
without adding the candidate when any existing normal satisfies:

```text
dot(existing, candidate) > 0.999
```

The comparison is strictly greater. A distinct candidate is allocated and
prepended, so the native list is newest first. The portable primitive receives
an explicit head-first span and returns whether the candidate should be
retained; ownership and capacity remain the caller's responsibility.

## Override predicate

For receiver plane `A`, other plane `B`, and requested movement `m`,
`CcConstraintPlane::Overrides` computes:

```text
projected = dot(B, m)
residual  = m - projected * B
result    = dot(A, residual) < 0
```

This predicate is directional. `A` can override `B` while `B` does not
override `A`.

The x87 implementation spills the projected X/Y components and X/Y residuals
to binary32 while the Z path remains on the x87 stack. The portable
implementation uses widened `double` staging with the same explicit binary32
spill points. This is a controlled approximation of 80-bit x87 intermediates,
not a claim that every adversarial subnormal produces identical bits on all
modern targets.

## Active-plane order

`AttemptMove` first scans the constraint list newest first. A plane becomes
active only when:

```text
dot(requestedMove, plane) < 0
```

Each active plane is prepended to a temporary chain. Selection therefore
starts with the oldest active plane and proceeds toward the newest. The
portable span traversal reproduces this reversal without allocating or
mutating the input planes.

The first active plane becomes `primary`. Each later candidate is compared in
both directions with `primary`:

- candidate only overrides primary: candidate replaces primary;
- both override: candidate becomes the secondary plane;
- a retained secondary is compared in both directions as well;
- three mutually overriding selections stop movement immediately.

This is the literal native branch structure, not a generic iterative
half-space solver.

## Projection outcomes

The native and portable stages have four results:

1. no active plane: return the requested movement unchanged;
2. one selected plane: remove the movement component along that normal;
3. two selected planes: project movement onto their cross-product direction;
4. conflicting planes: return zero movement.

For two planes, the three cross components are spilled to binary32. If:

```text
crossLengthSquared < 0.0001
```

movement stops. The comparison is strict. Otherwise the cross direction is
normalized unless its squared length is exactly `1.0`, and the requested
movement is projected onto that direction.

The native code assumes unit plane normals. The portable helpers preserve the
supplied magnitudes instead of silently normalizing or assigning new
semantics. Non-finite movement or normals fail atomically.

## Portable API

`LegacyGameplayCameraCollision` now exposes three allocation-free `noexcept`
primitives:

- `legacyGameplayCameraConstraintAcceptsPlane`;
- `legacyGameplayCameraConstraintPlaneOverrides`;
- `legacyGameplayCameraAttemptConstrainedMove`.

They operate only on caller-owned spans and values. They do not collect BSP
polygons, choose a penetration depth, mutate the camera position, or publish a
room.

## Validation fixtures

Synthetic tests cover:

- exact threshold bit patterns;
- accepted distinct and rejected duplicate normals;
- asymmetric override results;
- unchanged movement with no active plane;
- one-plane projection;
- two orthogonal planes projecting onto their common edge;
- three mutually overriding planes stopping movement;
- two close planes below the `0.0001` cutoff;
- two sufficiently separated planes above the cutoff;
- non-finite fail-closed behavior;
- repeated allocation-free evaluation and input immutability.

A fresh Release/Ninja build completed all 202 steps and all 61 portable tests
pass. The code-intelligence build and its 61 tests pass as well.

## Remaining boundary

The constraint stage is no longer a blocker. The next unresolved stage is
`PhSphere::GetCollision` plus
`PhCollidedPolyList::GetCollisionAndBestFree`: contact construction, exact
face/edge/vertex classification, penetration depth, and chosen direction need
golden fixtures before the full camera sphere resolver is implemented.

Dynamic-object BSP remains a later, separate runtime-state integration.

## Confidence

Confidence is **3/3** for function boundaries, strict comparisons, constants,
linked-list ordering, branch selection, and projection outcomes.

Confidence is **2/3** for last-bit parity of pathological x87 inputs on modern
targets. Normal finite gameplay inputs are covered by explicit spill ordering
and deterministic fixtures.

## Related evidence

- [Camera sphere-contact recovery](EXP-20260728-014-camera-sphere-contact-recovery.md)
- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
