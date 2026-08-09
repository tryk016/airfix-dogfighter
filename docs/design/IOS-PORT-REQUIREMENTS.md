# iOS port requirements and release checklist

**Status:** proposed product/system requirements

**Scope:** everything the private iOS sideload needs beyond reconstruction of
the original game logic; accepted v1.0 boundary is in `V1-SCOPE.md`

This is the iOS-specific contract within a two-product project. The parallel
playable Windows target is defined in `WINDOWS-X64-REQUIREMENTS.md` and
ADR-0007. Both products consume the same portable game, physics, resource,
save, and semantic-input implementation.

## Priority model

| Priority | Meaning |
|---|---|
| P0 | Required for the first playable iOS vertical slice |
| P1 | Required for a release-quality single-player port |
| P2 | Full parity or valuable platform integration after the core release path |
| P3 | Optional modernization/enhancement |

## Release layers

### Playable device slice

One level on a physical device with touch flight, combat, pause, audio, safe
resume, Metal rendering, and basic saving. This proves the architecture.

The accepted physical matrix is iPhone 17 Pro Max/iOS 26.6 and iPhone SE
(3rd generation)/iOS 26.3. iOS 16.4 is the deployment target, not a runtime test
environment.

### Single-player release candidate

Complete campaign path, all required controls, controller support, menus,
localization, saves, accessibility baseline, performance tiers, lifecycle and
interruption handling, packaging, diagnostics, and device matrix.

### Post-v1 optional work

Separately cleared higher-resolution replacement content and additional input
modes may follow v1. Native-resolution 3D, Hor+ widescreen, scalable sharp UI,
and the staged `Classic`/`Enhanced` renderer are product requirements under
ADR-0013, not a low-resolution upscale or an undefined post-v1 aspiration.
Multiplayer, House Editor, Paint Room, App Store services, and public
distribution are not part of v1.0.

## 1. Input and interaction

Detailed contract: `docs/systems/INPUT.md`.

| Requirement | Priority | Acceptance summary |
|---|---:|---|
| Complete touch-only gameplay | P0 | no controller or keyboard required |
| Dynamic flight stick and throttle | P0 | simultaneous flight/fire/throttle works |
| Fire, weapon, camera, status, pause controls | P0 | every core action reachable |
| Bluetooth/USB extended controller | P1 | hot-plug, remap, disconnect recovery |
| Controller-only menus and campaign | P1 | no forced screen touch after selection |
| Touch layout editor and presets | P1 | phone/tablet/left-handed/large controls |
| Input remapping and calibration | P1 | deadzones, curves, inversion, reset |
| Phone and controller haptics | P1 | optional, capability-based, safely stopped |
| iPad keyboard/mouse | P2 | useful secondary input, never required |
| Motion/gyro assistance | P3 | optional with recenter and fallback |

## 2. Screen, UI, and presentation

- Support both landscape orientations unless a measured hardware limitation
  proves one unsafe. Never rotate into portrait during gameplay. (P0)
- Recompute safe-area insets after rotation, system overlay, external display,
  or scene-size change. (P0)
- Keep controls/HUD clear of the Home indicator, rounded corners, camera cutout,
  Dynamic Island, and system gesture zones. (P0)
- Separate render resolution from logical UI coordinates and touch points. (P0)
- Use Hor+ as the standard widescreen policy while preserving reference
  vertical FOV. Keep Original 4:3 as an aspect-fitted comparison mode and never
  stretch geometry/UI. (P0/P1)
- Expose the shared, durable 0-25 degree vertical-FOV increase as an explicit
  option. It must not overwrite authored camera state or change simulation,
  render-target, UI, or safe-area domains. (P1)
- Keep reference camera coordinates, UI design space, Metal drawable pixels,
  3D render-target pixels, viewport/safe area, and render scale explicit. At
  100%, the 3D target exactly matches the drawable extent. (P0)
- Support at least 50-200% render scale subject to Metal device limits without
  changing HUD/text size or touch hit regions. (P1)
