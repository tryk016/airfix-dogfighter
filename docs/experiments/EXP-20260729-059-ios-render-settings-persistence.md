# EXP-20260729-059 - asynchronous iOS render-settings persistence

**Question:** Can iOS load and persist ADR-0014 presentation settings without
blocking UIKit or publishing a renderer state before its durable value?
**Source build/hash:** `28251b27`
**Working-copy hash:** PR #62 implementation commits `551e330`, `7c38e35`,
`9eed327`
**Environment:** Windows GCC/Ninja; isolated WSL Ubuntu GCC/Ninja; hosted Xcode
26.6 iPhoneOS and iPhoneSimulator
**Tool versions:** CMake 4.4.0; Ninja 1.13.2; hosted Xcode 26.6
**Offline/no-cloud confirmed:** public source only; no owner data uploaded
**Memory/file patching:** not applicable
**Safety boundary:** synthetic settings only; Application Support paths and
Foundation errors do not cross the adapter; no original asset is bundled
**Related IDs:** ADR-0014, EXP-20260729-057, EXP-20260729-058

## Prediction

One owner-thread gate can enforce:

```text
main capture -> worker prepare -> serial durable save
             -> main fresh validate -> immediate no-fail commit
```

A stale surface after the save should return to preparation with the same
persistent/effective values and a new attempt number, never to saving.

## Procedure

1. Add a portable ticketed gate and exercise new, already durable, stale,
   repeated-stale, wrong-thread, overflow, and sparse-override cases.
2. Capture an exact copy of the Metal transaction baseline on main.
3. Prepare an immutable Metal token on a dedicated serial queue without
   reading UIKit or live renderer state.
4. Save the persistent base through a second serial adapter rooted under the
   app-private Application Support directory.
5. Revalidate exact renderer, view, device, drawable extent, surface
   generation, and active revision on main; commit immediately on success.
6. Run clean portable builds/tests on Windows and WSL, the public-boundary
   scanner, and PR builds for both Apple SDKs and the portable/native products.

## Raw observations

- Windows GCC/Ninja: 99/99 CTests pass.
- Isolated WSL Ubuntu GCC/Ninja: 99/99 CTests pass.
- Public-boundary scanner: 505 files pass.
- The iPhoneSimulator and iPhoneOS jobs use hosted run
  [30505632588](https://github.com/tryk016/airfix-dogfighter/actions/runs/30505632588).
- Portable/macOS/Windows/clangd and the native D3D11/XAudio2 product use hosted
  run
  [30505632630](https://github.com/tryk016/airfix-dogfighter/actions/runs/30505632630).

## Interpretation

The storage and renderer transactions are now joined without main-thread file
I/O. Persistent base, session overrides, and effective renderer values remain
separate. A successful save is an obligation to publish: stale validation
increments only the preparation attempt, while the gate remains durable and
busy. Later UI requests are coalesced until that obligation finishes.

The adapter resolves and protects only its private settings leaf, requests the
stronger Darwin `F_FULLFSYNC` barrier after `fsync` where supported, and
returns path-free error categories.
`commitUnknown` is accepted only after a fresh load exactly matches the
candidate.

Independent review found retry-wakeup and linked-directory issues before
merge. The fixes add an exact-ticket 50 ms to 2 s retry with a separate timer
generation, and create each component with its final protection before opening
it no-follow. Permission changes use the stable directory descriptor; existing
protection is verified read-only and exact device/inode identity is rechecked.
The final independent re-review reports no remaining P0-P2.

This is compile-time and deterministic state-machine evidence. Physical-device
force-quit/relaunch, locked-device file-protection, memory-pressure, and
surface-churn acceptance remain required on the two target iPhones.

## Reproduction artifacts

- Portable tests:
  `airfix_render_settings_persistence_gate_tests` and
  `airfix_render_presentation_transaction_tests`
- Hosted source/build logs: GitHub Actions runs `30505632630` and
  `30505632588`
- No settings file, local profile path, screenshot, or owner-derived asset is
  committed.

## Integrity checks

- Source and copy hashes match the public manifest: not applicable
- Reports contain no local paths: yes
- Original files were not modified: yes
- Public-boundary check: pass, 505 files

## Follow-up

- Complete the cross-input settings UI using the coordinator's one persistent
  request boundary.
- Validate save/relaunch and failure recovery on iPhone SE 3 and iPhone 17 Pro
  Max.
- Continue to keep all presentation settings outside deterministic simulation.
