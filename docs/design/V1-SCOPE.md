# Version 1.0 product scope and target environments

**Status:** accepted

**Decision dates:** 2026-07-21; Windows x64 product added 2026-07-29

## Products and distribution

Version 1.0 has two parallel playable products:

1. a native Windows x64 desktop application, used as the primary rapid-debug
   and original-comparison environment; and
2. a native ARM64 iOS application installed as a private signed sideload on the
   project owner's registered devices.

Both products are independently written and consume the same portable C++20
gameplay, physics, resource, save, and semantic-input implementation. Windows
and iOS own separate window/lifecycle, renderer, physical-input, audio, and
filesystem/content adapters.

App Store distribution is explicitly out of scope and is not a future target
unless the owner reverses this decision. No public distribution right is
assumed for a content-bearing Windows build. Signed applications, converted
original assets, and owner-local packages remain private and are not published
or redistributed.

## Windows target

| Constraint | Accepted value |
|---|---|
| Architecture | Native x86-64; no original x86 code, emulator, or compatibility layer |
| Product role | Playable desktop reconstruction and primary debug/parity environment |
| Game implementation | Shared portable C++20 core used by iOS |
| Platform implementation | SDL3 window/events/input; D3D11/DXGI renderer with HLSL; separate Windows audio and filesystem adapters |
| Build | CMake-based x64 product target and data-less GitHub Actions validation |
| Content | Owner-local validated package; no proprietary content in source or public CI |
| Minimum Windows version | Selected during the Windows platform spike |

The existing Windows portable build and command-line tools validate common
code, but do not yet constitute the playable product. The full platform
acceptance contract is in `WINDOWS-X64-REQUIREMENTS.md`.

## iOS target

| Constraint | Accepted value |
|---|---|
| Minimum deployment target | iOS 16.4 |
| Large/high-end device | iPhone 17 Pro Max, iOS 26.6 |
| Compact/performance device | iPhone SE (3rd generation), iOS 26.3 |
| Developer account | Available |
| Build host | GitHub-hosted macOS runner through Actions |
| Local Mac/Xcode | Used later for interactive device debugging and profiling |
| Initial presentation | Landscape |
| Initial architecture | Native ARM64, Metal |

The iPhone 17 Pro Max covers a large 6.9-inch Dynamic Island/ProMotion display and
high-end Metal/thermal behavior. The iPhone SE 3 covers a compact 4.7-inch
Home-button layout and A15 performance. Both run iOS 26.x, so neither proves
runtime behavior on iOS 16.4. The accepted 16.4 boundary is build-time only:
deployment metadata, API availability, linker/dependency checks, and compile
coverage. Runtime testing below iOS 26.3 is not planned.

GitHub Actions supplies Xcode on a hosted macOS runner. The signed IPA contains
no game data; a private `.afpack` produced locally is imported separately on
each device.

Portable development and unsigned compile validation run through GitHub
Actions. Interactive physical-device debugging and profiling use local Xcode
and Apple developer tools when that phase begins.

## Included in version 1.0

- Original single-player campaign path and required mission progression.
- Aircraft flight, collision, weapons, damage, AI, actors, effects, HUD, menus,
  pause, localization, sound effects/voices found in available data, and saves.
- Windows keyboard/mouse and extended-controller play, with remapping,
  calibration, hot-plug, focus-loss safety, and controller glyphs.
- iOS touch-only and extended-controller-only play after initial selection,
  with touch profiles, remapping, calibration, hot-plug, and optional haptics.
- A Windows reference mode with deterministic scenario, state, draw, input, and
  audio capture suitable for controlled comparison with the original.
- Faithful renderer plus the agreed first set of resolution/lighting
  improvements after reference parity is established. Windows and iOS use
  separate renderer backends over the same API-neutral command model.
- Private content conversion and packaging from the available installation.

## Excluded from version 1.0

- Dogfight/multiplayer mode and networking.
- House Editor.
- Paint Room.
- App Store packaging, listing, review, public support, public asset import, or
  public redistribution.
- Public distribution of original or converted content in Windows packages,
  release downloads, or CI artifacts.
- Game Center, achievements, leaderboards, cloud saves, analytics, accounts,
  advertising, and in-app purchases.
- Original CD music, because neither the disc image nor audio tracks are
  available.

The excluded original modes may still be identified during analysis when they
help explain shared engine interfaces. They are not implemented or tested for
v1.0.

## Music policy

- The installation readme says music is read from the original CD.
- The owner does not have the CD image or audio tracks.
- Version 1.0 must operate correctly with the music content set absent.
- Sound effects and voices present in the available packages remain in scope.
- The audio engine keeps a versioned optional music interface so a legally
  available source can be added later without changing saves or gameplay.
- Do not download replacement copies of the original soundtrack from unofficial
  sources.

## Shared version 1.0 acceptance boundary

Version 1.0 is complete when:

1. Both products use one common C++20 implementation of resource formats,
   simulation, physics, game rules, saves, and semantic input frames.
2. The selected single-player campaign path completes on Windows x64 and iOS
   within the same documented simulation tolerances.
3. Both products safely survive their applicable focus/lifecycle, controller,
   audio-device/session, display, and save interruption scenarios.
4. Both faithful renderer backends pass documented reference scenarios before
   optional enhancements are accepted.
5. Neither product depends on multiplayer, either editor, CD audio, network
   access, or App Store services.
6. Both import the same validated private runtime package without uploading
   original or converted assets to GitHub.
7. Public CI builds and tests data-less targets and synthetic fixtures only.

### Windows x64 acceptance

1. A clean checkout produces a native x64 executable through documented CMake
   configuration.
2. The campaign is completable with keyboard/mouse and with a supported
   controller.
3. The native window, renderer, input, audio, and owner-local content adapters
   pass their product requirements.
4. Deterministic capture/replay and reference-mode diagnostics support fast,
   controlled side-by-side comparison with the external original.

### iOS acceptance

1. The application builds/signs reproducibly through GitHub Actions and
   installs privately from Windows on both owner devices.
2. The campaign is completable with touch only and with a supported physical
   controller after launch.
3. Pause, controller loss, audio interruption, background/foreground, safe
   areas, and save interruption are safe.
4. The build uses an iOS 16.4 deployment target with documented availability
   guards and passes the accepted iOS 26.x device matrix.

## Remaining platform decisions

- Which Windows audio API, D3D feature-level floor, minimum Windows version, and
  private packaging procedure will be supported?
- Which Windows-compatible IPA installation method will be used after Actions
  produces the signed artifact?

These decisions are proven through bounded platform spikes. They do not block
portable reconstruction, static analysis, archive decoding, or headless parity
work.
