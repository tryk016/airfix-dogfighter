# ADR-0006: Generate the first iOS application project with CMake

**Status:** Accepted

**Date:** 2026-07-21

**Deciders:** project owner and implementation lead

## Context

The port is developed primarily from Windows and built on GitHub-hosted macOS.
The repository already uses CMake for portable C++ libraries and tests. The
first iOS milestone needs a data-less UIKit/Metal application, Objective-C++ to
C++ boundary, explicit iOS 16.4 deployment target, and unsigned simulator/device
compilation. It does not yet need complex schemes, extensions, Swift packages,
or signing configuration.

Hand-maintaining an Xcode project from Windows would introduce a generated,
conflict-prone file that cannot be inspected locally with Xcode. Adding a second
project generator is possible, but should pay for itself with capabilities that
CMake cannot provide.

## Decision

Use CMake's Xcode generator for the first iOS application. Keep the checked-in
sources and `Info.plist.in` under `apps/airfix-ios`; generate `.xcodeproj` only
inside GitHub Actions build directories. The application target links portable
C++ libraries through a narrow Objective-C++ shell.

The initial target is iPhone-only, landscape-only, data-less, and unsigned. It
must build for both `iphonesimulator` and `iphoneos` with minimum iOS 16.4 and
must not upload either bundle. The public repository runs a forbidden-file
boundary check before every Apple build.

## Options considered

### A. CMake Xcode generator — selected

| Dimension | Assessment |
|---|---|
| New dependencies | none |
| Shared C++ build graph | direct |
| Windows maintenance | straightforward text files |
| Advanced Xcode features | adequate now, limited later |

**Pros:** reuses the existing build graph, keeps generated projects out of Git,
and makes deployment-target/toolchain inputs visible in CI.

**Cons:** Xcode-specific settings are expressed through CMake properties, and
large XCTest/UI-test/signing configurations can become cumbersome.

### B. XcodeGen

| Dimension | Assessment |
|---|---|
| New dependencies | one pinned generator |
| Shared C++ build graph | requires integration |
| Windows maintenance | YAML is reviewable |
| Advanced Xcode features | stronger than CMake |

This is the preferred revisit option if schemes, XCTest plans, extensions,
entitlements, or signing configurations outgrow the CMake target.

### C. Checked-in `.xcodeproj`

| Dimension | Assessment |
|---|---|
| New dependencies | none |
| Shared C++ build graph | duplicated manually |
| Windows maintenance | poor |
| Advanced Xcode features | complete |

Rejected because `project.pbxproj` is noisy, merge-prone, and difficult to
validate without a local Xcode installation.

## Trade-off analysis

The current milestone optimizes for a reproducible compile/link boundary, not
for native Apple project complexity. CMake is already required and expresses
all current needs. Choosing it now does not lock runtime architecture or asset
formats; only the project-generation layer may need replacement later.

## Consequences

- Portable logic remains testable on Windows/Linux/macOS without an Apple SDK.
- GitHub Actions is the authoritative Objective-C++/UIKit/Metal compiler.
- The app boots without game data and reports the missing private AFPACK state.
- No checked-in Xcode project is required.
- Simulator/device bundles stay ephemeral and unsigned in the public repo.
- Bundle ID, Team ID, signing, device profiles, importer UI, SDL3, and controller
  adapters remain intentionally outside this first shell.
- iPad is not claimed by the current target; add device family 2 only with a
  defined layout/runtime test plan.

## Revisit conditions

- CMake cannot express required XCTest/UI-test schemes or extensions cleanly.
- Signing/export configuration becomes fragile or duplicated.
- XcodeGen reduces measured maintenance/build work enough to justify pinning a
  new dependency.
- A checked-in Swift-heavy Apple layer becomes the dominant application code.

## Action items

1. [x] Add a portable application lifecycle/content state machine.
2. [x] Add the data-less UIKit/Metal shell and iOS 16.4 configuration.
3. [x] Add unsigned `iphoneos` and `iphonesimulator` Actions jobs.
4. [x] Keep both Apple jobs green on the hosted Xcode pin.
5. [ ] Re-evaluate the generator before adding complex native test/signing
   schemes.
