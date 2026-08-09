# First iOS device acceptance checklist

**Status:** first owner-local installation, AFPACK import, mission-selection
import, static mission rendering, touch UI, settings, and lifecycle have been
observed on one device; systematic two-phone acceptance remains pending

Use this checklist for the first installation on the registered iPhone 17 Pro
Max and iPhone SE (3rd generation). It validates the native iOS product shell,
not complete Airfix Dogfighter gameplay parity.

## Acceptance boundary

This pass can accept:

- private signed installation and offline, data-less startup;
- local AFPACK import and one authenticated mission snapshot;
- Metal rendering, native output sizing, safe-area layout, and diagnostics;
- touch and Bluetooth-controller transport, pause, disconnect, and recovery;
- the public synthetic audio-output probe, audio routes, and lifecycle policy;
- durable render, controller, and touch-control settings; and
- foreground/background behavior without automatic gameplay resume.

It cannot accept live aircraft movement, campaign completion, save migration,
gameplay-audio timing, spatial audio, controller rumble, or long thermal
performance. Those still depend on runtime producers or later milestones. Do
not substitute the synthetic audio probe for `SCN-AUDIO-001` parity evidence.

## Prerequisites

- Start from one trusted `main` source revision. Either build and sign that exact
  revision locally through Xcode, or download its public unsigned IPA and
  manifest and verify both before local sideloader signing.
- Never attempt to install the unsigned IPA directly. Xcode or the selected
  local sideloader must produce a valid development signature/profile for the
  target device and bundle identifier.
- Keep the signed IPA, certificate, profile, Apple session, device identifiers,
  AFPACK, HD textures, screenshots, and filled result record outside the public
  repository and GitHub artifacts.
- A sideloader must sign locally. Do not upload the IPA, Apple credentials, or
  signing material to a cloud-signing service.
- Transfer the owner-local AFPACK through a private channel. The IPA itself
  must remain data-less.
- Charge the phone, disable Low Power Mode for the performance observation,
  and record the exact device model and iOS version.
- For controller checks, pair one supported Bluetooth extended gamepad through
  iOS Settings. The application does not implement Bluetooth pairing UI.

## Source and installation preflight

Record these values in a private result record before installation:

| Field | Required evidence |
|---|---|
| Source revision | full trusted commit SHA |
| Installation route | local Xcode build or reviewed local sideloader |
| Product | ARM64 `iphoneos`, minimum OS 16.4, expected bundle identifier |
| Local signing | chosen local method, resulting bundle identifier, profile/signature expiry, target device eligibility |
| Payload boundary | no AFPACK, original files, HD textures, signing material, local paths, or simulator-smoke marker |

For the recommended Xcode route, record the clean checkout SHA, active Xcode
version, selected Apple Team, and locally built product identity. Xcode builds
and signs that source revision directly; the downloaded unsigned Actions IPA is
optional comparison evidence and is not the binary installed by Xcode.

For the sideloader route, additionally record the public workflow run and
manifest schema, then verify that the downloaded unsigned IPA SHA-256 equals the
manifest before giving it to the local signer. Stop before signing if the IPA,
source revision, or dependency-lock identity differs from the public manifest.

For either route, stop before installation if the local signer changes the
payload unexpectedly or cannot produce a valid device signature.

The public `Unsigned iOS` workflow produces the unsigned artifact only from
trusted `main`; pull requests build but do not upload an IPA. Detailed Xcode and
sideloader boundaries are in [the iOS CI design](../ci/GITHUB-ACTIONS-IOS.md).

## Device matrix

Run the whole first-pass sequence independently on both devices. Do not copy a
PASS from one row to the other.

| Scenario | iPhone 17 Pro Max / iOS 26.6 | iPhone SE 3 / iOS 26.3 |
|---|---|---|
| `SCN-IOS-DEVICE-001/002` install and launch | pending | pending |
| `SCN-IOS-001` offline/data-less boot | pending | pending |
| `SCN-IOS-DATA-001` private AFPACK import | pending | pending |
| `SCN-IOS-011` safe areas and paused UI | pending | pending |
| Touch transport subset of `SCN-IOS-002` | pending | pending |
| Controller transport subset of `SCN-IOS-003/004` | pending | pending |
| Lifecycle subset of `SCN-IOS-005` | pending | pending |
| Audio-output subset of `SCN-IOS-007` | pending | pending |
| Native presentation subset of `SCN-IOS-013` | pending | pending |

