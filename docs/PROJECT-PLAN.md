# Detailed project plan

**Baseline:** Airfix Dogfighter v1.01  
**Target:** parallel playable Windows x64 reconstruction and private native iOS
ARM64 sideload over one portable C++20 core
**Plan dates:** 2026-07-21; Windows x64 and native-resolution rendering
revisions 2026-07-29

## Definition of the outcome

The first complete release candidate must:

1. Start as a native Windows x64 application and on both accepted iPhones
   without original x86 code.
2. Load legally supplied/converted game data with validation and useful errors.
3. Support the main menu, at least the complete single-player path, flight,
   weapons, collision, AI, mission completion, audio, localization, and saves.
4. Match the reference build closely enough that recorded scenarios stay within
   defined simulation and visual tolerances.
5. Support Windows keyboard/mouse/controllers and iOS touch/controllers through
   the same semantic input model.
6. Own separate Windows and iOS window/lifecycle, renderer, physical-input,
   audio, and filesystem adapters over the shared game.
7. Support iOS lifecycle, safe areas, storage, interruptions, and resume.
8. Render 3D directly at the selected physical resolution when render scale is
   100%, with Hor+ widescreen, an optional Original 4:3 comparison mode, and
   UI resolution independent of the scene render target.
9. Provide separately selectable `Classic` and `Enhanced` visual profiles plus
   scalable quality settings without changing deterministic gameplay or
   physics.

Dogfight/multiplayer, House Editor, Paint Room, App Store distribution, and
original CD music are excluded from version 1.0. Exact legacy intro playback is
P2. See `docs/design/V1-SCOPE.md`, ADR-0003, and ADR-0007.

## Critical path

```text
immutable baseline
  -> module/import/export map
  -> UDSP directory parser
  -> asset inventory and viewers
  -> reference runtime captures
  -> portable engine bootstrap
       +-> one rendered room
       |    -> one controllable aircraft
       |    -> combat and collision
       |    -> one complete mission
       |    -> menus/audio/save
       |    -> playable Windows x64 product shell
       |    -> iOS product shell and controls
       |    -> full content parity
       `-> native-resolution foundation
            -> Hor+ + scalable UI
            -> lighting/materials/shadows
            -> HDR/atmosphere/post-processing
  -> cross-product release readiness
