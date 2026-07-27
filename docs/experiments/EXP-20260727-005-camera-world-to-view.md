# Camera world-to-view relation

**Date:** 2026-07-27  
**Evidence:** `EV-20260727-006`  
**Scope:** `CcSRT` layout, camera world snapshots, object-to-camera relation,
and the point transform consumed by polygon rendering

## Question

Recover the exact transform that moves world-space geometry into the
positive-Z camera space consumed by `CcPolyVertex::Project`. Determine the
matrix layout, scale treatment, translation order, and whether the legacy path
adds an axis reflection. Keep gameplay camera movement and Metal clip/depth
mapping outside this experiment.

## Inputs and method

| Module | SHA-256 | Image base |
|---|---|---:|
| `Cc.dll` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | `0x10000000` |

Ghidra 12.1.2 Headless and Rizin 0.9.1 independently analyzed the same
hash-verified read-only working copy. The audit compared exact instructions,
function boundaries, callsites, field offsets, and the arithmetic repeated by
the node, light, and projector paths. No binary was executed, patched,
uploaded, or opened in a cloud service. Binary Ninja was not used.

## Function boundaries

| Function | RVA range | Role |
|---|---:|---|
| `CcProjector::GetCameraRelation` | `[0x00003E90, 0x00004115)` | Repeats the camera-relative basis and position transform explicitly |
| `CcLight::GetCameraRelation` | `[0x00006A70, 0x00006B0B)` | Delegates to the node relation and consumes its scratch SRT |
| transform-triplet helper | `[0x0000E270, 0x0000E2C6)` | Computes one `q`-scaled camera-basis dot-product triplet |
| `CcSrtNode::GetNodeRelation` | `[0x0000E2D0, 0x0000E432)` | Builds object-to-camera scratch SRT |
| `CcSrtNode::GetWorldRelation` | `[0x0000E440, 0x0000E4FC)` | Recursively composes and caches the node world SRT |
| `CcCamera::RenderPointMB` | start `0x0001A3A0` | Uses current and prior camera snapshots for motion-blur endpoints |
| `CcCamera::Render` | `[0x0001BB20, 0x0001BEC8)` | Refreshes camera world relation and rotates the snapshots |
| `CcObject::AddVisiblePolygonsToRenderList` | `[0x0001FDD0, 0x00020AFA)` | Consumes object-to-camera scratch SRT for each vertex |
| `CcRoom::AddVisibleObjectsToRenderList` | `[0x00020D30, 0x00020F6A)` | Prepares object and attached-node camera relations |
| `CcSRT::InheritParentSRT` | `[0x0002BFD0, 0x0002C187)` | Composes child-local and parent-world SRT |

The end of `RenderPointMB` was not established in this bounded audit and is
therefore intentionally not claimed.

## `CcSRT` layout and algebra

For an SRT base address `B`:

```text
B+00 B+04 B+08   matrix triplet 0
B+0C B+10 B+14   matrix triplet 1
B+18 B+1C B+20   matrix triplet 2
B+24             uniform scale
B+28             inverse-square scale q, normally 1 / scale²
B+2C B+30 B+34   translation x/y/z
```

The numeric matrix is:

```text
A = [ a00 a01 a02
      a10 a11 a12
      a20 a21 a22 ]
```

Legacy code uses row-vector composition:

```text
p_parent = p_local * A + t
```

The per-point implementation consumed by the renderer is:

```text
out.x = x*a00 + y*a10 + z*a20 + tx
out.y = x*a01 + y*a11 + z*a21 + ty
out.z = x*a02 + y*a12 + z*a22 + tz
```

`CcSRT::InheritParentSRT` confirms:

```text
A_world = A_local * A_parent
t_world = t_local * A_parent + t_parent
```

The engine's `Cc3Matrix` names do not remove the row/column ambiguity on their
own. The equations and their existing `Mat3.columns` representation are the
durable contract.

## Camera inverse

Let the camera world SRT contain:

```text
Ac = camera world linear matrix
tc = camera world translation
qc = camera inverse-square scale
```

For the orthogonal, uniformly scaled basis maintained by `CcSRT`:

```text
Ac * transpose(Ac) = scale² * I
qc = 1 / scale²
inverse(Ac) = qc * transpose(Ac)
```

The exact world-to-camera point operation is:

```text
delta  = p_world - tc
p_view = qc * delta * transpose(Ac)
```

The implementation performs the subtraction first, then three dot products,
then multiplication by `qc`. Precomposing an affine translation would be
algebraically equivalent but would change floating-point operation order.

For an identity camera:

```text
view.x = world.x - camera.x
view.y = world.y - camera.y
view.z = world.z - camera.z
```

There is no extra sign change or reflection:

- `+X` is camera right;
- `+Y` is camera up;
- `+Z` is camera forward;
- screen Y is inverted later by `CcPolyVertex::Project`.

## Object-to-camera relation

`CcSrtNode::GetNodeRelation` first copies the object's 56-byte world SRT from
`object + 0x58` into the scratch relation at `object + 0x20`, then replaces it
with:

```text
A_relation = qc * A_object * transpose(Ac)
t_relation = qc * (t_object - tc) * transpose(Ac)
```

For the first object matrix triplet, the raw equations begin:

