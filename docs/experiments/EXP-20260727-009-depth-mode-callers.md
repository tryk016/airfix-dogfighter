# Legacy depth-mode callers and frame ordering

**Date:** 2026-07-27  
**Evidence:** `EV-20260727-010`  
**Scope:** `GtScreen::SetRenderState`, its Direct3D implementation, direct
callers in `Cc.dll`, `AfEngine.dll`, and `Dogfighter.exe`, and the relationship
between those calls, depth clears, and the D3D7 scene boundary

## Question

Determine which rendering categories select each of the four legacy depth
modes. Recover the exact virtual slot and direct callsites, then place those
state transitions relative to the room render, depth clear, `BeginScene`,
`EndScene`, and presentation.

## Inputs and method

| Module | SHA-256 | Image base |
|---|---|---:|
| `Dogfighter.exe` | `F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89` | `0x00400000` |
| `Cc.dll` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | `0x10000000` |
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |
| `gtDirect3D.dll` | `D7C25DC5D53EE717D5092D465137F18756D8C7951EDAB868892E2AC3DC48D4F3` | `0x10000000` |

Ghidra 12.1.2 Headless recovered MSVC symbols, signatures, containing
functions, and typed receiver fields. Rizin 0.9.1 independently scanned the
x86 call encodings for every register form of:

```text
CALL dword ptr [register + 0x18]
```

Each candidate was inspected in its containing function. Calls on COM, audio,
network, database, input-window, and other unrelated vtables were rejected
only after checking their receiver and surrounding API calls. The remaining
`GtScreen` calls were compared instruction by instruction with Ghidra. No
binary was executed, patched, uploaded, or opened in a cloud service.

This is a bounded direct-call inventory for the four listed modules. It does
not claim that an unusual two-instruction virtual dispatch, a different
graphics adapter, or an unexamined plugin could not select a mode.

## Virtual slot and concrete implementation

The exported abstract interface identifies the operation as:

```text
GtScreen::SetRenderState(GT_RS)
```

The `Cc.dll` `GtScreen` vtable is at RVA `0x0004A240`. The method is vtable
slot `+0x18`, index 6. The Direct3D screen vtable is at RVA `0x0000510C`; its
entry at RVA `0x00005124` points to:

```text
gtDirect3D.dll RVA 0x00003BF0
```

The concrete function occupies `[0x00003BF0, 0x00003D33)`. It stores the
requested logical mode at `screen + 0x44`, tracks the current D3D `ZFUNC` at
`screen + 0x98`, and tracks `ZWRITEENABLE` at `screen + 0x9C`.

For the active conventional-Z path, where `screen + 0x4A == 0`, the mapping
is:

| Mode | D3D `ZFUNC` | Depth write | Intended category established by callers |
|---:|---|---:|---|
| `1` | `GREATEREQUAL` | on | depth-tested opaque geometry and 3D lines |
| `2` | `ALWAYS` | on | unconditional depth replacement, 2D primitives, fonts, radar |
| `3` | `GREATEREQUAL` | off | depth-tested ordered/layer geometry |
| `4` | `ALWAYS` | off | lens stars/flares and final unoccluded overlays |

The dormant hardware-W path changes `GREATEREQUAL` to `LESSEQUAL` for modes 1
and 3. No enabling write to `screen + 0x4A` was found in the examined DLL.

Before changing either D3D state, the setter flushes pending transformed
vertices through the backend helper at RVA `0x00004220`. It then calls
`IDirect3DDevice7::SetRenderState` with state `0x17` (`ZFUNC`) or `0x0E`
(`ZWRITEENABLE`). A transition that changes both fields may flush once before
each changed field. A repeated logical mode whose two D3D fields already
match does not force a flush.

This ordering is important: triangles accumulated before a mode call are
submitted under the old state, and triangles accumulated afterward use the
new state.

## Direct caller inventory

### `Cc.dll`

| Caller | Callsite RVA | Mode | Confirmed render category |
|---|---:|---:|---|
| `CcCamera::RenderLine` | `0x0001ACA1` | `1` | non-degenerate clipped 3D line |
| `CcCamera::Render` | `0x0001BE02` | `4` | lens depth sampling followed by lens-star and lens-flare overlays |
| `CcCamera::RenderRoom` | `0x0001CB7D` | `2` | first gathered room/object polygon pass |
| `CcCamera::RenderRoom` | `0x0001E286` | `2` | later selected room/portal-object pass |
| `CcCamera::RenderRoom` | `0x0001E376` | `1` | primary depth-tested object/custom-object/sprite pass |
| `CcCamera::RenderRoom` | `0x0001E485` | `3` | sorted ordered/layer polygon drain |
| `GtTiltedFont::Print` | `0x0003FAC7` | `2` | font decoration primitive |
| `GtScreen::BlitStretch` | `0x0004517A` | `2` | textured 2D quad |
| `GtScreen::BlitStretchRotated` | `0x000454F3` | `2` | rotated textured 2D quad |
| `GtScreen::Fill(CcColorInt, float...)` | `0x00045766` | `2` | solid 2D quad |
| `GtScreen::Fill(CcColorInt, CcCoord2d*)` | `0x000458DB` | `2` | solid arbitrary 2D quad |
| base `GtScreen::ClearZ` fallback | `0x00045B8B` | `2` | zero-depth full/partial quad fallback |

