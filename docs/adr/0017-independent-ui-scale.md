# ADR-0017: independent native UI scale

- Status: accepted
- Date: 2026-07-31
- Deciders: project owner and implementation lead

## Context

ADR-0013 separates the output, 3D render-target, camera, and UI coordinate
domains. The existing `NativeRenderLayout` correctly aspect-fits a stable
logical UI canvas inside the output safe area, while the Windows settings
surface rasterizes with D2D/DWrite at the physical output extent and iOS uses
UIKit, Dynamic Type, Auto Layout, and a safe-area scroll view.

Multiplying the root logical-canvas transform would be the wrong UI-scale
implementation. At 100% that transform already fits the limiting safe-area
dimension. Enlarging it would clip the canvas or move hit regions outside the
safe area, and shrinking it would conflate user preference with camera-era
640x480 metadata. Scaling a finished bitmap would also blur text.

## Decision

`RenderPresentationSettings` gains an independent `uiScalePercent` field:

- valid range: 75-150%;
- default: 100%;
- menu step: 5%; and
- no effect on output extent, 3D render extent, camera projection, simulation,
  physics, mission state, or content loading.

The stable root UI canvas and safe-area mapping remain unchanged. UI scale is a
content/widget metric applied inside that safe rectangle. Text, controls,
icons, spacing, focus decoration, and corresponding hit regions use native
platform layout at the selected scale. Layouts reflow or scroll when enlarged;
they do not silently cancel the requested scale by shrinking everything back
to fit.

Windows composes per-monitor DPI with the user factor. D2D/DWrite continues to
rasterize directly at the output extent. A width constraint may reduce the
effective scale only when the window cannot contain the panel horizontally.
Vertical pressure instead creates a bounded row window centred around the
selection, so keyboard, mouse-wheel, and controller navigation auto-scroll
without drawing or accepting input outside the output.

iOS recomputes fonts from the current preferred Dynamic Type metrics and then
applies the user factor. Auto Layout, safe-area constraints, the vertical
scroll view, and minimum 48-point action controls remain authoritative.
Controller navigation scrolls the selected row into view. Setting-row vertical
margins retain at least the equivalent of a 44-point interactive target at
75%.

The portable diagnostics overlay is the first renderer-owned UI consumer. It
continues to rasterize after scene presentation, in output pixels, and combines
its output-resolution pixel scale with `uiScalePercent`. Future HUD, reticle,
mission-status, and menu components must consume the same content-scale policy
inside their safe-area anchors rather than modifying `NativeRenderLayout`'s
root transform.

## Settings and persistence

The settings delta classifies UI scale as `uiLayoutChanged`. It does not set
`scaleTargetsChanged` or scene `layoutChanged`, so it cannot recreate scene
color/depth targets or change projection.

AFRS advances to semantic schema 3 with ordered field 6 containing the exact
IEEE-754 binary32 UI-scale percentage:

- schema 1 migrates with safe FOV `0.0F` and UI scale `100.0F`;
- schema 2 migrates with UI scale `100.0F`;
- schema 3 is canonical; and
- schema 4 and later remain opaque and block downgrade writes.

The setting is durable on Windows and iOS. The settings panel previews its
draft value immediately; Cancel restores the applied value, and Apply uses the
existing prepare-save-publish transaction. No mission or cache reload is
required.

## Validation

Tests must prove:

- 75%, 100%, and 150% validation and exact persistence;
- atomic rejection of non-finite and out-of-range values;
- schema-1/schema-2 migration and opaque future-schema preservation;
- UI-only delta classification;
- unchanged output, render-target, camera, viewport, safe-area, and root UI
  transforms when only UI scale changes;
- native Windows DPI rasterization, bounded rows, and selection auto-scroll at
  small and enlarged layouts;
- iPhoneOS and iPhoneSimulator compilation of the UIKit/Dynamic Type path; and
- diagnostics scaling independently from the 3D render scale.

Physical iPhone acceptance remains required for touch sizing, Dynamic Type,
notch/safe-area behaviour, and VoiceOver focus order.

## Alternatives considered

### Scale the root 640x480 UI canvas

Rejected. It clips or letterboxes the safe-area mapping and couples a user
accessibility preference to legacy design metadata.

### Scale a completed UI texture

Rejected. It produces blurred text and inaccurate native control hit regions.

### Clamp every panel to the available height

Rejected. Enlarged UI would be silently shrunk back to the old size. Reflow and
scrolling preserve the user's selection.

## Consequences

- UI scale is an explicit cross-product setting rather than a side effect of
  resolution, render scale, or DPI.
- Platform layout code remains native while validation, persistence, delta
  classification, and limits are shared C++20 policy.
- Windows narrow-window behaviour may reduce the effective scale to prevent
  horizontal escape; vertical overflow scrolls instead.
- Full HUD adoption is incremental, but all new UI must use this decision
  rather than introducing another root-canvas multiplier.
