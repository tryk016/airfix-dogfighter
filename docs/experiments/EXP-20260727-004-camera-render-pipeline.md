# Camera projection, room rendering, and portal traversal

**Date:** 2026-07-27  
**Evidence:** `EV-20260727-005`  
**Scope:** `CcCamera` projection parameters, polygon submission, room
visibility, portal recursion, and camera room transitions

## Question

Recover the statically provable path from the current camera and room to the
legacy screen backend. Determine which values control projection, whether room
rendering traverses static BSP, and how the camera changes rooms. Do not infer a
Metal clip-space matrix or gameplay camera mode from incomplete evidence.

## Inputs and environment

| Module | SHA-256 | Image base |
|---|---|---:|
| `Cc.dll` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | `0x10000000` |
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |
| `gtDirect3D.dll` | `D7C25DC5D53EE717D5092D465137F18756D8C7951EDAB868892E2AC3DC48D4F3` | `0x10000000` |

Ghidra 12.1.2 Headless and Rizin 0.9.1 analyzed hash-identical read-only
working copies. Raw reports and project databases remain below ignored local
boundaries. No module was executed, debugged, patched, uploaded, or opened in a
cloud service. Binary Ninja was not used.

## Procedure

1. Export exact Ghidra functions and instructions for the camera render,
   projection, clipping, portal, and BSP entry points.
2. Independently analyze the same RVAs with Rizin and compare function starts,
   ends, callsites, ECX receiver use, and `RET n` cleanup.
3. Export the scalar values read by `CcCamera::Render`.
4. Trace setters and their callers to distinguish constructor defaults from
   the values installed by the game engine.
5. Follow the polygon path until the concrete Direct3D screen implementation.
6. Trace both render-time portal recursion and movement-time room selection.

## Function and ABI comparison

| Function | Ghidra / Rizin boundary | Confirmed ABI and role |
|---|---:|---|
| `CcPolyVertex::Project` | `[0x0000CCE0, 0x0000CDA0)` | `__thiscall(CcCamera*)`; converts one clipped view-space vertex to screen coordinates |
| `CcCamera::Render` | `[0x0001BB20, 0x0001BEC8)` | `__thiscall(bool)`; derives camera scalars and starts current-room rendering |
| `CcCamera::RenderRoom` | `[0x0001C6A0, 0x0001E524)` | `__thiscall(CcRoom*, CcScreenClip*, int, int)`; gathers room lists and recurses through portals |
| `CcCamera::RenderLayerPolys` | `[0x0001EB20, 0x0001EDF1)` | `__thiscall(CcRenderList&)`; drains an ordered room render list |
| render-layer clip helper | `[0x0001EE00, 0x0001F62E)` | `__stdcall(camera, v0, v1, v2, plane)`; recursively clips six planes |
| `CcObject::AddVisiblePolygonsToRenderList` | `[0x0001FDD0, 0x00020AFA)` | `__thiscall(CcCamera*, CcRenderList&, bool)`; transforms and queues visible polygons |
| `GtPolyRender::ProcessPoly` | `[0x00046130, 0x0004617B)` | forwards the processed triangle through `GtScreen` vtable slot `+0x44` |
| `CcDistClip::ProcessPoly` | `[0x000477F0, 0x00047BBA)` | clips far then near; projects three vertices only in its final phase |
| `CcRoom::TracePortals` | `[0x000216C0, 0x00021719)` | resolves the room reached by a camera movement segment |
| `PhLine::GetPortalBspCollision` | `[0x00039070, 0x000391CC)` | queries the room portal-BSP list |
| `PhLine::TraceBspNode` | `[0x000391D0, 0x000398E1)` | near-first segment traversal with nearest-hit retention |
| Direct3D `GtScreen::RenderTriangle` | `[0x000034B0, 0x00003669)` | stages three converted vertices in the concrete screen buffer |

Ghidra and Rizin agree on these raw boundaries and their relevant direct
callsites. Recovered MSVC declarations, ECX receiver use, and `RET n` cleanup
override Rizin's incorrect `cdecl` proposals for several exported methods.

## Exact projection path

`CcCamera::Render` clamps FOV to `[1, 175]` degrees and computes:

```text
windowWidth = right - left
focal       = 0.5 * windowWidth / tan(FOV * pi / 360)
nearInv     = 1 / near
projectScale = focal / near
farRatio    = far / near
farInv      = 1 / far
```

`CcPolyVertex::Project` then uses:

```text
factor  = near / z, when z > near
factor  = 1,        otherwise
screenX = projectScale * factor * x + centreX
screenY = centreY - projectScale * factor * y
```

The function therefore faces positive view-space Z and performs the screen-Y
inversion explicitly. `CcDistClip::ProcessPoly` clips against far first and
near second before the final three projection calls at RVAs `0x0004784F`,
`0x00047861`, and `0x00047873`. It is the only direct caller of
`CcPolyVertex::Project`.

Confirmed camera fields are:

| Offset | Meaning |
|---:|---|
| `+0x8FC` | derived far-corner radius; exact formula confirmed but durable semantic name pending |
| `+0x900` | focal screen scale |
| `+0x904` | `1 / near` |
| `+0x908` | `focal / near` |
| `+0x90C` | `far / near` |
| `+0x914` | `1 / far` |
| `+0x918` | `windowRight - windowLeft` |
| `+0x928`, `+0x92C` | near and far clipping distances |
| `+0x930` | FOV in degrees |
| `+0x934`, `+0x938` | screen centre X and Y |
| `+0x93C` through `+0x948` | window left, top, right, and bottom |

