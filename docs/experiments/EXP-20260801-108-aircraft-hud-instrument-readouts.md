# EXP-20260801-108: AirCraft HUD instrument readouts

**Date:** 2026-08-01

**Evidence ID:** `EV-20260801-007`

**Decision:** GO for the isolated two-readout state/plan and authenticated
native-output packet. NO-GO for ordinary-frame publication until the live
AirCraft values, x87 conversion policy, elapsed-clock producer, and render
event are connected.

## Question

Can the two four-digit readouts immediately following AirCraft's analog
instruments be reconstructed without assigning an unsupported gameplay label
or manufacturing live values?

## Static method

Ghidra 12.1.2 is the primary source for the hash-verified `AirCraft.type`
copy with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.
The enclosing HUD function is RVA `0x00006B00`, exact VA range
`[0x10006B00,0x10007321)`, 2,081 bytes. The two readout blocks are exact
half-open ranges `[0x10006C7E,0x10006CF9)` and
`[0x10006CF9,0x10006D52)`.

Rizin 0.9.1 independently reports the same enclosing boundary, bytes,
branches, field offsets, constants, and four helper calls. In particular, it
confirms calls to the rolling-number update/draw helpers at
`0x10006CCB/0x10006CF4` and `0x10006D1E/0x10006D4D`. No executable, GUI,
debugger, original asset, decoded image, or private capture is used by the
implementation or tests.

## Recovered value and draw contract

The right state pointer is `AirCraft+0x528`. When present, the HUD obtains a
three-component binary32 vector through virtual slot `+0x54`, evaluates its
length as `sqrt(x*x+y*y+z*z)`, multiplies by exact binary32 `100`, and passes
the result to the rolling-number helper. The vector's stronger gameplay label
is not proven here.

The left state pointer is `AirCraft+0x52C`. When present, the HUD evaluates:

```text
(AirCraft+0x400)/(AirCraft+0x3F8) * 100 + 0.5
```

and passes that value to the same helper. Existing field evidence supports the
bounded description "remaining/initial ratio" but not a stronger resource
name.

The rolling helper converts both float arguments through external MSVC
`_ftol`. The portable boundary therefore accepts the resulting signed int32
values. It does not claim a live halfway rule while the original process-wide
x87 rounding state remains uncaptured.

The right readout completes before the left readout starts. Both use the same
`AirCraft+0x54C` elapsed snapshot. Their exact logical origins are:

```text
right = (screenWidth/2 + 138 - 15, screenHeight - 40 + 10)
left  = (screenWidth/2 - 138 - 15, screenHeight - 40 + 10)
```

The reusable draw helper adds its own one-pixel vertical inset. At `640x480`,
the first right digit therefore begins at `(443,451)` and the first left digit
at `(167,451)`. Right tint is exact `0xFFFFFFFF`; left tint is the retained
`AirCraft+0x550` value. Missing state pointers skip their own update and draw
independently.

## Portable and authenticated boundary

`LegacyAircraftHudInstrumentReadoutsPlan` is allocation-free and advances the
two available rolling states transactionally. Failure in either update
publishes neither new state. A missing atlas suppresses commands but preserves
the native state advancement. The plan retains the four enclosing HUD gates,
right-before-left order, one shared elapsed snapshot, exact anchors, and tint.

`LegacyAircraftHudInstrumentReadoutsSubmission` maps the retained readouts
atomically through the already authenticated `digits.gti` owner and
`NativeRenderLayout`. Each entry is an existing
`LegacyAircraftHudRollingDigitsSubmission`, so both D3D11 and Metal can use
their already implemented fail-closed generic digit consumers without a new
shader, texture, or GPU resource. The wrapper rejects reordered sides,
mixed transaction identity/UI scale, incompatible logical extents, invalid
inner commands, and partial mapping.

Ordinary render callbacks remain unchanged. No player vector, ratio, tint,
clock, camera gate, or `_ftol` result is fabricated for a live frame.

## Synthetic validation

Public tests cover:

- exact right-before-left order and `640x480` geometry;
- white right tint and independently supplied left tint;
- one shared elapsed value and atomic two-state publication;
- independently absent state pointers and atlas-free state advancement;
- all four enclosing gate outcomes and their precedence;
- non-finite/negative elapsed rejection and second-update rollback;
- authenticated `1920x1080` output mapping at 100% and 150% UI scale;
- transaction ownership, side order, inner UV, logical extent, and scale
  failures; and
- a valid empty packet when no atlas-backed draw exists.

## Remaining gates

- Establish the real virtual-slot `+0x54` producer and publish its coherent
  vector with the left ratio, tint, two states, and shared elapsed snapshot.
- Accept a runtime x87 conversion policy or explicitly choose a portable
  presentation policy before computing either signed value in product code.
- Compose every recovered AirCraft HUD subsection into one render-event
  transaction without changing native order.
- Validate the complete live HUD on Windows and both physical target iPhones.
