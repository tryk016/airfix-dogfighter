# Direct3D depth-clear calls

**Date:** 2026-07-27  
**Evidence:** `EV-20260727-009`  
**Scope:** exact Direct3D 7 clear calls, values, ordering, callers, and the
reverse-depth consequence

## Question

Recover the exact legacy depth-buffer clear value and distinguish the
whole-target frame clear from the rectangular scene clear. Record their flags,
color, Z, stencil, buffered-render state, error handling, and confirmed callers.

## Inputs and method

| Module | SHA-256 | Image base |
|---|---|---:|
| `gtDirect3D.dll` | `D7C25DC5D53EE717D5092D465137F18756D8C7951EDAB868892E2AC3DC48D4F3` | `0x10000000` |
| `AfEngine.dll` | `A4CCC7E59E4119488322E80077803A5BB9922731D84CE1A420FD27516D45711E` | `0x10000000` |

Ghidra 12.1.2 Headless and Rizin 0.9.1 analyzed the same hash-verified
read-only copies. The hashes were checked again after analysis. The audit used
only static instructions, decompilation, vtable data, symbol-bearing callers,
and the documented Direct3D 7 COM ABI. No binary was executed, patched,
uploaded, or opened in a GUI.

Rizin's whole-text instruction search found exactly two indirect calls through
slot `+0x28` whose receiver dataflow is the `IDirect3DDevice7*` stored at
concrete-screen offset `+0x104`. Slot `+0x28` is
`IDirect3DDevice7::Clear`.

## Result

The legacy renderer clears the ordinary Z-buffer to exact floating-point
`0.0f`. This confirms the value required by its manually reversed reciprocal
depth:

```text
clear depth = 0
near depth  = 1
far depth   = near / far > 0
compare     = GREATER_EQUAL in ordinary opaque mode
```

Both clear paths use:

```text
D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER = 0x00000003
dvZ                                      = 0.0f
dwStencil                                = 0
```

The stencil argument is present because it belongs to the API signature, but
the stencil flag is not set.

## Functions and callsites

All ranges are end-exclusive.

| Stable ID / function | RVA range | Clear callsite | Role |
|---|---:|---:|---|
| `FN-GTDIRECT3D-00003010` | `[0x00003010, 0x00003052)` | `0x00003036` | Whole-target color and Z clear |
| `FN-GTDIRECT3D-00004580` | `[0x00004580, 0x000046DD)` | `0x000046D1` | Clipped rectangular color and Z clear |
| `NfMain::ProcessEvent` | `[0x00047880, 0x00048288)` | `0x00047C54` | Calls whole-target screen slot `+0x04` at frame start when no active local-player render owns the frame |
| `GadScene::Render` | `[0x00071E80, 0x00071F68)` | `0x00071EB9` | Calls rectangular screen slot `+0x7C` before rendering its camera |

The concrete screen constructor at RVA `0x00002660` installs vtable
`0x1000510C`. The relevant entries are:

```text
GtScreen vtable +0x04 -> 0x10003010
GtScreen vtable +0x7C -> 0x10004580
```

This links the engine-level calls to the two concrete Direct3D implementations
without relying on a decompiler-generated name.

## Whole-target clear

### Exact API call

At RVA `0x00003036`, the pushed arguments decode to:

```cpp
device->Clear(
    0,                   // dwCount: entire target
    nullptr,             // lpRects
    0x00000003,          // TARGET | ZBUFFER
    0x00000000,          // transparent black bit pattern
    0.0f,                // depth
    0);                  // stencil, ignored by these flags
```

The instruction sequence is unambiguous:

```text
0x00003020  push 0                 ; stencil
0x00003022  push 0                 ; float bits for dvZ
0x00003024  push 0                 ; color
0x00003026  push 3                 ; flags
0x00003028  push 0                 ; rectangles
0x0000302A  push 0                 ; rectangle count
0x0000302C  screen.pendingVertices = 0
0x00003036  call [device.vtable + 0x28]
```

### State and error path

Before the call:

- inactive screen flag `screen + 0x28 == 0` makes the method a no-op;
- an active screen discards any staged TL vertices by writing
  `screen + 0x54 = 0`;
- it does not call the normal primitive flush and does not change a render
  state.

After the call:

- color and depth are cleared over the current complete render target;
- the pending-vertex count remains zero;
- no explicit render state is changed;
- failure is inspected only for the literal HRESULT `0x887601C2`;
- on that value, the function invokes vtable slot `+0x64` on the
  `IDirectDraw7*` at `screen + 0xE8`, consistent with
  `RestoreAllSurfaces`;
- it does not retry `Clear` after the restoration call.

The error-code name is less important than the observed literal and recovery
call; those two instruction facts are independently confirmed.

### Confirmed frame caller

`NfMain::ProcessEvent` handles its render event and calls the screen's vtable
slot `+0x04` at RVA `0x00047C54` when:

```text
screen != null
and (main[0x654] == null or server.localPlayer == null)
```

The call occurs before the `PreRender`, main render, window, and `PostRender`
passes. The same event path invokes screen vtable slot `+0x08` at the end of
the frame. Static evidence therefore establishes this as the whole-target
frame-start clear for paths without an active local-player render owner; it
does not establish that every gameplay frame uses this path.

## Rectangular scene clear

### Input normalization

