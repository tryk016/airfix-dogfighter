# Camera and projection contract

**Status:** legacy scalar projection, world-to-view, depth presets, gameplay
camera presets, quaternion adapter, chase smoothing, collision
scalars/factors/line interpolation, look-at pose math, 4:3 layout, strict
BSP/portal adaptation, static sphere/contact resolution, atomic
position/room/factor publication, producer-side step composition,
mission-lifetime ownership, weak iOS endpoint, and Metal clip/depth consumption
plus a bounded packet exchange implemented; factor refresh delta, live health,
inactive state, pose sources, and lifecycle transitions confirmed; complete
live producer publication, dynamic-object collision, and device acceptance
remain

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

The recovered mode order inside `CcCamera` is `2 -> 2 -> 1 -> 3` for room
passes, followed by mode `4` for lens overlays:

| Mode | Compare | Write | Confirmed category |
|---:|---|---:|---|
| `1` | `greaterEqual` | yes | primary depth-tested geometry and 3D lines |
| `2` | `always` | yes | unconditional replacement, 2D, font, and radar draws |
| `3` | `greaterEqual` | no | ordered/layer geometry |
| `4` | `always` | no | lens and unoccluded overlays |

The Direct3D setter flushes pending transformed vertices before each actual
compare/write state change, preserving the old state for already staged
triangles.

Both recovered Direct3D clear paths pass `D3DCLEAR_TARGET |
D3DCLEAR_ZBUFFER` and exact depth `0.0f`. The whole-target path clears to color
`0x00000000`; the embedded-scene rectangular path flushes first and clears to
`0xFF000000`. Their confirmed callers are `NfMain::ProcessEvent` and
`GadScene::Render`, respectively. Static evidence does not claim that the
conditional whole-target path runs for every gameplay frame.

See
[EXP-20260727-006](../../experiments/EXP-20260727-006-reverse-depth.md)
for the embedded `GtVertex`, transformed-vertex layout, FVF, D3D states, and
dormant hardware-W branch, and
[EXP-20260727-008](../../experiments/EXP-20260727-008-depth-clear.md)
for the exact clear calls, ordering, and callers.
[EXP-20260727-009](../../experiments/EXP-20260727-009-depth-mode-callers.md)
records the complete direct-caller inventory and frame ordering.

## Legacy canvas and aspect

The application starts in exclusive fullscreen `640x480`. Camera windows and
centres are pixel coordinates. The engine has a full-current-screen setter
path, and menu/widget scenes use their own rectangles.

```text
logical canvas = 640 x 480
```

The fixed `(35,35)-(555,345)` window and `(295,190)` centre previously labeled
as gameplay belong to the House Editor camera. Symbols, frontend resource
strings, `freecam`/`fixedcam` state, and its test-world load independently
confirm that correction. House Editor remains outside version 1 scope, so its
rectangle is not part of the portable canvas contract.

The camera FOV is horizontal; focal length depends on window width only.
Window height does not enter projection. No adaptive letterbox, pillarbox,
Hor+, or widescreen policy was found in the audited setters.

The safe first iOS presentation policy is therefore an explicit reconstruction
decision: aspect-fit the logical 4:3 canvas without treating a modern device
aspect as extra legacy camera extent. A widescreen/Hor+ mode must remain a
separate option with its own HUD, culling, touch-layout, and visual tests.

See
[EXP-20260727-007](../../experiments/EXP-20260727-007-viewport-aspect.md)
for setter callsites, display setup, resize behavior, and preview/widget camera
rectangles.

## Gameplay camera

The actual gameplay camera is owned by `NfEngine + 0x30`. Event case five
assigns the complete current screen as its window and the screen midpoint as
its centre. At startup:

```text
window = (0,0) - (640,480)
centre = (320,240)
near/far/FOV = 0.25 / 200 / 90
```

Three persistent chase presets cycle `0 -> 1 -> 2 -> 0`; rear view is a held
override and does not change the persistent mode:

| Preset | Speed | Offset `(x,y,z)` |
|---|---:|---|
| `camera0` | `0.7` | `(0, 0.1, -0.75)` |
| `camera1` | `0.07` | `(0, 0.2, -0.85)` |
| `camera2` | `0.7` | `(0, 1.8, -0.75)` |
| `camera_rear` | `0.7` | `(0, 0.1, +1.0)` |

