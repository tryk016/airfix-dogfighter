# Gameplay-camera static sphere resolver

**Date:** 2026-07-28

**Evidence:** `EV-20260728-003`

**Scenario:** `SCN-CAMERA-001`

**Status:** implemented for retained static room BSP; dynamic-object BSP
remains separate

## Question

Can the native `PhSphere` candidate test, closest-feature selector, portal-room
walk, and iterative camera correction be transferred into a bounded portable
C++20 runtime without replacing the original rules with a generic
sphere-triangle algorithm?

## Evidence boundary

Ghidra 12.1.2 decompilation and x86 instruction reports from the verified
read-only `Cc.dll` working copy cover:

| Function | RVA | Recovered role |
|---|---:|---|
| `PhCollidedPolyList::GetCollisionAndBestFree` | `0x00036150` | select face, edge, or vertex contact and greatest positive penetration |
| squared-vector helper | `0x00036D40` | preserve x87 squared-distance comparisons for the three vertex cases |
| `PhSphere::GetCollision` | `0x00037220` | seven-axis sphere/triangle candidate test |
| `PhSphere::GetBspCollision` | `0x00037950` | process static rooms, visible type-zero portals, and optional dynamic objects |
| `PhSphere::TraceBspNode` | `0x00037C90` | radius-pruned B-then-A recursion and collided-polygon construction |

Rizin 0.9.1/rzpipe 0.6.2 independently discovered
`GetCollisionAndBestFree` at `0x10036150` with the exact automatic boundary
`[0x10036150, 0x10036D39)`, 3,049 bytes, and the expected 12 call sites.
Those include six calls to the squared-vector helper at `0x10036D40`.
Rizin again labels the decorated C++ method as `cdecl`; Ghidra, the export, ECX
receiver use, and `ret 4` establish `__thiscall`.

No original binary, proprietary polygon data, analysis database, report, or
local path is committed.

## Candidate test

`PhSphere::GetCollision` is not a generic closest-point predicate. For
triangle vertices `p0`, `p1`, `p2`, centre `c`, radius `r`, and the owning BSP
node's split normal `n`, it rejects separation in this order:

1. plane axis: `abs(dot(p0 - c, n))`;
2. the normalized `p0 - c` axis;
3. the normalized `p1 - c` axis;
4. the normalized `p2 - c` axis;
5. the normalized perpendicular axis derived from edge `p0 -> p1`;
6. the corresponding axis for `p1 -> p2`;
7. the corresponding axis for `p2 -> p0`.

All separating comparisons are strict, so exact tangency remains a broad-phase
candidate. The split normal belongs to the BSP node. The later polygon
`faceNormal` has a different role and cannot be substituted here.

The portable primitive retains the identified component-level binary32
subtractions and normalization stores. Finite zero-length/degenerate axes
produce unordered intermediate comparisons in the original; the portable
translation likewise lets such an axis make no separation claim. Non-finite
external input fails closed.

## Closest feature and direction contract

`GetCollisionAndBestFree` first projects the sphere centre toward the polygon
plane using the authored collision-facing `faceNormal`. It then evaluates the
three inward edge axes in native order:

```text
cross(edge01, faceNormal)
cross(edge12, faceNormal)
cross(edge20, faceNormal)
```

The sign and endpoint-product branches distinguish:

- the face interior;
- edge `01`, `12`, or `20`;
- vertex `0`, `1`, or `2`, tested in that order.

Face and edge tangencies are admitted at penetration depth zero. The vertex
path uses a strict squared-radius comparison and rejects exact vertex
tangency.

An important native irregularity is preserved:

- a face contact stores the unit `faceNormal` as its direction;
- an edge or vertex contact stores raw `contactPoint - sphereCenter` and does
  **not** normalize it.

The camera later requests `-penetrationDepth * direction`. Normalizing the
edge/vertex direction would therefore be a behavioral change, even though it
would look more conventional.

The collided-polygon representation retains `edge01 = p1 - p0` and
`edge02 = p2 - p0`. `edge02` is the binary32 sum of the two consecutive CCF
edges. The selector reconstructs `edge12 = edge02 - edge01`, matching the
native stored-field and rounding boundary rather than recomputing it from
three independently rounded points.

