# EXP-20260731-092: weapon-crosshair sprite submission

**Date:** 2026-07-31

**Evidence ID:** `EV-20260731-006`

**Status:** portable packet and native D3D11/Metal consumers implemented;
live weapon producer deliberately not connected

**Confidence:** high (`3`) for the static function boundaries, full-texture
UVs, ARGB tint, blend state, depth state, native D3D11 execution, and portable
provenance checks; medium (`2`) for the prepared Metal consumer until hosted
compilation and physical-device inspection complete

## Question

Can the staged authenticated sights enter native sprite passes without
guessing live weapon ownership, aim, collision, visibility, or simultaneous
primary/secondary composition?

## Static evidence

The canonical `Cc.dll` working copy has SHA-256
`18002A3AF1405D932579C6D6888256CE26B8A479735093D33441F10FC27AA8AF`.
Ghidra 12.1.2 identifies the `GtScreen` vtable rooted at VA `0x1004A240`.
The call made by `AfWeapon::RenderCrosshair` uses slot `+0x60`, which resolves
to the color-bearing simple `BlitStretch` overload at
`[0x10044E20,0x10044E5A)`. That overload supplies source coordinates
`{0,0,imageWidth,imageHeight}` to the full overload at vtable slot `+0x58`, so
the normalized texture rectangle is exactly `{0,0,1,1}`.

The full overload at `[0x10044EA0,0x100451D0)` decomposes `CcColorInt` as
`0xAARRGGBB` and writes normalized red, green, blue, and alpha into each
generated `GtVertex`. `AfWeapon` supplies `0x7FFFFFFF`: white RGB with alpha
exactly `127/255`. It calls `GtImage::IsAlpha`; selected sight format `8` is an
alpha format. The resulting material mode is `3`, already independently
recovered as source-alpha / inverse-source-alpha blending, with texture alpha
modulated by vertex alpha. Before material submission it selects render state
`2`, already recovered as depth comparison `ALWAYS` with depth writes enabled.
Two triangles are clipped by the screen clipper before submission.

Rizin 0.9.1 independently reports the same 58-byte simple overload and
816-byte full overload on the same hash-verified copy, including the indirect
call through vtable slot `+0x58`. Its automatically guessed calling convention
is not used as a source for the MSVC class ABI.

## Portable boundary

`LegacyWeaponCrosshairSpriteSubmission` is a value-only packet built from one
projected rectangle and one authenticated per-type binding. It carries:

- the weapon type, sight role, HUD-local texture ID, content revision, and
  exact authenticated-stream identity;
- the unclamped output-pixel rectangle and full UV rectangle;
- exact tint `0x7FFFFFFF`, source-alpha blending, and always-write depth mode;
- the independent viewport and recovered-depth labels from projection.

The caller must explicitly choose `draw` or `suppress`. A `draw` decision does
not silently reinterpret either visibility label; a later live producer owns
that policy. Invalid role/ID provenance, an invalid transaction identity, or a
non-finite/non-positive rectangle publishes no packet. Both backends recheck
the packet against the currently installed immutable sight set before indexing
native GPU resources, so an equal revision from another stream handle or an
old packet after mission replacement fails closed.

## Native consumers

D3D11 renders the packet after native-resolution scene presentation and before
diagnostics/product UI. It uses a six-vertex output-pixel quad, full UVs, a
linear/min-mag plus mip-point clamp sampler, explicit source-alpha blending,
and an `ALWAYS`/write depth state. The draw contributes one draw call and two
triangles to frame diagnostics. A private command-line capture mode can stage
the authenticated machine-gun sight at output centre solely to validate the
backend; it is not a live HUD claim.

Metal prepares a separate blended Depth32 pipeline, an `ALWAYS`/write depth
state, the same sampler policy, and a provenance-gated encoder consuming the
same packet and immutable sight array. No fake packet is produced by the
ordinary `MTKView` callback. The live selected-weapon coordinator remains a
later integration boundary.

## Verification

- The portable synthetic test proves exact UV/tint/blend/depth fields,
  explicit draw-versus-suppress behavior, preservation of off-screen labels,
  rejection of role/ID forgery and non-finite rectangles, and rejection of an
  equal-revision packet against another authenticated handle.
- A native MSVC/Ninja build compiles and links the complete Windows product;
  its full local suite passes `136/136`. The portable code-intelligence preset
  builds and passes `126/126` tests.
- A production owner-content D3D11 run loads the authenticated mission and all
  three staged sights, issues the sprite pass at exact `1920x1080`, and shows
  both the half-alpha sight and the diagnostics overlay. The private BMP is
  8,294,454 bytes with SHA-256
  `BDE38110E92A74068157583635D69841991B9124136A9C00DCCC72458C14FDBF`;
  it remains ignored and outside Git.
- The public-boundary scan passes across 656 tracked candidates; its synthetic
  regression suite, all three reverse-engineering wrappers, and all 12 Rizin
  normalization tests also pass. Hosted iPhoneOS/iPhoneSimulator compilation
  remains the publication gate. Physical-device Metal inspection remains a
  separate acceptance step.

## Decision

**GO** for the authenticated portable sprite packet and provenance-gated
D3D11/Metal submission consumers.

**NO-GO** for ordinary live crosshair wiring until the changing primary and
selected-secondary identities, owner/aim/collision producer, explicit
visibility policy, and primary/secondary composition order are established.