Use only `PASS`, `FAIL`, or `NOT RUN` in the private copy. Add a short reason
for every `FAIL` or `NOT RUN`.

## Test sequence

### 1. Installation and data-less boot

1. Remove any older test build and install the locally signed candidate.
2. Enable Airplane Mode before the first launch.
3. Launch the application without an AFPACK.
4. Confirm that the paused/import surface appears, remains responsive, and does
   not require network access or crash because content is absent.
5. Background and foreground the data-less app once. Confirm that it stays
   paused and does not begin audio or gameplay automatically.

Pass: clean offline startup reaches an actionable content-import state, all
controls respect the safe area, and no private data is already present.

### 2. Private content import and mission snapshot

1. Import the owner-local AFPACK using the system document picker.
2. Observe validation and installation without revealing the source path in
   the application UI.
3. Import the matching owner-local `.afmission` with `Import Mission
   Selection`. Confirm that neither logical paths nor checksums appear in UI.
4. Wait for the authenticated private mission to load.
5. If the optional HD package is configured, test Classic first and Enhanced
   second. A rejected or absent HD package must leave Classic usable.
6. Kill and relaunch the app. Confirm that the authenticated installed package
   and durable settings recover without another source-file dependency.
7. In Files, open `On My iPhone/Airfix Dogfighter`. Confirm that `README.txt`,
   `Imports`, and `Diagnostics` exist. Files copied into `Imports` must still
   require an explicit validated import in the app; they must never
   auto-activate.

Pass: import is atomic, a complete room snapshot appears, failed optional HD
content cannot prevent Classic fallback, and relaunch finds a valid installed
generation.

### 3. Rendering and safe areas

1. Inspect the room, placed objects, player aircraft, textures, and diagnostic
   overlay in both landscape orientations.
2. Confirm that no required button, throttle, stick, label, or menu item lies
   under the Dynamic Island, rounded corners, Home indicator, or screen edge.
3. Confirm that diagnostics report the physical render target and render scale
   rather than a historical 640x480 framebuffer.
4. Change render scale, UI scale, Classic/Enhanced selection when available,
   and safe FOV. Reopen settings and relaunch to verify persistence.
5. Watch for a black frame, missing geometry, texture fallback, flicker,
   stretched projection, NaN geometry, or Metal validation failure.

Pass: the scene and paused UI remain legible and correctly framed on both
devices and orientations. This is presentation acceptance only; the current
bootstrap aircraft pose is not evidence of live flight physics.

### 4. Touch controls

1. Resume, then exercise the dynamic flight stick, vertical latched throttle,
   primary and secondary fire, pause, and at least two simultaneous touches.
2. Confirm input diagnostics change and return to neutral when every touch is
   released or cancelled.
3. Change handedness, compact/large density, opacity, automatic-controller-hide
   policy, and throttle-detent feedback; verify relayout cancels active input.
4. Move the throttle through 0%, 50%, and 100% both slowly and quickly. Confirm
   at most one configured selection feedback per newly reached detent.
5. Background while a control is held, return, and explicitly resume.

Pass: no stuck action survives pause, relayout, background, or cancellation;
the overlay stays usable inside the safe area. Input-state transport is being
accepted here, not a complete mission or flight law.

### 5. Bluetooth controller

1. With the app paused, connect the paired controller and navigate every
   available paused-menu item without touching the screen.
2. Exercise calibration/remapping, save the profile, restart, and confirm that
   the profile is recovered.
3. Resume and exercise both sticks, triggers, D-pad, shoulders, A/B/X/Y,
   menu/options, pause, and weapon selection while watching input diagnostics.
4. Hold fire and an axis, then disconnect the controller.
5. Confirm immediate release, pause, no automatic resume, and touch-control
   recovery according to the saved visibility policy.
6. Reconnect and verify the neutral-generation gate before new gameplay input
   is accepted.

Pass: menu and transport paths work without screen touch, disconnect cannot
leave an action held, and reconnect does not replay stale state. Controller
rumble is outside this pass.

