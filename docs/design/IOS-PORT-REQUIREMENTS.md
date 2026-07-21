# iOS port requirements and release checklist

**Status:** proposed product/system requirements

**Scope:** everything the port needs beyond reconstruction of the original game
logic

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

### Single-player release candidate

Complete campaign path, all required controls, controller support, menus,
localization, saves, accessibility baseline, performance tiers, lifecycle and
interruption handling, packaging, diagnostics, and device matrix.

### Full parity and enhancement

Editors, multiplayer if selected, every original presentation feature, modern
lighting, optional higher-resolution content, platform services, and additional
input modes.

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
- Preserve original 4:3 composition in reference mode using a documented crop,
  fit, or expanded-view policy; never stretch geometry/UI. (P1)
- Provide scalable HUD/text, phone/tablet layouts, and readable mission text.
  (P1)
- Replace mouse-hover assumptions with visible touch focus/selection states.
  (P1)
- Support system text input where player/roster names are entered. (P1)
- Handle screenshots/photos/stickers through sandbox-safe APIs. (P2)
- Consider external display and Stage Manager only after core iPad behavior is
  stable. (P3)

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
- Correct aspect ratio, viewport, projection, UI scale, color space, gamma, and
  texture alpha. (P0/P1)
- Frame pacing with stable simulation timing independent of display refresh.
  Support 60 Hz target first; higher refresh is an enhancement only after logic
  remains deterministic. (P1)
- Quality tiers selected from measured GPU, memory, and thermal capabilities;
  settings remain user-overridable within safe bounds. (P1)
- Shader compilation/pipeline preparation must not cause repeated combat stutter.
  (P1)
- Reference and enhanced render modes have separate screenshot baselines. (P1)
- Modern lighting, shadows, anti-aliasing, post-processing, MetalFX, and upgraded
  assets follow the ordered Phase 10 plan. (P3)

## 5. Performance, memory, storage, and power

Budgets are fixed after selecting the oldest target device, but instrumentation
exists before optimization.

- Record CPU/GPU frame time, fixed-tick overruns, input-to-tick latency, peak and
  resident memory, asset load duration, storage usage, and thermal state. (P0)
- Avoid loading all converted resources at startup; use validated streaming and
  ranked caches. (P1)
- Bound archive decompression and transient conversion memory. (P1)
- Define cold-start, menu-ready, level-load, and resume budgets. (P1)
- Provide 60 fps performance mode and a stable lower-power fallback only if
  device measurements justify it. Simulation speed cannot change with frame
  rate. (P1)
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
- Determine whether original CD audio tracks are available and legally usable;
  transcode from a lossless source, never from an arbitrary online copy. (P1)
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
- Development/private builds may bundle locally converted content. Any public
  build includes only content whose distribution rights are documented. (P0)
- Fail startup with an actionable error when package version or required assets
  are missing; do not crash deep in a level loader. (P0)
- Decide whether a public product bundles assets, imports user-owned data, or uses
  another authorized model only after rights and review implications are clear.
  (P1)
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
- Original dogfight networking is separately analyzed for protocol, security,
  determinism, and viability; do not expose an unsafe legacy protocol directly.
  (P2)
- If multiplayer is selected, specify discovery/matchmaking, NAT/relay,
  disconnect/rejoin, host authority, cheating, version compatibility, privacy,
  abuse handling, and service operating cost before implementation. (P2)
- Game Center achievements/leaderboards and cloud saves are optional integrations
  after core gameplay works offline. (P3)
- No analytics, account, advertising, tracking, microphone, camera, contacts, or
  location permission is added without a concrete product requirement. (P1)

## 13. Security and robustness

- Treat original/converted assets, saves, localization, replays, and network data
  as untrusted; validate all counts, offsets, sizes, paths, and decompression.
  (P0)
- No runtime downloaded executable code or original x86 execution on iOS. (P0)
- No path traversal, writes outside app containers, or format-driven unbounded
  allocation. (P0)
- Fuzz parsers and test malformed packages/saves before public data import is
  offered. (P1)
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
  controller state, simulation tick, and deterministic state hash. (P0/P1)
- Automated unit, format, integration, replay, parity, screenshot, lifecycle,
  and clean-install tests. (P0/P1)
- Physical-device matrix includes oldest/newest target phone, representative
  tablet, safe-area variants, at least three controller families, low storage,
  thermal soak, airplane mode, and interrupted audio. (P1)
- Support document covers known limitations, controller setup, save recovery,
  content version, and diagnostic export. (P1)

## 15. Build, signing, release, and rights

- Reproducible CMake/native build for the portable core and Xcode build/signing
  on macOS. (P0)
- Separate debug/development, internal test, and release configurations; release
  excludes analysis tools and private fixtures. (P0)
- Automated check that no original executable, Ghidra database, dump, trace, or
  unauthorized asset is packaged. (P0)
- Define supported iOS/iPadOS range only after device/performance measurements.
  (P1)
- Prepare app metadata, age rating, privacy declarations, support contact,
  dependency attribution, export/compliance answers, and review notes. (P1)
- Written authorization/rights evidence for distributed code, name, artwork,
  video, voice, music, and other game data is a public-release gate. (P0)
- Private sideload/testing and public App Store release are separate milestones
  with different packaging and rights acceptance criteria. (P0)

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

## Decisions that need owner input later

- Private personal build, public App Store product, or both.
- Minimum acceptable phone/tablet generations after the device spike.
- Whether version 1.0 includes House Editor, Paint Room, and multiplayer.
- Whether faithful 4:3 framing or an expanded widescreen camera is the default.
- Whether original CD music and videos are available and cleared for packaging.
- Whether optional Game Center/cloud features matter for the first public release.

None of these blocks executable analysis, `UDSP` work, or the portable engine
foundation.