- Provide scalable HUD/text, phone/tablet layouts, and readable mission text.
  (P1)
- Replace mouse-hover assumptions with visible touch focus/selection states.
  (P1)
- Support system text input where player/roster names are entered. (P1)
- Handle screenshots/photos/stickers through sandbox-safe APIs. (P2)
- Consider external display and Stage Manager only after core iPad behavior is
  stable. (P3)
- Explicitly test large 6.9-inch Dynamic Island/ProMotion layout on iPhone 17 Pro
  Max and compact 4.7-inch Home-button layout on iPhone SE 3. (P0)

## 3. App lifecycle and interruptions

The app must have explicit states rather than letting UIKit/SDL callbacks mutate
the simulation arbitrarily:

```text
ColdStart -> Loading -> Running <-> Paused
                         |
                         v
                    Inactive -> Background -> Suspended
                         |
                         v
                       Resume
```

- On resign-active: release all inputs, pause simulation, stop haptics, and mark
  audio/render state inactive. (P0)
- On background: finish or cancel bounded writes safely, stop unnecessary CPU/GPU
  work, and save an atomic recovery checkpoint when the current state permits.
  (P0)
- On foreground: recreate transient graphics/audio/controller resources, keep
  gameplay paused, and require deliberate resume. (P0)
- Handle phone/system overlays, lock screen, notifications, control center,
  route changes, and audio interruptions. (P0/P1)
- Respond to memory warnings without discarding assets that would corrupt the
  active frame or save; unload ranked caches. (P1)
- Track thermal state and reduce optional effects/frame target before the OS must
  intervene. (P1)
- Never rely on background execution to advance gameplay or multiplayer in the
  initial single-player release. (P1)

## 4. Rendering and graphics

- Native Metal renderer with validated shader/resource lifetime. (P0)
- Faithful fixed-function-equivalent path for original texture stages,
  transparency, fog, depth, lightmaps, and blending. (P0/P1)
- Rasterize 3D directly at the drawable extent when render scale is 100%;
  never enlarge a 640x480, 1120x480, or other historical framebuffer as the
  mandatory presentation path. (P0)
- Correct Hor+ aspect ratio, viewport, projection, safe user FOV, UI scale,
  safe area, input mapping, weapon/HUD/reticle/effect projection, color space,
  gamma, and texture alpha. (P0/P1)
- Frame pacing with stable simulation timing independent of display refresh.
  Support 60 Hz target first; higher refresh is an enhancement only after logic
  remains deterministic. (P1)
- `Classic` renders a faithful high-resolution presentation; `Enhanced` adds
  the modern lighting/material/effect path. Both remain independent of
  `Low`/`Medium`/`High`/`Ultra` quality tiers and separate shadow, MSAA,
  effect, and render-scale controls. (P1)
- Quality defaults are selected from measured GPU, memory, and thermal
  capabilities; settings remain user-overridable within safe bounds. (P1)
- Shader compilation/pipeline preparation must not cause repeated combat stutter.
  (P1)
- Classic and Enhanced render modes have separate screenshot baselines. (P1)
- Implement linear/sRGB correctness, depth precision, mipmapping, anisotropic
  filtering and anti-aliasing before staged directional/point/spot lights,
  adjustable shadows, physically informed materials, HDR, tone mapping,
  exposure, controlled bloom, fog/atmosphere, performance-qualified ambient
  occlusion, improved particles, and switchable post-processing. Optional
  MetalFX cannot replace native rasterization at 100%. (P1/P2)

## 5. Performance, memory, storage, and power

Budgets use iPhone SE 3/A15 as the available performance floor and iPhone 17 Pro
Max as the high-quality/high-refresh target. Instrumentation exists before
optimization.

- Record CPU/GPU frame time, fixed-tick overruns, input-to-tick latency, peak and
  resident memory, asset load duration, storage usage, and thermal state. (P0)
- Avoid loading all converted resources at startup; use validated streaming and
  ranked caches. (P1)
