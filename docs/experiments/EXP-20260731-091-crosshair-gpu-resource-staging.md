# EXP-20260731-091: crosshair GPU resource staging

**Date:** 2026-07-31

**Status:** implemented on D3D11 and Metal; visible sprite submission pending
**Confidence:** high (`3`) for authenticated ownership, bounded validation,
atomic GPU preparation, and Windows execution; medium (`2`) for the unexecuted
Metal runtime path until physical-device acceptance

## Goal

Prepare the three recovered weapon-crosshair textures as native GPU resources
without weakening the existing content transaction or inventing a visible HUD
policy. This is a resource-ownership step, not a claim that a live crosshair is
already rendered.

## Transaction boundary

The private product path now loads the complete
`LoadedLegacyWeaponCrosshairTextureSet` through the same unchanged
`VerifiedContentSession` that supplies the mission room. The caller requires
the set to belong to that exact session and to carry the expected revision
before ownership can move to a platform renderer.

Windows prepares room geometry, scene textures, all three crosshair shader
resource views, the pose runtime, and the gameplay-camera runtime before one
`MissionResources` ownership swap. The old two-argument room install remains
available only for data-less synthetic renderer tests. The product uses the
three-argument transaction and fails closed when the crosshair set is absent.

iOS moves room, aircraft audio, and crosshairs into one private Objective-C
snapshot. Metal preparation then:

1. validates the room and complete crosshair set against one revision;
2. revalidates dense HUD-local IDs and every upload/mipmap record;
3. includes the three textures in logical GPU admission and the aligned Metal
   heap plan;
4. creates and uploads every scene and crosshair texture before publication;
5. generates all requested mip chains in one checked command-buffer phase; and
6. retains the texture array and authenticated set inside the immutable room
   resource owner.

The existing main-thread ticket validation and no-fail Metal publication are
unchanged. A missing texture, invalid mip chain, resource-limit failure, heap
allocation failure, upload failure, or mip-generation failure leaves the
previous room installed.

## Resource ownership and diagnostics

Crosshair IDs remain local to the dedicated HUD set; they are never appended
to the room texture namespace. D3D11 and Metal hold separate native texture
arrays so later sprite binding cannot accidentally index a scene material.

D3D11 diagnostic GPU-memory estimation now includes the three staged shader
resources. Metal includes them in both the checked logical byte total and the
measured heap reservation. The small CPU-side authenticated set is retained so
future per-type binding can still verify role, revision, and transaction
identity; it is not silently converted into an unproven global HUD mode.

## Verification

- A complete MSVC/Ninja Windows product build succeeds.
- All 136 Windows CTests pass, including the crosshair loader/binder tests,
  D3D11 renderer tests, and both native product smokes.
- A local owner-content run completed the production loader, D3D11 resource
  preparation, atomic mission publication, and 1920x1080 full-room capture.
  The frame contains the room, placed objects, player aircraft, textures, and
  diagnostics overlay. The capture and all owner data remain ignored and
  outside Git.
- Hosted `iphoneos` and `iphonesimulator` compilation both pass in pull-request
  run `30645339243`. Physical-device Metal inspection remains a separate
  acceptance gate.

## Deliberate non-goals

This slice does not bind a changing primary or selected-secondary weapon, does
not choose their simultaneous composition order, and does not manufacture aim,
collision, visibility, alpha-blend, or sprite-depth behavior. The renderer
therefore creates the resources but issues no crosshair draw call.

## Decision

**GO** for atomically staging the complete authenticated crosshair set as
native D3D11 and Metal resources with bounded accounting.

**NO-GO** for claiming a visible or parity-complete crosshair until live weapon
ownership, aim/collision production, primary/secondary composition, and native
sprite submission are independently established.
