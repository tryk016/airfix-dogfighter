# EXP-20260729-050: native backend render-scale targets

- Date: 2026-07-29
- Evidence ID: `EV-20260729-005`
- Status: implemented; physical-device acceptance pending
- Scope: D3D11 and Metal offscreen scene targets and native presentation

## Question

Can both native renderers execute the portable 50-200% render-scale contract
without changing the output resolution, using a low-resolution upscale only
when explicitly selected and preserving exact direct native rendering at 100%?

## Method

1. Keep the DXGI backbuffer and Metal drawable at the physical output extent.
2. At exactly 100%, render the 3D scene directly into that native target.
3. At any other valid scale, allocate backend-private BGRA8 color and depth
   textures at `NativeRenderLayout::renderTargetExtent()`.
4. Render the scene and its Hor+ or Original 4:3 viewport into the private
   target, then sample it through a linear full-output presentation pass.
5. Replace color and depth targets transactionally only after both allocations
   succeed; reject invalid scale, excessive dimensions, or failed allocation
   without silently selecting a different scale.
6. Expose 50-200% and Original 4:3 through the Windows command line and a
   main-thread Metal renderer setting boundary.
7. Add a data-less Windows product smoke that exercises 50%, Original 4:3, and
   the presentation pass in public CI.
8. Capture the same owner-local scene at a 960x540 output with 50% and 200%
   scale. Keep both captures and all source content outside Git.

## Result

- D3D11 creates a 480x270 scene target for a 960x540 output at 50%, then
  presents it to the unchanged 960x540 backbuffer.
- D3D11 creates a 1920x1080 scene target for that same output at 200%, then
  downsamples it to the unchanged backbuffer.
- The two 960x540 captures have different SHA-256 values. The 50% frame shows
  the expected reduced raster detail; the 200% frame preserves visibly finer
  model and room edges. These are validation artifacts, not target visual
  quality.
- At 100%, neither backend creates or samples an intermediate scene target.
  The exact 3840x2160-at-100% acceptance path therefore remains a direct
  3840x2160 raster path.
- Metal implements the same two-pass policy with private `MTLTexture` color and
  depth targets, a fullscreen triangle, and an independently native-sized
  drawable.
- Independent complete MSVC 19.51 and MinGW GCC 15.2 Windows product rebuilds
  each pass all 94 tests, including direct and scaled D3D11 product smoke
  tests.

## Confidence

High for the portable extent policy and D3D11 allocation, rendering,
presentation, and readback behavior. High for the Metal API and shader
compilation boundary after the hosted iPhoneOS and iPhoneSimulator gates pass.
Physical-device Metal rendering, sustained memory pressure, and performance
remain unverified.

## Limits and next work

- This pass uses linear spatial resampling. It is not temporal reconstruction,
  a sharpening system, or final anti-aliasing.
- Finished settings UI and persistence are not implemented. Windows exposes a
  diagnostic command boundary; iOS exposes the renderer setting boundary.
- UI/HUD composition after the scene presentation pass, safe FOV controls, and
  the developer overlay remain pending.
- The intermediate target is BGRA8. Linear-light/sRGB correctness, HDR, MSAA,
  tone mapping, and final image-quality passes remain later ADR-0013 stages.
- Dynamic render scale is not yet a frame-time controller and cannot influence
  the deterministic simulation clock.
- Owner-derived images and original or converted content remain private and
  outside Git, CI, and release artifacts.

## Acceptance rule

This evidence closes the backend offscreen-target action item in ADR-0013. It
does not claim completion of modern image quality, finished product settings,
the diagnostic overlay, physical-device acceptance, or the playable game.

## Related

- [ADR-0013](../adr/0013-native-resolution-modern-rendering.md)
- [Native layout foundation](EXP-20260729-049-native-render-layout-horplus.md)
- [Architecture](../ARCHITECTURE.md)