The four shorter `BlitStretch` overloads and the simpler `Fill` overloads
delegate to the implementations listed above. They therefore select mode 2
transitively rather than containing another direct `+0x18` dispatch.

The base `GtScreen::ClearZ` implementation constructs a zero-depth quad and
selects mode 2 before submission. The Direct3D adapter overrides this vtable
entry with a rectangular hardware clear, so normal Direct3D virtual dispatch
does not use the base quad fallback.

### `AfEngine.dll`

All semantic `GtScreen` matches in `AfEngine.dll` belong to
`NfMission::RenderRadar`, RVA `[0x00031930, 0x000339A6)`. Every one selects
mode 2:

```text
0x00031B76
0x00031D36
0x00032373
0x00032577
0x0003277B
0x00032970
0x00032CF3
0x00033063
0x0003332C
0x00033827
```

Each call is followed by material selection and two triangle submissions for
a radar/HUD primitive. Rizin confirms an immediate `PUSH 2` at all ten
callsites. The other `+0x18` candidates in `AfEngine.dll` operate on D3D/Direct
Show/DirectSound COM interfaces, databases, sockets, server objects, or window
input objects and are not `GtScreen::SetRenderState`.

No direct mode 1, 3, or 4 selection was found in `AfEngine.dll`.

### `Dogfighter.exe`

Two unnamed, statically linked window renderers select mode 2:

| Function RVA | Callsite RVA | Category evidence |
|---:|---:|---|
| `0x00002BB0` | `0x00003229` | mission-number text, menu textures, and an animated two-triangle selection marker |
| `0x00003760` | `0x00003D79` | mission/detail text, menu textures, and an animated two-triangle selection marker |

Both functions receive a `GtScreen*`, store it in the same temporary global,
push `2`, call vtable slot `+0x18`, select a material through slot `+0x14`,
and submit the two marker triangles through slot `+0x44`. This establishes a
2D mission-selection UI category without inventing an unavailable class name.

No direct mode 1, 3, or 4 selection was found in `Dogfighter.exe`.

### `gtDirect3D.dll`

The adapter installs the concrete method in its vtable but does not call its
own `GtScreen` mode slot. Its unrelated `+0x18` indirect calls belong to D3D
and DirectDraw COM interfaces.

## Gameplay room sequence

The recovered mode order in `CcCamera` is:

```text
CcCamera::Render
  CcCamera::RenderRoom(currentRoom, rootClip, 1, 0)
    mode 2  ALWAYS / write
      first gathered room/object polygon pass
      clipped portal recursion may repeat the RenderRoom sequence

    mode 2  ALWAYS / write
      selected room/portal-object pass

    mode 1  GREATEREQUAL / write
      primary visible objects, custom objects, sprites

    mode 3  GREATEREQUAL / no-write
      ordered/layer polygons

  mode 4  ALWAYS / no-write
    lens depth lookup, lens stars, lens flares
```

The two mode-2 lists are distinguished by exact list/call boundaries, but
durable source enum names for those room categories are not available.
Calling the first pass “background” or the second pass “portal mask” would be
a hypothesis, so this report retains structural names.

Mode 3 is selected immediately before two calls to
`CcCamera::RenderLayerPolys` around the `CcLinkSort` operation. This makes the
depth-tested/no-write interpretation stronger than a state-table-only guess.
Mode 4 is selected only after `RenderRoom` returns and before the lens-effect
lists are traversed.

## Clear and D3D scene relationship

Depth clearing is not performed by the mode setter.

The confirmed embedded `GadScene` camera path is:

```text
GadScene::Render                           AfEngine RVA 0x00071E80
  GtScreen::ClearZ(widget rect)            vtable +0x7C
  CcCamera::SetWindow(widget rect)
  CcCamera::SetCentre(widget midpoint)
  CcCamera::SetClippingDist(0.02, 2000)
  CcCamera::SetScreen
  CcCamera::SetRoom
  CcCamera::Render(true)
```

The Direct3D `ClearZ` override at RVA `0x00004580` flushes pending vertices and
calls:

```text
IDirect3DDevice7::Clear(
    count   = 1,
    rects   = clipped widget rectangle,
    flags   = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
    color   = 0xFF000000,
    z       = 0.0f,
    stencil = 0)
```

Thus the active reverse-depth target is cleared to exactly zero immediately
before the camera begins its room passes. The same call clears that camera
rectangle to opaque black.

