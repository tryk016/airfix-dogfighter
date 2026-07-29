# EXP-20260729-051: shared frame diagnostics and native overlays

- Date: 2026-07-29
- Evidence ID: `EV-20260729-006`
- Status: implemented; hosted Apple and physical-device acceptance pending
- Scope: portable telemetry, D3D11/Metal timing, memory labels, and developer
  overlay composition

## Question

Can both native products expose comparable renderer measurements and compose a
sharp developer panel in output pixels without coupling presentation timing to
the deterministic game state or inheriting the 3D render scale?

## Method

1. Add one portable sample/snapshot contract for output and scene-target
   extents, render scale, frame timing, workload, lights, and GPU memory.
2. Reject non-finite, contradictory, unlabelled, or empty samples atomically.
   Smooth only timing fields; publish the current frame's resolution and
   workload fields immediately.
3. Produce one locale-independent four-line report and rasterize it with a
   bounded built-in 5x7 font into RGBA8.
4. Composite that panel after the scaled-scene presentation pass, or after the
   direct native scene at 100%, so the panel always uses output pixels.
5. Exclude the diagnostic pass from its own counters. Timing and dynamic
   resource observations remain renderer-only and cannot enter simulation.
6. On D3D11, use four asynchronous timestamp-query sets without blocking the
   immediate context. Label resource-size accounting as estimated.
7. On Metal, consume completed command-buffer GPU timestamps and label current
   admitted/`allocatedSize` accounting as backend reported. Offset the panel by
   UIKit safe-area insets converted into drawable pixels.
8. Add an explicit public Windows capture mode and run the complete portable
   and Windows product suites without private content.

## Display contract

The panel shows:

```text
OUT <width>X<height>  RT <width>X<height>  SCALE <percent>%
FPS <value>  CPU <milliseconds>MS  GPU <milliseconds>MS|N/A
DRAW <total>/<scene>  TRI <total>/<scene>  LIGHT <count>
VRAM <MiB> EST|REPORTED
```

`total` includes renderer-owned scene-presentation work but excludes the
developer panel itself. GPU time is `N/A` until an asynchronous sample has
completed. D3D11 memory is an estimate of known swap, depth, scale, mesh,
texture, uniform, and panel resources. Metal memory is the current tracked
snapshot admission plus backend-reported drawable, depth, scale, and panel
allocation sizes; neither value claims whole-process or system-wide GPU use.

## Result

- The portable unit test covers atomic rejection, exact latest-frame metadata,
  deterministic timing smoothing, required labels, unavailable measurements,
  visible raster output, and 1080p/4K output-relative pixel scales.
- D3D11 product smoke exercises the overlay on both the direct 100% path and a
  50% offscreen scene with Original 4:3 presentation.
- A public synthetic 2560x1440 capture at 100% reports an exact
  `OUT 2560X1440` and `RT 2560X1440` and visibly retains the panel in the
  backbuffer readback. SHA-256:
  `4EF8D286642D3326F80B4FF444F3632042A2BD925F55ABF37289503A345DDCA4`.
- The complete portable Ninja build passes 91/91 tests. Independent complete
  MinGW GCC 15.2 and MSVC 19.51 Windows product builds each pass 95/95 tests.
- The capture contains only the public synthetic scene. It remains a local,
  ignored progress artifact; no original or converted game content is added to
  Git.

## Limits and next work

- The first captured frame legitimately reports GPU time as unavailable;
  live rendering fills it only after a timestamp query/command buffer
  completes.
- Active lights are currently zero because the modern light command model is a
  later ADR-0013 stage.
- CPU time covers backend frame preparation and encoding before presentation;
  it is not a whole-game main-thread profiler.
- Physical iPhone safe-area placement, Metal timing availability, and sustained
  memory/performance figures require device acceptance on both target phones.
- Finished settings screens and persistence still need to bind diagnostics,
  render scale, Original 4:3, UI scale, and safe FOV.

## Acceptance rule

This evidence closes the implementation part of the ADR-0013 diagnostic-overlay
action. It does not close physical-device acceptance, final settings UI,
modern lighting/materials, performance targets, or the playable-game goal.

## Related

- [ADR-0013](../adr/0013-native-resolution-modern-rendering.md)
- [Native layout foundation](EXP-20260729-049-native-render-layout-horplus.md)
- [Backend render-scale targets](EXP-20260729-050-backend-render-scale-targets.md)
- [Architecture](../ARCHITECTURE.md)