The chase update rotates `(offset.x, 0, offset.z)` by vehicle rotation and adds
offset Y in world-up space. It applies the recovered nonlinear per-refresh
step without a time multiplier, resolves portal/sphere/line collision, then
rebuilds camera rotation to look at the vehicle. No normal player-triggered
legacy recenter binding was found.

`LegacyGameplayCameraPreset.cpp` implements the exact tuples, persistent cycle,
held rear selection, and projection defaults. Invalid raw modes fail closed.

`LegacyGameplayCameraChase.cpp` implements the allocation-free stateless
target and smoothing stage. Its matrix is in the runtime column-vector
convention; only offset X/Z are rotated, offset Y remains world-up, and error
uses the recovered `(vehicle - camera) + worldOffset` operation order. The
strict nonlinear branch uses the exact binary32 constants and advances each
axis as `(factor * speed) * error` without `dt`. Non-finite input or derived
results fail atomically. The returned candidate is not a final camera pose:
portal mutation, sphere/line collision correction, look-at rotation, and pose
publication remain deliberately outside this contract.

`LegacyQuaternionRotation.cpp` supplies the preceding vehicle-pose adapter.
It consumes `(w,x,y,z)`, preserves the recovered pairwise-product expression
grouping without premature binary32 rounding, and publishes the mathematical
column matrix used by `applyRuntimeColumn`. The legacy routine does not
normalize: zero produces identity and finite non-unit input is retained.
Non-finite input or an unrepresentable final binary32 matrix fails atomically.
The portable `double` staging does not claim bit-identical x87 extended
precision for every possible input.

`LegacyGameplayCameraCollision.cpp` supplies the next backend-neutral stage.
It implements the exact `near * 1.1` sphere radius, post-sphere per-axis
factor reduction without an upper clamp, aircraft-specific factor recovery,
normalized line-hit interpolation, exact raw portal-call arguments `0.2` and
`0.1`, and the recovered `CcAxisRot::FromDirection` plus X-then-Y camera
look-at matrix. The returned raw camera-world matrix is directly compatible
with `LegacyCameraTransformConfig::linear` and maps the anchor onto positive
camera-space Z.

Fresh sphere-contact recovery establishes that CCF material child `0x2152`'s
first u32 becomes native `CcMaterial + 0x5C`. The gameplay camera deletes
collected contacts whose non-null material carries raw value `0` or `8`.
The parser and retained mission polygon arena now preserve this value through
the triangle's source-local material reference. Missing or ambiguous material
bindings remain absent rather than receiving guessed semantics.

All helpers are allocation-free, `noexcept`, and fail atomically on
non-finite input or intermediate overflow. The look-at uses widened
intermediates as a portable approximation and rejects the still-unverified
zero-direction case. Static and portal BSP line tracing plus the portal/room
state proposal are now present. The static room sphere collector and resolver
are also present; dynamic-object queries and final camera publication are not.

See
[EXP-20260727-010](../../experiments/EXP-20260727-010-gameplay-camera-modes.md)
for field offsets, input bindings, formulas, collision order, SRT mutation,
call graph, and the House Editor correction.

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

The public synthetic Metal path retains its diagnostic aspect correction:

```text
clip = diagnosticAspect * modelFromLocal * position
```

Private mission rooms now use a separate gameplay pipeline. An immutable
`LegacyGameplayCameraClipPacket` preserves the recovered scalar transform and
projection while adapting them to homogeneous Metal clip coordinates:

```text
wClip = cameraZ / near
zClip = 1
xClip = (2 * projectScale / width) * cameraX
      + (2 * centreX / width - 1) * wClip
yClip = (2 * projectScale / height) * cameraY
      + (1 - 2 * centreY / height) * wClip
farClipDistance = far - cameraZ
```

The divide therefore yields the exact screen-space NDC conversion and
`zNdc = near / cameraZ`. Metal uses recovered depth mode one
(`greaterEqual`, writes enabled), zero depth clear, and the port's shared
native-resolution Hor+ viewport. `[[clip_distance]]` supplies the separate far
plane. The 640x480 values remain the recovered reference-camera canvas; they
are not the Metal drawable or 3D raster extent.

