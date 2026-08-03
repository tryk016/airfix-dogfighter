# EXP-20260803-110: full-HUD Windows validation capture

**Status:** implemented and locally validated

**Evidence boundary:** consumes `EV-20260801-008`; adds no recovered semantic
claim

**Decision:** GO for the private capture-only D3D11 integration; NO-GO for
ordinary-frame HUD publication or treating fixed validation values as gameplay

## Question

Can the already authenticated complete AirCraft HUD event and weapon sight be
drawn together by the native Windows renderer at modern physical resolutions
without wiring invented state into simulation or the ordinary frame loop?

## Boundary

The existing portable composer and D3D11 consumer already preserve the
recovered subsection order:

1. analog instruments;
2. right then left rolling readouts;
3. primary and secondary weapon panels;
4. armour/health gauge; and
5. aircraft identity, health, team, and technology status.

This experiment adds no new function name, address, data-layout claim, timing
claim, or x87 policy. The unsuccessful dynamic Phase A observation remains
`NO-GO`. The new path is an explicit owner-private visual harness only.

## Product transaction

`--capture-hud-validation-frame` requires the same complete authenticated
mission transaction as normal owner-local startup. The renderer verifies that
the installed mission owns every HUD texture set, at least one weapon icon,
one aircraft icon, and a sight texture. It then:

1. builds the native output/UI layout for the actual backbuffer;
2. advances a local one-use elapsed clock by a fixed finite value;
3. creates one representative input with explicit fixed instrument, readout,
   ammunition, health, team, and technology values;
4. resolves icon indices only from the installed authenticated catalogues;
5. builds and authenticates the existing atomic full-HUD event;
6. builds one centred sight submission through the existing sight owner; and
7. draws scene, event, sight, and path-free diagnostics into one captured
   backbuffer.

The renderer method is reachable only from the mutually exclusive one-shot
capture option. The public `renderFrame(bool)` API and interactive application
loop remain unchanged. Fixed values and rolling-state replacements are local
temporaries and are never published to actor, mission, input, camera, audio, or
settings state.

The capture also closes an adjacent lifecycle inconsistency: the existing
health-gauge validation option now selects the same hidden session-only window
policy as the other capture modes.

## Visual validation

One owner-local authenticated mission was captured through the real
SDL3/D3D11 application at render scale 100%, Enhanced presentation, Hor+, and
the diagnostics overlay. Exact physical output and render-target extents were
verified for:

- 1920x1080;
- 2560x1440;
- 3840x2160; and
- 3440x1440 ultrawide.

The 16:9 and ultrawide images were visually inspected. The scene expands
horizontally rather than stretching, while the HUD remains sharp and anchored
inside the stable 640x480 UI design root. The aircraft, centred sight, armour,
aircraft identity, paired analog/readout instruments, both weapon panels, team
badge, technology display, and diagnostics are visible. The images are derived
private content and remain outside Git.

## Validation

- Visual Studio 2026 MSVC 19.51/Ninja builds the full SDL3/D3D11/XAudio2
  product, including the complete HUD capture path and HLSL.
- The dedicated command-line test accepts the complete request and rejects a
  missing mission, disabled diagnostics, and mixed capture modes.
- Both data-less Windows product smoke tests pass.
- Existing synthetic full-HUD tests remain the authority for transaction
  rollback, ownership, native order, and six rolling-state updates.
- A complete fresh-worktree MSVC/Ninja build succeeds and all 157/157 native
  CTests pass, including the D3D11 renderer, UI Automation, command-line,
  complete-HUD, and both product smoke tests.
- Public-boundary tests, the 754-file repository scan, warnings-as-errors
  formatting, changed-document links, changed-scope local-path review, and
  `git diff --check` pass. Hosted platform checks remain the final publication
  gate.

## Decision

**GO** for retaining the one-shot private Windows visual-validation path and
using it for future matched screenshots.

**NO-GO** for calling the HUD live, playable, or parity-complete. Ordinary
frames must stay disconnected until one coherent recovered producer supplies
the actual camera gates, values, identities, visibility decisions, elapsed
clock cadence, and accepted numeric policy. Physical iPhone acceptance also
remains pending.
