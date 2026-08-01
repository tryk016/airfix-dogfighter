# EXP-20260801-103: authenticated rolling-digit atlas and native submission

**Date:** 2026-08-01
**Decision:** GO for authenticated atlas ownership, native-output submission and
dormant D3D11/Metal consumers; NO-GO for ordinary-frame drawing until the live
value producers, HUD clock and enclosing render gates are recovered.

## Scope

This experiment closes the asset and backend prerequisites left by
`EXP-20260801-102`. It does not assign semantic names to the six helper states,
quantize a live float through an assumed `_ftol` policy, manufacture elapsed
time, or insert a validation value into ordinary gameplay.

## Owner-local atlas check

The existing read-only `gti-preview` parser opened the authenticated logical
entry through the original `Resource.up` archive and selected GTI format 8 at
exactly `16x128`. This independently resolves the earlier inventory statement
that the source is format-4/format-8 capable: the reconstruction's normal GTI
selection policy chooses the decoded RGBA8-capable format-8 variant.

The generated preview remained below the ignored private-artifact boundary.
No GTI bytes, decoded pixels, archive, local host path or derived image entered
Git, CMake resources, the application bundle or CI.

## Portable content transaction

`LoadedLegacyAircraftHudRollingDigitsTextureSet` owns exactly one HUD-local
texture ID and the opaque authenticated-stream identity. Its loader:

- resolves the exact logical entry only in the session-owned source archive;
- reads through the already authenticated seekable handle without reopening a
  host path;
- bounds stored/unpacked source footprint, decoded RGBA, upload RGBA, resident
  RGBA, dimensions, mip counts and metadata;
- requires selected format 8 and exact base dimensions `16x128`;
- publishes no partial payload on lookup, parse, decode, limit, cancellation,
  callback, allocation or session-identity failure; and
- revalidates the unchanged content revision and opaque transaction identity
  before publication.

The texture ID is dense only inside this dedicated set. It cannot be confused
with a room, crosshair or health-gauge texture merely because the integer value
is zero.

## Native-output packet

`LegacyAircraftHudRollingDigitsSubmission` binds the fixed four-command plan
to the exact atlas owner. It converts each recovered destination rectangle from
the `640x480` UI design domain into physical output pixels while leaving the
3D render target independent. The user UI scale is applied around the widget
origin reconstructed from the native slot pitch, so enlarging the digits does
not move their anchor.

Source rectangles are normalized against the verified `16x128` atlas. The
packet retains fractional vertical coordinates, caller ARGB tint, strict slot
order, source-alpha/one-minus-source-alpha blending, linear-clamp sampling and
ALWAYS/write depth. It rejects mixed transaction identity, forged IDs,
reordered slots, invalid UVs, non-finite/out-of-output geometry, incompatible
UI domains and out-of-policy scale atomically.

## Native backend staging

Windows loads and uploads the atlas before the no-fail D3D11 mission ownership
swap. iOS carries it through the private Objective-C++ snapshot, includes it in
the shared Metal admission/heap plan, generates required mips and publishes it
under the same immutable room owner.

Both backends contain a fail-closed consumer for the portable packet. The
existing overlay shader ABI was extended with an explicit UV rectangle;
existing diagnostics, product UI and crosshair calls provide full `[0,1]` UVs.
The digit consumers revalidate the installed atlas identity, command order,
state, output bounds, texture resources and target dimensions before emitting
any draw. They remain deliberately uncalled by the ordinary render callback.

## Synthetic validation

The new public test builds only synthetic AFPACK/UDSP/GTI bytes. It covers the
successful authenticated format-8 `16x128` path, exact checksum propagation,
unknown/forged roles and IDs, missing atlas, format mismatch, dimension
mismatch, source limit, cancellation, throwing progress callback, equal
revision on a different transaction, exact physical-output/UV mapping,
fractional rolling windows, 150% origin-retaining UI scale and atomic packet
failures.

A clean GCC 15.2/Ninja build passes all 132 portable tests. A clean MSVC 19.51
HostX64/Ninja build compiles the complete Windows product and passes all 144
tests, including the digit contract, D3D11 renderer and runtime HLSL
compilation. The public-boundary scanner accepts all 699 tracked files. Hosted
`iphoneos` and `iphonesimulator` compilation remains the Objective-C++/Metal
publication gate for this change.

## Remaining gates

- Recover the semantic owner and live value for all six helper states.
- Establish the enclosing HUD elapsed-time accumulator and accepted `_ftol`
  policy without inferring them from presentation output.
- Publish packets only from the recovered ordinary render-event order and
  visibility gates.
- Validate the complete HUD on physical iPhone devices and the required output
  aspect ratios after real producers exist.
