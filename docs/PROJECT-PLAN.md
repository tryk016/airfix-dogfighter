# Detailed project plan

**Baseline:** Airfix Dogfighter v1.01  
**Target:** behavior-compatible desktop reconstruction followed by native iOS
ARM64 port  
**Plan date:** 2026-07-21

## Definition of the outcome

The first complete release candidate must:

1. Start on a physical iPhone/iPad without original x86 code.
2. Load legally supplied/converted game data with validation and useful errors.
3. Support the main menu, at least the complete single-player path, flight,
   weapons, collision, AI, mission completion, audio, localization, and saves.
4. Match the reference build closely enough that recorded scenarios stay within
   defined simulation and visual tolerances.
5. Support touch and external controllers, iOS lifecycle, safe areas, storage,
   interruptions, and resume.
6. Render at device-native presentation resolution with a stable performance
   budget.
7. Keep visual modernization optional and independently testable.

House Editor, Paint Room, dogfight multiplayer, and exact playback of legacy
intro videos are parity goals after the single-player path. Their ordering will
be confirmed once module and asset dependencies are known.

## Critical path

```text
immutable baseline
  -> module/import/export map
  -> UDSP directory parser
  -> asset inventory and viewers
  -> reference runtime captures
  -> portable engine bootstrap
  -> one rendered room
  -> one controllable aircraft
  -> combat and collision
  -> one complete mission
  -> menus/audio/save
  -> iOS shell and controls
  -> full content parity
  -> optional modern graphics
```

## Phase 0 — provenance and immutable baseline

**Goal:** make every later claim reproducible and prevent accidental damage to
the originals.

Tasks:

- Record relative paths, sizes, timestamps, SHA-256, magic values, PE metadata,
  and archive signatures.
- Mark `E:\roms\Airfix Dogfighter` read-only by policy; never run analysis tools
  in write mode against it.
- Record edition/version, language packs, patches, disc contents, and whether CD
  audio tracks are available.
- Keep original files and original-derived private fixtures outside Git.
- Establish rights gates for private use, redistribution of engine code,
  redistribution of assets, trademarks, music, and App Store submission.

Exit gate:

- Complete machine-readable manifest can be regenerated and matches the initial
  manifest, except for explicitly classified mutable user/configuration files.

## Phase 1 — reproducible toolchain

**Goal:** install only pinned, auditable tools and prove that the binaries can be
processed consistently.

Tasks:

- Install Ghidra with a supported JDK and Python integration.
- Install an x86-aware debugger, PE inspection tools, C++ compiler, CMake,
  Ninja, and test framework.
- Create a version lock document with download source, version, hash, and license.
- Create one Ghidra project per immutable build; enable versioned symbol/export
  scripts, but ignore the database itself.
- Add scripts for hashes, PE headers, imports, exports, sections, strings, RTTI,
  and headless analysis reports.
- Compile and run a trivial C++20 test program in the Windows harness.

Exit gate:

- A fresh machine can recreate the inventory and headless analysis from written
  instructions; the build/test skeleton passes.

## Phase 2 — controlled reference runtime

**Goal:** obtain trustworthy behavioral observations without treating an old
binary as safe.

Tasks:

- Use an isolated Windows VM or disposable test system with snapshots and no
  unnecessary network access.
- Verify which of `Dogfighter.exe` and `Dogfighter.icd` contains/launches the
  effective entry point and whether any copy-protection dependency remains.
- Record module load order, filesystem reads, registry access, DirectX calls,
  input polling, audio behavior, timers, and thread creation.
- Capture reproducible gameplay scenarios at fixed configuration and input:
  boot, menus, level load, take-off, straight flight, turn, collision, each
  weapon, AI encounter, death, mission success, save/load, editor entry.
- Save frame videos, screenshots, logs, configuration, input scripts, and
  scenario metadata. Store copyrighted/dump material outside Git; commit only
  descriptions, hashes, numeric measurements, and derived tests where permitted.

Exit gate:

- At least ten deterministic or tightly controlled reference scenarios can be
  replayed and measured.

## Phase 3 — executable and module map

**Goal:** turn raw decompiler output into a navigable behavioral model.

Analysis order:

1. `UdsPack.dll` — small and directly relevant to unlocking content.
2. `Dogfighter.exe` / `Dogfighter.icd` — bootstrap and protection boundary.
3. `AfEngine.dll` — core services, scene, render, audio, input, object model.
4. `Cc.dll` — determine ownership and role.
5. `gtDirect3D.dll` — reconstruct renderer interface and fixed-function state.
6. `.mode` modules — high-level game flow.
7. `.type` modules — actors, physics, weapons, effects, and interactions.

For each module:

- Recover exports/imports, entry points, relocations, compiler/RTTI clues, global
  objects, strings, vtables, factories, serialization calls, and module edges.
- Assign stable function IDs using module plus RVA.
- Recover signatures and types conservatively; record confidence separately from
  naming convenience.
