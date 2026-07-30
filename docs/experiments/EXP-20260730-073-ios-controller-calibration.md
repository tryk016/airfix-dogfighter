# EXP-20260730-073: iOS controller calibration for next launch

## Question

Can the private iOS product edit and durably save controller-axis calibration
without replacing position-indexed input state, exposing device identity, or
making gameplay depend on Application Support availability?

## Scope

The UIKit surface edits only the four standardized V1 stick-axis calibration
records: inner deadzone, outer saturation, sensitivity, response curve,
inversion, per-axis reset, and all-axis reset. It does not remap bindings,
select a physical controller, calibrate triggers, replace the active profile,
or read private game content. A changed save applies on the next process
launch.

## Design

`AirfixControllerInputProfileCoordinator` is a main-thread owner of one
immutable active record and one mutable persistent record. It has no reference
to `AirfixIOSInputCoordinator`. The calibration panel creates the shared
portable `ControllerInputProfileMenuModel`, so edits, cancel, repair-only save,
ticket serialization, binding preservation, retry, and restart-required state
use the same tested semantics as Windows.

The native store copies a complete value record onto the process-wide serial
settings queue. A save is durable only after the portable AFIP store confirms
it. If publication is ambiguous, recovery accepts only a fresh valid `current`
AFIP exactly equal to the candidate. Backup/default recovery is not commit
proof. Storage-unavailable and future-schema-blocked states disable saving but
leave the installed in-memory profile and gameplay operational.

The pause surface offers Display settings and Controller calibration as
separate selectable actions. Both overlays use the validated `menu` input
context; valid custom AFIP profiles therefore do not need modal or
control-editor bindings. The controller panel has an overview and a per-axis
detail screen, supports touch and controller navigation, stays inside the safe
area, and never resumes gameplay on close.

`AirfixUIInputSnapshot` adds only a connected flag and four raw standardized
Q15 axes. No vendor name, product category, generation, GUID, Bluetooth
address, path, checksum, profile bytes, or Foundation error enters the panel.
Samples are zeroed on disconnect and every input/lifecycle reset. The panel
refreshes at 15 Hz and computes its adjusted value only through
`transformControllerAxisForTransport(raw, axis, resolvedDraftProfile())`.
Draft preview values never enter the router or simulation.

## Synthetic verification

Existing portable tests cover every calibration edit, invalid range, full
profile validation, binding preservation, cancel, retry, serial exhaustion,
repair-only save, restart-required state, and exact preview/runtime parity,
including the `4095 -> 0` and `4096 -> 4096` transport-floor discriminator.

The shared store tests now also prove the native repair policy:

- clean missing current/backup defaults do not request repair;
- a valid current does not request repair;
- validated backup recovery requests repair;
- replaceable invalid current/default recovery requests repair; and
- a read-only future-schema boundary never offers repair.

## Current validation

A complete GCC/Ninja portable build passes 114/114 CTests. A complete Visual
Studio 2026 MSVC/Ninja Windows rebuild links `AirfixDogfighter.exe` and passes
124/124 CTests, showing that the shared repair-policy extraction did not regress
the desktop product. `clang-format` and `git diff --check` pass.

Hosted unsigned `iphoneos` and `iphonesimulator` compilation remains the native
Objective-C++ publication gate. Physical-device acceptance remains required
for safe-area/VoiceOver layout, controller navigation and preview, Data
Protection inheritance, save, force-quit, relaunch, background suspension, and
recovery on iPhone 17 Pro Max and iPhone SE 3.

## Limits

- Saving never changes controller behavior in the current process.
- Binding remapping, trigger calibration, glyphs, haptics, and device-specific
  profiles are not implemented.
- The UI intentionally exposes no controller identity.
- No background execution time is assumed; interrupted writes rely on the
  existing atomic partial/current/backup recovery protocol.
- A physical iPhone is still required for final persistence and lifecycle
  acceptance.