Until the simulation owns changing event-5 publications, private rooms use an
explicit tested port bootstrap at the recovered camera0 target relative to the
authenticated player spawn. It is generation one/step zero, retains the spawn
room, and does not fabricate a prior smoothing position. This is coherent
initial camera consumption, not a complete camera-parity claim.

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
logical canvas, then provides a validated aspect-fit policy for a
caller-supplied target rectangle. The immutable build result is
backend-neutral, `noexcept`, allocation-free, and rejects non-finite,
non-positive, underflowing, or overflowing layouts. It deliberately knows
nothing about UIKit points, display scale, orientation, safe areas, or Metal.

`src/airfix/render/NativeRenderLayout.cpp` is the accepted product policy above
that recovered evidence. It separates output, 3D render-target, camera, UI,
safe-area, and input domains; makes 100% scale an exact target identity; and
implements full-target Hor+ plus Original 4:3 comparison. D3D11 and Metal
consume its 100% path. This policy is documented in `EXP-20260729-049` and must
not be mistaken for behavior recovered from the original executable.

`src/airfix/render/LegacyDepthState.cpp` maps untrusted raw mode numbers
`1` through `4` to the confirmed active-path compare/write presets and rejects
every other value. It also exposes the confirmed reverse-depth clear scalar.
This is an allocation-free backend-neutral contract; it does not alter the
current diagnostic Metal renderer or decide batching/encoder policy.

`src/airfix/render/MissionWorldRuntimeSpatialTrace.cpp` converts runtime camera
segments through the mission basis into the retained source-world BSP and
adapts points and plane covectors back to runtime space. Portal traversal
validates every local legacy fraction before its gate or room change.
`src/airfix/render/LegacyGameplayCameraRoomState.cpp` then produces one complete
candidate `{runtimeWorldPosition, worldRoomIndex}` only after that traversal
finishes. Failures may retain a diagnostic hit and completed-hop count but
never expose a partial state. The same module now composes the completed static
sphere resolver, per-axis collision reduction, and corrected-position portal
trace into one proposed `{position, room, axisFactors}` value. A correction may
therefore prevent or preserve a portal transition before any state is exposed.
`src/airfix/render/LegacyGameplayCameraStateOwner.cpp` now applies that
proposal only after
`completeLegacyGameplayCameraRetainedStaticFrameState` has also run the
retained current-room vehicle-to-camera static line query and the conditional
second portal update. The dedicated single-writer owner then exposes one
owning immutable snapshot to the render consumer. Its two fixed slots use
writer/reader claims and a generation tag, so a rejected or contended commit
cannot change the producer state and a consumer cannot copy mixed fields. This
is an SPSC reconstruction architecture decision, not a recovered claim about
the legacy engine's threading.

`src/airfix/render/LegacyGameplayCameraPoseSnapshot.cpp` consumes one acquired
generation and its matching vehicle anchor. It constructs the recovered
look-at, unit-scale world-to-view relation, and explicit scalar projection as
one immutable backend-neutral value. `CcSrtNode::SetMatrixRotation` and the
final `CcMatrixRot::Reset` both write identity basis, scale `1.0`, and cached
inverse-square `1.0`; canonical Ghidra and an independent Rizin export agree,
so the builder does not accept a guessed scale. Its `project` operation keeps
world-to-view and screen projection separate and does not invent clipping,
NDC, Metal layout, or presentation policy.

`src/airfix/render/LegacyGameplayCameraClipPacket.cpp` owns one copied pose and
derives the homogeneous mapping above without publishing a generic projection
matrix. Its CPU tests compare the divide directly to the ordered scalar
projection, cover exact near/far behavior, and verify the explicit camera0
bootstrap. Objective-C++ repacks the camera basis, translation, cached
inverse-square, projection scalars, and logical canvas into a fixed Metal ABI.
The shader repeats the recovered operations instead of precomposing them.

