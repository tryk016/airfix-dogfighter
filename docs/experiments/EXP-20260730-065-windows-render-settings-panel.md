# EXP-20260730-065: Windows render-settings panel

## Question

Can the ADR-0014 presentation snapshot be exposed through the native Windows
product without bypassing the D3D11 prepare/save/publish transaction, leaking
gameplay input while paused, persisting launch-only overrides, or depending on
private content?

## Scope

This slice implements a data-less pause and Display settings surface for:

- scene render scale from 50% through 200%;
- Widescreen Hor+ or Original 4:3 presentation;
- Classic or Enhanced visual intent; and
- renderer statistics off or on.

It adds no original game data, converted texture, private package, machine
path, or user preference document to the repository. The Enhanced selector is
still a visual-intent preview; it neither enables an optional HD texture pack
nor claims that the later lighting and post-processing stages exist.

## Design

### Product UI composition

`AirfixWindowsRenderSettingsPanel` owns only the Windows interaction and view
projection above the portable applied/draft/ticket model. Its view snapshot is
bounded, path-free, copyable, and expressed in physical output pixels. Layout
uses the current backbuffer extent and SDL display scale, caps the panel width,
and remains centered at 16:9 and ultrawide extents. If the preferred DPI-scaled
layout would not fit, one derived layout scale reduces rows, gaps, controls,
and fonts together. Every title, status, row, and hit target must remain inside
the output. Interactive windows have a 640x360 logical minimum; the same
bounded math still fails closed for arbitrary synthetic capture dimensions.

`AirfixWindowsUiRasterizer` uses system DirectWrite, Direct2D, and WIC APIs to
produce a complete premultiplied BGRA8 raster. The D3D11 backend accepts only a
complete raster whose extent exactly matches the backbuffer, uploads it
transactionally to a BGRA8 texture, and composites it with
`ONE / INV_SRC_ALPHA` after the scene and developer diagnostics but before
capture and `Present`. A rejected replacement retains the previous complete UI
resource. Clearing the panel releases its texture and labelled GPU-memory
charge.

The renderer owns game drawing. SDL3 supplies only the window, display scale,
events, pointer coordinates, and standardized controller transport.

### Pause and input boundary

Opening the panel:

1. pauses `AppSession` and deactivates XAudio2;
2. changes the shared input router to the menu context;
3. resets every physical source at that gameplay boundary; and
4. publishes the pause surface through the D3D11 product-overlay path.

Keyboard arrows, Enter, Escape, Q/E, mouse motion/click/wheel, left stick,
D-pad, shoulders, south face, and east face feed the same semantic menu
actions. D-pad Left/Right were added as menu navigation controls without
changing their gameplay-context meaning. Axis hysteresis requires a return to
neutral before another analog selection.

Resume is a separate explicit intent. It is disabled unless the session is
foreground-paused, the window is focused, the simulation pipeline is healthy,
the player presentation owner is healthy, and an authenticated mission/spawn
exists. Cancel returns from Display settings to the pause surface and never
auto-resumes. Resize, DPI changes, focus changes, and controller disconnect
refresh the surface and readiness state.

### Persistence and launch overrides

`AirfixWindowsRenderSettingsCoordinator` keeps three values distinct:

```text
persistent base
    + sparse launch-only overrides
    -> effective renderer snapshot
```

The panel edits and stores only the complete persistent base. The renderer
receives the base resolved with the immutable launch overrides. A fully masked
base change is saved without an unnecessary renderer transaction; a startup
renderer rejection is represented by a separate actual active snapshot so the
next Apply still enters renderer preparation.

For an effective change, D3D11 prepares a complete candidate and calls the
durability gate immediately before no-fail publication. A save must finish
before the renderer publishes. If save outcome is `commitUnknown`, only a
fresh, valid `current` record equal to the candidate confirms durability.
Defaults and backup recovery cannot confirm the attempted current-file commit,
even when their semantic settings happen to match.

Smoke, validation, private/public frame capture, and settings-panel capture are
session-only. They do not resolve the real preference root and cannot read or
modify a user's AFRS record.

## Public capture

`--capture-settings-panel <public-output.bmp>` renders the real product surface
over the synthetic public scene. It:

- requires a new `.bmp` output and never overwrites;
- is mutually exclusive with other one-shot modes;
- rejects private content arguments;
- accepts the existing physical `--capture-size` extent; and
- returns before XAudio2 or private-content loading.

Local captures at 960x540, 1920x1080, and 3440x1440 verify a complete, sharp,
centered panel without clipping at the default product extent or stretching at
ultrawide aspect ratios. Generated BMP files remain under the ignored artifact
boundary and are not committed.

## Synthetic verification

Tests cover:

- keyboard/controller navigation, analog hysteresis, shoulders, bounds, Apply,
  Cancel, explicit Resume, and unavailable Resume;
- physical-pixel pointer hit testing, wheel clamping, ultrawide layout, and
  complete bounded geometry at 960x540, 1280x720 with 1.5 display scale, and
  the 640x360 interactive minimum;
- bounded view metadata, session-override masking, and path-free status;
- DirectWrite/Direct2D raster completeness at those adaptive extents,
  dimensions, premultiplication, color variation, out-of-output rejection, and
  absence of private strings;
- D3D11 product-overlay allocation, publication, rendering, memory accounting,
  rejected replacement retention, capture, and clearing;
- prepare-save-publish ordering, save/prepare failures, masked changes,
  startup rejection recovery, launch-override isolation, commit-unknown
  confirmation/mismatch, default and backup rejection, blocked storage, and
  invalid snapshots;
- SDL keyboard/mouse/controller mappings, D-pad Left/Right menu routing,
  disconnect/focus neutralization, and absence of gameplay leakage; and
- command-line mode isolation, extension checks, mutual exclusion, private
  content rejection, and capture-size validation.

A clean native MSVC 19.51/Ninja build completes all 624 build steps. All 115
local CTests pass, including both data-less Windows product smokes. The public
boundary scanner passes 545 files, its synthetic test passes, and
`git diff --check` passes. Hosted and isolated WSL validation are recorded
after publication.

## Limits and next evidence

This surface is reconstructed product UI, not evidence for the original game's
menu art or exact interaction. Physical keyboard/mouse/controller, mixed-DPI
monitor movement, device removal, persistence across relaunch, GPU device
loss, fullscreen transitions, and accessibility acceptance remain required.

Low/Medium/High/Ultra, safe FOV, UI scale, shadows, MSAA, effects, dynamic
render scale, and the optional private HD texture mode remain later settings
slices. Live gameplay is still blocked by incomplete flight-law and mission
integration, not by the presentation-settings transaction.