At the frame level, `NfMain::ProcessEvent` RVA `0x00047880`, event 4:

1. Calls `GtScreen::ScreenClear` through vtable `+0x04` only when there is no
   local player.
2. Runs pre-render, main render, windows/widgets, post-render, and final render
   heap phases.
3. Calls `GtScreen::ScreenFlip` through vtable `+0x08`.

The Direct3D `ScreenClear` implementation at RVA `0x00003010` resets the
staging count and calls:

```text
Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 0.0f, 0)
```

The Direct3D `ScreenFlip` implementation at RVA `0x00003060` performs:

```text
flush pending vertices
IDirect3DDevice7::EndScene
present primary/back surface
IDirect3DDevice7::BeginScene
```

Device initialization issues the first `BeginScene`. Each flip ends and
presents the scene just rendered, then immediately opens the D3D scene for the
next frame. Mode changes and rectangular clears therefore happen inside the
already-open scene; they are not scene boundaries.

## Representative pseudocode

```text
setDepthMode(mode):
    if not screenReady:
        return

    requestedMode = mode
    desiredCompare =
        mode in {1, 3} ? activeReverseCompare : ALWAYS
    desiredWrite =
        mode in {1, 2}

    if currentCompare != desiredCompare:
        flush()
        device.SetRenderState(ZFUNC, desiredCompare)
        currentCompare = desiredCompare

    if currentWrite != desiredWrite:
        flush()
        device.SetRenderState(ZWRITEENABLE, desiredWrite)
        currentWrite = desiredWrite
```

```text
renderEmbeddedSceneView(rect):
    screen.ClearZ(rect, color=opaqueBlack, depth=0)
    camera.Configure(rect, room, screen)
    camera.RenderRoom()        // modes 2 -> 2 -> 1 -> 3
    camera.RenderLensEffects() // mode 4
```

## Tool comparison and confidence

| Question | Ghidra | Rizin | Result |
|---|---|---|---|
| vtable slot and receiver type | strongest: exported MSVC `GtScreen` symbols and typed `CcCamera + 0xF8` / `NfMain` screen fields | confirmed vtable words and raw indirect calls | agreement |
| exact mode arguments | recovered all C++ calls, including the large `RenderRoom` function | confirmed immediate `PUSH` values at all simple sites and raw call positions in `RenderRoom` | agreement |
| backend state transition | clearest pseudocode and D3D call roles | clearest switch bytes, state IDs, and flush calls | agreement |
| frame/clear ordering | clearest named `NfMain`, `GadScene`, and `CcCamera` call chain | confirmed vtable offsets and exact instruction order | agreement |

Confidence is **3/3** for:

- slot `+0x18`, concrete RVA `0x00003BF0`, and all listed direct callsites;
- the four D3D compare/write combinations;
- the `CcCamera` mode order;
- all ten radar mode-2 selections and both Dogfighter UI selections;
- flush-before-state-change behavior;
- depth value zero in both full-screen and rectangular D3D clears;
- `flush -> EndScene -> present -> BeginScene` at `ScreenFlip`.

Confidence is **2/3** for the prose category attached to the two unnamed
mode-2 room lists. Their exact structural positions are confirmed, but their
original enum/member names are not.

## Reconstruction consequence

The native renderer should expose four explicit legacy depth-state presets,
not infer depth behavior from material alpha:

```text
opaqueDepthTestWrite       = greaterEqual, write
unconditionalDepthWrite    = always,       write
layerDepthTestNoWrite      = greaterEqual, no-write
overlayNoDepthNoWrite      = always,       no-write
```

`LegacyDepthState` now implements this raw-mode-to-preset table as a
fail-closed, allocation-free portable C++20 boundary and exposes the confirmed
zero clear scalar. It does not change Metal pipeline state; encoder and batch
integration remain a later step after the gameplay camera contract.

Changing a preset must close or flush the current draw batch. The confirmed
embedded scene path must clear its owned rectangle to reverse-depth zero
before its room passes. Lens effects and 2D UI must remain separate categories
even when a later Metal implementation combines compatible commands.

The recovered order does not authorize copying D3D7's half-pixel convention,
changing sort keys, or assigning invented material names to the two unnamed
room passes.

## Remaining limits

- Durable names for the two mode-2 `RenderRoom` lists remain unknown.
- This audit does not cover `gt3DFX.dll` or every game plugin.
- No runtime capture has verified which conditional room branches are taken
  in a particular mission.
- The unusual point branch in `CcCamera::RenderLine` does not select a mode
  itself and therefore inherits the current state; its practical callers
  remain to be classified.
- Metal pipeline-state packaging, encoder breaks, and load/store actions are
  reconstruction decisions and are not encoded in the legacy API.

The exact full-target and rectangular clear contracts are recorded separately
in
[EXP-20260727-008](EXP-20260727-008-depth-clear.md).