`src/airfix/render/NativeGameplayScreenProjection.cpp` is the separate modern
screen-consumer bridge. It first obtains the recovered reference-canvas point,
then uses `NativeRenderLayout` to translate it into the centred Hor+ or
safe-FOV camera canvas and through the actual output-pixel scene viewport.
Render scale affects the intermediate 3D target but not the resulting output
coordinate. Original 4:3 maps through its pillarboxed output viewport. The
bridge validates the reference canvas and horizontal FOV, returns off-screen
coordinates without clamping, and labels viewport inclusion independently of
the recovered near/far interval. This is port policy for future HUD/reticle/
weapon/effect consumers, not evidence that the original executable supported
widescreen or used the same off-screen visibility rule. The implementation and
required-aspect tests are recorded in
[EXP-20260731-087](../../experiments/EXP-20260731-087-native-gameplay-screen-projection.md).

`src/airfix/render/LegacyGameplayCameraStepCoordinator.cpp` composes the
recovered producer stages without hiding missing inputs. It accepts the raw
AirCraft scheduler delta, live health, and inactive state, applies cumulative
camera presses to the persistent three-mode cycle, treats rear view as a
momentary tuple, and preserves the distinct native chase-position and
vehicle-anchor inputs. It prebuilds the complete next pose and clip packet
before committing state, mode, or the input count. Failures at any stage
expose no packet and retain the prior authoritative values. The caller still
owns the retained arena, basis, and bounded collision workspaces; no
cross-thread Metal publication is claimed by this producer-side contract.

`src/airfix/render/LegacyGameplayCameraPacketExchange.cpp` provides that
separate render transport for complete owning clip packets. Its two fixed
slots use writer/lease claims plus an exchange-local generation; simulation
steps and camera generations must increase but may skip after a legitimate
busy result.

`src/airfix/render/LegacyGameplayCameraMissionRuntime.cpp` now owns the
authenticated arena, immutable placed/player dynamic BSP assets, a copy of the
runtime basis, exact-size candidate/constraint and flat dynamic-line
workspaces, the coordinator, and that exchange. Metal mission snapshots
account the additional workspace/exchange bytes, validate camera0 through the
runtime's first lease, and retain a lease through command encoding. A weak iOS
producer endpoint expires with mission replacement; a temporary strong lock
borrows no static or dynamic collision data from the Objective-C room
envelope. The endpoint is not advanced and no player collision frame is
published until all live AirCraft inputs are evidence-backed.

The exact next solver boundary is recorded in
[EXP-20260728-014](../../experiments/EXP-20260728-014-camera-sphere-contact-recovery.md).
It fixes static sphere traversal order, the sphere-versus-triangle entry, the
material deletion rule, best-penetration fields, and the constraint loop while
keeping dynamic-object **sphere** collision separate. Runtime-owned placed and
player **line** collision is documented in
[EXP-20260729-041](../../experiments/EXP-20260729-041-mission-runtime-dynamic-collision-ownership.md).

