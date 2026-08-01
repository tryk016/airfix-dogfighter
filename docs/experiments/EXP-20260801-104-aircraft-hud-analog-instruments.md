# EXP-20260801-104: AirCraft HUD analog instruments

**Date:** 2026-08-01

**Evidence ID:** `EV-20260801-003`

**Decision:** GO for the isolated two-instrument presentation plan,
authenticated texture ownership, native-output packet, and dormant D3D11/Metal
consumers. NO-GO for ordinary-frame publication until the live AirCraft values
and render-event producer are connected without capture-only substitutes.

## Question

Can the complete pair of analog HUD instruments be reconstructed from static
evidence as a bounded, renderer-neutral C++20 primitive and prepared for both
native backends without inventing gameplay state?

## Static method

Ghidra 12.1.2 is the primary source for the hash-verified `AirCraft.type` copy
with SHA-256
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.
The helper is RVA `0x00007330`, exact VA range
`[0x10007330,0x1000742F)`, 255 bytes and 82 instructions. Rizin 0.9.1 plus
rz-ghidra independently reproduce the bytes and control flow after the exact
entry is supplied. Rizin's automatic discovery did not identify this helper;
the function was created for cross-checking only after Ghidra established the
entry and boundary.

The recovered signature receives the AirCraft owner, screen, optional face
texture, centre X/Y, a normalized presentation value and ARGB tint. The face
uses screen virtual slot `+0x50`; the pointer stored below the aircraft type's
HUD owner at `+0x9C` is the shared indicator texture and uses the rotated-quad
slot `+0x68`.

Raw disassembly, decompilation, constants and Rizin reports remain ignored
local evidence. No module, archive, GTI payload, decoded image, analysis
database or host path enters Git or CI.

## Recovered draw contract

The enclosing AirCraft HUD draws the right instrument before the left. Their
centres in the logical screen domain are:

```text
right = (screenWidth/2 + 138, screenHeight - 40)
left  = (screenWidth/2 - 138, screenHeight - 40)
```

Each available `64x64` face is drawn from `(centreX-32, centreY-32)` with the
caller's tint. The right caller supplies white; the left caller supplies the
retained AirCraft tint at `+0x550`. Missing face textures suppress only their
own face.

The shared indicator uses source rectangle `(0,0)-(7,30)` from a `16x32`
texture. Its angle is:

```text
angle = (0.5 - value) * 3.9269909858703613
```

The span is exact binary32 `0x407B53D2` (225 degrees). The recovered pivot
offsets are binary32 `3.5` and `23`; the native helper also adds `0.5` to the
horizontal centre. It spills sine and cosine independently to binary32 before
building the four points. The portable presentation policy preserves that
operation boundary with double staging but does not claim live x87 bit parity
while the original process control word remains uncaptured.

The right input is the already-smoothed field at `AirCraft+0x540`. The left
input is the ratio `AirCraft+0x400 / AirCraft+0x3F8`. Static construction and
event evidence supports describing these as current and initial quantities;
the public C++ boundary therefore calls the result a remaining/initial ratio
and does not assign a stronger gameplay name. Every finite input is allowed to
extrapolate, matching the native helper; non-finite values fail closed.

## Portable and authenticated boundary

`LegacyAircraftHudInstrumentsPlan` is an allocation-free four-command value
type. It preserves both camera observations and the enclosing HUD gate order,
right-face/right-indicator/left-face/left-indicator ordering, independent
missing-texture suppression, exact logical geometry and tint. It neither owns
gameplay state nor chooses the physical 3D render resolution.

`LegacyAircraftHudInstrumentTextureSet` opens all three roles through the
already authenticated session handle:

- `clock1` and `clock2` come from the manifest-selected localization archive,
  require selected format 8 and exact `64x64` dimensions;
- `indicator` comes from the source archive, requires selected format 8 and
  exact `16x32` dimensions.

The loader bounds per-file and aggregate source footprint, decoded/upload/
resident RGBA bytes, mip counts and dimensions. It revalidates the unchanged
revision and opaque transaction identity and publishes no partial set after a
missing, ambiguous, malformed, oversized, cancelled, callback, allocation or
identity failure. Its dense IDs are private to this three-texture namespace.

`LegacyAircraftHudInstrumentsSubmission` binds the fixed plan to that exact
owner and maps the proven `640x480` logical UI domain into native output pixels
through `NativeRenderLayout`. Independent UI scale is applied around the lower
centre of the design canvas. Full face UVs and the indicator's exact
`(0,0)-(7/16,30/32)` UV rectangle, ARGB tint, source-alpha blending,
linear-clamp sampling and ALWAYS/write depth are retained. Forged IDs,
reordered commands, mixed transaction identity, invalid UVs, incompatible UI
domains and non-finite or out-of-output geometry reject atomically.

## Native backend staging

Windows and iOS include the three textures in their all-or-nothing mission
resource transactions and GPU budget accounting. The shared arbitrary-quad
shader ABI now carries an explicit UV rectangle; existing health-gauge calls
provide full UVs, while the indicator uses its recovered sub-rectangle.

Both backends contain a fail-closed consumer for the same portable packet.
D3D11 validates the installed texture owner and resources before drawing with
the existing HUD quad pipeline. Metal additionally validates target/depth
formats and extents before opening its encoder. Ordinary rendering does not
call either consumer yet, because no live packet producer has been accepted.

## Synthetic validation

Public tests use only synthetic AFPACK, UDSP and GTI bytes. They cover gate
precedence, native order and geometry, finite extrapolation, independent
missing layers, allocation-free planning, exact cross-archive ownership,
format and dimensions, budgets, cancellation, callback and moved-session
failure, native-output/UI-scale mapping, indicator UVs, tint, provenance,
forged IDs, reordered commands and atomic rejection. The Windows x64 product
and runtime HLSL compile with the enlarged quad-uniform ABI. A fresh GCC
15.2/Ninja build passes all 134 portable CTests. A fresh MSVC 19.51
HostX64/Ninja build compiles the complete Windows product and passes all 146
native CTests, including the D3D11 renderer and both product smokes. Hosted
iPhoneOS and iPhoneSimulator compilation remains the Objective-C++/Metal
publication gate for this change. The synthetic boundary suite and public
repository scanner pass; the latter accepts all 708 candidate files.

## Remaining gates

- Connect the verified smoothed right value, remaining/initial ratio and left
  tint through the ordinary render-event producer in recovered order.
- Capture the live x87 control word only if bit-identical presentation becomes
  necessary; no simulation state depends on this helper.
- Validate the composed HUD at the required output aspect ratios and safe
  areas after real producers exist.
- Perform physical-device Metal visual and timing acceptance.
