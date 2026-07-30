# EXP-20260730-072: Windows controller calibration for next launch

## Question

Can the Windows product expose useful controller-axis calibration and a live
preview while preserving the invariant that position-indexed active input state
is never mutated or rebuilt during the running process?

## Scope

This slice edits only the four V1 stick-axis calibration records:

- inner deadzone;
- outer saturation;
- sensitivity;
- linear, squared, or cubic response;
- inversion; and
- per-axis or all-axis reset.

It does not add binding remapping, device selection, controller glyphs, trigger
calibration, live profile replacement, private content, or original game data.
A successful save affects the next process launch only.

## Design

`ControllerInputProfileMenuModel` owns separate immutable-active, persisted, and
draft records. Every edit revalidates the complete profile and preserves the
schema and binding table exactly. Save begins with an immutable, serial-tagged
full-record ticket. Success promotes only persisted and draft state, so the
model continues to report `restartRequired` whenever the running adapter was
constructed from a different record.

The native panel is part of the existing DPI-aware pause surface. Its controller
overview selects one of four standardized axes, offers all-axis reset, and
labels the durable action `Save for next launch`. The axis surface uses bounded
fixed labels and values; no device name, GUID, Bluetooth address, checksum,
document byte, or filesystem path enters the view snapshot.

Runtime and UI share `transformControllerAxisForTransport`. It applies the exact
V1 transport floor before the resolved integer-only profile transform. The
panel samples only four Q15 stick values plus a connected flag from SDL at
15 Hz and renders signed `Raw` and `Adjusted` bars. The preview does not feed
the router, simulation, or active adapter.

`AirfixWindowsControllerProfileCoordinator` owns synchronous storage only. It
validates the full candidate, maps durable store outcomes, and accepts an
ambiguous commit only when a fresh valid *current* AFIP exactly matches the
candidate. Backup/default recovery cannot prove an ambiguous publication.
Unavailable or blocked persistence disables saving without disabling the
in-memory active profile or aborting gameplay. A recovered valid backup/default
can request an explicit repair write even when the semantic draft is unchanged.

## Synthetic verification

Data-less tests cover:

- exact preview parity with the configured runtime transform, including the
  `4095 -> 0` and `4096 -> 4096` transport-floor discriminator;
- valid edits, range shoulders, inversion, response curves, per-axis reset, and
  all-axis reset;
- binding-table preservation in the immutable save ticket;
- cancel restoring persisted state;
- failed save retaining a retryable draft and issuing a fresh serial;
- unavailable persistence disabling save;
- repair-only save without a dirty draft;
- accepted save returning to pause while retaining restart-required state;
- exact commit-unknown readback, mismatch, backup/default rejection, and
  persistence capability downgrade;
- controller calibration layouts and Direct2D rasterization at 1080p, 1440p,
  ultrawide, and the supported small-output floor; and
- rejection of malformed calibration/view snapshots.

The command-line capture
`--capture-controller-calibration-panel <public-output.bmp>` constructs only a
synthetic profile and synthetic Q15 sample. It is mutually exclusive with
private content and other one-shot capture modes, never opens the AFIP store,
and produces a checked D3D11 frame suitable for public UI review.

## Current validation

The portable profile model and runtime-preview helper build and pass targeted
tests under GCC and MSVC. The Windows command-line, persistence coordinator,
panel, rasterizer, and complete `AirfixDogfighter.exe` link pass under MSVC
19.51. A 1920x1080 synthetic D3D11 capture was generated and inspected locally.
A fresh GCC 15.2/Ninja build completes 365 steps and passes all 114 portable
CTests. The complete MSVC 19.51/Ninja Windows product build passes all 124
CTests, including both D3D11 product smokes. Synthetic public-boundary tests and
the 586-file repository scan pass. Independent review reports GO with no P0-P3
finding. Hosted Actions remain the publication gate for this change.

## Limits

- Saving does not change the current process's controller behavior.
- No binding remapping UI is present.
- No iOS profile save/editor surface is present.
- No controller identity or per-device profile selection is stored.
- Physical Xbox, PlayStation, generic HID, reconnect, backup-recovery, and
  force-quit durability acceptance remain required.