- Bound archive decompression and transient conversion memory. (P1)
- Define cold-start, menu-ready, level-load, and resume budgets. (P1)
- Provide 60 fps performance mode and a stable lower-power fallback only if
  device measurements justify it. Simulation speed cannot change with frame
  rate. (P1)
- Target 60 FPS on iPhone 17 Pro Max at native resolution or a controlled,
  visible dynamic render scale. On iPhone SE 3 require a stable 30 FPS minimum
  and target 60 FPS with an appropriate quality profile. (P1)
- Pause or reduce work when obscured/inactive and stop work in background. (P0)
- Prevent runaway log, save, screenshot, and cache growth. (P1)
- Test sustained play for heat/battery, not only short benchmark scenes. (P1)

## 6. Audio, music, video, and haptics

- Recreate sound event timing, positional audio, priorities, and volume groups.
  (P0/P1)
- Configure an appropriate iOS audio session and handle interruptions, route
  changes, headphones, Bluetooth audio latency, and user volume/mute policy.
  (P1)
- Prevent audio from continuing while gameplay is backgrounded or unexpectedly
  paused. (P0)
- Provide independent master/music/effects/voice controls and subtitle controls
  where voice/dialogue exists. (P1)
- Original CD audio tracks are unavailable. Treat music as an optional absent
  content set, never fail boot/level loading because tracks are missing, and do
  not obtain unofficial copies. Preserve a future optional music interface. (P1)
- Decode/transcode legacy AVI intros into an Apple-supported delivery path or
  replace/omit them with documented behavior if rights/codec constraints require
  it. (P2)
- Haptic events and controller rumble follow the input-system feedback contract.
  (P1)

## 7. Saves, settings, and data safety

- Use sandbox-correct Application Support/Documents/Caches placement by data
  class; do not assume Windows paths or case-insensitive lookup. (P0)
- Atomic save writes: temporary file, validation, replace, and recoverable backup.
  (P0)
- Version every save/settings/input schema and implement forward migration. (P1)
- Save at safe checkpoints, mission boundaries, explicit user action, and
  lifecycle transition where valid. Never serialize half-mutated game state.
  (P1)
- Validate corrupt/truncated/unknown saves and present a recoverable choice.
  (P1)
- Decide backup exclusion for reproducible caches and include irreplaceable saves
  appropriately. (P1)
- Provide reset controls separately for graphics, audio, input, progress, and all
  data, with confirmation. (P1)
- iCloud save sync is optional and requires conflict/version design; local atomic
  saves come first. (P3)

## 8. Asset packaging and content installation

- Offline converter reads the original installation/disc and writes a versioned,
  validated runtime package. It never modifies source files. (P0)
- Runtime package records source build ID, converter version, schema, entry
  checksums, required/optional content, localization, and compatibility range.
  (P0)
- Private builds may bundle locally converted content for installation on the
  owner's registered device. The accepted CI design instead keeps the IPA
  data-less and imports a locally converted private `.afpack` after installation.
  Signed builds and converted packages are not published or redistributed. (P0)
- The app boots without content, exposes a private import flow, validates and
  atomically installs `.afpack`, and preserves saves when content is replaced.
  (P0)
- Fail startup with an actionable error when package version or required assets
  are missing; do not crash deep in a level loader. (P0)
- No public content import or distribution model is required. (P0)
- Converted asset caches are reproducible and can be rebuilt; saves never depend
  on cache file offsets. (P1)
- Test case-sensitive paths because Apple filesystems and archive lookup behavior
  may expose assumptions hidden on Windows. (P1)

## 9. Menus, onboarding, help, and discoverability

- First-run flow chooses language, explains touch controls, offers controller
  setup, and calibrates only when needed. (P1)
- Interactive tutorial teaches stick, throttle, primary/secondary fire, weapon
  switching, camera, mission status, and pause. (P1)
- Settings are reachable from the main menu and pause menu; dangerous resets are
  not placed next to resume. (P1)
