# EXP-20260730-074: bounded controller binding remap core

## Question

Can the shared controller-profile editor support useful gameplay-button
remapping without exposing raw AFIP records, weakening recovery controls, or
replacing the active position-indexed input router?

## Scope

This slice adds a portable action-centric model for seven gameplay-only digital
actions:

- primary fire;
- secondary fire;
- next weapon;
- rear view;
- camera cycle;
- camera recenter; and
- mission status.

It does not add native Windows or iOS picker surfaces, physical-event capture,
analog or menu remapping, delete/unassign, controller identity, live profile
replacement, private content, or original game data.

## Design

`ControllerInputBindingCatalog` owns bounded typed catalogs for the supported
actions and standardized controller buttons/triggers. It reports each action
as uniquely editable, missing, ambiguous, or unsupported instead of exposing
binding indices or transport internals to native interfaces.

`ControllerInputProfileMenuModel` remains the single owner of active,
persisted, and draft state. A move to an unused physical control normalizes
button or trigger transport fields and publishes only after resolving the
complete candidate. A use in a disjoint context is allowed. An overlapping use
returns a typed conflict and leaves the draft unchanged.

Cancel is the default conflict result. An explicit atomic swap is available
only when the conflicting record is another unique supported gameplay action.
Pause, global back, menu navigation, D-pad throttle, analog targets, and
custom/multi-binding layouts are protected. Replace, unassign, delete, and
keep-both behavior are absent by design.

Reset restores the complete canonical default binding table while preserving
all four calibration records. Save still serializes the full validated AFIP,
failure retains the full draft for retry, and a changed successful save takes
effect only on the next process launch.

## Synthetic verification

Data-less tests cover:

- bounded unique action/control catalogs and complete controller-control traits;
- classification of default, missing, ambiguous, and unsupported layouts;
- an unused-control move when the same control is used only in a disjoint menu
  context;
- cancel-first non-mutating conflicts;
- trigger-to-button and button-to-trigger normalization during atomic swap;
- protected pause/global-back and D-pad throttle conflicts;
- invalid action/control rejection and custom-layout fail-closed behavior;
- reset-all-bindings with exact calibration preservation; and
- frozen edits during save plus complete retryable-draft retention after
  failure.

## Current validation

The portable GCC/Ninja build completes successfully and all 115 CTests pass.
The complete MSVC 19.51/Ninja Windows product builds, links
`AirfixDogfighter.exe`, and passes all 125 CTests including both D3D11 product
smokes. Clang-format warnings-as-errors, synthetic public-boundary tests, the
596-file public repository scan, and `git diff --check` pass.

Independent review found and verified fixes for a forged conflict-resolution
value that initially selected swap instead of failing closed, missing explicit
`globalBack` coverage, one parity-ledger regression, and a stale model comment.
Final re-review reports GO with no remaining P0-P3 finding. Hosted Actions
remain the publication gate.

## Limits

- Players cannot reach this model until native text pickers are added.
- Saving does not change controller behavior in the current process.
- Analog axes, throttle directions, recovery/menu actions, and custom
  multi-binding layouts cannot be edited.
- Windows Narrator accessibility is not claimed until a bounded UI Automation
  tree exists.
- Physical Xbox, PlayStation, generic HID, restart, and recovery acceptance
  remain required.
