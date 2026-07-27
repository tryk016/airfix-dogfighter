# Camera and projection contract

**Status:** legacy scalar screen projection and world-to-view implemented;
reverse depth, 4:3 canvas, and room/portal render chains recovered

This document separates confirmed legacy behavior from reconstruction
decisions. The complete evidence record is
[EXP-20260727-004](../../experiments/EXP-20260727-004-camera-render-pipeline.md).

## Confirmed render chain

```text
CcCamera::Render                         0x0001BB20
  -> CcCamera::RenderRoom                0x0001C6A0
     -> CcObject::AddVisiblePolygons...  0x0001FDD0
     -> CcCamera::RenderLayerPolys       0x0001EB20
        -> render-layer clip helper      0x0001EE00
        -> CcDistClip::ProcessPoly        0x000477F0
           -> CcPolyVertex::Project       0x0000CCE0
           -> GtPolyRender::ProcessPoly   0x00046130
              -> GtScreen::RenderTriangle
```

The Direct3D implementation of `GtScreen::RenderTriangle` is at
`gtDirect3D.dll` RVA `0x000034B0`. It stages three converted vertices in the
concrete screen buffer and flushes through a local helper. Per-triangle
submission does not call `GtDevice` directly.

Ghidra and Rizin independently agree on the important boundaries and raw
callsites. Ghidra's recovered MSVC declarations, ECX receiver use, and
`RET n` cleanup establish `__thiscall` where Rizin proposes `cdecl`.

## Legacy screen projection

Camera space faces positive Z. `CcCamera::Render` clamps the horizontal FOV to
`[1, 175]` degrees and derives:

```text
windowWidth  = right - left
focal        = 0.5 * windowWidth / tan(FOV * pi / 360)
projectScale = focal / near
```

`CcPolyVertex::Project` preserves this exact operation order:

```text
factor  = near / z, when z > near
factor  = 1,        otherwise
scale   = projectScale * factor
screenX = scale * x + centreX
screenY = centreY - scale * y
```

`CcDistClip::ProcessPoly` performs far and then near clipping before the final
three projection calls. The point-projection function itself does not reject a
point outside either distance.

Constructor defaults:

| Parameter | Value |
|---|---:|
| Near | `1` |
| Far | `100` |
| Horizontal FOV | `90°` |
| Centre | `(0.5, 0.5)` |
| Window | `(0, 0)-(1, 1)` |

Engine initialization changes near/far to `0.25`/`200` and sets FOV to `90°`.
Console paths can subsequently change those values.

Confirmed camera fields:

| Offset | Meaning |
|---:|---|
| `+0x900` | focal screen scale |
| `+0x904` | `1 / near` |
| `+0x908` | `focal / near` |
| `+0x90C` | `far / near` |
| `+0x914` | `1 / far` |
| `+0x918` | window width |
| `+0x928`, `+0x92C` | near and far |
| `+0x930` | FOV degrees |
| `+0x934`, `+0x938` | centre X/Y |
| `+0x93C` through `+0x948` | window left/top/right/bottom |

## Reverse depth

`CcPolyVertex::Project` also preserves camera Z and writes:

```text
rhw = factor
```

After inclusive near/far clipping:

```text
near <= cameraZ <= far
near / far <= rhw <= 1
```

The active Direct3D path uses `D3DZB_TRUE`, not hardware
`D3DZB_USEW`, and copies `rhw` into both transformed-vertex `z` and `rhw`.
Opaque mode one uses `D3DCMP_GREATEREQUAL` with depth writes:

```text
legacyDepth = near / cameraZ
near -> 1
far  -> near / far
```

The D3D viewport depth range is `[0,1]`. This is a conventional Z-buffer
carrying manually reversed reciprocal depth. Modes two through four separately
select unconditional comparison and/or disable depth writes.