- Display the current control source and show correct action glyphs. (P1)
- Provide an always-available control reference and a way to reset a broken
  custom layout. (P1)
- Explain controller Bluetooth pairing through system instructions rather than
  inventing an in-game pairing flow. (P1)
- Support offline startup and play for all single-player content. (P1)

## 10. Accessibility

- Scalable text/HUD and large touch-control preset. (P1)
- Left-handed/mirrored layout, adjustable control size/opacity/position. (P1)
- Remappable controller actions, stick/trigger calibration, invert options,
  hold-versus-toggle choices for suitable actions, and haptic intensity/off.
  (P1)
- Do not communicate critical state by color, sound, or haptics alone. (P1)
- Subtitles/captions for critical spoken content where source data allows it;
  distinguish speakers/events where practical. (P1)
- Menu focus order, labels, and controller navigation are consistent. Native
  accessibility semantics are provided for menu/settings UI. (P1)
- Gameplay assists such as aim assist or simplified flight are explicit options
  and separate from faithful parity. (P2/P3)
- Reduced motion/flashing options apply to new post-effects and UI transitions.
  (P2)

## 11. Localization and text

- English, Danish, Norwegian, and Swedish source sets are inventoried. (P1)
- Treat text as Unicode internally even when original packages use legacy
  encodings. Preserve raw IDs and document conversion. (P0/P1)
- Layout must handle expansion, line wrapping, safe areas, larger type, and
  missing glyph detection. (P1)
- Localize new iOS-only UI, touch-control names, controller settings, errors,
  privacy/support text, and tutorial. (P1)
- Do not assume keyboard layout or decimal/date formatting. (P1)

## 12. Networking and platform services

- Single-player must not require network connectivity or an account. (P1)
- Dogfight/multiplayer and network protocol reconstruction are outside the v1.0
  product scope. Shared interfaces may be documented when analysis encounters
  them. (P3/deferred)
- Game Center, achievements, leaderboards, and cloud saves are outside v1.0.
  (P3/deferred)
- No analytics, account, advertising, tracking, microphone, camera, contacts, or
  location permission is added without a concrete product requirement. (P1)

## 13. Security and robustness

- Treat original/converted assets, saves, localization, replays, and network data
  as untrusted; validate all counts, offsets, sizes, paths, and decompression.
  (P0)
- No runtime downloaded executable code or original x86 execution on iOS. (P0)
- No path traversal, writes outside app containers, or format-driven unbounded
  allocation. (P0)
- Fuzz parsers and test malformed packages/saves before any user-selected data
  import is offered, even in a private build. (P1)
- Avoid embedding private source paths, developer credentials, signing secrets,
  or original analysis dumps in builds. (P0)
- Dependency inventory, licenses, security advisories, and update policy are part
  of release readiness. (P1)

## 14. Diagnostics, testing, and support

- Structured diagnostic log with categories, rate limits, privacy review, and a
  user-triggered export path. (P1)
- Crash reports/symbolication only with a documented privacy choice and retention
  policy. (P1)
- Developer overlay for frame timings, memory, asset IDs, active input source,
  controller state, simulation tick, deterministic state hash, drawable and 3D
  render extents, render scale, FPS, draw calls, triangles, lights, and clearly
  labelled GPU-memory data. (P0/P1)
- Automated unit, format, integration, replay, parity, screenshot, lifecycle,
  and clean-install tests. (P0/P1)
- Projection, viewport, aspect-ratio, safe-area, UI-scale, and input-coordinate
  tests cover 4:3, 16:10, 16:9, 19.5:9, 21:9, and 32:9. Major stages capture
  matched 1080p, 1440p, 4K, and ultrawide scenes for the original reference,
  Classic, and Enhanced where available; owner-content images stay outside
  Git. (P1)
- Physical-device matrix includes oldest/newest target phone, representative
  tablet, safe-area variants, at least three controller families, low storage,
  thermal soak, airplane mode, and interrupted audio. (P1)
