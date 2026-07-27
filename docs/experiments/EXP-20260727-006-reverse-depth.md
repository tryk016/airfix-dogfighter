# Direct3D reverse-depth contract

**Date:** 2026-07-27  
**Evidence:** `EV-20260727-007`  
**Scope:** clipped camera depth, `GtVertex`, Direct3D transformed vertices,
depth-buffer selection, compare/write modes, and the Metal depth consequence

## Question

Determine exactly how a clipped positive-Z camera-space vertex becomes a D3D7
depth value. Distinguish hardware W-buffer use from a conventional Z-buffer
carrying a manually computed reciprocal depth. Recover the compare/write
states and viewport range without assuming a Metal projection matrix.

## Inputs and method

| Module | SHA-256 | Image base |
|---|---|---:|
| `Cc.dll` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | `0x10000000` |
| `gtDirect3D.dll` | `D7C25DC5D53EE717D5092D465137F18756D8C7951EDAB868892E2AC3DC48D4F3` | `0x10000000` |

Ghidra 12.1.2 Headless and Rizin 0.9.1 independently analyzed hash-verified
read-only copies. The audit compared exact instructions, function boundaries,
field offsets, FVF constants, device states, and callsites. No binary was
executed, patched, uploaded, or opened in a cloud service. Binary Ninja was
not used.

## Result

The active `gtDirect3D.dll` path uses an ordinary D3D7 Z-buffer, not the
hardware W-buffer mode. It manually stores reciprocal positive camera depth in
both transformed-vertex fields:

```text
D3D z   = near / cameraZ
D3D rhw = near / cameraZ
```

Nearer fragments therefore have larger depth values. The ordinary opaque
depth mode uses `D3DCMP_GREATEREQUAL`.

## Function boundaries

| Function | RVA range | Role |
|---|---:|---|
| `CcPolyVertex::CcPolyVertex` | `[0x0000CCC0, 0x0000CCD7)` | Constructs the embedded `GtVertex` and clears projection flags |
| `CcPolyVertex::Project` | `[0x0000CCE0, 0x0000CDA0)` | Writes screen X/Y, camera Z, and reciprocal homogeneous W |
| `GtPolyRender::ProcessPoly` | `[0x00046130, 0x0004617D)` | Forwards three embedded `GtVertex` records |
| `CcDistClip::ProcessPoly` | `[0x000477F0, 0x00047BBA)` | Clips far then near before projection |
| `CcDistClip::ClipLine` | `[0x00047BC0, 0x00047C7C)` | Interpolates one plane intersection and vertex attributes |
| D3D `RenderTriangle` | `[0x000034B0, 0x00003669)` | Accepts three processed vertices |
| D3D TL conversion | `[0x00004000, 0x00004213)` | Creates one 44-byte transformed/lit vertex |
| D3D flush | `[0x00004220, 0x0000451E)` | Calls `DrawPrimitive` |
| D3D screen/device setup | `[0x00002790, 0x00002D8E)` | Selects Z/W mode and installs the D3D viewport |

Ghidra provided the most reliable object and API types. Rizin independently
confirmed the raw instructions, offsets, FVF, and render-state constants;
private functions at RVAs `0x34B0`, `0x4000`, and `0x4220` required manual
function definitions in Rizin.

## Projection and clipping

After far and near clipping, the accepted range is inclusive:

```text
near <= cameraZ <= far
```

Far rejects only when `far < cameraZ`; near rejects only when
`cameraZ < near`. A clipped line intersection uses:

```text
t = (outside.z - planeZ) / (outside.z - inside.z)
result = outside + t * (inside - outside)
result.z = planeZ
```

`GtVertex::ClipFrom` interpolates the embedded vertex attributes.

`CcPolyVertex::Project` then computes:

```text
factor = cameraZ > near ? near / cameraZ : 1

screenX = (focal / near) * factor * cameraX + centreX
screenY = centreY - (focal / near) * factor * cameraY

projectedCameraZ = cameraZ
rhw = factor
```

For a vertex that has passed clipping:

```text
near / far <= rhw <= 1
```

The relevant camera fields remain the offsets established by the camera
pipeline audit:

| Camera offset | Meaning |
|---:|---|
| `+0x900` | focal |
| `+0x904` | `1 / near` |
| `+0x908` | `focal / near` |
| `+0x918` | camera window width |
| `+0x928`, `+0x92C` | near and far |
| `+0x934`, `+0x938` | screen centre X/Y |

