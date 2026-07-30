# EXP-20260730-071: private iOS controller-profile startup

## Question

Can iOS resolve the private AFIP controller profile before native input starts,
while retaining the existing current-to-backup-to-default recovery contract,
failing safely when storage is unavailable, and avoiding live replacement of
position-indexed router state?

## Scope

This slice is load-only. It does not add profile editing, saving, import, live
replacement, private content, original game data, or a repository-visible
profile. The only UI output is a bounded category such as current, backup
recovered, defaults, read-only, or storage unavailable. It never includes a
path, checksum, document byte, device identifier, or exception text.

## Design

`AirfixIOSControllerInputProfileStore` runs the portable AFIP
`current -> backup -> canonical default` resolver away from the main thread and
delivers one value-only completion on main. When the private Application
Support settings leaf cannot be prepared, the controller installs the
in-memory canonical default through the same one-time pre-start seam.

AFIP and AFRS use one process-wide serial persistence queue because both
portable stores own files in the same private directory. Directory preparation
requires an absolute file-system path, rejects links and wrong-type entries,
opens each leaf with `O_NOFOLLOW`, verifies the opened and final inode/device,
enforces owner-only permissions, and requires
`NSFileProtectionCompleteUntilFirstUserAuthentication`. Failure is closed and
path-free.

The input coordinator can start only when all four facts are true:

1. the view is visible;
2. profile loading completed;
3. the frame consumer and one-time profile installation succeeded; and
4. the application is active.

The policy is expressed as portable C++20 and tested without UIKit. A late
profile completion also re-evaluates the already-published mission's paused
readiness. It restores menu context and exposes Resume only after renderer,
simulation, input, and content gates all pass. If the display-settings panel is
open, modal context and the hidden Resume action are retained. No branch
automatically resumes gameplay.

## Synthetic verification

The startup-policy tests cover:

- content publication before profile completion;
- profile completion after the view disappears;
- profile completion while the application is inactive;
- the complete active/visible startup case;
- late completion exposing Resume for a fully published paused mission;
- input, renderer, and settings-overlay failures keeping Resume unavailable.

A fresh GCC 15.2/Ninja build completes 362 steps and all 113 CTests pass. The
new policy test is portable C++20. Objective-C++, UIKit, Foundation Data
Protection, and the complete application lifecycle remain gated by hosted
iPhoneOS/iPhoneSimulator compilation and later physical-device acceptance.

## Limits

- No profile save or editor API exists on iOS.
- A durable profile change still requires the next process launch.
- No live router/bridge replacement is authorized.
- Directory and file Data Protection behavior still requires acceptance on the
  iPhone SE 3 and iPhone 17 Pro Max.
- Hosted Apple compilation validates the native boundary but does not replace
  UI, background/foreground, force-quit, backup-recovery, or storage-failure
  tests on physical devices.
