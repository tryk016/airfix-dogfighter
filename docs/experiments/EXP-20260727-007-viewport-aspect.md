# Viewport, aspect ratio, and legacy canvas

**Date:** 2026-07-27  
**Evidence:** `EV-20260727-008`  
**Scope:** display-mode selection, camera window/centre callers, gameplay and
widget viewports, horizontal FOV, and the safe parity policy for iOS

## Question

Determine whether the original engine adapts its camera to display aspect,
uses normalized or pixel coordinates, or implements letterbox, pillarbox,
Hor+, or vertical cropping. Separate the full display, gameplay HUD viewport,
and menu/widget preview cameras.

## Inputs and method

| Module | SHA-256 | Image base |
|---|---|---:|
| `Dogfighter.exe` | `F48CD7D16E8D993B262F4E35731ABFB1D95288E0C688506065F168E1B4090E89` | `0x00400000` |
| `Cc.dll` | `18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF` | `0x10000000` |
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |
| `gtDirect3D.dll` | `D7C25DC5D53EE717D5092D465137F18756D8C7951EDAB868892E2AC3DC48D4F3` | `0x10000000` |

Ghidra 12.1.2 Headless and Rizin 0.9.1 analyzed hash-verified read-only copies.
The audit enumerated callers of the camera setters, traced startup and console
resolution paths, and cross-checked the DirectDraw/D3D screen setup. No binary
was executed, patched, uploaded, or opened in a cloud service. Binary Ninja
was not used.

## Result

The original application starts unconditionally at `640x480` and uses an
exclusive fullscreen DirectDraw mode. The main gameplay camera uses a fixed
pixel rectangle inside that logical canvas:

```text
window = (35, 35) - (555, 345)
size   = 520 x 310
centre = (295, 190)
aspect = 52 / 31 ~= 1.677419
```

This is an embedded 3D viewport inside the HUD, not an adaptive 16:9 mode.
The examined code contains no automatic letterbox, pillarbox, Hor+, or
widescreen policy.

## Camera setter callsites

`CcCamera::SetWindow` and `SetCentre` have no caller inside `Cc.dll`. Confirmed
external callsites are:

| Module / function | RVA range | Callsites | Values |
|---|---:|---:|---|
| `AfEngine` `NfEngine::ProcessEvent` | `[0x00040250, 0x0004203B)` | window `0x4068E`, centre `0x406B1` | Full current `GtScreen`: `(0,0,width,height)`, midpoint |
| `AfEngine` `GadScene::Render` | `[0x00071E80, 0x00071F68)` | window `0x71EEC`, centre `0x71F18` | Widget pixel rectangle and midpoint, refreshed each render |
| `Dogfighter` preview function | `[0x0001AC20, 0x0001B29E)` | window `0x1B21F`, centre `0x1B24B` | Menu/aircraft-preview widget rectangle |
| `Dogfighter` gameplay camera init/reset | `[0x0002B8D0, 0x0002B9CF)` | window `0x2B8F8`, centre `0x2B90E` | Fixed `(35,35)-(555,345)`, centre `(295,190)` |

Working names for the two private `Dogfighter.exe` functions are semantic and
carry confidence 2/3; their exact callsites and constants are confidence 3/3.

Related camera functions:

| Function | RVA / callsite | Confirmed behavior |
|---|---:|---|
| gameplay render function | RVA `0x0002B450` | Sets screen and renders; mirror/rear branch changes room/clipping, not window/centre |
| gameplay FOV state | RVA `0x0002AB80`, call `0x2B224` | Applies FOV stored at object `+0x4E4` |
| preview scene constructor | RVA `0x00032130`, call `0x32194` | Sets FOV `50` |
| `NfEngine` constructor | RVA `0x0003F8E0`, call `0x3FA3A` | Sets FOV `90` |
| `NfEngine::ConsoleCommand` | RVA `0x000422A0`, call `0x42640` | Can change FOV |

Gameplay initialization has two recovered parameter branches:

```text
near 0.1, far 500, FOV 90
near 8,   far 500, FOV 50
```

