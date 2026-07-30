# EXP-20260730-076: native controller remap pickers

## Question

Can Windows and iOS expose the ADR-0015 seven-action remap transaction through
small native text surfaces without duplicating draft ownership, exposing raw
AFIP records, or changing the active input router?

## Scope

This slice adds:

- one allocation-free portable picker interaction model;
- a Windows keyboard, mouse, and controller surface;
- an iOS touch and controller surface;
- cancel-first conflict presentation with a separate explicit swap; and
- reset, outer cancel, and next-launch-only save paths shared with calibration.

It does not add physical-event capture, controller identity, glyphs, analog or
menu remapping, live router replacement, Windows UI Automation, private
content, or original game data.

## Shared interaction model

`ControllerInputBindingPickerModel` owns only the current action, bounded
control-catalog index, phase, and optional conflicting action. The existing
`ControllerInputProfileMenuModel` remains the sole owner of active, persisted,
draft, and save-ticket state.

Opening the picker requires one uniquely editable supported action and
preselects its exact current control. Applying a selection always uses the
cancel conflict policy first. A swap is attempted only after a separate
confirmation call and revalidates the current draft instead of trusting stale
conflict metadata. Save-in-progress, malformed, missing, ambiguous, custom, and
protected states fail closed.

## Native surfaces

The Windows settings panel adds a compact five-row text picker for action,
assignment, move, reset, and back. A conflict switches to a two-row screen
whose initial selection is `Cancel`; `Swap assignments` is a separate action.
The renderer consumes a bounded typed snapshot and fits the surface at the
existing 640x360 minimum as well as representative desktop outputs.
A data-less `--capture-controller-bindings-panel <output.bmp>` path constructs
the canonical default profile and captures the exact D3D11 surface without
opening private storage.

The iOS controller-settings panel keeps calibration and remapping in one
safe-area scroll view and one AFIP draft. It lists all seven actions and all
fourteen assignable controls, scrolls the selected row into view, supports
touch and controller-only navigation, and uses Dynamic Type, accessibility
labels, selected traits, announcements, and Accessibility Escape. Conflict and
reset confirmations both default to `Cancel`.

## Synthetic verification

Portable and Windows tests cover:

- exact current-control preselection and bounded movement;
- an unused-control move;
- cancel-first conflict with unchanged draft;
- explicit atomic swap and stale-conflict revalidation;
- protected Menu and custom/multi-binding rejection;
- reset with exact calibration preservation;
- shared outer Cancel and complete next-launch save ticket;
- pointer, keyboard, and controller navigation;
- bounded snapshots and malformed snapshot rejection; and
- 640x360 plus representative desktop raster layouts.

## Current validation

The complete portable GCC/Ninja build passes 117/117 CTests. The complete MSVC
19.51/Ninja Windows product links `AirfixDogfighter.exe` and passes 127/127
CTests, including both D3D11 product smokes. Focused Windows picker/rasterizer
tests, clang-format warnings-as-errors, synthetic public-boundary tests, the
604-file public repository scan, and `git diff --check` pass.

Independent review found that controller-only Windows input could leave a
visually disabled panel during an in-flight save and that one visible iOS
selected-state prefix was not localizable. A central save-phase guard, a
regression test, and a localized format fixed both; final re-review reports GO
with no remaining P0-P3 finding. Hosted unsigned Apple compilation and
physical-device acceptance remain publication or device gates.

## Limits

- A successful changed save still applies only on the next process launch.
- Windows Narrator support is not claimed until a bounded UI Automation tree
  exists.
- UIKit accessibility and layout need physical iPhone SE 3 and iPhone 17 Pro
  Max acceptance, including maximum Dynamic Type and VoiceOver.
- Controller glyphs, identity, haptics, live replacement, and finished product
  menus remain later layers.