The exact legacy clear call/value is still untraced. Clearing a reverse-depth
target to zero is logically required for `greaterEqual`, but remains a port
requirement rather than a confirmed original callsite.

See
[EXP-20260727-006](../../experiments/EXP-20260727-006-reverse-depth.md)
for the embedded `GtVertex`, transformed-vertex layout, FVF, D3D states, and
dormant hardware-W branch.

## Legacy canvas and aspect

The application starts in exclusive fullscreen `640x480`. Camera windows and
centres are pixel coordinates. Main gameplay uses a fixed window embedded in
the HUD:

```text
logical canvas = 640 x 480
gameplay window = (35,35) - (555,345)
gameplay size = 520 x 310
gameplay centre = (295,190)
```

The camera FOV is horizontal; focal length depends on window width only.
Window height does not enter projection. No adaptive letterbox, pillarbox,
Hor+, or widescreen policy was found, and the fixed gameplay rectangle is not
recomputed after a console resolution change.

The safe first iOS presentation policy is therefore an explicit reconstruction
decision: aspect-fit the logical 4:3 canvas and keep the legacy gameplay
window/HUD coordinates unchanged. A widescreen/Hor+ mode must remain a
separate option with its own HUD, culling, touch-layout, and visual tests.

See
[EXP-20260727-007](../../experiments/EXP-20260727-007-viewport-aspect.md)
for setter callsites, display setup, resize behavior, and preview/widget camera
rectangles.

## Current room and portals

`CcCamera + 0xF4` is the current room. `SetPosAndTracePortals` traces the old to
new camera position and updates this field only when `CcRoom::TracePortals`
returns a room.

`Render` starts with:

```text
RenderRoom(currentRoom, rootClip, 1, 0)
```

Render-time portals come from a room object list and point to their target room
through `portalObject + 0x13C`. A portal must survive culling and produce a
non-empty scoped clip before recursion. The fourth `RenderRoom` parameter is
depth; its default limit is `2`, and no general visited-room set was found.

`portalType == 1` reflects camera position and basis across the portal plane
and reverses culling. This behaves like a mirror, but the durable enum name is
not yet confirmed.

## BSP boundary

The render path does **not** walk the static-BSP child tree. It renders room
object lists and follows explicit clipped portals.

Camera movement separately queries the portal-BSP list through:

```text
CcRoom::TracePortals                    0x000216C0
  -> PhLine::GetPortalBspCollision      0x00039070
     -> PhLine::TraceBspNode            0x000391D0
```

The BSP traversal computes signed distances for segment endpoints, visits the
start-side child first, tests polygons on a crossed plane, then visits the
end-side child while retaining the nearest hit. The hit polygon resolves the
next room through its owning portal object.

## World-to-view

The camera world SRT contains a 3x3 orthogonal basis with uniform scale, a
translation, and cached `q = 1 / scale²`. Legacy composition uses row vectors.
For one world point:

```text
delta  = worldPoint - cameraWorldTranslation
view   = q * delta * transpose(cameraWorldLinear)
```

The implementation subtracts first, evaluates the three basis dot products,
and multiplies the results by `q`. `CcSrtNode::GetNodeRelation` uses the same
inverse to prepare object-to-camera scratch SRT before
`AddVisiblePolygonsToRenderList` consumes it:

```text
relation.linear =
    q * objectWorldLinear * transpose(cameraWorldLinear)
relation.translation =
    q * (objectWorldTranslation - cameraWorldTranslation)
      * transpose(cameraWorldLinear)
```

`CcLight::GetCameraRelation` delegates to this path, while
`CcProjector::GetCameraRelation` independently repeats the same equations.
There is no hidden axis reflection: `+X` is right, `+Y` is up, and `+Z` is
forward in camera space.

The complete layout, render snapshot fields, function boundaries, and
cross-checks are recorded in
[EXP-20260727-005](../../experiments/EXP-20260727-005-camera-world-to-view.md).