## Static room collection

The portable resolver follows the confirmed native order:

1. visit child B when `distance >= -radius`;
2. visit child A when `distance < radius`;
3. test the node's polygons in retained prepend-list order;
4. prepend each accepted collided polygon logically, so selection scans the
   reverse of discovery order;
5. keep the first candidate at an exact greatest-depth tie.

A type-zero portal polygon that intersects the sphere is not a contact.
When its object-visible flag is set, its target room is added to the
unprocessed room set; duplicate and self rooms are suppressed. Unlike the line
portal path, this sphere-room gate does not read mesh `selectionFlagB`.
A nonzero portal type remains solid. An invisible type-zero portal is ignored.

After collection, optional material mode `0` or `8` removes the contact.
Missing material metadata remains collidable, matching the native null-material
branch.

The retained runtime currently has no moving-object BSP state. The native
dynamic-object broad phase, object-local sphere transform, scaled radius, and
world-space contact adaptation therefore remain explicitly outside this
static resolver.

## Iterative correction

For the remaining candidates, the resolver:

1. selects the first greatest **positive** penetration in native list order;
2. sends `-depth * direction` through the recovered
   `CcConstraint::AttemptMove`;
3. applies the accepted binary32 displacement to the source-space centre;
4. submits `-direction` through the strict `CcConstraint::AddPlane` duplicate
   test;
5. removes the selected candidate;
6. reruns the seven-axis test on all remaining candidates;
7. repeats until no positive penetration remains.

Zero-depth contacts produce a typed `touching` result and no displacement.
Every positive iteration removes one candidate, so the candidate workspace
also provides a natural finite iteration bound.

## Runtime boundary and safety

`resolveMissionWorldRuntimeStaticSphere` is read-only with respect to the
mission arena, allocation-free, and `noexcept`. The caller supplies bounded
candidate and constraint-plane workspaces. Exhaustion is a typed failure and
never publishes a partial corrected position.

The solver performs native contact arithmetic in retained source-world space.
It accepts only an orthonormal source-to-runtime basis plus the established
positive uniform unit scale. A general shear or anisotropic basis would map a
runtime sphere to a source-space ellipsoid and is rejected rather than silently
changing the collision shape.

The native room graph walk has no portable safety limit. This implementation
adds a caller-selectable maximum of at most 256 visited rooms, consistent with
the existing portal-trace ceiling.

## Golden fixtures

Public synthetic tests cover:

- separated, intersecting, and exact plane-tangent seven-axis candidates;
- face contact;
- all three edge branches;
- all three vertex branches;
- strict vertex tangency;
- the legacy all-`0xFFC00000` face-normal sentinel;
- raw non-normalized edge direction;
- reverse-discovery tie selection;
- material modes `0`, `8`, and a collidable nonzero mode;
- zero-depth touching;
- visible type-zero adjacent-room discovery, invisible type-zero suppression,
  nonzero solid portal behavior, and the room limit;
- candidate and constraint workspace exhaustion;
- uniform runtime scale and rejection of an ellipsoid-producing basis;
- 4,096 allocation-counted complete resolver calls.

Focused portable builds and both new test targets pass. A fresh
Release/Ninja build completed all 205 steps and the complete 62-test portable
suite passes. The code-intelligence preset and its same 62-test suite also
pass. Cross-platform CI and unsigned iOS validation are performed before this
stage is merged.

## Confidence

Confidence is **3/3** for finite nondegenerate face, edge, vertex, tie,
material, portal-room, and constraint-loop behavior.

Confidence is **2/3** for pathological degenerate geometry and x87
extended-precision bit parity. The implementation preserves identified
binary32 stores and comparison strictness but does not claim every
intermediate bit for arbitrary inputs.

Dynamic-object collision remains unimplemented and has no portable parity
claim.

## Related evidence

- [Sphere-contact and material recovery](EXP-20260728-014-camera-sphere-contact-recovery.md)
- [Camera constraint solver](EXP-20260728-015-camera-constraint-solver.md)
- [Portal BSP line trace](EXP-20260727-011-portal-bsp-line-trace.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
