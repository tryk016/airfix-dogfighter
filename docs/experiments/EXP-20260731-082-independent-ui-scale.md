# EXP-20260731-082: independent native UI scale

- Date: 2026-07-31
- Status: implemented and locally validated; hosted platform validation pending
- Scope: portable settings/persistence, Windows native UI, iOS UIKit, and
  renderer diagnostics

## Question

Can both products expose a durable UI-size preference without changing the 3D
render resolution, camera projection, safe-area root transform, input
semantics, mission state, or deterministic simulation?

## Implemented boundary

ADR-0017 defines `uiScalePercent` as a 75-150% presentation setting with an
exact 100% default and 5% menu steps. The portable settings model validates and
resolves it atomically and classifies only `uiLayoutChanged`; scene target and
projection delta flags remain clear.

`NativeRenderLayout` deliberately does not consume the setting. Its logical
UI-canvas aspect fit, safe rectangle, scene viewport, camera canvas, and
output/render-target extents remain unchanged. UI scale applies to content
inside the safe root.

Windows multiplies per-monitor DPI by the user factor and continues to render
the panel directly into the output-sized D2D/DWrite surface. Width remains
bounded by the output. If enlarged rows exceed available height, the snapshot
contains a bounded window around the selected row; navigation automatically
reveals later rows without overlapping or escaping the output.

iOS recomputes title, body, footnote, monospaced-digit, and action fonts from
the active Dynamic Type metrics and then applies the user factor. Native
segmented-control text, margins, spacing, corner radii, Auto Layout, safe-area
constraints, and the existing vertical scroll view remain active. Action
buttons keep their 48-point minimum, controller focus scrolls its selected row
into view, and setting-row vertical margins retain a 44-point-compatible target
at 75%.

The shared diagnostics rasterizer combines its existing output-resolution
integer pixel scale with UI scale. D3D11 and Metal pass the active setting when
refreshing the overlay. The overlay remains an output-resolution pass after
scene presentation and is independent of 3D render scale.

## Persistence

AFRS advances from 79-byte schema 2 to canonical 87-byte schema 3. Ordered
field 6 stores the exact IEEE-754 binary32 UI-scale percentage.

- Schema 1 defaults safe FOV to `0.0F` and UI scale to `100.0F`.
- Schema 2 retains its exact safe FOV and defaults UI scale to `100.0F`.
- Schema 3 requires all six fields exactly once in canonical order.
- Schema 4 and later remain opaque exact bytes and block downgrade writes.

The Windows and iOS stores require no platform-specific format change because
both already persist the portable AFRS document.

## Validation

Local results at implementation:

- portable GCC/Ninja build and complete suite: 120/120 tests passed;
- fresh Windows x64 MSVC 19.51/Ninja product build: 669 steps passed, including
  SDL3, D3D11, D2D/DWrite, XAudio2, and the product executable;
- complete Windows product suite: 130/130 tests passed, including native and
  scaled D3D11/XAudio2 product smoke tests;
- isolated WSL2 Clang 18 compiled every changed portable module and passed all
  five affected tests. Strict warnings-as-errors passed for those modules,
  retaining only the documented pre-existing format-security warning in the
  diagnostics formatter;
- synthetic public-boundary tests and the 621-file repository scan passed;
- changed-line formatting, local-path review, and `git diff --check` passed;
  and
- SDL3 3.4.12 input archive SHA-256 matched the pinned project value
  `f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7`.

Hosted Windows, iPhoneOS, and iPhoneSimulator builds remain publication gates.
Physical-device iOS visual, touch, Dynamic Type, and VoiceOver acceptance
remains separate.

## Result

GO for the shared setting, schema-3 migration, native settings-surface scaling,
bounded Windows row scrolling, and diagnostic-overlay adoption.

NO-GO for claiming complete HUD support. HUD, reticle, mission-status, and
other renderer-owned widgets must adopt ADR-0017 incrementally and receive
their own aspect/safe-area captures and device acceptance.
