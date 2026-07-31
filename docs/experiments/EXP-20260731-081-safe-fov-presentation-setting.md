# EXP-20260731-081: safe FOV presentation setting

- Date: 2026-07-31
- Status: implemented; hosted platform validation pending
- Scope: portable layout, durable settings, Windows D3D11, and iOS Metal

## Question

Can both products offer an optional wider vertical field of view without
overwriting recovered camera state, changing deterministic gameplay, coupling
the view to the display aspect ratio, or recreating GPU targets?

## Implemented boundary

ADR-0016 defines one presentation-only setting:
`verticalFovAdjustmentDegrees`, bounded to `0..25` with an exact zero default.
The portable layout converts it to a finite tangent-space multiplier. The
multiplier expands the logical camera canvas around its unchanged center while
Hor+ continues to derive horizontal coverage from the scene aspect.

The implementation deliberately does not write to the gameplay camera, physics,
input frame, mission content, or renderer output extent. Output pixels, render
target pixels, the Original 4:3 viewport, safe area, and UI coordinates are
identical before and after an FOV-only edit. Pointer and touch camera
coordinates use the expanded logical extent.

D3D11 and Metal pass the same validated value into `NativeRenderLayout`.
Windows exposes one-degree keyboard, mouse, and controller edits plus the
bounded `--vertical-fov-adjustment <0-25>` session override. The iOS safe-area
panel exposes an equivalent one-degree slider and controller adjustment.

## Persistence and transaction behavior

The semantic presentation record and canonical AFRS envelope advance from
schema 1 to schema 2. Ordered field 5 stores the exact IEEE-754 binary32 value.
A schema-1 document migrates to schema 2 with exact `+0.0F`; schema 3 and later
remain opaque and downgrade-protected. Non-finite, out-of-range, missing,
duplicate, or malformed values fail closed.

The shared delta classifies FOV as layout-only. Applying it reuses the existing
complete color/depth target bundle and follows the established
prepare-save-publish transaction. No mission/cache reload is required.

## Synthetic validation

Portable tests cover:

- exact zero behavior and the inclusive `0..25` range;
- NaN, infinity, and adjacent out-of-range rejection;
- monotonic finite projection for authored horizontal FOV values `1`, `90`,
  and `175` degrees;
- 4:3, 16:10, 16:9, 19.5:9, 21:9, and 32:9 layout/input projection;
- unchanged output, render target, viewport, safe area, and UI transforms;
- layout-only transaction deltas and target-bundle reuse;
- canonical 79-byte schema-2 encoding, schema-1 migration, and opaque future
  schema preservation;
- persistent-store recovery and menu draft/apply/cancel behavior; and
- Windows command-line and settings-panel bounds/navigation.

Local validation completed:

- portable CMake/Ninja build: passed;
- complete portable CTest suite: 120/120 passed;
- MSVC syntax checks for the changed Windows command-line, settings-panel, and
  rasterizer translation units: passed;
- `git diff --check`: passed.

The managed Windows sandbox could not download the already pinned SDL 3.4.12
archive because external certificate credentials were unavailable. The hosted
Windows product build and both hosted Apple builds therefore remain the
platform compilation gates for this slice. Physical-device visual acceptance
on iPhone SE 3 and iPhone 17 Pro Max remains required.

## Limits

- This slice does not complete HUD, weapon, reticle, or screen-effect
  composition at the wider setting.
- It does not add independent UI-scale controls or modern lighting stages.
- The default remains zero until owner-device comparison shows that a wider
  default is desirable.
- No original asset, private texture, content path, checksum, or capture is
  included.

## Related

- [ADR-0013](../adr/0013-native-resolution-modern-rendering.md)
- [ADR-0014](../adr/0014-render-presentation-settings.md)
- [ADR-0016](../adr/0016-safe-fov-settings.md)
- [Native layout foundation](EXP-20260729-049-native-render-layout-horplus.md)