- Group functions into systems and write behavioral contracts before production
  implementation.
- Build call-graph and ownership maps for initialization, per-frame update,
  render submission, level loading, and shutdown.

Exit gate:

- All modules have an owner/purpose hypothesis; the bootstrap, update, render,
  asset, and shutdown paths have no unexplained critical edge.

## Phase 4 — `UDSP` and asset formats

**Goal:** list, validate, extract, view, and convert content without relying on
the original engine.

Tasks:

- Infer the `UDSP` header, directory records, names/hashes, offsets, stored and
  unpacked sizes, compression/encryption flags, alignment, and checksums.
- Cross-reference `UdsPack.dll` with the first bytes and repeated patterns of all
  `.up` files.
- Implement bounds-checked `list`, `verify`, and `extract` commands; never write
  into the source folder.
- Create synthetic malformed archives for parser security tests.
- Inventory extracted extensions and cluster unknown formats by magic/entropy.
- Document and implement viewers/converters in dependency order: text/config,
  textures/palettes, meshes/materials, animation, rooms/levels, collision,
  sounds/music, scripts/missions, UI/fonts.
- Prefer lossless intermediate representations and retain original coordinate,
  unit, color-space, alpha, and winding metadata.

Exit gate:

- Every archive entry can be listed and verified; all assets needed for one
  vertical slice can be decoded or converted reproducibly.

## Phase 5 — portable foundation

**Goal:** create a small engine skeleton whose behavior is testable without a
renderer or device.

Tasks:

- Establish C++20/CMake layout, warnings-as-errors policy, sanitizers where
  available, unit tests, formatting, and CI-ready presets.
- Implement math types only after confirming handedness, matrix layout, angle
  units, coordinate scale, and original numeric precision.
- Implement deterministic clock/update steps, random streams, object handles,
  event queues, resource IDs, logging, and serialization primitives.
- Define platform, renderer, audio, input, and filesystem interfaces.
- Create replay input and state-snapshot formats with explicit versions.

Exit gate:

- Headless deterministic tests produce the same state hash across repeated runs
  and supported development compilers.

## Phase 6 — desktop vertical slice

**Goal:** complete one narrow, end-to-end playable scenario before broadening
the rewrite.

Slice contents:

- Bootstrap converted resources.
- Load one room/level and one aircraft.
- Draw geometry, textures, depth, transparency, and faithful basic lighting.
- Support camera, thrust, pitch, bank, one primary weapon, collision, one target,
  HUD, one sound, pause, and clean exit.
- Capture frame/state comparisons against the reference scenario.

Exit gate:

- A five-minute Windows run is playable, leak-checked, repeatable, and satisfies
  documented parity tolerances.

## Phase 7 — gameplay and content parity

**Goal:** expand by original module boundaries while protecting the vertical
slice.

Order:

1. Aircraft variants and flight state.
2. Projectiles and all weapons.
3. Ground/water units, pickups, interactive objects, and effects.
4. AI perception, navigation, targeting, and difficulty.
5. Mission rules, triggers, objectives, failure/success, and campaign flow.
6. Dogfight mode and networking feasibility.
7. House Editor and Paint Room data flows.

For each subsystem: evidence note -> contract -> headless tests -> implementation
-> reference scenario -> parity sign-off.

Exit gate:

- The feature matrix has no P0/P1 gaps and all shipped levels complete without
  progression blockers.

## Phase 8 — presentation, persistence, and localization

**Goal:** recover the complete user-facing loop.

Tasks:

- Menus, HUD, mission briefing/status, settings, pause, errors, and credits.
- Saves, rosters, screenshots/stickers, settings migration, and corruption
  handling.
- Sound effects, voice/localization selection, music strategy, and video
  replacement/transcoding where rights permit.
- English first, followed by Dansk, Norsk, and Svenska with layout tests.
- Accessibility pass for remapping, readable scaling, subtitles where source
  material permits, contrast, motion, and touch target sizes.

Exit gate:

- New game through save/resume and mission completion works in every supported
  localization.

## Phase 9 — native iOS productization

**Goal:** ship the proven core through an iOS-native shell.

Tasks:

- Add Xcode project generation/build on macOS and ARM64 device tests.
- Integrate SDL3 xcframework or the result of its technical spike.
- Implement Metal surface/backend, shader compilation, resource residency, and
  device-loss/lifecycle handling.
- Design touch controls rather than merely overlaying the keyboard: configurable
  virtual stick, throttle, weapons, camera, pause, haptics, and handedness.
- Support Game Controller devices, safe areas, rotation policy, interruptions,
  background/foreground, audio sessions, thermal state, memory warnings, and
  sandboxed save locations.
- Implement semantic actions, input contexts, remapping, deterministic
  `InputFrame` capture/replay, source arbitration, controller hot-plug, synthetic
  release on cancellation, layout profiles, calibration, and touch/controller
  haptic routing according to `docs/systems/INPUT.md`.