The bounded constraint stage is implemented and documented in
[EXP-20260728-015](../../experiments/EXP-20260728-015-camera-constraint-solver.md).
It preserves the native newest-first plane list, oldest-first active scan,
directional `Overrides` predicate, exact `0.999` duplicate cutoff, exact
`0.0001` two-plane cutoff, and one-/two-/three-plane projection outcomes.
The static BSP contact collector and face/edge/vertex penetration selector are
implemented in
[EXP-20260728-016](../../experiments/EXP-20260728-016-camera-static-sphere-resolver.md).
They preserve seven-axis broad-phase tests, static portal-room discovery,
material filtering, raw edge/vertex directions, reverse-discovery tie order,
and iterative constrained correction. The caller supplies bounded workspaces,
and non-orthonormal bases fail because they would turn a sphere into an
ellipsoid. The all-or-nothing state join is documented in
[EXP-20260728-017](../../experiments/EXP-20260728-017-camera-static-state-integration.md).
[EXP-20260728-018](../../experiments/EXP-20260728-018-camera-state-owner-snapshot.md)
documents the ownership, generation, contention, and immutable-copy contract.
[EXP-20260728-019](../../experiments/EXP-20260728-019-camera-retained-static-pose-snapshot.md)
documents line completion, unit SRT evidence, and pose composition.
[EXP-20260728-020](../../experiments/EXP-20260728-020-metal-gameplay-camera-packet.md)
documents homogeneous Metal packaging, reverse depth, presentation, and the
explicit bootstrap policy.
[EXP-20260728-021](../../experiments/EXP-20260728-021-camera-step-coordinator.md)
documents recovered producer ordering, explicit raw inputs, and transactional
step behavior.
[EXP-20260728-022](../../experiments/EXP-20260728-022-camera-packet-exchange.md)
documents complete-packet SPSC publication, lease lifetime, generation policy,
and Metal consumption.
[EXP-20260728-023](../../experiments/EXP-20260728-023-camera-refresh-time-contract.md)
confirms the scheduler-to-factor-recovery delta in seconds and its nominal
12 ms AirCraft value.
[EXP-20260728-024](../../experiments/EXP-20260728-024-camera-mission-runtime.md)
documents mission ownership, workspace admission, weak endpoint lifetime, and
the deliberate live-producer gate.
[EXP-20260728-025](../../experiments/EXP-20260728-025-camera-aircraft-input-contract.md)
names the complete live input set, including actor health and the vehicle
inactive latch, through a Ghidra/Rizin cross-check.
[EXP-20260728-035](../../experiments/EXP-20260728-035-dynamic-bsp-line-adapter.md)
implements bounded per-mesh dynamic BSP construction and the non-portal
static/dynamic `PhLine` competition. The camera still needs the distinct
dynamic-object `PhSphere` contact path, live room-object publication, and
transparent collision-portal composition before that geometry can join its
state transaction.

## Still unknown

- runtime parity of the recovered chase recurrence and collision-axis factors,
  including controlled traces of the now-confirmed seconds-based refresh
  delta;
- first/third-person pose differences in actor plugins other than the examined
  aircraft path;
- physical-device validation of the implemented Metal clip mapping,
  raster-space Y convention, precision, and native Hor+ presentation;
- whether the original executable had any widescreen behavior beyond the
  confirmed 4:3 evidence. Independently, the reconstruction's accepted port
  policy is now full-target Hor+ and must not be described as recovered
  original behavior;
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
- [Depth-clear experiment](../../experiments/EXP-20260727-008-depth-clear.md)
- [Depth-mode caller experiment](../../experiments/EXP-20260727-009-depth-mode-callers.md)
- [Gameplay camera experiment](../../experiments/EXP-20260727-010-gameplay-camera-modes.md)
- [Portal BSP line trace](../../experiments/EXP-20260727-011-portal-bsp-line-trace.md)
- [Runtime spatial adapter](../../experiments/EXP-20260727-012-runtime-spatial-adapter.md)
- [Camera room-state proposal](../../experiments/EXP-20260728-013-camera-room-state-proposal.md)
- [Camera sphere-contact recovery](../../experiments/EXP-20260728-014-camera-sphere-contact-recovery.md)
- [Camera constraint solver](../../experiments/EXP-20260728-015-camera-constraint-solver.md)
- [Camera static sphere resolver](../../experiments/EXP-20260728-016-camera-static-sphere-resolver.md)
- [Camera static state integration](../../experiments/EXP-20260728-017-camera-static-state-integration.md)
- [Camera state owner and snapshot](../../experiments/EXP-20260728-018-camera-state-owner-snapshot.md)
- [Retained-static line and pose snapshot](../../experiments/EXP-20260728-019-camera-retained-static-pose-snapshot.md)
- [Metal gameplay-camera clip packet](../../experiments/EXP-20260728-020-metal-gameplay-camera-packet.md)
- [Gameplay-camera step coordinator](../../experiments/EXP-20260728-021-camera-step-coordinator.md)
- [Gameplay-camera packet exchange](../../experiments/EXP-20260728-022-camera-packet-exchange.md)
- [Gameplay-camera mission runtime](../../experiments/EXP-20260728-024-camera-mission-runtime.md)
- [AirCraft camera input contract](../../experiments/EXP-20260728-025-camera-aircraft-input-contract.md)
- [Native render layout and Hor+ integration](../../experiments/EXP-20260729-049-native-render-layout-horplus.md)
- [Native gameplay screen projection](../../experiments/EXP-20260731-087-native-gameplay-screen-projection.md)
