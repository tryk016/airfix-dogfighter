# EXP-20260729-049: native render layout and Hor+ integration

- Date: 2026-07-29
- Evidence ID: `EV-20260729-004`
- Status: implemented foundation; platform closure pending
- Scope: portable projection/layout math, D3D11 integration, Metal integration

## Question

Can the reconstruction separate physical output, 3D raster resolution,
reference-camera coordinates, UI coordinates, safe areas, and input mapping
while satisfying the exact 4K-at-100% requirement and replacing the temporary
4:3 default with correct Hor+?

## Method

1. Introduce strong portable C++20 types for every coordinate and extent
   domain.
2. Define 100% render scale as an exact integer identity and validate a bounded
   50-200% policy without allocating.
3. Derive vertical FOV from the recovered 4:3 reference projection, hold that
   vertical FOV constant, and compute the horizontal FOV and logical camera
   width for the actual scene viewport.
4. Implement an aspect-fitted Original 4:3 comparison policy separately from
   the default full-target Hor+ policy.
5. Fit UI design coordinates into the physical output safe area independently
   of 3D render scale and provide reversible output/UI and output/camera input
   transforms.
6. Make the D3D11 and Metal gameplay paths consume the same layout at the
   currently supported 100% native scale.
7. Test 4:3, 16:10, 16:9, 19.5:9, 21:9, and 32:9, including invalid inputs and
   allocation behavior.
8. Render one representative owner-local scene through D3D11 and read the
   backbuffer directly at 1920x1080, 2560x1440, 3840x2160, and 3840x1080.
   Captures and source content remain ignored local artifacts.

## Result

- At 100%, `renderTargetExtent == outputExtent` by an explicit identity branch.
  The 3840x2160 acceptance case therefore produces a 3840x2160 3D target.
- Hor+ uses the full render target, preserves the reference vertical FOV, and
  increases horizontal scene coverage as aspect ratio widens. It does not
  stretch geometry or crop vertical coverage.
- Original 4:3 produces a centred aspect-fit viewport without stretching.
- UI layout and input mapping depend on output pixels and safe area, not the 3D
  render extent.
- Direct D3D11 BMP readbacks reported the exact requested pixel dimensions for
  1080p, 1440p, 4K, and 32:9. The 32:9 frame visibly contains more horizontal
  scene geometry than the 16:9 frame.
- The backends no longer use the recovered 640x480 canvas as a physical raster
  extent. It remains reference-camera metadata.

## Confidence

High for the portable mathematics, exact 100% identity, D3D11 target sizes, and
Windows Hor+ behavior. High for the Metal compilation boundary after hosted
iPhoneOS and iPhoneSimulator builds passed. Physical-device iOS rendering,
safe-area interaction, and performance remain untested.

## Limits and next work

- The portable core computes 50-200% render extents, but D3D11 and Metal do not
  yet allocate separate offscreen scene targets or perform the final
  resolve/resampling pass. Current backend rendering is intentionally 100%.
- Original 4:3 and render-scale selection are not yet exposed in product UI.
- UI layout/input mathematics exists; a finished HUD/text renderer does not.
- The developer overlay, frame timing, draw/triangle/light counters, and GPU
  memory estimate remain pending.
- Classic/Enhanced, modern color management, lighting, shadows, materials, HDR,
  atmosphere, particles, and post-processing are later ordered stages.
- Matched original/Classic/Enhanced comparisons begin only when those profiles
  exist. Owner-derived images must remain outside Git and public CI.

## Portable acceptance rule

This evidence proves the first architecture milestone only. It must not be used
to claim completion of ADR-0013, a playable build, physical-device acceptance,
or modern image-quality stages.

## Related

- [ADR-0013](../adr/0013-native-resolution-modern-rendering.md)
- [Camera projection](../re/systems/CAMERA-PROJECTION.md)
- [Windows x64 requirements](../design/WINDOWS-X64-REQUIREMENTS.md)