`src/airfix/render/LegacyCameraTransform.cpp` implements only the confirmed
world-point portion. Its factory validates finite inputs, a nonzero uniform
scale, positive cached `q`, normalized Gram orthogonality and column lengths,
and `q * scale²` consistency. A scale requiring subnormal or unrepresentable
inverse-square `float` is rejected explicitly. The immutable hot path is
allocation-free and `noexcept`; it preserves the exact subtraction,
dot-product, and `q` multiplication order and rejects non-finite input or
output atomically.

It does not publish an affine matrix because precomposing translation changes
float operation order. It also does not copy the unverified relation-`q`
cache, construct gameplay camera poses, or expose Metal types.

## Reconstructed renderer boundary

The current Metal renderer still applies only a diagnostic aspect correction
to each model transform:

```text
clip = diagnosticAspect * modelFromLocal * position
```

It is not a recovered camera. World coordinates may fall outside clip space.
The diagnostic rasterizer's auto-fit view is also not legacy evidence.

The portable implementation in
`src/airfix/render/LegacyScreenProjection.cpp` is deliberately narrow:

- validate the confirmed projection parameters;
- derive `focal` and `focal / near`;
- project an already camera-space point using the exact branch and float
  operation order above;
- preserve camera-space Z and the exact `near / cameraZ` factor as
  `legacyRhw` so a later backend can consume the recovered reverse-depth
  scalar without recomputing it;
- keep clipping separate;
- expose no generic matrix or claimed parity camera.

Its build factory rejects non-finite or invalid near/far/window values, clamps
finite FOV exactly as the legacy render path does, and validates all derived
scalars. The hot `project()` path is `noexcept`, allocation-free, reports the
near fallback explicitly, and rejects non-finite input or output atomically.
Portable tests cover defaults, engine near/far/FOV with a synthetic viewport,
FOV bounds, centre and Y convention, the exact near comparison, projection
beyond far, reciprocal-depth monotonicity and boundaries, invalid
configuration, atomic failure, overflow, and repeated zero-allocation use.

`src/airfix/render/LegacyCanvasLayout.cpp` keeps the recovered facts and the
port decision separate. It exposes compile-time constants for the 640x480
logical canvas and 520x310 gameplay rectangle, then provides a validated
aspect-fit policy for a caller-supplied target rectangle. The immutable build
result is backend-neutral, `noexcept`, allocation-free, and rejects non-finite,
non-positive, underflowing, or overflowing layouts. It deliberately knows
nothing about UIKit points, display scale, orientation, safe areas, or Metal.

## Still unknown

- gameplay camera modes, chase offsets, smoothing, rear view, and recentering;
- the exact original depth-clear call/value and which gameplay draws select
  each of the four depth modes;
- final Metal clip-matrix packaging and raster-space Y convention;
- optional widescreen/Hor+ behavior beyond the confirmed parity-first 4:3
  policy;
- durable semantic names for every room list, clip virtual method, and portal
  enum;
- whether any game path raises the portal depth limit above its constructor
  default.

No code may invent these values or claim camera parity before the missing
evidence is recovered.

## Related evidence

- [Function catalogue](../FUNCTION-CATALOG.csv)
- [Coordinate contract](COORDINATE-CONTRACT.md)
- [Render data contract](RENDER-DATA-CONTRACT.md)
- [Startup and plugin chain](STARTUP-PLUGINS.md)
- [Static tool cross-check](../../experiments/EXP-20260727-001-static-tool-crosscheck.md)
- [Camera render experiment](../../experiments/EXP-20260727-004-camera-render-pipeline.md)
- [Camera world-to-view experiment](../../experiments/EXP-20260727-005-camera-world-to-view.md)
- [Direct3D reverse-depth experiment](../../experiments/EXP-20260727-006-reverse-depth.md)
- [Viewport/aspect experiment](../../experiments/EXP-20260727-007-viewport-aspect.md)