The method receives four `float` values interpreted by its only confirmed
caller as `x`, `y`, `width`, and `height`. It reads the screen dimensions from
`screen + 0x20` and `screen + 0x24`.

Concise pseudocode:

```text
if screen is inactive or x >= screenWidth or y >= screenHeight:
    return

if x < 0:
    width += x
    x = 0
if y < 0:
    height += y
    y = 0
if width <= 0 or height <= 0:
    return

if x + width >= screenWidth:
    width = screenWidth - x - 1
if y + height >= screenHeight:
    height = screenHeight - y - 1

flush pending primitives
rect = {
    intByLegacyFtol(x),
    intByLegacyFtol(y),
    intByLegacyFtol(x) + intByLegacyFtol(width),
    intByLegacyFtol(y) + intByLegacyFtol(height)
}
clear(rect)
```

The `-1` edge clamp and separate legacy `_ftol` conversions are observed
behavior. They should not be generalized into a modern viewport rule without
a parity test.

### Exact API call

At RVA `0x000046D1`, the call is:

```cpp
device->Clear(
    1,                   // dwCount
    &rect,               // one D3DRECT
    0x00000003,          // TARGET | ZBUFFER
    0xFF000000,          // opaque black bit pattern
    0.0f,                // depth
    0);                  // stencil
```

Before this call, RVA `0x00004675` invokes the normal primitive flush at
`0x00004220` with argument zero. That preserves draw-before-clear ordering and
leaves the staged vertex count at zero. The rectangular path ignores the
`Clear` HRESULT and does not alter an explicit render state after the call.

### Confirmed caller

`GadScene::Render` calls screen vtable slot `+0x7C` with its fields:

```text
x      = GadScene + 0x58
y      = GadScene + 0x5C
width  = GadScene + 0x60
height = GadScene + 0x64
```

It then assigns the same rectangle to `CcCamera::SetWindow`, derives the
centre, sets near/far clipping, and renders the scene camera. The rectangular
call therefore clears both color and reverse depth for one embedded scene
viewport before drawing it.

## Ghidra/Rizin comparison

| Item | Ghidra | Rizin | Resolution |
|---|---|---|---|
| Whole-clear boundary | Automatically found `[0x3010,0x3052)` | Required an explicit in-memory function at the vtable-confirmed entry; same boundary | Raw bytes and return agree |
| Whole-clear signature | Mislabels the receiver as a fastcall integer, but decompiles all seven COM arguments correctly | Guesses zero-argument cdecl | Vtable position and receiver dataflow establish a receiver-only screen method |
| Rectangle boundary | Required a function definition at the vtable-confirmed entry; `[0x4580,0x46DD)` | Required an explicit in-memory function; same boundary | Vtable entry, `ret 0x10`, and adjacent function agree |
| Rectangle signature | Recovers `this` plus four `float` parameters | Guesses cdecl with only two parameters | Ghidra plus stack offsets, `ret 0x10`, and caller arguments establish four floats |
| Clear flags/values | `3`, color `0`/`0xFF000000`, Z bits `0`, stencil `0` | Identical pushes and indirect slot `+0x28` | No disagreement |
| Callers | Symbol-bearing decompilation identifies `NfMain::ProcessEvent` and `GadScene::Render` | Independently confirms caller boundaries and callsite instructions | No disagreement in control flow |

Ghidra produced the more reliable high-level signatures and pseudocode. Rizin
was most useful as an independent exact-byte, boundary, vtable, and whole-text
call inventory. Neither tool automatically named the COM indirect call; the
ABI interpretation is based on the documented slot and exact stack layout.

## Confidence and port consequence

| Claim | Confidence | Reason |
|---|---:|---|
| Both active clear paths write depth `0.0f` with flags `0x3` | 3 — high | Two independent static implementations agree on exact instructions and ABI |
| Full and rectangular color bit patterns differ | 3 — high | Exact immediate operands at both calls |
| Whole clear call is conditional rather than unconditional | 3 — high | Confirmed `NfMain::ProcessEvent` branch |
| `0x887601C2` recovery is a lost-surface restoration path | 2 — medium | Literal HRESULT, `IDirectDraw7` receiver, and no-argument slot `+0x64`; not runtime-observed |

The iOS/Metal reconstruction can now use a confirmed reverse-depth clear value
of `0.0`, paired with `greaterEqual` for the ordinary opaque depth mode. The
portable renderer should preserve the behavioral distinction between a
whole-target clear and a per-scene rectangular clear, while expressing color
alpha, clipping, and presentation in modern Metal terms rather than copying
the D3D7 half-pixel or `_ftol` implementation details blindly.

`LegacyDepthState` now records this confirmed zero scalar beside the four
active compare/write presets. It remains backend-neutral and does not replace
the diagnostic Metal state before gameplay camera integration.

## Remaining limits

- No runtime D3D7 capture has verified the condition frequency or visible
  alpha behavior.
- The exact public source-level names of the two concrete screen methods are
  not present in this DLL; their working roles come from vtable linkage and
  confirmed callers.
- The rectangular `screenDimension - origin - 1` edge rule needs a synthetic
  parity scenario before deciding whether the Metal port should reproduce or
  intentionally correct it.
- Other renderer DLLs may implement the same `GtScreen` slots differently;
  this evidence is specific to the hash-locked `gtDirect3D.dll`.