```text
rel[00] = qc*(obj58*cam58 + obj5C*cam5C + obj60*cam60)
rel[04] = qc*(obj58*cam64 + obj5C*cam68 + obj60*cam6C)
rel[08] = qc*(obj58*cam70 + obj5C*cam74 + obj60*cam78)
```

Translation uses the same camera triplets after subtracting camera position.
The helper at `0x0000E270` repeats this operation for another triplet.

The relation's scale caches are written exactly as legacy code specifies:

```text
relation.scale = camera.q * camera.scale * object.scale
relation.q     = camera.scale * object.q
```

The second equation does not preserve an obvious inverse-square invariant. It
may be an unused cache or legacy quirk and must not be “corrected” without
auditing its consumers.

## Render ordering

`CcRoom::AddVisibleObjectsToRenderList` prepares the relation before polygon
processing:

```text
0x00020D9C -> GetNodeRelation(object, camera)
0x00020EA8 -> GetNodeRelation(attached node, camera)
```

`CcObject::AddVisiblePolygonsToRenderList` then consumes either
`object + 0x20` or `attachedNode + 0x20`:

```text
view.x = rel00*x + rel0C*y + rel18*z + rel2C
view.y = rel04*x + rel10*y + rel1C*z + rel30
view.z = rel08*x + rel14*y + rel20*z + rel34
```

Representative `RenderRoom` pairs are:

```text
0x0001C7C0 -> AddVisibleObjectsToRenderList
0x0001CBBD -> AddVisiblePolygonsToRenderList

0x0001DA35 -> AddVisibleObjectsToRenderList
0x0001DA60 -> AddVisiblePolygonsToRenderList

0x0001E2B5 -> AddVisibleObjectsToRenderList
0x0001E2CF -> AddVisiblePolygonsToRenderList

0x0001E34E -> AddVisibleObjectsToRenderList
0x0001E3EC -> AddVisiblePolygonsToRenderList
```

Polygon processing does not construct the relation itself.

## Independent corroboration

`CcLight::GetCameraRelation` calls `CcSrtNode::GetNodeRelation` and uses the
same light scratch relation for its camera-space direction.

`CcProjector::GetCameraRelation` independently repeats:

```text
projectorViewPosition =
    qc * (projectorWorldPosition - tc) * transpose(Ac)

projectorViewBasis[i] =
    qc * projectorWorldBasis[i] * transpose(Ac)
```

This is strong independent confirmation of translation subtraction,
transpose/dot order, scale treatment, and the absence of hidden axis flips.

## Camera snapshots

| Meaning | SRT base | Matrix | Scale | q | Translation |
|---|---:|---:|---:|---:|---:|
| current node world SRT | `+0x58` | `+0x58..+0x78` | `+0x7C` | `+0x80` | `+0x84..+0x8C` |
| prior render snapshot | `+0x978` | `+0x978..+0x998` | `+0x99C` | `+0x9A0` | `+0x9A4..+0x9AC` |
| current render snapshot | `+0x9B0` | `+0x9B0..+0x9D0` | `+0x9D4` | `+0x9D8` | `+0x9DC..+0x9E4` |

`CcCamera::Render` obtains the current world relation, copies the previous
current snapshot into the prior slot, and copies the node world SRT into the
current slot. `RenderPointMB` consumes both. Snapshot meaning is confirmed;
the full motion-blur contract remains only partial.

## Confidence and remaining limits

Confidence is 3/3 for:

- SRT matrix, translation, scale, and inverse-square fields;
- row-vector composition;
- subtraction of camera translation before dot products;
- `qc * transpose(Ac)` as the uniform-scale camera inverse;
- object-relation preparation before polygon rendering;
- no X/Y/Z reflection in world-to-camera;
- current and prior camera snapshot roles.

Still open:

- gameplay camera modes, offsets, smoothing, rear view, and recentering;
- pitch/yaw/roll positive-sign policy at the gameplay layer;
- behavior for shear or non-uniform scale, which `CcSRT` does not invert
  generally;
- use-sites and meaning of the relation `q` cache quirk;
- full motion-blur endpoint semantics;
- legacy W-buffer and exact Metal `[0,1]` depth mapping;
- widescreen and final viewport policy.

This list records the boundary of this experiment when it was completed.
Follow-up experiments
[EXP-20260727-006](EXP-20260727-006-reverse-depth.md) through
[EXP-20260727-010](EXP-20260727-010-gameplay-camera-modes.md) subsequently
closed the depth, clear, viewport, and gameplay-camera facts. Stateful chase
pose implementation and the final Metal join remain separate.

## Reconstruction consequence

`LegacyCameraTransform` now validates an orthogonal uniformly scaled camera
SRT and transforms a finite world point with the exact
subtract-then-dot-then-`q` order. Its Gram validation operates on normalized
double-precision products so very small finite scales cannot hide a zero,
sheared, or non-uniform column. Scales whose mathematical inverse-square would
fall outside the normal positive `float` range are rejected with a dedicated
typed issue rather than claiming unreliable subnormal parity. The immutable
transform and builder are allocation-free and `noexcept`.

The component deliberately excludes gameplay camera construction,
object-relation cache quirks, motion-blur snapshots, clipping, projection,
NDC, Metal depth, and graphics-API matrices.