Their gameplay-mode meanings remain to be named.

## Horizontal FOV and aspect consequence

The legacy camera uses horizontal FOV:

```text
focal = 0.5 * windowWidth / tan(horizontalFov / 2)
```

Projection uses:

```text
screenX = focal * x / z + centreX
screenY = centreY - focal * y / z
```

Window height and display aspect do not participate in `focal`. The implied
vertical FOV is:

```text
verticalFov =
    2 * atan((windowHeight / windowWidth) * tan(horizontalFov / 2))
```

If a caller widens the camera window without changing horizontal FOV, vertical
FOV decreases: the result is vertical cropping, not Hor+ expansion.

Coordinates are pixel/logical-canvas values, not normalized fractions.

## Startup display mode

`NfMain::Open`, RVA `0x000448F0`, passes:

```text
width  = 640
height = 480
bpp    = 0
```

to the graphics-device `FindMode` slot (`+0x14`) and then creates the screen
through slot `+0x18`.

In `gtDirect3D.dll`:

```text
vtable +0x14 -> FindMode RVA 0x1990
vtable +0x18 -> create-screen RVA 0x1A20
screen/device initialization RVA 0x2790
```

Initialization calls DirectDraw `SetCooperativeLevel` with `0x813`, selecting
exclusive fullscreen behavior, then calls `SetDisplayMode(width,height,bpp)`.
The D3D viewport covers the selected display dimensions. Integer and float
screen dimensions are retained in `GtScreen`.

The detailed preference among matching bit depths when `bpp == 0` has
confidence 2/3 and does not affect the confirmed 640x480 logical extent.

## Resolution changes

`NfMain::ChangeScreenSize`, RVA approximately
`[0x00045AD0, 0x00045C84)`, searches for exact width/height/depth with a
depth-zero fallback, rebuilds the screen, updates stored width/height/depth,
reapplies Z clipping, and restores the prior mode if setup fails.

`NfMain::ConsoleCommand`, RVA `0x000487E0`, exposes this path for commands with
two or three numeric arguments.

The audit found no automatic resize event or scaling policy. A console-selected
wider mode changes full-screen camera dimensions where that caller reads the
screen, but the fixed gameplay window remains `(35,35)-(555,345)`.

## Port consequence

Binary facts:

- logical startup display is `640x480` / 4:3;
- gameplay 3D renders inside the fixed 520x310 HUD rectangle;
- menu/widget cameras receive their own pixel rectangles;
- the FOV is horizontal and projection has no implicit aspect correction;
- mirror/rear rendering reuses the gameplay window;
- no adaptive widescreen policy was found.

Safe first parity policy for iOS, explicitly a reconstruction decision:

1. Retain a logical `640x480` canvas.
2. Preserve gameplay camera window `(35,35)-(555,345)` and centre `(295,190)`.
3. Aspect-fit the logical 4:3 canvas inside the physical drawable.
4. Treat pillar/letterbox regions and device safe areas as presentation space,
   not as extra legacy camera extent.
5. Add widescreen/Hor+ only as a separate option with dedicated HUD, culling,
   touch-layout, and visual-parity tests.

This policy prevents a modern device aspect from silently changing vertical
framing or moving the original HUD.

`LegacyCanvasLayout` now implements that policy as a backend-neutral,
allocation-free C++20 factory. Recovered canvas/gameplay constants are exposed
separately from the aspect-fit result, and the factory validates target
extents and derived endpoints before publishing an immutable layout. UIKit
safe areas, orientation, display scale, and Metal drawable conversion remain
outer-platform responsibilities.

## Remaining limits

- The full semantics of `NfEngine::ProcessEvent` case five are unnamed.
- Later scripts/configuration could invoke console resizing; full
  `system.ini` behavior was not part of this audit.
- The available display-mode list and selected bit depth require runtime
  DirectDraw enumeration to observe.
- `gt3DFX.dll` is outside this result; the active Direct3D path is confirmed.
- Exhaustive raw writes that bypass the named camera setters were not proven
  absent.
