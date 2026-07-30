# ADR-0015: bounded controller binding remapping

- Status: accepted
- Date: 2026-07-30
- Deciders: project owner and implementation lead

## Context

AFIP V1 already stores a complete, fixed-capacity controller binding table and
four-axis calibration. The resolver rejects malformed controls, overlapping
contexts, unreachable pause/menu recovery actions, non-canonical tails, and
bindings that cannot be compiled into the shared `InputRouter`. Windows and
iOS load one immutable active AFIP at startup and can durably save a complete
replacement for the next process launch.

The products still need a user-facing remapping flow. A general table editor
would expose transport fields that users cannot reason about, including
physical event kind, target kind, context masks, Q15 scale, thresholds, and
neutral-gate behavior. It could also strand controller-only navigation or
silently steal a control from an unrelated custom binding. Physical
"press any button" capture adds device-lifecycle, stale-edge, and accessibility
questions before the conflict model is settled.

The first remapping stage must therefore be useful without weakening the
existing recovery guarantees or implying live profile replacement.

## Decision

### First supported action set

The first editor is action-centric and limited to these existing,
single-binding, gameplay-only digital actions:

- primary fire;
- secondary fire;
- next weapon;
- rear view;
- camera cycle;
- camera recenter; and
- mission status.

Pause, back, menu confirmation/cancellation/navigation, throttle directions,
all analog targets, and custom multi-binding layouts are protected. A profile
may remain valid with multiple controls targeting one action, but that action
is reported as a custom/ambiguous layout and is not edited by this first-stage
model.

### Typed catalog and picker

Portable C++20 owns a bounded catalog of the seven actions and standardized
controller buttons/triggers. Native interfaces render localized text from
these typed values. They never construct or mutate raw
`ControllerBindingRecord` fields.

The first UI uses an explicit text picker. It does not require a controller to
be connected and does not capture arbitrary incoming events. This keeps
keyboard, touch, controller navigation, VoiceOver, and later Windows UI
Automation deterministic.

### Conflict policy

Selecting an unused control changes only the chosen action's physical control
and normalizes its transport:

- a trigger uses an analog physical event, the fixed AFIP V1 trigger threshold,
  unit scale, and the neutral gate;
- another button uses a digital physical event, threshold one, unit scale, and
  the neutral gate.

A control used only in disjoint contexts is not a conflict. For example, a
menu-only face-button binding does not prevent that control from serving a
gameplay action.

An overlapping conflict defaults to cancel and never mutates the draft:

- if the other binding is one of the seven editable actions, the UI may offer
  an explicit atomic swap;
- if the other binding is protected, custom, or ambiguous, swap is unavailable;
- replace, unassign, delete, and "keep both on one control" are not offered.

Swap exchanges only the two physical controls, normalizes both transports, and
preserves action targets, contexts, binding indices, binding count,
calibration, schema, and the canonical unused tail. The complete candidate
must still pass `resolveControllerInputProfile` before publication to the
draft.

### Draft, persistence, and runtime

Binding changes extend the existing `ControllerInputProfileMenuModel`; they do
not create a second draft owner. Calibration and remapping therefore share one
cancel/save transaction and cannot overwrite each other.

The existing persistence contract is unchanged:

- save serializes the complete validated AFIP;
- failure keeps the complete draft for retry;
- success updates persisted/draft only;
- active input remains immutable for the process lifetime;
- the UI labels the operation `Save for next launch`;
- commit-unknown is accepted only after an exact valid-current readback; and
- future-schema and unavailable-storage protections remain in force.

Reset-all-assignments is an explicit recovery action. It restores the complete
default controller binding table while preserving all four calibration
records.

## Options considered

### A. General AFIP table editor

| Dimension | Assessment |
|---|---|
| Coverage | High |
| Recovery risk | High |
| UX complexity | High |
| Accessibility cost | High |

This would expose contexts, directions, duplicate targets, deletion, and
analog/digital conversions before their conflict semantics are specified. It
is rejected for the first release.

### B. Bounded action picker with cancel-first swap

| Dimension | Assessment |
|---|---|
| Coverage | Medium |
| Recovery risk | Low |
| UX complexity | Low |
| Testability | High |

This is the selected option. It covers the main combat/camera actions while
keeping every recovery binding and custom layout fail-closed.

### C. Keep the fixed baseline mapping

| Dimension | Assessment |
|---|---|
| Coverage | None |
| Recovery risk | Low |
| Accessibility | Insufficient |
| V1 requirements | Incomplete |

This avoids implementation work but does not satisfy the documented Windows
and iOS remapping requirement.

## Consequences

- Portable tests can prove move/swap atomicity, trigger normalization,
  protected conflicts, custom-layout rejection, reset behavior, and unchanged
  persistence semantics without private data or hardware.
- Native UIs get a small typed surface instead of access to the AFIP record
  internals.
- Touch, keyboard, mouse, and the active router are unaffected.
- Analog remapping, menu remapping, deletion, multiple bindings per action,
  physical capture, and live replacement remain deliberately unavailable.
- The eventual Windows UI needs a bounded UI Automation tree before remapping
  can be called Narrator-accessible.

## Action items

1. [x] Add the portable action/control catalog and lookup classification.
2. [x] Extend the shared profile menu model with move, cancel-first conflict,
       atomic swap, and reset-all-assignments operations.
3. [x] Add exhaustive synthetic tests and preserve codec/store round trips.
4. [ ] Add the iOS text-picker surface with Dynamic Type and VoiceOver.
5. [ ] Add the Windows picker plus UI Automation exposure.
6. [ ] Run controller-only physical acceptance before release.
