# Detailed project plan

**Baseline:** Airfix Dogfighter v1.01  
**Target:** behavior-compatible desktop reconstruction followed by a private,
native iOS ARM64 sideload
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

Dogfight/multiplayer, House Editor, Paint Room, App Store distribution, and
original CD music are excluded from version 1.0. Exact legacy intro playback is
P2. See `docs/design/V1-SCOPE.md` and ADR-0003.

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
- Record that the output is a private signed build and prevent redistribution of
  the application, original assets, or converted packages.

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
- Connect a private GitHub repository and add a portable validation workflow.
- Reserve an explicit GitHub-hosted macOS runner/Xcode selection for iOS jobs;
  PR jobs remain unsigned and secret-free.
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
- Define the versioned private `.afpack` runtime package. It is built locally on
  Windows, transferred separately, and imported/validated by the data-less iOS
  app; it is never uploaded to GitHub Actions.

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
6. Record shared interfaces encountered in dogfight/editor modules without
   implementing those out-of-scope v1.0 features.

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
- Sound effects and voice/localization selection. Music is an optional absent
  content set in v1.0 because the original CD/audio tracks are unavailable.
  Video replacement/transcoding remains P2.
- English first, followed by Dansk, Norsk, and Svenska with layout tests.
- Accessibility pass for remapping, readable scaling, subtitles where source
  material permits, contrast, motion, and touch target sizes.

Exit gate:

- New game through save/resume and mission completion works in every supported
  localization.

## Phase 9 — native iOS productization

**Goal:** ship the proven core through an iOS-native shell.

Tasks:

- Add Xcode project generation and build it on an explicit GitHub-hosted macOS
  runner. Local Xcode/Mac access is not required.
- Set the deployment target to iOS 16.4 and use availability guards for any
  newer API.
- Add unsigned simulator CI for pull requests and a protected, manually triggered
  signed IPA workflow using an ephemeral keychain and GitHub environment secrets.
- Register and use the available Apple Developer account with iPhone 17 Pro Max
  on iOS 26.6 and iPhone SE (3rd generation) on iOS 26.3. Set 16.4 in build
  metadata and availability checks, but do not claim runtime coverage on 16.4.
- Make the app boot without data and implement atomic `.afpack` import before the
  first full device slice.
- Select and document a Windows-compatible installation path for the signed IPA.
- Measure the complete Actions-to-device feedback loop. Request a MacBook only
  for a hard local-Xcode/LLDB/Metal/Instruments gate or a documented >=20%
  reduction in the remaining affected work, following
  `docs/process/MACBOOK-GATE.md`.
- Implement native UIKit/Game Controller adapters first. Add an SDL3 desktop or
  common adapter later only if the cross-platform shell makes it materially
  useful, as staged in ADR-0002.
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
  with touch only and once with a physical controller only. Compact-layout and
  A15 performance scenarios pass on iPhone SE 3/iOS 26.3; large-screen,
  Dynamic-Island, high-refresh, and high-quality scenarios pass on iPhone 17 Pro
  Max/iOS 26.6.

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

## Phase 11 — verification and private sideload readiness

**Goal:** prove completeness, ownership readiness, and reproducibility.

Tasks:

- Full campaign soak tests, save compatibility/migration, malformed asset tests,
  localization sweeps, controller matrix, lifecycle torture tests, and device
  performance matrix.
- Rebuild from a clean checkout plus separately supplied legal asset input.
- Produce dependency licenses, local diagnostic instructions, provisioning
  notes, Actions workflow/version manifest, and a reproducible private Windows
  installation procedure.
- Verify signature, provisioning, entitlements, ARM64 slice, iOS 16.4 minimum,
  IPA checksum, and forbidden-file scan in CI.
- Verify that the signed application and converted assets are not placed in any
  public artifact, repository, package feed, or distribution channel.

Exit gate:

- Private-release checklist is fully evidenced and a clean signed build installs
  and passes acceptance on the owner's iPhone 17 Pro Max.

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
| Original CD music unavailable | Missing soundtrack | Treat music as an optional absent content set; do not source unofficial copies |
| Copy protection obscures effective executable | Delays dynamic work | Separate launcher from engine modules; prioritize unprotected DLL interfaces |
| Unknown `UDSP` compression | Blocks assets | Analyze small `UdsPack.dll` first; compare all five archives |
| Compiler optimizations erase types | Slower recovery | RTTI/vtable/string clustering plus runtime object traces |
| Physics changes across CPU/compiler | Gameplay drift | Preserve float precision/update order; deterministic curves and tolerances |
| Transparent/lightmapped legacy rendering | Visual mismatch | Record render states and create a faithful fixed-function shader path first |
| GitHub runner image changes Xcode | Breaks CI/reproducibility | Explicit runner/Xcode selection, preflight and build manifest |
| Signed IPA cannot be installed from Windows | Blocks device tests | Prove the installation path in the first signed device spike |
| No iOS 16.4 runtime device | Minimum-version behavior can regress | Treat 16.4 as build/availability coverage only and state the limitation |
| Cloud build/device loop slows interactive tuning | Delays UI/Metal/debug work | Measure the loop; invoke MacBook gate only at blocker or >=20% saving |
| Private artifacts are accidentally shared | Unauthorized redistribution | Ignore originals/converted assets and audit every packaged/staged artifact |
| Scope expansion into enhancements | Delays playability | Lock faithful vertical slice before modern rendering work |

## Immediate ordered backlog

1. **Completed:** immutable inventory, pinned toolchain, module reports, portable
   C++/CMake skeleton, public GitHub boundary, and portable/unsigned-iOS CI.
2. **Completed:** bounded UDSP listing/verification/decompression, AFPACK v1,
   GTI base conversion, CCF materials/meshes/blueprints, FourCC object parsing,
   and object-to-texture dependency resolution.
3. **Completed:** deterministic portable semantic input router with touch and
   controller binding defaults; native adapters remain separate.
4. **Completed:** define and test the CCF coordinate, matrix, UV policy, and
   triangle-winding conversion contract for an API-neutral/Metal vertex path.
5. **Completed:** decode exact GTI mip chains to canonical RGBA8 and define the
   authored-chain/anomaly-fallback Metal upload contract.
6. **Completed:** produce a private first-model diagnostic using resolved mesh,
   material, and texture edges without committing derived asset data.
7. Complete AFPACK semantic manifest checks and an atomic iOS
   import/replacement service; the no-music package configuration is tested.
8. Establish the isolated reference runtime and record the first deterministic
   flight/control/render scenarios; this remains independent of host static
   analysis until the isolated environment is available.
9. **In progress:** the bounded blueprint graph, seam-safe draw-model payload,
   and multi-instance diagnostic have assembled and rendered a complete grouped
   aircraft. Derive parent-relative locals for animation, decode placed-room
   nodes, build the first-room renderer, and connect the payload to Metal
   through the existing data-less shell.
10. Implement native UIKit touch capture and Apple Game Controller adapters,
    followed by the configurable visual overlay and on-device usability tests.
