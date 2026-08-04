# ADR-0002: Semantic actions with touch and controller adapters

**Status:** Accepted; portable router, controller-profile core, native startup
installation seams, touch-layout geometry/profile V1, and private durable touch
preferences V1 implemented

**Date:** 2026-07-21

**Deciders:** project owner and implementation lead

## Context

The Windows game reads keyboard, mouse, and joystick inputs. The iOS port must be
fully usable on a touchscreen, support Bluetooth/USB controllers, allow
remapping, handle hot connection and lifecycle interruptions, and preserve
deterministic gameplay behavior. Mapping mobile inputs directly to original key
codes would mix platform UI with the reconstructed simulation and make replay
testing fragile.

## Decision

Define a platform-neutral semantic action layer and immutable per-tick
`InputFrame`. Touch, controller, desktop test input, and optional motion are
independent input sources. A context-aware binding router resolves them into
actions. Feedback uses a separate semantic event path for phone haptics and
controller rumble.

Use custom touch controls for the final product because flight requires a
dynamic stick, latching throttle, weapon selection, contextual buttons, and a
layout editor. The first iOS adapters use UIKit touch delivery and Apple's Game
Controller framework because the current shell is native and this avoids an
unneeded platform dependency in the vertical slice. SDL3 remains an optional
common desktop/controller adapter if a later cross-platform shell benefits from
it. Core Haptics stays behind a narrow Apple feedback bridge.

Keep touch-control placement in a platform-neutral, versioned geometry profile.
UIKit supplies an already inset safe-area rectangle and consumes the returned
visual/capture rectangles; UIKit types do not enter the portable model. Schema
V1 preserves the existing right-handed automatic layout as its default, permits
a forced compact density, and mirrors geometry (but never semantic action IDs)
for left-handed play. A profile or safe-area change first cancels active touches
so a finger cannot retain ownership of a control that moved underneath it.

Persist handedness, density, a bounded resting-opacity percentage, and the
controller-visibility policy in a separate private
[`touch-controls.aftc`](../formats/AFTC.md) document. AFTC uses the
shared durable current/backup publication policy, rejects malformed current
schemas, preserves intact future schemas without downgrade, and defaults safely
when absent. The iOS pause surface owns a recovery-safe editor; a successful
change is durable before it is applied to the active overlay. Schema V2
migrates V1 to automatic controller hiding without a load-time write. Free
placement remains a later policy.

The default visual language uses a vertical aircraft throttle with a broad
horizontal handle and translucent, dark-backed controls with persistent accent
borders. Resting controls leave the scene readable; touch/held state raises
fill and border emphasis without moving the capture rectangle. Text receives a
dark shadow because scene luminance is uncontrolled. Do not use full-screen
blur or make color the only state signal. The V1 `50-100%` overlay-strength
preference scales resting background fills only. It never reduces text, accent
borders, active-state emphasis, or the independent 44-point capture targets.

## Options considered

### A. Semantic actions plus custom touch UI — selected

| Dimension | Assessment |
|---|---|
| Fidelity and deterministic tests | High |
| Touch ergonomics | High |
| Controller portability | High |
| Initial implementation cost | High |
| Long-term maintainability | High |

**Pros:** one simulation contract for every device; full touch customization;
hot-plug and replay are testable; platform details stay outside game logic.

**Cons:** custom gesture capture, layout editing, migration, accessibility, and
device testing require substantial work.

### B. Apple `GCVirtualController` plus Game Controller only

| Dimension | Assessment |
|---|---|
| Fidelity and deterministic tests | Medium to high |
| Touch ergonomics | Medium |
| Controller portability | Apple-only |
| Initial implementation cost | Low to medium |
| Long-term maintainability | Medium |

**Pros:** fast iOS spike, standard controller semantics, less touch plumbing.

**Cons:** less control over throttle, contextual layout, visual language, and
layout editor; introduces Apple types deeper into a cross-platform path.

`GCVirtualController` remains useful for an early device spike, but it is not the
final UI contract.

### C. Map touch/controller inputs to original keyboard commands

| Dimension | Assessment |
|---|---|
| Fidelity and deterministic tests | Low |
| Touch ergonomics | Low |
| Controller portability | Medium |
| Initial implementation cost | Low |
| Long-term maintainability | Low |

**Pros:** quickest prototype after keyboard input exists.

**Cons:** loses analog meaning, spreads legacy key assumptions, complicates
context and remapping, and cannot express an absolute touch throttle cleanly.

## Consequences

- The game core depends on actions and `InputFrame`, never SDL or Game Controller
  types.
- Original keyboard commands become one binding profile rather than the model.
- Touch-only completion and controller-only completion are independent release
  tests.
- Input profile schemas require versioning and migration.
- Touch-layout calculations are deterministic C++20 and testable without
  UIKit; native code owns only safe-area acquisition, view presentation, touch
  capture, and accessibility projection.
- Changing handedness or density is a main-thread transaction that releases all
  active touch ownership before relayout. Mirroring never changes action IDs.
- An opacity-only publication does not move capture rectangles or release
  control ownership. A combined snapshot is validated and applied coherently.
- Automatic visibility is evaluated only for active gameplay. Connecting a
  controller hides the overlay through the view's cancellation boundary, so
  every held touch is neutralized before presentation changes. Disconnect
  retains the existing synthesize-release-and-pause policy; the overlay returns
  only after explicit resume. `Always visible` remains available for hybrid
  input.
- AFTC is private app state, not an original game resource, and must never carry
  host paths, device identities, or controller metadata.
- Controller prompts require a last-active-source service.
- Lifecycle transitions and device loss are part of input correctness, not app
  shell edge cases.
- Advanced controller capabilities are detected at runtime and always have a
  fallback.

## Revisit conditions

- A later cross-platform shell makes an SDL3 adapter materially cheaper than
  maintaining native adapters.
- `GCVirtualController` proves sufficiently customizable in the device spike.
- Multiplayer introduces more than one local player or simultaneous controller
  ownership.
- Reference analysis reveals input timing semantics that cannot be represented
  by the current `InputFrame`.

## Action items

1. [ ] Confirm action semantics and timing from the Windows reference build.
2. [x] Implement action IDs, contexts, bindings, and deterministic input frames.
3. [x] Implement the native custom multi-touch stick, throttle, weapons, camera,
   mission, and pause surface, including the portable safe-area layout profile,
   automatic/forced compact geometry, and semantic-preserving left-handed mode.
4. [x] Implement Apple Game Controller hot-plug on iOS; add an SDL3 desktop
   adapter only when desktop parity work needs it.
5. [x] Implement a bounded controller profile, deterministic integer
   calibration, strict remapping validation, and private durable persistence.
6. [x] Install profiles only while constructing the Windows adapter or through
   the one-time iOS pre-start seam. Live replacement remains deferred until a
   host-owned pause transaction can prepare and publish a fresh router/bridge
   pair; an input context enum is not authorization.
7. [ ] Validate whether an Apple bridge is required for haptics/glyph metadata.
8. [ ] Run phone/tablet/controller usability and lifecycle tests.
9. [x] Add the private durable AFTC document, recovery-safe handedness/density
   editor, and bounded resting-opacity policy.
10. [x] Add recovery-safe visibility/automatic controller-hide policy with a
    V1-to-V2 migration and an always-visible override.
11. [ ] Add free placement only after last-active-source and recovery input are
    proven.