## Embedded `GtVertex`

`CcPolyVertex` constructs `GtVertex` at `this + 0x0C`.

Relative to the embedded `GtVertex`:

```text
+0x08  screenX
+0x0C  screenY
+0x10  cameraZ
+0x64  rhw = near / cameraZ
```

`GtPolyRender::ProcessPoly` passes three pointers at
`CcPolyVertex + 0x0C` to the `GtScreen` vtable slot at `+0x44`.

## D3D7 transformed vertex

The converter at RVA `0x4000` creates a 44-byte record:

```text
+0x00 float x       = screenX - 0.5
+0x04 float y       = screenY - 0.5
+0x08 float z
+0x0C float rhw     = source.rhw
+0x10 DWORD diffuse
+0x14 float u0
+0x18 float v0
+0x1C float u1
+0x20 float v1
+0x24 float u2
+0x28 float v2
```

The half-pixel subtraction belongs to the D3D7 raster convention and must not
be copied blindly into Metal.

The active path has `screen + 0x4A == 0`:

```text
dst.z   = source.rhw
dst.rhw = source.rhw
```

The dormant branch uses:

```text
dst.z   = source.cameraZ
dst.rhw = source.rhw
```

Flush calls:

```text
DrawPrimitive(
    D3DPT_TRIANGLELIST,
    0x344,
    vertexBuffer,
    vertexCount,
    0)
```

FVF `0x344` is:

```text
D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX3
```

## Z-buffer selection

Device setup uses:

```text
SetRenderState(D3DRENDERSTATE_ZENABLE, bool(screen[0x4A]) + 1)
```

Therefore:

```text
screen+0x4A == 0  -> D3DZB_TRUE = 1
screen+0x4A != 0  -> D3DZB_USEW = 2
```

The field is zeroed during initialization. The audit found its reads at RVAs
`0x37E1`, `0x3C17`, `0x3CCC`, and `0x4025`, but no write that sets it to one.
The examined driver therefore actively uses `D3DZB_TRUE`: a conventional
Z-buffer carrying manually reversed reciprocal depth.

## Depth modes

The setter beginning at RVA `0x3BF0` implements:

| Mode | Active `ZFUNC` | `ZWRITEENABLE` |
|---:|---|---:|
| `1` | `GREATEREQUAL` | `1` |
| `2` | `ALWAYS` | `1` |
| `3` | `GREATEREQUAL` | `0` |
| `4` | `ALWAYS` | `0` |

The dormant hardware-W branch substitutes `LESSEQUAL` for modes one and
three. D3D render-state IDs are:

```text
ZWRITEENABLE = 0x0E
ZFUNC        = 0x17
```

## Viewport depth

Device initialization installs:

```text
dwX      = 0
dwY      = 0
dwWidth  = requestedWidth
dwHeight = requestedHeight
dvMinZ   = 0
dvMaxZ   = 1
```

The native target can therefore retain Metal's `[0,1]` depth range.

## Metal consequence

The evidence-backed scalar mapping is:

```text
depth = near / cameraZ
near  -> 1
far   -> near / far
```

Opaque legacy mode one corresponds to `greaterEqual` with depth writes.
Modes two through four separately control unconditional comparison and writes.

The portable `LegacyScreenProjection` result now carries the input
camera-space Z and the exact projection factor as `legacyRhw`. This is a
bounded C++20 implementation of the confirmed scalar contract; it does not yet
select a Metal compare function, clear value, or clip-space matrix.

The follow-up
[depth-clear audit](EXP-20260727-008-depth-clear.md) recovered both concrete
Direct3D clear calls. Each uses `TARGET|ZBUFFER` and exact depth `0.0f`, so zero
is now confirmed original evidence rather than only a reconstruction
requirement.

## Remaining limits

- No runtime D3D7 capture has been performed.
- Reachability of the dormant hardware-W branch in other driver versions is
  unknown; the examined DLL contains no enabling write.
- Some attributes interpolated by `GtVertex::ClipFrom` remain unnamed.
- Gameplay camera pose and viewport facts are recorded in
  [EXP-20260727-010](EXP-20260727-010-gameplay-camera-modes.md); their stateful
  implementation and Metal packaging remain separate contracts.