- Complete the cross-cutting product checklist in
  `docs/design/IOS-PORT-REQUIREMENTS.md`, including atomic saves, asset package
  compatibility, audio-route handling, accessibility, diagnostics, and release
  packaging.
- Measure cold start, level load, peak memory, sustained frame time, battery, and
  heat on the oldest and newest target devices.

Exit gate:

- A physical device completes the selected single-player path with no lifecycle,
  memory, input, save, audio, or safe-area blockers and meets the agreed
  sustained performance budget. The selected campaign path is completable once
  with touch only and once with a physical controller only.

## Phase 10 — visual modernization

**Goal:** enhance presentation without losing the reference mode.

Order:

1. Native presentation resolution, correct aspect ratios, UI scaling, mipmaps,
   filtering, gamma/color-space correctness.
2. Higher precision transforms, depth handling, particles, and transparency.
3. Dynamic lights and improved light attenuation.
4. Shadow maps/contact shadows with quality tiers.
5. Physically informed materials only where source textures can support them.
6. Ambient occlusion, bloom, tone mapping, anti-aliasing, and optional MetalFX.
7. Higher-resolution asset replacements only with separately cleared content.

Each feature has `off/reference/enhanced` modes where meaningful, screenshot
baselines, GPU timing, memory cost, and a fallback tier.

Exit gate:

- Reference mode remains within parity tolerances; enhanced mode meets visual
  and sustained device budgets.

## Phase 11 — verification and release readiness

**Goal:** prove completeness, ownership readiness, and reproducibility.

Tasks:

- Full campaign soak tests, save compatibility/migration, malformed asset tests,
  localization sweeps, controller matrix, lifecycle torture tests, and device
  performance matrix.
- Rebuild from a clean checkout plus separately supplied legal asset input.
- Produce dependency licenses, attribution, privacy declarations, support and
  crash-diagnostic plan.
- Obtain written rights clearance for every distributed asset, trademark, video,
  voice, and music item; be ready to provide authorization during review.

Exit gate:

- Release checklist is fully evidenced; a clean device build passes acceptance;
  distribution rights are documented.

## Verification strategy

| Layer | Main method | Example evidence |
|---|---|---|
| Binary understanding | Static cross-references plus runtime observation | function ID and trace |
| File formats | Golden listing, bounds/fuzz tests, optional round trip | manifest and parser test |
| Math/simulation | Deterministic scenario/state hashes | position and velocity curves |
| Rendering | Key-frame and perceptual image comparison | reference/enhanced screenshots |
| Audio | Event/timing comparison, not waveform identity by default | event log |
| Game flow | Scripted end-to-end scenario | mission transition log |
| iOS | Physical-device lifecycle/performance suite | metric capture |

No subsystem is called “done” merely because it compiles. It is done when its
contract, evidence, implementation, tests, parity result, and documentation agree.

## Principal risks and mitigations

| Risk | Impact | Mitigation / early test |
|---|---|---|
| Missing CD audio or disc-only content | Incomplete release | Inventory the original disc/image in Phase 0 |
| Copy protection obscures effective executable | Delays dynamic work | Separate launcher from engine modules; prioritize unprotected DLL interfaces |
| Unknown `UDSP` compression | Blocks assets | Analyze small `UdsPack.dll` first; compare all five archives |
| Compiler optimizations erase types | Slower recovery | RTTI/vtable/string clustering plus runtime object traces |
| Physics changes across CPU/compiler | Gameplay drift | Preserve float precision/update order; deterministic curves and tolerances |
| Transparent/lightmapped legacy rendering | Visual mismatch | Record render states and create a faithful fixed-function shader path first |
| Windows-only development environment | Delays iOS proof | Secure Mac/device by the end of the vertical-slice phase, not at project end |
| Rights cover ownership but not redistribution | Blocks App Store | Treat rights as a release gate; do not publish original assets by default |
| Scope expansion into enhancements | Delays playability | Lock faithful vertical slice before modern rendering work |

## Immediate ordered backlog

1. **Completed:** initialize Git and commit planning baseline (`59828ed`).
2. Classify mutable versus immutable source files and regenerate the manifest.
3. Confirm availability of the original CD/disc image and audio tracks.
4. Install pinned JDK, Ghidra, Python, compiler, CMake, and Ninja.
5. Generate import/export/section/string reports for all 16 PE modules.
6. Determine launcher/ICD relationship without executing on the host.
7. Create Ghidra projects and stable module/RVA symbol naming.
8. Fully analyze exports and callers in `UdsPack.dll`.
9. Write a read-only `UDSP list` prototype.
10. Inventory entry names/types in `Resource.up` and `English.up`.
11. Establish the isolated reference runtime and record boot/module-load traces.
12. Define the first deterministic flight scenario and its measurements.
13. Create the C++20/CMake test skeleton.
14. Build a viewer for the first decoded model/room format.
15. Re-estimate remaining work after archive inventory and first vertical-slice
    dependencies are known.