- Support document covers known limitations, controller setup, save recovery,
  content version, and diagnostic export. (P1)

## 15. Build, signing, and private installation

- Reproducible CMake/native build for the portable core and unsigned Xcode
  build through GitHub Actions on an explicit hosted macOS runner. No local
  Mac/Xcode environment is required for portable development and compile
  validation; signing occurs only on an owner-controlled computer, and local
  Apple tools remain the reference for device debugging and profiling. (P0)
- Separate debug/development, internal test, and release configurations; release
  excludes analysis tools and private fixtures. (P0)
- Automated check that no original executable, Ghidra database, dump, trace, or
  unauthorized asset is packaged. (P0)
- Set `IPHONEOS_DEPLOYMENT_TARGET` to 16.4 and guard newer APIs. Use a current
  suitable Xcode version that still supports that deployment target. (P0)
- Use the available Apple Developer account to sign/provision the owner's iPhone
  17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3. Record device registration,
  certificate/profile, install, renew, and recovery steps without committing
  credentials. (P0)
- Pull requests build without secrets and publish no IPA. Trusted `main` builds
  publish a strictly verified data-less unsigned IPA; Xcode or an accepted
  local sideloader signs during installation. GitHub never receives signing
  credentials, profiles, device IDs, or signed outputs. (P0)
- Verify the iOS 16.4 deployment target and API/dependency availability at build
  time; clearly report that no iOS 16.4 runtime test exists. (P0)
- App Store metadata, review, public support, and public distribution artifacts
  are not produced. (P0)
- Audit the private package to ensure it is not uploaded to a public CI artifact,
  repository, package feed, or sharing service. (P0)

## Cross-cutting acceptance scenarios

| ID | Scenario | Pass condition |
|---|---|---|
| `SCN-IOS-001` | clean install, offline boot | menu and single-player path available without network |
| `SCN-IOS-002` | touch-only mission | mission completes without controller/keyboard |
| `SCN-IOS-003` | controller-only mission | menus and mission complete without screen touch |
| `SCN-IOS-004` | controller disconnect while firing | all actions release, game pauses, touch recovery works |
| `SCN-IOS-005` | background while flying | no stuck input/audio; explicit safe resume |
| `SCN-IOS-006` | background during save | old or new valid save survives; never partial corruption |
| `SCN-IOS-007` | audio route/interruption | audio recovers according to policy without simulation jump |
| `SCN-IOS-008` | memory warning in level | ranked caches unload; active state remains valid |
| `SCN-IOS-009` | sustained thermal run | stable gameplay at an allowed quality/performance tier |
| `SCN-IOS-010` | corrupted content/save | actionable error/recovery, no crash/out-of-bounds read |
| `SCN-IOS-011` | phone/tablet safe areas | no required control or text overlaps system regions |
| `SCN-IOS-012` | upgrade from prior schema | progress/settings/input migrate or recover safely |
| `SCN-IOS-013` | native-resolution and Hor+ matrix | exact 3D target at 100%; projection, UI, safe area, and input pass required aspect ratios |

## Confirmed product decisions

- Private signed sideload only; never an App Store product.
- Minimum deployment target iOS 16.4.
- Runtime devices: iPhone 17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3; Apple
  Developer account available.
- GitHub Actions compiles and validates only a public, data-less unsigned device
  package. Signing is owner-local at installation time: Xcode on a Mac is the
  recommended path, while an independently reviewed local sideloader may be used
  without uploading the package or credentials to a cloud service.
- Version 1.0 excludes House Editor, Paint Room, and multiplayer.
- Original CD image/audio tracks are unavailable, so original music is absent.

## Decisions that need owner input later

- Which reviewed Windows-compatible sideloader will locally sign and install the
  verified unsigned Actions IPA on both registered iPhones, if the Xcode path is
  not used.
- Whether the available legacy videos are retained in v1.0 after codec testing.

None of these blocks executable analysis, `UDSP` work, or the portable engine
foundation.
