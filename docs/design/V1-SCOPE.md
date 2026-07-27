# Version 1.0 scope and target environment

**Status:** accepted

**Decision date:** 2026-07-21

## Distribution

- Version 1.0 is a private, signed sideload for the project owner.
- App Store distribution is explicitly out of scope and is not a future target
  for this project unless the owner reverses this decision.
- The build is installed through Apple development/ad hoc provisioning using the
  available Apple Developer account.
- The signed application and converted original assets remain private and are
  not published or redistributed.

## Apple target

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
no game data; a private `.afpack` produced by the Windows converter is imported
separately on each device.

Portable development and unsigned compile validation run through GitHub
Actions. Interactive physical-device debugging and profiling use local Xcode
and Apple developer tools when that phase begins.

## Included in version 1.0

- Original single-player campaign path and required mission progression.
- Aircraft flight, collision, weapons, damage, AI, actors, effects, HUD, menus,
  pause, localization, sound effects/voices found in available data, and saves.
- Complete touch-only control path.
- Complete extended-controller-only control path after initial selection.
- Touch layout profiles, remapping, calibration, controller hot-plug, and
  optional phone/controller haptics.
- Faithful renderer plus the agreed first set of resolution/lighting
  improvements after reference parity is established.
- Private content conversion and packaging from the available installation.

## Excluded from version 1.0

- Dogfight/multiplayer mode and networking.
- House Editor.
- Paint Room.
- App Store packaging, listing, review, public support, public asset import, or
  public redistribution.
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

## Version 1.0 acceptance boundary

Version 1.0 is complete when the selected single-player campaign path:

1. Builds/signs reproducibly through GitHub Actions and installs privately from
   Windows on both owner devices.
2. Can be completed with touch only.
3. Can be completed with a supported physical controller only after launch.
4. Safely survives pause, controller loss, audio interruption,
   background/foreground, and save interruption scenarios.
5. Contains no dependency on multiplayer, either editor, CD audio, network
   access, or App Store services.
6. Uses an iOS 16.4 deployment target with documented API availability guards.
7. Imports a validated private `.afpack` without uploading original or converted
   assets to GitHub.

## Remaining implementation decision

- Which Windows-compatible IPA installation method will be used after Actions
  produces the signed artifact?

This is proven during the first signed device spike and does not block Windows
static analysis, archive decoding, reconstruction, or the desktop vertical slice.