### 6. Synthetic audio-output probe

1. Pause gameplay or remain data-less and start the public synthetic audio
   probe from its dedicated control.
2. Confirm an audible, bounded test tone through the phone speaker, then stop
   it explicitly and confirm silence.
3. Repeat with the Ring/Silent switch in silent mode. The Ambient-session policy
   should respect silent mode.
4. Repeat over a Bluetooth audio route, then remove or change the active route
   during the probe.
5. During a tone, open Control Center or otherwise cause an audio interruption;
   then return to the app.
6. Confirm that interruption, route loss, background, mission resume, or view
   disappearance stops the probe and never replays it automatically.

Pass: speaker and Bluetooth output are audible when policy permits, stop is
immediate, failures are path-free/actionable, and recovery requires an explicit
new start. This does not accept authenticated AirCraft audio scheduling.

### 7. Lifecycle and short stability observation

1. Resume, hold touch or controller input, background for at least ten seconds,
   then foreground.
2. Confirm neutral input, stopped audio, paused Metal/game session, hidden
   gameplay overlay, and an explicit Resume action.
3. Repeat during content validation if a disposable re-import is available;
   never interrupt or replace the only known-good package.
4. Run the loaded static scene for ten minutes while switching the supported
   display profiles and input source. Record FPS range, thermal state shown by
   iOS, memory warnings, visible corruption, and crashes.

Pass: no automatic resume, stuck input, continuing sound, corrupted installed
content, crash, or progressive rendering failure. Ten minutes is a first-spike
observation, not final `SCN-IOS-009` thermal acceptance.

### 8. Owner-local diagnostic journal

1. Open `On My iPhone/Airfix Dogfighter/Diagnostics/latest.jsonl` in Files or
   copy it to the owner-controlled computer after the preceding tests.
2. Confirm the file is non-empty and no larger than 1 MiB. A single optional
   `previous.jsonl` may exist after restart or rotation.
3. Confirm records begin with `session.started` (or `session.continued` after a
   size rotation) and include renderer/content states, mission load result,
   lifecycle transitions, pause/resume, and bounded input samples.
4. Search the copied journal for original filenames, mission logical paths,
   AFPACK/HD checksums, source URLs, device-container paths, Apple account data,
   and signing material. None may appear.
5. Relaunch once and confirm the former `latest.jsonl` becomes
   `previous.jsonl`, a new `latest.jsonl` is created, and total app-written log
   storage remains bounded to two 1 MiB files.
6. During a resumed mission, move the flight stick and throttle and press then
   release primary fire. Confirm a later `input.sample` contains the non-zero
   axis values and that fire press/release edges are present even when they
   occur between periodic samples.

Pass: the owner can retrieve actionable path-free evidence without a Mac,
network upload, or screenshot transcription; file sharing does not bypass any
private-content validation or installation transaction.

## Result record

Keep the completed record private. Include:

- source SHA, workflow run, unsigned IPA SHA-256, manifest schema, local signing
  method, resulting bundle identifier, and provisioning expiry;
- device model, exact iOS version, available storage, and controller model;
- AFPACK schema/source-build identity and SHA-256 without its local path;
- Classic/Enhanced selection and render/UI scales;
- one result per checklist row plus reproduction steps for failures;
- FPS/thermal/memory observations;
- the copied `latest.jsonl`/`previous.jsonl` journal when applicable; and
- crash identifier or a small redacted diagnostic excerpt when applicable.

Do not commit filled records, screenshots containing private game assets,
device identifiers, IPA files, profiles, certificates, crash dumps, or private
content inventories. A public issue may contain only a redacted behavioral
summary and a source revision.

## Completion rule

The first device spike is complete only when both phones have a recorded result
for every row. A failure does not authorize weakening the invariant or hiding
the result; return to a synthetic/local reproduction where possible, fix it,
produce a new unsigned build, sign it locally, and rerun the affected row on
both devices.

See also:

- [iOS port requirements](../design/IOS-PORT-REQUIREMENTS.md)
- [GitHub Actions iOS build and signing design](../ci/GITHUB-ACTIONS-IOS.md)
- [Audio system](../systems/AUDIO.md)
- [Parity matrix](PARITY-MATRIX.md)
