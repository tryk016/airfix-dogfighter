# EXP-20260730-064: iOS render-settings panel

## Question

Can the four presentation settings accepted by ADR-0014 be exposed on iOS
without bypassing the existing prepare, durable-save, and Metal-publication
transaction, leaking gameplay input into the overlay, or claiming that
`Enhanced` effects already exist?

## Scope

This slice uses only public synthetic state. It adds no original game data,
private texture data, local content path, image, or device identifier.

Implemented settings:

- scene render scale from 50% through 200%;
- Widescreen Hor+ or Original 4:3 presentation;
- Classic or Enhanced visual intent; and
- renderer statistics overlay off or on.

Safe FOV, UI scale, quality tiers, effect controls, and the independent private
HD texture selector remain outside the current schema and UI.

## Design

### Portable menu state

`RenderPresentationSettingsMenuModel` owns validated `applied` and `draft`
snapshots. Typed edits resolve atomically through the existing settings
validator. An Apply operation captures an immutable candidate, exact delta, and
non-wrapping serial ticket. While that ticket is active, editing and draft
cancel are frozen.

Only the exact ticket can complete. Success promotes its candidate to
`applied`; failure retains the dirty draft for retry; a stale, forged, or
duplicate completion changes nothing. Persistence availability is a capability
and never silently changes a field. `VisualProfile::enhanced` remains the
existing preview selector and is not an HD-texture availability signal.

### iOS transaction result

`AirfixRenderSettingsCoordinator` remains the only owner of:

```text
capture -> prepare Metal resources -> durable save -> fresh validation -> publish
```

The new completion reports path-free result categories. `Applied` is emitted
only after the settings are durable and the prepared Metal snapshot has
successfully passed final publication. Pending requests retain the existing
latest-complete-candidate behavior and explicitly report a superseded older
pending request. A portable request queue owns exact, non-wrapping request
identities, two-phase completion delivery, and reentrant latest-wins
arbitration; Objective-C owns only the completion blocks. The UI reads its
baseline from the renderer's active snapshot, not from a persistent base that
may be waiting for stale-surface reprepare.

### Pause and input boundary

The panel is a child-view-controller overlay. It does not use a full-screen
presentation that could stop the parent input coordinator.

Opening the panel:

1. deactivates game audio;
2. pauses `AppSession` and `MTKView`;
3. changes the router to the modal context, which resets every physical source;
4. hides flight touch controls; and
5. mounts one safe-area-aware UIKit form above Metal.

Closing returns to the pause-menu context and resets sources again. It never
resumes gameplay. The pause surface exposes a touch Resume action; with a
controller, A opens display settings and B explicitly resumes. Resume changes
back to gameplay context only after all readiness gates pass.

Closing during Apply detaches the UI but does not cancel or misreport the
durable transaction. A new panel cannot open until that transaction and any
queued request finish, preventing a stale renderer snapshot from overwriting
the in-flight complete candidate.

`AirfixUIInputSnapshot` is an immutable Objective-C projection of only UI
navigation axes and actions. It is published after the exact portable
`InputFrame` reaches its consumer and only in menu/modal contexts. Reentrant
resets suppress later callbacks from the stale frame. The left stick navigates
both axes, D-pad Up/Down navigates rows, shoulders adjust the selected value,
A confirms, and B cancels or closes. Axis hysteresis requires a return to
neutral before another change.

### UIKit and accessibility

The form uses standard slider, segmented-control, switch, and button semantics.
It provides Dynamic Type fonts, a scrolling safe-area layout, minimum
48-point action heights, VoiceOver labels/hints, a modal accessibility view,
screen-change notifications, path-free status text, and an accessibility
escape action.

The Enhanced label explicitly says `preview`, and supporting text separates the
profile from any optional private HD texture package.

## Synthetic verification

Portable tests cover:

- valid boundary combinations and every invalid scale/enum class;
- exact single-field and combined deltas;
- atomic invalid edits and editing back to a clean draft;
- persistence unavailable, in-flight freeze, exact success, exact failure,
  retry, stale tickets, cancel, and serial exhaustion;
- latest-wins pending replacement, exact active completion identity,
  stale/duplicate callback rejection, completion reentrancy, external-busy
  promotion, and request-serial exhaustion; and
- menu/modal controller bindings, D-pad row navigation, A/B/shoulders, and
  absence of gameplay actions or axes under the overlay.

Local Windows GCC/Ninja builds and the two focused CTests pass. The full public
boundary scanner also passes. Hosted iPhoneOS and iPhoneSimulator compilation
is the acceptance gate for Objective-C++/UIKit integration; those jobs do not
run the UI.

## Limits and next evidence

This is a reconstructed product settings panel, not evidence for the original
game's main-menu layout or behavior. The following remain required:

- hosted iPhoneOS and iPhoneSimulator build/link validation;
- touch, controller-only, VoiceOver, largest Dynamic Type, safe-area,
  background/foreground, save/force-quit/relaunch, and failed-allocation tests
  on physical devices;
- device acceptance on iPhone SE 3 and iPhone 17 Pro Max; and
- an equivalent Windows product settings UI.

No claim is made that the Enhanced rendering stages or private HD texture mode
are implemented by this slice.