The constructor defaults are near `1`, far `100`, FOV `90`, centre
`(0.5, 0.5)`, and window `(0, 0)-(1, 1)`. Engine initialization replaces near
and far with `0.25` and `200`, and sets FOV to `90`. Console paths can later
change both values.

The scalar constants are exact:

| Cc.dll RVA | Float | Use |
|---:|---:|---|
| `0x0004A0D0` | `1.0` | reciprocal numerator and lower FOV bound |
| `0x0004A0F4` | `0.5` | half-width |
| `0x0004A67C` | `0.00872664619` | `pi / 360` |
| `0x0004A680` | `175.0` | upper FOV bound |
| `0x0004A688` | negative zero | orientation threshold input |
| `0x0004A6A0` | `0.75` | far-distance reduction in recursive room rendering |
| `0x0004A6A4` | `0.998000026` | layer-position scale before clipping |

## Polygon submission

The confirmed path is:

```text
CcCamera::Render
  -> CcCamera::RenderRoom
     -> CcObject::AddVisiblePolygonsToRenderList
     -> CcCamera::RenderLayerPolys
        -> six-plane render-layer clip helper
        -> CcDistClip::ProcessPoly
           -> CcPolyVertex::Project
           -> GtPolyRender::ProcessPoly
              -> GtScreen::RenderTriangle
                 -> concrete Direct3D screen staging and flush
```

`RenderRoom` is the only direct caller of
`AddVisiblePolygonsToRenderList`, at RVAs `0x0001CBBD`, `0x0001DA60`,
`0x0001E2CF`, and `0x0001E3EC`. It calls `RenderLayerPolys` at
`0x0001E492` and `0x0001E4C6`.

The concrete Direct3D screen stores its device link at `screen + 0x2C`, while
the device stores the screen at `device + 0x2C`. Per-triangle submission stages
through `GtScreen`; it does not call `GtDevice` directly.

## Current room and portal recursion

`CcCamera + 0xF4` is the current room. `SetRoom` accepts only a non-null room
whose `room + 0x2C` is non-null, and invalid input does not erase a previously
accepted room. `SetPosAndTracePortals` compares the old and new world
positions, calls `CcRoom::TracePortals`, and replaces the current room only when
that query returns a room.

`CcCamera::Render` begins with:

```text
RenderRoom(currentRoom, rootClip, 1, 0)
```

Render-time portal candidates come from the list at `room + 0x0C`; their target
room is stored at `portalObject + 0x13C`. A candidate must survive object
culling and produce a non-empty clip. Each recursive call owns a scoped
`CcShapeBuilder`, so the derived clip exists only during that call.

The fourth `RenderRoom` parameter is recursion depth. The constructor installs
the limit `2`; every recursive entry increments depth. No general visited-room
set was found. The hard limit is the confirmed cycle bound.

For `portalType == 1`, the code reflects camera position and basis across the
portal plane and reverses culling before rendering the target. The behavior
strongly indicates a mirror portal, but the enum name is not yet proven. The
other branch clips normally and enters the target room without reflection.

## Portal-BSP is not render traversal

Camera movement uses the portal-BSP list at `room + 0x44`.
`PhLine::TraceBspNode` computes signed distances for both segment endpoints and
the crossing fraction `t = d0 / (d0 - d1)`. It visits the child on the segment
start side, tests polygons on the crossed plane, then visits the end-side
child, retaining the nearest hit and its polygon. That polygon resolves the
next room through its owning object.

Follow-up experiment
[EXP-20260727-011](EXP-20260727-011-portal-bsp-line-trace.md) closes the
child-side mapping, prepend traversal order, exact polygon test, portal follow
gate, unused third argument, and bounded portable implementation.

The confirmed `RenderRoom` path does **not** walk static-BSP left/right
children. It renders room object lists with culling and explicit portal
recursion. Static BSP must therefore remain disabled as a rendering
optimization until a different proven caller requires it.

## Confidence and remaining unknowns

Confidence is 3/3 for the listed function boundaries, raw calls, projection
formula, constructor defaults, engine near/far/FOV values, current-room field,
portal depth limit, and separation between render-time portal recursion and
movement-time portal-BSP tracing.

The following are still unknown:

- the complete camera view/basis layout and a validated `viewFromWorld`;
- the exact Metal clip/depth mapping and legacy W-buffer equivalence;
- gameplay camera modes, chase offsets, smoothing, rear view, and FOV policy;
- durable names for every room list, clip virtual method, and portal enum;
- whether any path raises the portal recursion limit above its default;
- widescreen policy and whether faithful 4:3 or expanded framing is intended.

This list records the boundary of this experiment when it was completed.
Follow-up experiments
[EXP-20260727-005](EXP-20260727-005-camera-world-to-view.md) through
[EXP-20260727-010](EXP-20260727-010-gameplay-camera-modes.md) subsequently
closed the world-to-view, depth, clear, viewport, and gameplay-camera facts
without changing the evidence reported above.

## Reconstruction consequence

`LegacyScreenProjection` now reproduces and tests the confirmed scalar
contract for points already in camera space. It preserves the near fallback
and float operation order, projects beyond far without clipping, and exposes
no view, NDC, depth, or Metal matrix. The native renderer must not replace its
diagnostic matrix with a claimed parity camera until those remaining contracts
are recovered.