```

The native-resolution foundation, projection tests, and renderer API are
cross-cutting work and may advance alongside reconstruction. Costly visual
effects follow the playable parity slice. ADR-0013 is the authoritative
rendering contract.

## Phase 0 — provenance and immutable baseline

**Goal:** make every later claim reproducible and prevent accidental damage to
the originals.

Tasks:

- Record relative paths, sizes, timestamps, SHA-256, magic values, PE metadata,
  and archive signatures.
- Mark the owner-provided original game directory read-only by policy; never run
  analysis tools in write mode against it.
- Record edition/version, language packs, patches, disc contents, and whether CD
  audio tracks are available.
- Keep original files and original-derived private fixtures outside Git.
- Record that original and converted content remains private for both products
  and prevent its redistribution in applications, installers, or packages.

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
- Connect the public data-less GitHub repository and add a portable validation
  workflow; keep signing and proprietary content outside public jobs/artifacts.
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
  Windows, imported/validated by the data-less Windows and iOS applications,
  and never uploaded to GitHub Actions.

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

## Phase 6 — playable Windows x64 vertical slice

**Goal:** complete one narrow, end-to-end playable scenario in the native
Windows x64 product before broadening the reconstruction.

Slice contents:

- Bootstrap converted resources.
- Implement the SDL3 x64 window/events and keyboard/mouse/controller adapters,
  the D3D11/DXGI faithful renderer with HLSL, the XAudio2 2.9 backend, and a
  separate owner-local content adapter behind the portable interfaces.
- Load one room/level and one aircraft.
- Draw geometry, textures, depth, transparency, and faithful basic lighting.
- Support camera, thrust, pitch, bank, one primary weapon, collision, one target,
  HUD, one sound, pause, and clean exit.
- Capture frame/state comparisons against the reference scenario.

Exit gate:

- A five-minute Windows run is playable, leak-checked, repeatable, and satisfies
  documented parity tolerances. The product is a native x64 build and exercises
  no Windows-specific gameplay or physics implementation.

Implementation checkpoint (2026-07-31): Windows and iOS now own the same
bounded `LegacyGameplayCameraMissionRuntime` contract. Each backend keeps one
camera-packet lease for an entire gameplay draw pass, and each product shell
holds only a replacement-safe weak producer endpoint. Windows does not advance
that endpoint from its 60 Hz semantic-input loop. A moving camera remains
blocked with live flight on the separately recovered 12 ms AirCraft producer,
x87 runtime evidence, and accepted numeric policy; no substitute platform
movement law is introduced by this checkpoint.

Windows private-content checkpoint (2026-07-31): the product now owns a
one-shot `--import-afpack` adapter over the shared authenticated installer. It
resolves an undisclosed SDL-private root, generates the transaction UUID,
serializes same-session product importers, redacts path-bearing errors, and
supports later validation or mission launch without repeating the root path.
The native picker, progress surface, and rollback-choice UI remain staged over
the same transaction; they must not create a second package parser or store.

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

**Goal:** ship the same core proven by the Windows product through an
iOS-native shell.

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
- Use local Xcode, LLDB, Metal tools, and Instruments when interactive
  physical-device debugging and profiling begin.
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

## Phase 10 — native-resolution and modern rendering

**Goal:** deliver a genuinely modern native renderer while preserving the
game's model-kit character and a high-resolution faithful comparison profile.
ADR-0013 defines the binding architecture and acceptance criteria.

This phase is split into ordered, independently shippable stages:

1. **Resolution foundation:** separate reference camera coordinates, UI design
   space, output/backbuffer pixels, 3D render-target pixels, and render scale.
   At 100%, the 3D scene renders at the exact output extent. Add resize,
   50-200% render scale, telemetry, and sharp output-resolution UI.
2. **Projection and layout:** make Hor+ the standard widescreen behavior while
   preserving reference vertical FOV. Add Original 4:3 comparison, safe FOV
   adjustment, safe-area layout, HUD/reticle/weapon/effect projection, and
   correct input transforms.
3. **Sampling and color:** linear rendering, explicit sRGB handling, adequate
   depth precision, mipmaps, filtering, anisotropy, MSAA, and appropriate
   final-image anti-aliasing.
4. **Lighting and materials:** directional, point, and spot lights; adjustable
   shadows; physically informed materials suited to painted plastic and
   domestic locations; optional normal maps when supported by suitable data.
5. **HDR and atmosphere:** internal HDR, tone mapping, exposure, controlled
   bloom, fog, atmosphere, and performance-qualified ambient occlusion.
6. **Effects:** scalable particles, smoke, fire, explosions, projectile trails,
   and individually switchable post-processing.
7. **Performance closure:** `Low`, `Medium`, `High`, and `Ultra` defaults;
   separate shadow/MSAA/effects/render-scale controls; dynamic render scale
   where useful; device profiling and memory budgets.

Implementation checkpoint (2026-07-31): stages 1-2 have their portable
foundation. Strongly typed resolution domains, exact 100% target identity,
Hor+, Original 4:3 layout math, safe-area/UI fitting, input transforms, and the
required aspect-ratio tests are implemented. The shared `0..25` degree
vertical-FOV increase is now a durable setting on Windows and iOS; it expands
only the logical projection and composes with recovered camera FOV. A separate
75-150% native UI-content scale now persists in AFRS schema 3, migrates schema
1/2 to exact 100%, and changes neither the root safe-area transform nor scene
targets/projection.
The recovered world-to-reference-screen projection now composes with that
layout through one allocation-free native-output bridge. It validates the
camera canvas and reference FOV, maps through the real output-pixel scene
viewport independently of render scale, and preserves off-screen and
out-of-depth states for later HUD policy. Actual reticle, weapon, HUD, and
screen-effect consumers remain open; no art or gameplay rule is inferred by
the bridge.
D3D11 and Metal render directly
to the native output at 100% and use backend-private offscreen color/depth
targets plus linear presentation at non-100% scales. Windows direct backbuffer
captures verify 1080p, 1440p, 4K, and 32:9, and a controlled 50%/200% trial
verifies that output and 3D raster extents vary independently. Remaining stage
1-2 work is actual HUD/effect adoption of the shared projection and UI metrics
plus iOS runtime/device acceptance. Both platform shells now expose the shared settings
transaction through explicit pause boundaries: iOS uses a safe-area
touch/controller panel, while Windows uses a DPI-aware DirectWrite/Direct2D
raster composed by D3D11 and controlled by keyboard, mouse, or gamepad. The
shared telemetry contract and output-resolution developer overlay are
implemented: both backends publish the same resolution, timing, workload,
light, and labelled GPU-memory fields; D3D11 uses non-blocking timestamp
queries and Metal uses completed-command-buffer timing. This checkpoint does
not advance the lighting/material/post-processing stages.

ADR-0014 now supplies the finished cross-backend runtime settings foundation:
one validated portable snapshot for render scale, Hor+/Original 4:3,
safe FOV, diagnostics, and the Classic/Enhanced selector; sparse launch/UI overrides;
deterministic delta classification; a versioned storage-neutral record; and a
portable prepare/final-validate/commit transaction with immutable target
ownership and deterministic resize retry. D3D11 prepares a complete replacement
target bundle and permits a durable-save gate before publication. Metal records
the exact view/device/extent/generation, charges each complete color/depth pair
to the shared GPU ledger, and retains one immutable frame lease through command-
buffer completion. Both retain the previous complete snapshot after validation,
surface, allocation, or resize failure; exactly 100% remains their direct native
output path. A canonical checksummed AFRS store now supplies bounded exact
reads, current/backup/default recovery, future-schema preservation, and durable
atomic replacement. Windows binds that store to SDL's private preference
directory and resolves defaults -> persistent snapshot -> sparse session-only
launch overrides; CI smoke/capture modes never open the real profile. iOS now
binds the same store semantics to its private Application Support directory:
Metal resources prepare off-main, the persistent base saves on a serial queue,
and a fresh main-thread surface/revision check precedes publication. A stale
post-save surface reprepares without saving twice. Both product settings
surfaces consume that transaction through the same portable applied/draft
model and explicit pause-menu boundaries. Windows additionally keeps launch
overrides session-only, persists only the unmasked base, and exposes a
data-less settings-panel capture for deterministic UI validation. Physical-
device iOS acceptance remains a separate slice.

`Classic` and `Enhanced` are visual-intent profiles independent of the quality
tier. Classic renders faithfully at the chosen high resolution; Enhanced adds
the modern lighting/material/effect path. Neither may change simulation state.
Higher-resolution replacement assets are admitted only when separately cleared
and are never required for native geometry rendering.

Validation at every major stage includes portable projection/viewport/safe-area
tests for 4:3, 16:10, 16:9, 19.5:9, 21:9, and 32:9; Windows and iOS build gates;
and matched 1080p, 1440p, 4K, and ultrawide captures for the original reference,
Classic, and Enhanced where available. Material comparison captures are shown
in the active project work chat. Owner-content captures remain private.

Performance targets:

- iPhone 17 Pro Max: 60 FPS at native resolution or a controlled dynamic render
  scale;
- iPhone SE 3: stable 30 FPS minimum and 60 FPS target with an appropriate
  quality profile; and
- Windows x64: 60 FPS at 1080p/1440p on reasonable contemporary hardware, with
  scalable 4K settings.

Exit gate:

- a 3840x2160 output at 100% render scale demonstrably rasterizes the 3D scene
  into a 3840x2160 target rather than upscaling a historical framebuffer;
- Hor+, Original 4:3, UI/input transforms, profiles, quality controls, and the
  diagnostic overlay meet ADR-0013; and
- Classic remains within parity tolerances while Enhanced meets the stated
  sustained platform budgets.

## Phase 11 — Windows x64 and private iOS release readiness

**Goal:** prove completeness, cross-platform consistency, ownership readiness,
and reproducibility for both products.

Tasks:

- Full campaign soak tests, save compatibility/migration, malformed asset tests,
  localization sweeps, Windows keyboard/mouse/controller and iOS
  touch/controller matrices, lifecycle/focus/device-transition torture tests,
  and platform performance matrices.
- Rebuild both products from a clean checkout plus separately supplied legal
  asset input.
- Compare shared deterministic scenario hashes and tolerances across headless,
  Windows, and iOS execution; investigate every product-specific simulation
  divergence.
- Produce dependency licenses, local diagnostic instructions, provisioning
  notes, Actions workflow/version manifest, a private Windows x64 packaging and
  installation procedure, and a Windows-compatible iOS installation procedure.
- Verify the native Windows x64 architecture, data-less public package, private
  content import, symbols/diagnostics, input devices, audio devices, display
  transitions, and faithful-reference capture path.
- Verify signature, provisioning, entitlements, ARM64 slice, iOS 16.4 minimum,
  IPA checksum, and forbidden-file scan in CI.
- Verify that the signed application and converted assets are not placed in any
  public artifact, repository, package feed, or distribution channel.

Exit gate:

- Both product checklists are fully evidenced: a clean Windows x64 build passes
  campaign and parity acceptance, and a clean signed iOS build installs and
  passes acceptance on both owner devices.

## Verification strategy

| Layer | Main method | Example evidence |
|---|---|---|
| Binary understanding | Static cross-references plus runtime observation | function ID and trace |
| File formats | Golden listing, bounds/fuzz tests, optional round trip | manifest and parser test |
| Math/simulation | Deterministic scenario/state hashes | position and velocity curves |
| Rendering | Key-frame and perceptual image comparison | reference/enhanced screenshots |
| Audio | Event/timing comparison, not waveform identity by default | event log |
| Game flow | Scripted end-to-end scenario | mission transition log |
| Windows x64 | Native product smoke, debugger/parity capture, focus/display/input/audio transitions | state hashes, logs, frame capture |
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
| Cloud build/device loop slows interactive tuning | Delays UI/Metal/debug work | Move interactive profiling and debugging to local Xcode |
| Platform code forks gameplay or physics | Windows/iOS behavior diverges | One portable core, narrow adapters, cross-platform state hashes, and identical replay scenarios |
| Platform APIs leak into the core | Expensive platform lock-in | Keep SDL3, D3D11/DXGI/HLSL, XAudio2, and AVAudioEngine behind ADR-0007/0008/0009/0010 adapters and test API-neutral command boundaries |
| Private artifacts are accidentally shared | Unauthorized redistribution | Ignore originals/converted assets and audit every packaged/staged artifact |
| Unbounded visual feature work | Delays playability | Implement ADR-0013 in gated stages: native resolution/projection/UI first, costly effects after the playable parity slice |

## Immediate ordered backlog

1. **Completed:** immutable inventory, pinned toolchain, module reports, portable
   C++/CMake skeleton, public GitHub boundary, and portable/unsigned-iOS CI.
2. **Completed:** bounded UDSP listing/verification/decompression, AFPACK v1,
   GTI base conversion, CCF materials/meshes/blueprints, FourCC object/world/
   level/briefing/path parsing, and object-to-texture dependency
   resolution.
3. **Completed:** deterministic portable semantic input router with touch and
   controller binding defaults; native adapters remain separate.
4. **Completed:** define and test the CCF coordinate, matrix, UV policy, and
   triangle-winding conversion contract for an API-neutral/Metal vertex path.
5. **Completed:** decode exact GTI mip chains to canonical RGBA8 and define the
   authored-chain/anomaly-fallback Metal upload contract.
6. **Completed:** produce a private first-model diagnostic using resolved mesh,
   material, and texture edges without committing derived asset data.
7. **Completed:** the atomic portable AFPACK import/replacement and startup
   recovery services are linked into the iOS target with native document
   picker, bounded private copy, progress, startup gate, and verified rollback
   presentation.
8. Establish the isolated reference runtime and record the first deterministic
   flight/control/render scenarios; this remains independent of host static
   analysis until the isolated environment is available.
9. **Foundation complete:** the native Windows x64 boundary now has SDL3
   window/events/input, D3D11/DXGI/HLSL rendering, XAudio2 2.9 audio, and a
   data-less product smoke that drives shared reconstructed AirCraft audio
   commands without platform gameplay. Owner-local content, live aircraft
   state, and the first playable vertical slice remain.
10. **In progress:** the bounded blueprint and placed-scene graphs, seam-safe
   draw-model payload, and multi-instance diagnostic have assembled and
   rendered a complete grouped aircraft. Parent-relative local derivation, the
   public synthetic Metal smoke path, placed-record decoder, and flat bounded
   room fog/BSP parser plus polygon/object/mesh/portal resolver are implemented.
   The conservative explicit-CCF-room plan and multi-mesh/multi-instance draw
   assembly are implemented and validate every physical room across all
   scenes. Exact world/CCF/TEXU binding, dense runtime texture IDs, cross-role
   deduplication, and bounded GTI upload metadata are complete. Connect the
   explicit-room payload through the existing data-less Metal shell and
   separately recover the gameplay room-selection rule; the structurally
   different World `ROOM` catalog is not treated as a join. BSP culling remains
   disabled until its traversal semantics are proven.
11. Implement native UIKit touch capture and Apple Game Controller adapters,
    followed by the configurable visual overlay and on-device usability tests.
12. Continue the implemented ADR-0013 foundation without blocking active
    player/gameplay reconstruction: extend the completed native-resolution,
    50-200% scene-scale, telemetry, independent UI-scale, and profile-dependent
    texture-sampling foundation into HUD/effect projection and explicit color
    handling before lighting or post-effects.
