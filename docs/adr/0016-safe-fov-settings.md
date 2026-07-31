# ADR-0016: safe vertical-FOV presentation setting

- Status: accepted
- Date: 2026-07-31
- Deciders: project owner and implementation lead

ADR-0017 subsequently advances the current AFRS record to schema 3 for
independent UI scale. The schema-2 rules below remain the authoritative
migration definition for field 5; schema 2 is now accepted as an older record
and migrated with UI scale at exact 100%.

## Context

ADR-0013 requires widescreen Hor+ to preserve the reference vertical field of
view by default and requires any wider vertical view to be an explicit,
independent user choice. The recovered gameplay camera owns its authored
horizontal FOV, including any later zoom or camera-mode changes. A product
setting must therefore change presentation only: it cannot overwrite the
recovered camera state, enter simulation hashes, recreate scene targets, or
couple vertical coverage to the output aspect ratio.

The existing presentation record is schema 1 and contains render scale,
Hor+/Original 4:3, Classic/Enhanced intent, and the diagnostics toggle. Both
products already apply that record through the prepare-save-publish transaction
defined by ADR-0014.

## Decision

### User setting

The portable snapshot gains:

```text
vertical FOV increase: 0..25 degrees, default 0
```

The setting is named `verticalFovAdjustmentDegrees`. It is available through
the Windows and iOS Display settings panels and through the Windows
`--vertical-fov-adjustment <0-25>` session override. Product controls move in
one-degree steps. Zero is exact Classic-compatible behavior; the product never
increases FOV implicitly because the display became wider.

Only non-negative adjustment is exposed. This preserves the recovered
composition as the narrowest product view and avoids turning the setting into
an undocumented zoom control. Twenty-five degrees raises the recovered
90-degree-horizontal, 4:3 camera from approximately 73.74 to 98.74 vertical
degrees. The limit is a product safety policy, not a claim about an original
menu.

### Projection law

The renderer does not replace the camera's authored horizontal FOV. Instead it
converts the selected increase into a positive tangent-space multiplier,
calibrated so that the recovered 90-degree-horizontal reference camera gains
the selected number of vertical degrees:

```text
reference vertical half angle = atan(tan(45 degrees) / reference aspect)
multiplier =
  tan(reference vertical half angle + adjustment / 2)
  / tan(reference vertical half angle)

effective vertical half angle =
  atan(tan(authored vertical half angle) * multiplier)
```

This law has four required properties:

1. adjustment zero is an exact multiplier of one;
2. the authored camera FOV remains monotonic and is never written back;
3. every accepted authored FOV remains strictly below the 180-degree
   projection singularity; and
4. the same effective vertical FOV is used for 4:3, 16:10, 16:9, 19.5:9,
   21:9, and 32:9, while Hor+ derives horizontal coverage from the scene
   aspect.

`NativeRenderLayout` expresses the wider view by increasing the logical camera
height by the same multiplier and deriving width from the active scene aspect.
The unchanged reference canvas is centered in that logical extent. Existing
backend projection scale therefore produces the wider view without mutating a
recovered camera snapshot. Output pixels, render-target pixels, the scene
viewport, UI scale, safe area, and UI coordinates remain unchanged.

### Runtime transaction

FOV is a layout-only delta. Applying it:

- does not allocate or replace color/depth scene targets;
- does not reload a mission or invalidate content;
- does not reset input, audio, camera simulation, or physics;
- publishes through the existing durable presentation transaction; and
- reaches D3D11 and Metal through the same portable layout configuration.

Pointer and touch conversion already consume the expanded logical camera
extent, so their camera coordinates follow the effective projection. UI and
HUD design coordinates remain independent. Complete weapon/HUD/effect
composition is still a separate ADR-0013 integration task.

### Persistence migration

The semantic and AFRS record advances to schema 2. Field 5 stores the exact
IEEE-754 binary32 bits of `verticalFovAdjustmentDegrees`.

- Schema 1 is decoded as schema 2 with an exact `0.0F` adjustment.
- The next changed or explicit save writes canonical schema 2 while rotating a
  valid schema-1 current record through the existing backup transaction.
- Schema 3 and later remain opaque and block downgrade writes exactly as
  before.
- Malformed, duplicate, missing, non-canonical, non-finite, or out-of-range
  values fail closed without partially applying another field.

The record still contains no game content, local path, checksum, device
identity, or simulation value.

## Options considered

### Replace the authored camera FOV with one absolute user value

Rejected. It would erase later recovered camera zoom and mode behavior and
could feed a presentation preference into reconstructed camera state.

### Add degrees directly to every authored vertical FOV

Rejected. The recovered camera accepts a very wide range. A fixed addition
could cross 180 degrees and make projection singular.

### Change horizontal FOV directly

Rejected. A horizontal control makes visible vertical coverage depend on
aspect ratio and conflicts with the Hor+ requirement.

### Clamp an unbounded effective result

Rejected. Silent per-frame clamping would make the setting response depend on
camera state and hide invalid policy. The bounded tangent-space law remains
finite by construction.

## Consequences

- Both native products expose the same durable, deterministic presentation
  preference.
- Default and Original 4:3 comparison output remain unchanged at `+0`.
- The setting composes safely with later authored camera-FOV changes.
- Layout and input projection tests must cover both endpoints at all required
  aspect ratios.
- Changing the chosen range later requires an explicit product-policy and
  migration review.

## Verification

Public synthetic validation covers:

- `0`, `25`, adjacent out-of-range values, NaN, and infinity;
- exact schema-2 bytes, schema-1 migration, and opaque future schemas;
- layout-only delta classification and target-bundle reuse;
- identical output/render-target/UI extents before and after adjustment;
- approximately 98.74 vertical degrees at the maximum setting for the
  recovered reference camera;
- monotonic finite results for authored horizontal FOV values `1`, `90`, and
  `175` degrees;
- all required aspect ratios and centered reference-camera/input mapping;
- Windows keyboard, mouse, and controller settings navigation; and
- hosted iPhoneOS and iPhoneSimulator compilation of the UIKit and Metal
  bindings.

Physical-device visual acceptance on iPhone SE 3 and iPhone 17 Pro Max remains
required before the wider view receives a release-quality default other than
zero.
