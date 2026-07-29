# ADR-0007: Add Windows x64 as a parallel playable product

**Status:** Accepted

**Date:** 2026-07-29

**Deciders:** project owner and implementation lead

## Context

ADR-0001 established a portable C++20 reconstruction with a Windows reference
harness followed by a native iOS port. That harness is useful for tests and
small diagnostics, but it is too narrow a target for completing and validating
the whole game.

The original is a 32-bit Windows application. A native 64-bit Windows product
provides the shortest edit-build-debug loop, the most practical environment for
running controlled comparisons beside the original, and a usable desktop
version that does not depend on Apple build, signing, or device cycles.

The iOS product remains a private ARM64 sideload. Lawfully owned original data
is available only to the owner and cannot become part of the public source
repository or public continuous-integration artifacts.

## Decision

The project has two parallel playable product targets:

1. A native Windows x64 desktop application. It is the primary environment for
   rapid debugging, parity investigation, and side-by-side comparison with the
   original.
2. A native ARM64 iOS application, distributed as a private signed sideload
   with iOS 16.4 as its minimum deployment target.

Both products use one portable C++20 implementation of:

- content identities, runtime resource formats, and validated asset loading;
- game state, update scheduling, mission and campaign rules;
- flight, physics, collision, damage, weapons, and AI;
- semantic input actions and immutable simulation input frames;
- API-neutral render and audio command models;
- saves, localization data, and deterministic verification facilities.

Gameplay, physics, or resource behavior must not fork by product. A
platform-specific policy is permitted only when the operating system or
hardware requires it, and it must be documented and tested without changing
the shared simulation contract.

Each product owns separate outer adapters:

| Boundary | Windows x64 | iOS |
|---|---|---|
| Window and lifecycle | SDL3 window/events plus Windows product lifecycle | UIKit scene and application lifecycle |
| Rendering | Direct3D 11, DXGI, and HLSL | Metal renderer |
| Physical input | SDL3 keyboard, mouse, and game controller | touch and Game Controller |
| Audio | XAudio2 2.9 device/session backend | Apple audio session and device backend |
| Files and private content | owner-local desktop import and storage | sandboxed document import and storage |

ADR-0008 selects SDL3 for the Windows window/events/physical input and
D3D11/DXGI with HLSL for rendering. ADR-0009 selects XAudio2 2.9 for Windows
audio. SDL3 owns neither game rendering nor audio; no implementation choice may
change the interfaces or ownership split above.

The original executable may run only as an external, read-only reference in a
controlled environment. The reconstructed Windows application must not load,
link, translate, or redistribute original executable modules.

Both products consume owner-supplied content converted locally into the same
versioned runtime representation. Original files, converted `.afpack` packages,
content-bearing builds, private traces, and local paths remain outside Git,
public Actions caches, logs, and artifacts.

## Consequences

- The Windows executable is a release product, not merely a diagnostic harness.
- Windows becomes the default environment for fast gameplay iteration and
  parity capture. iPhone testing remains authoritative for iOS lifecycle,
  touch, controller, performance, and Metal acceptance.
- The Windows platform target needs its own window, renderer, input, audio,
  filesystem, packaging, and release acceptance work.
- Shared deterministic scenarios, state hashes, and reference-mode captures
  must be runnable through both products to detect platform drift.
- Platform headers and APIs cannot enter the portable core.
- Public CI remains data-less. It can build and smoke-test the Windows product
  with synthetic fixtures once the platform shell exists.
- The existing private/no-App-Store decision for iOS is unchanged. This ADR
  does not grant or assume public redistribution rights for either product or
  the original data.

## Acceptance boundary

The Windows x64 product is accepted when a clean 64-bit build:

1. boots through its native window and lifecycle;
2. renders the faithful pipeline and plays required sound through its own
   backends;
3. supports complete single-player play with keyboard/mouse and a supported
   controller;
4. loads a validated owner-local private content package without embedding it
   in public source or CI;
5. completes the selected campaign path using the same game, physics, resource,
   save, and input-frame code as iOS;
6. records deterministic state, render, input, and audio evidence suitable for
   controlled comparison with the original; and
7. can be built and smoke-tested from a clean checkout without proprietary
   content.

The corresponding iOS acceptance boundary remains in
`docs/design/IOS-PORT-REQUIREMENTS.md`. The combined release scope is defined
in `docs/design/V1-SCOPE.md`.
