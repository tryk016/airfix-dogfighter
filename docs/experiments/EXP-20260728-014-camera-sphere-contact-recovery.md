# Gameplay-camera sphere contact recovery

**Date:** 2026-07-28

**Evidence:** `EV-20260728-001`

**Scenario:** `SCN-CAMERA-001`

**Status:** material contract implemented; static sphere collection and
constraint resolution specified but not yet implemented

## Question

What exact native stages turn the smoothed third-person camera candidate into a
sphere-corrected position, and which part can be transferred safely before the
complete contact mathematics is reconstructed?

## Evidence boundary

Ghidra 12.1.2 remains the canonical source for types and pseudocode. Fresh
address-selected decompilation and instruction reports were generated from the
verified `Cc.dll` working copy for:

| Function | RVA | Role |
|---|---:|---|
| `CcConstraint::AddPlane` | `0x00025ED0` | retain a non-duplicate contact plane |
| `CcConstraint::AttemptMove` | `0x00025F40` | project requested movement through active planes |
| `PhCollidedPolyList::GetCollisionAndBestFree` | `0x00036150` | select penetration depth, direction, and polygon |
| triangle-distance helper | `0x00036D40` | x87 helper used by closest-feature selection |
| `PhSphere::PhSphere` | `0x00037160` | initialize collision-list, room, and three query flags |
| `PhSphere::GetCollision` | `0x00037220` | sphere-versus-triangle separating-axis test |
| `PhSphere::GetBspCollision` | `0x00037950` | collect room and dynamic-object contacts |
| `PhSphere::TraceBspNode` | `0x00037C90` | radius-pruned BSP traversal and polygon collection |

Rizin 0.9.1/rzpipe 0.6.2 independently confirms the
`PhSphere::GetCollision` entry at RVA `0x00037220`, its range through
`0x00037946`, four explicit arguments, calls to the expected vector helpers,
and the final boolean return paths. Rizin labels the calling convention as
`cdecl`; the decorated export and Ghidra agree that it is a C++ `__thiscall`.
The Rizin report is therefore useful boundary/instruction corroboration, not
the authoritative ABI.

Binary Ninja Free was deliberately not opened for this pass because its manual
pilot is paused. No original file or derived binary was uploaded.

## Native camera order

`AfVehicle::ProcessEvent` event `5` performs these stages in order:

1. Copy the camera position and current room into a temporary `CcPosition`.
2. Construct a `PhSphere` at that position.
3. Set radius to `nearClipping * 1.1F`.
4. Query room BSP and enabled dynamic-object BSP through
   `PhSphere::GetBspCollision`.
5. Remove each collected polygon whose mesh polygon has a non-null material
   and whose material value at `+0x5C` is exactly `0` or `8`.
6. Repeatedly select the best penetration, pass
   `-depth * direction` through `CcConstraint::AttemptMove`, add the accepted
   displacement to the sphere centre, add the negated direction as a new
   constraint plane, release the selected polygon, and re-test the remaining
   list.
7. Reduce the three camera factors by twice the absolute correction on each
   axis and clamp negative factors to zero.
8. Trace the corrected position through portals with raw argument `0.2F`.
9. Publish the resulting camera room and position before the later camera line
   occlusion and look-at stages.

The material condition is easy to misread: modes `0` and `8` are deleted from
the camera collision list. Other values, a null mesh polygon, or a null material
pointer remain candidates.

## CCF material provenance

`CcRoom::LoadScene` reads material child `0x2152` as:

```text
u32
float32
float32
```

The temporary `CcMaterial` begins at the decompiler's stack object, and the
first `0x2152` value lands exactly `0x5C` bytes after that object. The gameplay
camera reads the same `CcMaterial + 0x5C` location before deleting modes `0`
and `8`. This establishes a collision use for the first value without
establishing durable names for either numeric value or meanings for the two
following floats.

The portable parser now exposes that first value as
`CcfMaterialMetadata::collisionMode2152`. It remains optional because the
native loader accepts an absent `0x2152` child.

## Retained polygon binding

Each BSP polygon already resolves through:

```text
BSP polygon
  -> placed object reference
  -> source-local mesh
  -> triangle index
  -> triangle material reference
```

`ResolvedBspPolygonBinding` now adds an optional source-local material index
only when exactly one material matches the triangle reference.
`MissionWorldSpatialPolygon` copies the optional raw `collisionMode2152` into
the mission-lifetime pointer-free arena.

Missing or ambiguous references remain absent rather than inventing a material.
That preserves the native null-material branch for missing data and is
conservative for an unobserved ambiguous-reference case. The shipped corpus
previously measured zero duplicate current material references within a CCF.

## Static sphere collection contract

The next implementation stage can use the retained static trees with these
confirmed rules:

- for signed centre-to-split distance `d`, recurse child B when
  `d >= -radius`;
- recurse child A when `d < radius`;
- process the node polygon list after both recursive calls;
- build triangle vertices as `point0`, `point0 + edge01`, and
  `point0 + edge01 + edge12`;
- use `PhSphere::GetCollision`, not a substituted closest-point test;
- omit a contact only when its optional material mode is present and equals
  `0` or `8`;
- preserve legacy tree and polygon prepend order already retained by the arena.

`PhSphere::GetCollision` first rejects plane separation, then tests three
vertex axes and three edge-derived axes. Its vector normalization, binary32
spills, x87 comparisons, and degenerate-input behavior must be translated and
tested before the portable collector is claimed complete.

## Constraint boundary

The native closest-feature selector records:

- greatest penetration at list `+0x20`;
- its direction at `+0x28..+0x30`;
- its polygon at `+0x40`;
- smallest accepted penetration at `+0x24`;
- its direction at `+0x34..+0x3C`;
- its polygon at `+0x44`.

`CcConstraint::AddPlane` rejects a new plane when an existing plane has a dot
product above the native duplicate threshold. `AttemptMove`:

- returns the original vector when no stored plane opposes it;
- projects against one selected plane;
- projects onto the normalized cross product of two selected planes; or
- returns zero when the selected constraints conflict or their cross product
  is below the native threshold.

The `Overrides` predicate and both native threshold constants still require
their own addressed recovery before this stage is coded. The large
`GetCollisionAndBestFree` function also requires golden closest-face,
closest-edge, and closest-vertex fixtures. Transliteration without those
contracts would create plausible but unverified camera movement.

## Dynamic objects

`PhSphere::GetBspCollision` also broad-phases active room objects by current
radius, transforms the sphere into object-local space, scales its radius,
traverses the object's BSP, and transforms collected contact data back to
world space. The retained mission arena currently represents authored
source-world room trees, not moving object BSP state. Static collection and
dynamic-object collection therefore remain separate implementation stages.

## Validation

Synthetic tests prove:

- present `0x2152` value `8` is decoded exactly;
- an absent `0x2152` remains absent;
- unique source-local material references produce a material binding;
- missing and ambiguous references remain nullable;
- the mission arena retains the resolved mode on static and portal polygons;
- retained-byte accounting includes the enlarged polygon record automatically.

No original asset is required by these tests, and no generated analysis report
or local path is committed. A fresh Release/Ninja build completed all 202 steps
and all 61 portable tests pass; the code-intelligence preset completed all 135
rebuilt steps and its 61 tests pass.

## Confidence

Confidence is **3/3** for the `0x2152` first-u32 to `CcMaterial + 0x5C`
provenance and the gameplay-camera deletion rule for values `0` and `8`.

Confidence is **3/3** for the native stage order and static BSP recursion
conditions.

Confidence is **2/3** for the complete closest-feature/constraint semantics
until `CcConstraintPlane::Overrides`, constants, and boundary fixtures are
recovered. Dynamic-object parity remains outside this stage.

## Related evidence

- [Gameplay camera modes](EXP-20260727-010-gameplay-camera-modes.md)
- [Portal BSP line trace](EXP-20260727-011-portal-bsp-line-trace.md)
- [Camera room-state proposal](EXP-20260728-013-camera-room-state-proposal.md)
- [CCF format](../formats/CCF.md)
- [Camera and projection contract](../re/systems/CAMERA-PROJECTION.md)
