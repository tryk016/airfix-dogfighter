# Project status

**Updated:** 2026-07-30
**Stage:** Phase 1 — static analysis and archive recovery in progress

## Now

- Planning and documentation system established.
- Original installation inventoried without executing binaries.
- Port strategy recorded in ADR-0001.
- Playable Windows x64 and private iOS ARM64 are accepted as parallel products
  over one portable C++20 core. Windows is the primary rapid-debug/parity
  environment and owns separate window, renderer, input, audio, and filesystem
  adapters; ADR-0007 and both product checklists define the boundary.
- ADR-0008 selects SDL3 for the Windows window/events/keyboard/mouse/controllers
  and D3D11/DXGI with HLSL for the Windows renderer. SDL3 does not own game
  rendering; iOS remains on its native Metal backend. ADR-0009 selects inbox
  XAudio2 2.9 for the Windows native audio backend and keeps SDL3 audio
  disabled. ADR-0010 selects AVAudioEngine for iOS over the same portable
  command/PCM contract.
- ADR-0013 makes native-resolution modern rendering an official cross-product
  requirement. At 100% render scale the 3D target must exactly match the output
  extent; standard widescreen is Hor+, Original 4:3 is comparison-only, and UI,
  text, safe areas, and input coordinates remain independent of scene render
  scale. Classic/Enhanced profiles, Low-Ultra tiers, staged lighting/material/
  HDR/effect work, platform budgets, diagnostics, aspect-ratio tests, and
  matched screenshots are specified. The first implementation slice now
  provides strongly typed domains, exact 100% target identity, Hor+, Original
  4:3 comparison math, safe-area/UI fitting, and input transforms. Both native
  backends consume the same 100% direct path and now allocate private
  color/depth scene targets plus a linear native-output presentation pass at
  non-100% scales. Windows exposes validated 50-200% and Original 4:3 command
  switches; Metal exposes the equivalent backend setting boundary. Windows
  direct D3D11 readbacks verify 1080p, 1440p, 4K, and 32:9. A controlled
  960x540 comparison uses 480x270 at 50% and 1920x1080 at 200%; owner-content
  images remain private. The shared renderer now also publishes smoothed FPS,
  CPU/GPU frame time, output and scene extents, render scale, draw calls,
  triangles, lights, and labelled GPU-memory measurements. D3D11 and Metal
  composite the same output-resolution developer panel after scene
  presentation; Metal applies UIKit safe-area offsets. Finished settings UI,
  modern visual stages, and iOS runtime/device acceptance remain pending.
  Hosted iPhoneOS and iPhoneSimulator builds compile the complete Metal path;
  only physical-device visual/timing acceptance remains for this slice.
- ADR-0014 defines one cross-platform render-presentation settings transaction.
  Its portable C++20 foundation is implemented: render scale, Hor+/Original
  4:3, diagnostics, and the Classic/Enhanced selector form one validated
  snapshot; sparse overrides reject atomically; a deterministic delta
  identifies target/layout/overlay/profile work; and a versioned semantic
  record fails closed on future schemas or malformed values. A second portable
  layer now prepares immutable active/candidate states against exact
  view/device/extent/generation stamps, validates stale candidates, owns an
  optional copyable target lease, and supplies bounded 0, 1, 2, 4, ...,
  120-frame retry policy. D3D11 applies the full snapshot as one prepare-before-
  publish transaction. Exact old color, RTV, SRV, depth, and DSV identities
  survive invalid candidates, unavailable surfaces, late allocation failure,
  failed scaled resize, and a rejected pre-publication persistence gate.
  Metal now also replaces its independent setters with one complete snapshot:
  non-100% prepares both private color/depth textures under the shared GPU
  ledger, final validation rejects a changed drawable surface, and one immutable
  lease survives through command-buffer completion. Failed positive resize
  retains the last good pair but does not render it to a mismatched drawable;
  zero extent retains it for restore. Exactly 100% remains a direct native
  target on both backends, diagnostics now defaults off on iOS, and Enhanced
  remains a selector only, not a claim that modern passes exist. The portable
  persistence slice now adds a canonical, checksummed, 4 KiB-bounded AFRS
  record plus strict current/backup/default recovery and atomic durable writes.
  Windows resolves the private `settings/` leaf only through SDL's preference
  API, layers one valid persistent snapshot under sparse launch-only overrides,
  and never touches the real profile during smoke, capture, or validation
  modes. Linked files/directories, oversized/malformed records, partial fields,
  and future schemas fail closed; future bytes are preserved and downgrade
  writes are blocked. iOS now resolves a protected private Application Support
  settings leaf off-main, loads and saves through a serial adapter, prepares
  immutable Metal candidates on a separate worker, and publishes only after a
  fresh main-thread surface/revision validation. A durable candidate that
  becomes stale is prepared again without another save, and first presentation
  waits for startup resolution. The final settings UI remains pending.
- The native Windows x64 product foundation is implemented. A statically
  linked SDL3 shell owns the window and lifecycle events; a separate
  D3D11/DXGI backend compiles HLSL, uploads the same public scene used by Metal,
  preserves shared draw-command order, supports resize, and falls back from
  hardware D3D11 to WARP. Its hidden CTest mode reads the real back buffer and
  rejects a clear-only frame. SDL3 keyboard/mouse/standardized-gamepad events
  now feed the shared 60 Hz semantic input path with ordered tap preservation,
  dead-zone/trigger normalization, hot-plug, disconnect release, and focus
  neutralization. An authenticated mission now enters an explicit paused-ready
  session; pause/menu is the only resume path. Each eligible Windows frame is
  consumed exactly once by the same deterministic player-state boundary as
  iOS, and pause, focus loss, controller disconnect, or a rejected transition
  resets every physical source and never auto-resumes. The diagnostic
  state/hash changes, but the player remains at the authenticated spawn because
  no flight law is inferred from input intentions. One portable planner now
  validates exact pose limits, retained bytes, and bit-identical step-zero
  publication for both native renderers. D3D11 owns that runtime with its
  mission snapshot, retains one coherent lease through each gameplay draw
  pass, and exposes the same replacement-safe weak producer endpoint as Metal.
  A separate XAudio2 2.9 backend consumes the new bounded portable
  command/PCM16 contract, owns copied clips, pauses with focus, recovers
  outside its callback, and accepts commands safely without an output
  endpoint. The shared AirCraft audio
  coordinator now preserves the recovered
  phase-transition/destroyed-dive/modulation order. Windows can authenticate an
  owner-local AFPACK root, decode the six recovered PCM16 samples through the
  same retained content session, register the complete clip set, and derive
  explicit role/voice bindings. An explicit mission request now builds the
  manifest and complete room through that same session, prepares dense D3D11
  geometry and authored/generated texture mip chains, bootstraps the recovered
  camera projection, and renders with reverse depth through the shared
  full-target Hor+ layout. The hidden private validator requires visible GPU
  output, and a separate one-shot mode saves that checked frame as a
  non-overwriting private BMP for local parity work; an optional capture extent
  is accepted only when it matches the physical backbuffer. The public product
  smoke remains synthetic and data-less. The loaded room and camera are still
  static because live flight
  simulation is not connected, so this is not yet a playable build.
- The native iOS audio adapter consumes the same monotonic command batches.
  It converts bounded PCM16 registrations once into AVAudioEngine's standard
  deinterleaved Float32 representation, uses one player/varispeed graph per
  voice, retains only looping state across graph recovery, and rejects stale
  completion callbacks through voice generations. Ambient-session activation
  occurs only after deliberate gameplay resume. Pause/background paths
  deactivate it; interruption, route loss, and media-service reset force
  gameplay pause and never auto-resume. Mission preparation now carries the six
  authenticated owner-local samples through the opaque snapshot, registers all
  of them in a replacement backend, and commits renderer, backend, bindings,
  and ticket atomically. Live aircraft scheduling and physical-device audible/
  route acceptance remain pending.
- Cross-platform input/control/haptics system specified; semantic input
  architecture recorded in ADR-0002.
- Local Git repository initialized on branch `main`; planning baseline committed
  as `59828ed`.
- GitHub remote `tryk016/airfix-dogfighter` connected and the planning baseline
  published.
- Phase 1 toolchain installed and pinned: Ghidra 12.1.2, Temurin JDK 21.0.11,
  Python 3.14.6, CMake 4.4.0, Ninja 1.13.2, LLVM 22.1.8, and GCC 15.2.0.
- Added a local-only reverse-engineering workbench around canonical Ghidra
  headless reports, scriptable Rizin/rzpipe reports, Cutter inspection, and a
  prepared but not yet executed x32dbg dynamic-analysis stage. Verified
  archives, databases, traces, working copies, and original-derived reports
  remain outside Git.
- Established a dedicated, isolated `Airfix-Dev` WSL2 distribution. Automount
  and Windows interop are disabled; a native Linux CMake/Ninja build completed
  135 targets and passed all 39 tests without modifying other WSL
  distributions.
- Cross-checked one rendering, one input, and one aircraft-flight function.
  Ghidra produced the most reliable ABI and pseudocode; Rizin independently
  matched exact boundaries and useful call/data sites, but misclassified
  calling conventions and needed an explicit, recorded function creation for
  the large flight routine.
- Initial LLVM report for `UdsPack.dll` found 44 named C++ exports covering
  `UpPackage`, `UpFile`, `UpFileInfo`, and `UpHashTable`.
- Recovered the complete header layout, both 24-byte record layouts, legacy
  signed-byte name hash, and compression opcodes for FMT-UDSP.
- Implemented portable C++20 metadata parser, listing/verifier CLI, decompressor,
  and malformed-input tests.
- All five supplied archives and all 1,911 compressed streams pass the new
  parser; original data remains external and read-only.
- Added three-platform portable CI including native ARM64 `macos-26`.
- Generated reproducible LLVM sections/imports/exports reports for all 16 PE
  modules and confirmed the plugin factory ABI names.
- Changed normal UDSP file opening to read only the header and metadata tail;
  `Resource.up` metadata buffering fell from 170,642,453 to 130,961 bytes.
- Added the stable-source UDSP foundation. `Archive::open(path)` now uses one
  handle, while `open(stream, sourceLabel)` and indexed stream entry reads keep
  the caller's binary seekable handle and treat the label as diagnostics only.
  Exact current size, archive/payload regions, index and output limits, and
  `streamoff`/`streamsize` representability are checked before the relevant
  allocation or I/O. Stream position is unspecified and shared access must be
  serialized; legacy `path + FileEntry` reads remain convenience APIs without
  backing-object provenance.
- Synthetic UDSP tests cover standalone and embedded prefix/archive/suffix
  layouts, compressed and uncompressed reads, zero-byte prefixes, poisoned
  labels, bad indices, exact/one-under limits, size mismatches, suffix
  containment, and sparse pre-allocation stream-limit rejection. This is not
  yet an end-to-end installed-AFPACK runtime asset path.
- Draft PR #1 is published; portable CI passes on Ubuntu, Windows, and ARM64
  macOS 26, including the post-fix AFPACK streaming tests on Windows.
- Added a data-less UIKit/Metal iPhone shell generated through CMake, a portable
  lifecycle/content state machine, and a public-source boundary scanner.
- Added a bounded public-data Metal adapter that consumes
  `DrawSubmissionPlan`, owns buffers per mesh, and renders two reusable meshes
  through three ordered instances (`1 -> 0 -> 1`). Commands select transforms
  by `instanceIndex`; valid texture ID zero and the explicit one-pixel
  missing/coordinate-free fallback remain distinct. Private content is not
  bundled.
- Unsigned ARM64 `iphoneos` and `iphonesimulator` builds pass on Xcode 26.6 with
  verified minimum iOS 16.4 and no private files in either bundle.
- Recovered package-chain startup and the type/mode/graphics plugin discovery,
  version gates, metadata, factory calls, and unloading behavior.
- Classified `Dogfighter.exe` as the usable v1.01 bootstrap reference and the
  near-maximal-entropy `.icd` as an older protected/compressed artifact.
- Specified AFPACK v1 and implemented its bounded parser, deterministic local
  writer, streaming SHA-256 verifier, normalized-path checks, and atomic partial
  output behavior.
- Added explicit archive/metadata/entry/path/string-table/payload ceilings before
  allocation or reads. Bounded file-backed entry reads verify SHA-256 and reject
  both changed file size and same-size payload replacement.
- Implemented strict bounded JSON and semantic validation for AFPACK manifest
  v1, including exact game/source/capability compatibility, locale allowlisting,
  and a one-to-one audit against the container entry table.
- Added parser limits for nested UDSP archive/metadata/tables/names and declared
  stored, per-entry unpacked, and aggregate unpacked sizes. Checks precede
  metadata allocation and the real Resource archive passes the default policy.
- Added one-handle AFPACK validation across metadata, payload, manifest, and
  nested UDSP regions, followed by a second authentication pass that rejects
  same-size mutation during semantic validation.
- Defined and implemented canonical AFAC v1 activation records. Active and
  rollback package names are derived only from SHA-256, with bounded sizes,
  exact layouts, strict generation handling, and no stored input paths.
- Added cross-platform durable file primitives for exclusive creation, atomic
  replacement, and no-replace publication, including source-identity and path
  alias checks for the private installer transaction boundary.
- Composed those primitives into a bounded, cancellable private AFPACK
  installer with content-addressed publication, validated collision reuse,
  idempotent AFAC rotation, and an explicit recovery result for ambiguous
  post-rename durability. The portable implementation is linked into iOS.
- Implemented bounded startup content inspection and verified rollback. It
  authenticates only AFAC-derived current/previous packs, distinguishes
  malformed, unusable, and transient-unavailable states, rechecks stale active
  state on rollback, and requires exact durable readback after commit.
- Startup inspection now retains the authenticated pack handle in a move-only
  `ActiveContentLease` together with AFPACK, manifest, and exact
  `source/Resource.up` metadata. `VerifiedContentSession` adopts this proof
  without a pathname reopen, redundant full hash, or repeated metadata parse.
- Added the native iOS private-content coordinator: document picker, bounded
  security-scoped copy into app-private storage, serialized install/inspection/
  rollback, lifecycle cancellation, progress and recovery UI, strict orphan
  temporary cleanup, and `AppSession` readiness gating.
- The native coordinator now consumes the exact ready `ActiveContentLease` into
  a `VerifiedContentSession` on its serialized content worker. Authenticated
  handles never move to the main or render threads, and install/rollback closes
  every reader before entering the serialized writer transaction.
- Added bounded UDSP-in-container reads and completed a real English content
  pack round trip (192,755,765 bytes, 3 entries) outside Git; the temporary pack
  was removed after verification.
- Inventoried the selected Resource/English payload set and validated portable
  GTI metadata parsing for 1,985 files/3,990 image variants plus CCF framing for
  all 286 scene signatures.
- Recovered GTI pixel-size/alpha/palette semantics and CCF's inclusive
  six-byte nested chunks plus the four stable top-level scene sections.
- Indexed every direct child in all 286 CCF scenes; material/room IDs are
  stable, and the independent blueprint/placed-node tables have equal physical
  counts in every file but no assumed index mapping.
- Implemented faithful base-level RGBA8 conversion for every observed GTI pixel
  format and decoded all 3,990 variants; a private aircraft texture diagnostic
  confirms channel/orientation correctness and the paletted/32-bit paths agree
  closely.
- Implemented bounded `AfChunkContainer` framing and object-definition parsing;
  all 299 selected containers/7,376 chunks and all 215 object definitions pass.
- Implemented bounded HOUS, complete observed FHOU placement/state, BRIF, and
  PATH semantics. Read-only validation covers 29 worlds/493 rooms/47,589 radar
  line records; 31 levels with 2,444 static placements, 1,176 model placements,
  and 172 runtime-state records/643 words; 23 briefings; and 833 path deltas.
- Added a metadata-only level dependency resolver for the world and every
  static/model object definition. It preserves source order/data, reports
  missing/invalid/not-found/unique/ambiguous paths, applies aggregate limits,
  and never reads payloads or guesses extensions. All 31 world, 2,444 static,
  and 1,176 model references resolve uniquely in the selected corpus.
- Implemented strict CCF material metadata parsing for all 10,385 records,
  exposing material names/references and 10,256 primary plus 263 environment
  texture dependencies without assigning unproven numeric semantics.
- Audited the CC0 `RonnyReverse/cc-tools` project at a pinned commit; its CCF
  mesh map is useful independent evidence, while the local bounded parser and
  corpus/Ghidra validation remain authoritative.
- Implemented bounded CCF mesh geometry parsing for all 6,995 records,
  validating 136,363 vertices and 170,039 indexed triangles plus UV, paint,
  transform, and control metadata without exporting proprietary geometry.
- Added bounded UDSP logical-path lookup with legacy hash/case behavior, full
  collision comparison, traversal rejection, and explicit ambiguous results as
  the first dependency-resolver primitive.
- Indexed all 9,328 CCF blueprints as 2,062 roots and 7,266 ordered edges with
  maximum depth 7 and zero missing parents, duplicate references, or cycles.
  Resolved all 215 object `CCFF`/`MESH` selectors through 90 mesh roots, 124 null
  roots, and one no-selector case.
- Implemented the portable deterministic input core: stable action IDs, Q15
  axes, bounded sequence ordering, multi-source arbitration, context releases,
  cancellation, lifecycle neutral gating, and default touch/controller/test
  bindings. Native iOS now transports the full baseline gameplay-action surface:
  a safe-area-aware multitouch flight/throttle/combat/camera/weapon overlay and
  a bounded Apple extended-controller adapter feed a validated portable batch
  reconciler and renderer-independent fixed 60 Hz pump. Context transitions,
  controller generations, FIFO overflow, lifecycle and gameplay boundaries
  reset fail closed. Remapping, profiles, menu UI, glyphs, haptics, and device
  acceptance remain pending.
- Expanded the deterministic simulation consumer. Portable
  `PlayerAircraftState` preserves uninterpreted flight/throttle/camera Q15
  intentions, primary/secondary/rear-view held state and exact edges, discrete
  weapon selection, action counters, its own completed-step count, monotonic
  source tick, and an explicit canonical hash. Invalid schema, Q15, weapon,
  tick, and counter transitions are rejected without partial mutation. A
  shared presentation coordinator commits state only with a successful or
  temporarily busy actor-pose publication; an expired endpoint or other
  publication failure is terminal and atomic. Both native products use the
  replacement-safe endpoint when an authenticated player runtime exists; a
  mission without a player remains on the explicit headless path. Both products
  advance exactly once per eligible running frame and fail closed. The
  currently published pose is still the authenticated spawn: movement,
  throttle integration, camera behavior, and weapon spawning remain
  intentionally absent.
- Recovered the 63-slot `AirCraft.type` vtable and 16 function entries missed
  by automatic analysis. The per-step method at `0x10003F40` consumes a float
  time delta and contains 15 force, five torque-only, and one force-only call
  sites. The static-collision method at `0x10007920` consumes collided polygons
  and resolves static/BSP contact lists.
- Recovered the complete player/AI control-event join. Player commands and
  analog input emit signed `PITCH_SET`, `BANK_SET`, and
  `THRUST_SET/APPLY` payloads into four `AfVehicle` fields read directly by
  the flight-force step. Discrete keyboard commands also emit
  `TURN_SET (-32/0/+32)` into a fifth sleep-gate control whose AirCraft
  force-law consumer remains unknown. The similarly named
  `THROTTLE_SET/APPLY` events are
  confirmed no-ops for the AirCraft inheritance path. A static Ghidra/Rizin
  cross-check now also fixes the exact `PITCH_SET`/`BANK_SET` branches:
  signed `int32` payloads are multiplied in x87 by the binary32 constant at
  bits `0x3D3020C5`, then overwrite vehicle `+0x448`/`+0x44C`.
  Active nonzero products clear the shared signed rest duration; active zero
  writes positive zero without clearing it; inactive events do not mutate
  either field. Keyboard, successfully range-configured DirectInput, and AI
  producers are separately bounded. Exact full-domain PC53/RN vectors are a
  startup-compatible conditional model, not a branch-time observation:
  process-wide live x87 precision and rounding control, cross-producer
  ordering, and the nominal-12-ms sample phase remain explicitly unproven. The
  isolated turn/pitch/bank typed-write reducers are implemented over the full
  signed native payload domain. They return exact binary32 bits under an
  explicitly named startup-compatible PC53/nearest-even policy using shared
  host-FP-independent integer rounding. A simulation-thread-confined owner
  commits those writes and the thrust writes with their shared rest clear as
  one transaction and exposes the five controls in native sleep-gate order.
  The discrete command callback is now also represented by a pure eight-flag
  reducer: each already-ordered bool invocation updates exactly one flag and
  emits one typed `TURN_SET`, `PITCH_SET`, `BANK_SET`, or `THRUST_APPLY`
  input, including repeats, opposite-held releases, and zero releases. It is
  intentionally separate from the Q15 `InputFrame`, which cannot retain
  opposing-command order or in-tick taps. One allocation-free step now
  composes exactly one such caller-ordered invocation through the matching
  decoder and transactional field owner. Its staged result retains the earlier
  callback flag update when an inactive event is ignored or an active
  reconstruction policy is rejected, while control-event state changes only
  after a typed write commits. It owns no batch, replay format, producer,
  source order, Q15 conversion, or clock.
  Live producer, scheduler, force-law, and player-state wiring remains
  intentionally absent.
- Recovered the scheduler-visible aircraft order:
  `EulerODE -> ResetForceAndTorque -> CalcAuxiliary -> slot45 force
  accumulation -> collision/slot30 -> slot44 refresh`. `EulerODE` consumes
  the prior accumulated forces; slot45 prepares the next integration step.
  AirCraft uses a 12 ms dependant interval converted to `0.012f` seconds.
  All 21 force/torque sites, direct state transitions, helper transforms, and
  constructor-field joins in slot45 are now statically mapped. The surrounding
  signed 64-bit rest timer, exact 2000 ms sleep gate, ground/water speed
  thresholds, exported `IsOnGround` byte, and threshold transition are also
  recovered. Slot 44's engine-start transition now proves the `+0x564`
  smoothing branch, while slot 30 and slot 44 prove the full `+0x568`
  collision-degraded thrust-integrity lifecycle and its next-step timing.
  Slot 44's counter now also proves the five-call engine-audio cadence, exact
  start/running/stop command order, five sound roles, and the speed/thrust/
  orientation pitch-volume equations. Isolated allocation-free C++20 helpers
  specify sleep; already-formed native `THRUST_SET/APPLY` typed writes and
  nonzero rest-clear directives; the ordered health-gated target/apply/clamp followed
  by unconditional smoothing in an executed slot-45 step; collision
  degradation; recovery/clamp; and the bounded engine-only command stream
  without pretending the frozen player simulation owns scheduling, event
  sampling, rigid-body integration, or audio playback. Physical units,
  Q15-to-native event timing, runtime contact/audio traces, deterministic PRNG
  ownership, full x87 numeric tolerance, mixer integration, and dynamic
  actor-to-render publication remain unknown.
- Recovered the complete player spawn/type/primary-actor event chain and the
  fixed 16-entry mission start table. The selector uses requested index modulo
  count, with the primary receiving CCF room as the empty-table fallback.
- Added a bounded, non-executing parser for the exact AFS `AddStartPos` call and
  an atomic exact-name resolver to physical `CcfMetadata::rooms`. A private
  aggregate census resolves all 20 campaign setup starts uniquely with zero
  parse, missing, ambiguous, or capacity issues; no original script, room name,
  or path is published. This legacy helper deliberately remains a
  selected-main-CCF normalizer; the ordered world catalog below is the
  multi-source lookup contract.
- Recovered `CcName` and `CcWorld::GetRoomByName`, the exact mission-root CCF
  load order/flags, anonymous-root lifetime, newest-first ordinary-room lookup,
  and the gate that runs `AddStartPos` only after world initialization. Added a
  bounded ordered multi-CCF catalog with distinct runtime-room indices,
  source/physical contributor lists, root/full-name and ordinary/name-only
  matching, ASCII-only parity, and fail-closed world start selection.
- A private full-sequence census covers 20 main CCFs, seven backgrounds, and
  2,101 object CCF load calls. All 20 starts resolve uniquely to ordinary main
  rooms; backgrounds and object CCFs add no named ordinary rooms, and the
  effective namespace has no non-empty exact/ASCII-fold collision. Only these
  aggregate counts are published.
- Recovered the per-load `CcLoadedScene` room-reference lifetime, placed record
  masks, source-local mesh/material identity, receiver fallback, room content
  ownership, and `FreezeAll` rebuild boundary. A bounded source-aware mission
  draw plan now scans each enabled source once in load/physical order, applies
  transient last-wrapper room-reference rebinding, emits root fallback exactly
  once per placed record, and suppresses all placed geometry for flag `0x2000`.
- Added atomic multi-CCF runtime-room assembly with global first-use mesh slots,
  per-source material binding, parallel numeric provenance, aggregate limits,
  and direct compatibility with the existing `DrawSubmissionPlan`. Repeated
  room sections, two CCFs contributing one ordinary room, cross-source local-ID
  collisions, same-source mesh reuse, disabled placed scenes, late failures,
  forged state, and exact/one-under limits have synthetic coverage.
- Recovered the grouped player-aircraft visual as the selected skin's up to
  three complete blueprint hierarchies. The broader actor-owned UID graph also
  includes weapons and auxiliary effects and is not a valid visual selector.
  The original roles of the three blueprint slots remain deliberately unnamed.
- Added deterministic headless Ghidra exporters for exact scalar values and
  instruction listings. The report wrapper can now process a literal program
  already in the ignored local Ghidra project without requiring or recording
  the original file path; its import/reuse/error contracts have Windows tests.
- Established the recovered right-handed CCF basis, row-vector matrix
  convention, reverse-cross winding, degenerate `+Y` normal, and raw UV policy;
  the bounded API-neutral converter validates all 6,995 corpus meshes.
- Implemented owned RGBA8 decoding for complete GTI mip chains. Corpus policy
  uploads all authored levels for 3,974 variants and uses base-plus-Metal
  generation for 16 legacy dimension anomalies, decoding 9,539 levels safely.
- Recovered the engine's exact `TEXU\\source.gti` lookup rule. Full selected
  subtrees cover 2,687 blueprint nodes, 1,858 mesh instances, 2,842 material
  uses, and 2,959 uniquely resolved texture edges with no dependency errors.
- Implemented a deterministic seam-safe draw-mesh payload plus bounded CPU
  rasterizer. A private 111-triangle textured model renders correctly in both
  explicit V policies; generated PPM/PNG files remain ignored and outside Git.
- Confirmed that object loading skips the independent `0x4000` placed table,
  selects a `0x3000` blueprint by name, and recursively instances descendants.
  The bounded graph resolver and multi-instance diagnostic render a private
  grouped aircraft containing 46 nodes, 25 mesh instances, 21 null groups, 663
  triangles, and maximum depth 3. Raw V preserves its markings; explicit V flip
  does not. Private outputs remain ignored and outside Git.
- Decoded all independent `0x4000` placed object/null/light records, including
  transforms, references, bounded light properties, zero-copy null payloads,
  and opaque BSP containers. All 286 CCF scenes parse 9,328 placed records:
  6,995 objects, 2,130 nulls, and 203 lights, all using the matrix orientation.
- Parsed all 392 CCF room prefixes and implemented bounded placed-scene
  reference resolution across instantiated nodes, mesh prototypes, rooms, and
  portals. The selected corpus forms 2,062 roots and 7,266 edges at maximum
  depth 7 with zero missing, ambiguous, or cyclic dependencies.
- Decoded bounded room fog and static/portal BSP into flat iterative arenas:
  98,095 nodes and 198,210 polygon descriptors across all 286 scenes. The
  fail-closed spatial resolver joins every descriptor to its unique placed
  object, mesh, in-range triangle, ordinary room, and (for all 332 portal
  descriptors) target room with zero corpus issues.
- Implemented a bounded conservative explicit-CCF-room draw plan and backend-
  neutral assembly. They ignore BSP visibility, share first-use meshes,
  preserve physical instance order, and apply each placed authored-world
  transform exactly once. Across all 392 rooms in 286 scenes, coverage proves
  that all 6,995 objects appear exactly once; assembly yields 170,039
  triangles with zero plan, transform, geometry, material, or coverage issue.
- Added exact world `CCFF` source-entry validation, a generic bounded
  `TEXU\\source.gti` resolver, and a fail-closed runtime texture plan. Expected
  dependencies are checked one-to-one before dense first-use IDs are assigned
  across primary, secondary, and environment roles.
- Added backend-neutral GTI upload metadata with the recovered `8, 7, 4, 3, 6`
  variant preference, authored-chain/base-plus-generated-mips policy, hard
  parser bounds, and checked decoded/upload/resident RGBA8 budgets. Complete
  object-subtree validation resolves 2,959 edges into 614 per-object unique
  import requests: 612 authored-chain and two generated-chain imports.
- Added atomic GTI runtime materialization. `prepareGtiUpload` enforces the
  source limit before parsing, composes parse/plan/decode, validates every
  decoded level and aggregate byte total against its plan, and publishes the
  plan plus owned RGBA8 levels only together. Authored chains materialize in
  full; legacy anomalies expose only the valid base for backend mip generation.
- Added fail-closed backend draw submission. `buildDrawSubmissionPlan`
  preflights all aggregate/source/command limits before output allocation,
  validates complete mesh/range/bounds/transform/material/texture invariants,
  and emits deterministic mesh uploads plus instance-then-range commands only
  on complete success. Optional primary/secondary/environment roles and valid
  texture ID zero remain distinct.
- Wired that plan into the synthetic Metal adapter. Vertex/index sizes, draw
  offsets and ranges, instance/mesh relationships, a 64 MiB packed-CPU budget,
  a 64 MiB aggregate GPU budget, and `device.maxBufferLength` are checked
  before packed payload or Metal-buffer allocation. Valid empty/no-draw meshes
  retain nullable resources in strongly owned per-mesh wrappers; commands may
  reference only populated resources. Renderer state is published only after
  the entire pipeline, buffer, texture, and immutable resource snapshot is
  ready, with C++ failures contained at the Objective-C boundary.
- All 29 world definitions bind uniquely to their named CCF. Explicit physical
  CCF-room selection validates all 135 rooms and assigns all 4,911 objects
  exactly once to 105 non-empty ordinary rooms. All 29 receiver rooms and one
  ordinary room are valid empty plans. The World `ROOM` catalog is not treated
  as an index, ID, name, or reference join to CCF rooms, and BSP remains
  disabled as a visibility rule.
- Implemented finite, invertible parent-relative local transform derivation and
  composition in runtime column-vector order. Round-trip tests include
  non-commutative rotations and shear; `rawScalar` is not treated as scale.
- Added the portable `airfix_content` ownership boundary. Recovery returns a
  move-only lease that binds its already authenticated AFPACK handle to
  `{generation, size, digest}`, parsed pack/manifest, and exact
  `source/Resource.up` archive. `VerifiedContentSession::adopt` consumes it
  without reopen, rehash, or reparse and exposes only serialized bounded nested
  reads. A separate synthetic-stream entry point still covers wrong
  size/digest/generation, cancellation during hashing, malformed source UDSP,
  exact/one-under limits, and a poisoned label that points at another real file.
- Implemented an atomic `WorldRoomLoader` on that session. A request contains
  only an exact World logical path and a legacy single-room index; CCF
  and GTI entry indices are derived internally from the same archive. The
  loader validates World -> `CCFF`, room dependencies, dense first-use texture
  IDs, GTI plans and RGBA data, room assembly, and draw submission before
  publishing one revision-tagged result. It preserves raw UVs and does not use
  World `ROOM` or BSP as an inferred selection/visibility rule.
- Global limits cover unique texture count, aggregate source bytes,
  decoded/upload/resident RGBA, and published CPU data. GTI plan-only
  preflight rejects exact aggregate overruns before decode; compressed source
  budgets account for the simultaneous stored and unpacked buffers before I/O.
  Synthetic end-to-end tests cover valid/empty rooms, exact `CCFF` among two
  CCFs, texture ID zero, two-texture budgets, deduplication, cancellation,
  malformed later GTI atomicity, compressed peak allocation, and published CPU
  exact/one-under bounds.
- A private external smoke run exercised the production writer, installer,
  AFAC inspection, same-handle lease adoption, and `WorldRoomLoader` against an
  ordinary room from the owner's original content. It published 62 meshes,
  62 instances, 23 dense textures, and 167 validated draw commands. Generated
  private artifacts and original-derived bytes never entered Git.
- Added a portable `WorldRoomPublicationGate` and the native mission handoff.
  Main-thread state binds each request to the exact active
  `{generation, size, digest}` revision and a monotonically increasing serial;
  replacement, lifecycle invalidation, content changes, and serial exhaustion
  fail closed. Exact ticket/revision `consume` and `abandon` operations are
  additionally confined to the gate's owner thread; stale or cross-thread
  completion cannot mutate a newer request.
- The native request is an explicit private setup logical path, Level logical
  path, and `uint32_t` start index. The content worker builds
  `MissionLoadManifest` and then `LoadedMissionWorldRoom` through the same
  pinned `VerifiedContentSession` and revision. A private Objective-C++
  snapshot owns that aggregate C++ result exactly once, exposes aggregate
  counts only, and does not expose or log the requested paths. A stale,
  unconsumed payload is detached and destroyed on a serial off-main queue.
- Implemented two-phase aggregate-mission Metal publication. Complete mesh buffers,
  authored RGBA8 mip levels, generated mip chains, texture arrays, draw offsets,
  payload, submission state, setup/start selection, and numeric provenance are
  prepared off-main. On main, renderer validation is read-only, then the exact
  current publication ticket is consumed and the already validated candidate
  is installed with a no-fail constant-time strong-pointer assignment. A
  failed or discarded current candidate is conditionally abandoned; stale
  candidates cannot clear newer tickets. Preparation checks a 128 MiB packed-CPU
  ceiling, 256 MiB logical and heap-plan ceilings, and
  `device.maxBufferLength`. Buffers and textures are created only from retained
  shared/tracked Metal heaps. CPU texture pixels are released after Metal owns
  the complete texture set, while setup/start selection and mesh/instance
  provenance remain retained by the published room.
- A move-only reservation token admits the heap descriptor plan before
  allocation, then charges each accepted snapshot by the measured
  `MTLHeap.currentAllocatedSize` exactly once. Page-rounding growth must obtain a
  supplemental reservation before publication. The 384 MiB aggregate ledger
  includes candidates, current and retired snapshots, and GPU-in-flight owners.
  Because Metal publishes no pre-creation upper bound for heap page rounding,
  the short unpublished creation window is not claimed as a hard physical byte
  ceiling.
- Draw submissions retain their immutable snapshot through the Metal command
  buffer completion callback, while final large-resource destruction is
  dispatched to a serial off-main release queue. Resources and heaps are
  destroyed before their exact measured debit is released.
- Added a neutral scene-bound dynamic pose exchange. It publishes bounded,
  sorted absolute instance transforms through two preallocated SPSC slots,
  rejects stale identities and malformed frames atomically, and uses stable
  RAII leases plus authored-transform fallback. Its shared control block keeps
  outstanding leases safe after exchange destruction and prevents allocator
  address reuse from reviving stale handles. The authenticated player runtime
  now connects this exchange to Metal and the recovered primary-actor/
  skin-hierarchy contract.
- Added global source-aware mission texture binding. It replays the canonical
  room plan, validates every supplied CCF logical-path/file-index pair against
  the same UDSP metadata, resolves each source-local `TEXU`, preserves
  `{sourceIndex, materialReference}` isolation, and assigns dense global IDs by
  first authenticated archive-file use. The same GTI is deduplicated across
  sources and roles without reading its payload. The caller must bind parsed
  metadata and the entry index in one immutable load transaction; the binder
  does not prove independently constructed metadata provenance. Late-source,
  ambiguous, forged, overflow, and exact/one-under limit failures clear all
  output.
- The mission texture-binding regression target brought the portable suite to
  40/40.
- Added the authenticated `MissionLoadManifest` metadata transaction. The
  move-only, private-constructor result retains the exact `ContentRevision` and
  owns every published path/index identity. Moving transfers validity and
  explicitly invalidates the source; `valid()`, `belongsTo()`, and result
  `success()` reject moved-from state. During construction, the builder also
  pins the opaque identity of the exact authenticated handle. The request
  requires independent explicit setup and Level logical paths. It performs
  exact setup lookup only; there is no Level-name derivation, extension search,
  sibling search, or path fallback.
- One `VerifiedContentSession` handle reads the setup AFS, Level, World, and
  every unique physical Level `OBJE` definition. The non-executing setup parser
  recognizes only bounded `AddStartPos` calls, accepts finite values, and
  enforces the fixed 16-entry legacy capacity. The manifest owns the canonical
  setup path/index, checked setup-source footprint, and parsed starts; it
  retains no raw AFS bytes or parser views. A present but empty authenticated
  setup is valid. The handle token is not published: `belongsTo()` means the
  same authenticated `ContentRevision` and intentionally permits a separately
  authenticated session for those bytes.
- Exact unique lookup continues from Level -> World -> main `CCFF`, optional
  first `BCKD`, and every physical `OBJE` definition -> `CCFF`. Repeated
  object-definition archive indices reuse one parse-cache entry, while CCF
  descriptors retain main/backdrop/physical-OBJE order and repeated entries. A
  referenced `OBJE` that parses with a `MODL` root fails closed. Level `MODL`
  paths are resolved as metadata only, are not read, and never become
  mission-root CCF sources.
- No CCF or GTI payload is read in this phase. Per-entry compressed-source peak,
  aggregate unique definition source, per-entry and aggregate planned CCF,
  descriptor-count, and published-CPU limits are checked before publication;
  the conservative CCF aggregate charges every descriptor, including repeats.
  Cancellation, callback exceptions, allocation/overflow, or any late
  lookup/read/parse/type/limit error clears the candidate and returns no partial
  manifest. World/`OBJE`/`MODL` resolution is cancellable per item, and a
  callback-driven session replacement or move-out returns
  `sessionIdentityChanged`, even with the same revision. Handle identity and
  revision are checked before/after callbacks and before publication.
- The manifest optionally authenticates one explicit player `MODL`-root visual
  definition. Its canonical definition and model-CCF identities, visible
  slot-zero blueprint selector, texture root, and checked source footprints are
  retained as a separate descriptor. Level `OBJE` placements still require
  `OBJE`; an `OBJE`-root player definition now fails closed. The player CCF is
  planned but never read during manifest construction and is not inserted into
  the room-catalogue load list.
- Added `MissionWorldRoomLoader`, the atomic consumer of that proof. It
  validates the full descriptor shape and exact logical-path/file-index
  identity, pins the loader session's revision and opaque transaction marker,
  and reads each unique physical CCF once through that handle. Repeated CCF
  descriptors remain separate semantic sources and produce the exact
  main/backdrop/physical-OBJE catalogue order; the result records the
  descriptor-to-cache mapping without publishing raw metadata pointers.
- The loader request contains no caller start span. It copies the
  manifest-owned starts, builds `MissionWorldRoomCatalog`, resolves the fixed
  table with modulo selection, and uses root fallback for a present-empty
  setup. It then constructs one selected-room global texture namespace,
  materializes every unique GTI, preserves mesh/instance source provenance,
  and validates `DrawSubmissionPlan`. The loaded result carries the canonical
  setup identity and source footprint without retaining raw AFS. CCF/GTI source
  footprints, decoded/upload/resident RGBA, and published CPU have independent
  checked limits. Retained CCF metadata has a separate post-parse logical
  counter; parser admission remains governed by the CCF parser's hard caps, so
  this is not a peak-allocation or process-RSS ceiling. A late CCF, GTI,
  assembly, submission, callback, cancellation, or session-identity failure
  publishes no room.
- `VerifiedContentTransactionIdentity` now uses a retained private marker rather
  than an `ifstream` address. Session moves carry the marker, copied guard
  tokens keep displaced markers alive, and independently opened same-revision
  sessions receive distinct identities, closing allocator-address ABA.
- Public synthetic coverage proves four semantic loads over three physical CCF
  reads, and a second case reuses one physical CCF across different semantic
  roots and `0x2000` placement flags. It covers legacy start selection,
  authenticated empty-setup root fallback, exact setup provenance, first-use
  texture-ID ordering, independently recomputed ownership counters, exact/N-1
  compressed and aggregate budgets, callback-time destruction of snapshotted
  inputs, separately authenticated equal revisions, moved and replaced
  sessions, identity-versus-callback-error precedence, cancellation, and late
  atomic failures. The portable suite now passes 47/47.
- Joined the optional authenticated player actor into the aggregate
  mission-world loader and its allocation-free publication boundary. The
  loader shares physical CCF reads, preserves the room's mesh/instance and
  texture-ID prefixes, appends actor-only resources in deterministic first-use
  order, applies the authenticated absolute spawn pose exactly once, and emits
  one validated draw model and submission plan. Typed provenance, independent
  count/byte ceilings, fail-closed session/callback/cancellation handling, and
  synthetic shared/separate-source and tamper cases cover the join. Native
  physical-device rendering and visual acceptance remain pending.
- Added a bounded portable player-pose frame builder and reusable
  `PlayerActorPoseRuntime`. Metal preparation derives an exact actor-only
  step-zero frame from authenticated provenance, verifies it bit-for-bit
  against the final authored instances, and admits source storage, scratch,
  and two exchange slots under independent iOS ceilings and the CPU budget.
  The main-thread simulation consumer publishes accepted later steps without
  allocation before committing their deterministic state; a busy exchange
  drops only that visual update. Each rendered frame acquires at most one
  lease, and static instances retain their authored fallback. The producer
  currently republishes the authenticated spawn world because the movement
  law has not yet supplied a changing actor pose.
- Recovered the bounded legacy screen-projection and room render chain.
  Constructor defaults, engine near/far/FOV values, exact FOV clamp/focal
  formula, near fallback, screen centre, Y inversion, far/near clipping order,
  render-list drain, and concrete Direct3D triangle staging are confirmed by
  Ghidra and Rizin. `RenderRoom` uses object lists and clipped portal recursion
  with a default depth limit of two; it does not traverse static-BSP children.
  Portal-BSP is instead confirmed in camera movement room transitions. The
  exact scalar projection is now implemented as a validated, allocation-free
  portable C++20 contract. The camera SRT layout and exact world-to-camera
  transform are also recovered: subtract camera translation, apply three
  transposed-basis dot products, then multiply by cached inverse-square scale,
  without changing axis signs. That point transform is implemented behind an
  immutable, normalized-Gram-validated, allocation-free C++20 boundary.
  The active Direct3D path is now confirmed as a normal Z-buffer carrying
  manual reverse depth `near / cameraZ`, with `greaterEqual` comparison and
  `[0,1]` viewport depth. Both concrete Direct3D clear paths use
  `TARGET|ZBUFFER` with exact depth `0.0`. Startup uses a logical 640x480
  canvas; no adaptive widescreen policy was found. A follow-up symbol/string
  audit corrected the fixed `(35,35)-(555,345)` rectangle from “gameplay” to
  the deferred House Editor. The portable projection result now preserves
  camera Z and the exact reciprocal-depth scalar, while an immutable,
  allocation-free legacy layout contract preserves the recovered 640x480
  aspect-fit comparison without presenting that port policy as original
  behavior. The newer native-render layout drives both product backends with
  full-target Hor+ by default. A second portable
  contract fail-closed maps raw depth modes `1` through `4` to the recovered
  compare/write presets and confirmed zero clear. The actual gameplay camera
  is now separated from the House Editor: it uses a full-current-screen
  viewport, projection defaults `0.25/200/90`, three persistent chase tuples,
  and a held rear tuple. Preset selection/cycling is implemented as another
  fail-closed portable contract. The exact vehicle quaternion matrix plus
  stateless per-refresh target/smoothing recurrence are also implemented.
  Backend-neutral camera collision primitives now cover the exact sphere
  radius, per-axis collision reduction, aircraft-specific recovery, line-hit
  interpolation, raw portal arguments, and final look-at matrix. The actual
  retained all-room BSP arena now survives CCF-cache destruction and supplies
  bounded allocation-free static/portal line tracing, exact legacy list/tie
  order including binary32 spills and unordered traversal, portal
  mesh/type/visibility gates, and source-aware runtime-room targets. Catalog
  replay, pre-allocation aggregate limits, exact publication topology, and a
  hard 256-transition safety ceiling protect the retained backend. A new
  allocation-free runtime/source-world adapter now applies the mission basis
  and units in both directions, transforms plane normals by inverse transpose,
  preserves diagnostic legacy fractions, and rejects any out-of-segment hit
  before a camera-facing portal trace can change rooms. An allocation-free
  camera room-state boundary now returns a complete candidate position and
  final room only after the full portal chain succeeds; later-hop failures
  retain diagnostic evidence without publishing an intermediate room.
  Sphere-contact recovery now also carries the first CCF `0x2152` u32 from a
  uniquely resolved triangle material into every retained spatial polygon.
  Native evidence confirms that the gameplay camera deletes collected
  contacts for non-null material modes `0` and `8`; numeric labels remain raw.
  The constraint projection stage now also reproduces strict plane admission,
  directional override selection, oldest-active ordering, one-/two-plane
  projection, and conflicting-plane stop behavior without allocation.
  Static sphere collision now reproduces the seven-axis triangle candidate
  test, B-then-A BSP traversal, visible type-zero portal-room discovery,
  material filtering, face/edge/vertex selection, native tie order, and
  iterative constrained correction through caller-owned bounded workspaces.
  The corrected centre now feeds factor reduction and portal tracing in native
  order, producing one allocation-free all-or-nothing proposal containing
  position, final room, and all three axis factors. Workspace, geometry,
  basis, factor, and later portal failures expose no partial state.
  The retained-static vehicle-to-camera line stage now consumes only a
  complete sphere result, preserves a miss, and on a hit applies the exact
  adapted point plus the native second portal update before exposing state.
  A preallocated single-writer camera owner commits only that completed result
  and publishes immutable `{step, generation, position, room, factors}` copies
  through a bounded two-slot SPSC exchange. Invalid, stale, exhausted, or
  contended commits retain the exact prior state; concurrent synthetic tests
  reject torn fields. Ghidra and Rizin independently confirm that both camera
  matrix reset paths establish unit scale and inverse-square, allowing one
  acquired generation and vehicle anchor to build an immutable look-at,
  world-to-view, and scalar gameplay projection snapshot without guessed SRT
  state. Placed/player dynamic line collision and transparent line portals are
  now runtime-owned; dynamic-object sphere collision remains unimplemented.
  The private mission-room Metal path now consumes the immutable pose through
  a tested homogeneous clip packet, recovered reverse depth, explicit far
  clip, and the shared native-resolution Hor+ viewport. Until
  the simulation publishes changing event-5 generations, it starts at an
  explicit camera0 target anchored to the authenticated spawn. The synthetic
  public-data path remains diagnostic and no full parity claim has been
  introduced. A producer-side coordinator now composes recovered AirCraft
  factor recovery, cumulative mode/rear input, chase, the complete
  retained-static collision path, pose, clip packet, and atomic state commit
  as one allocation-free transaction. Ghidra and Rizin independently confirm
  that its factor-recovery argument is the same scheduler delta converted from
  milliseconds to seconds; the nominal 12 ms AirCraft interval supplies
  `0.012f`, not the 60 Hz input-pump interval. Exported Ghidra symbols and
  independent Rizin instructions now name the two explicit inputs as live
  `NfActor` health and the `AfVehicle` inactive latch. A fixed
  mission-lifetime SPSC exchange transports only complete
  owning clip packets, and Metal retains one lease through encoding. A new
  non-moving mission runtime now owns the arena, immutable placed/player
  colliders, exact-size camera workspaces and dynamic-line buffers,
  coordinator, and exchange under checked portable and iOS memory ceilings.
  It republishes and portal-traces only a complete producer-supplied
  placed/player frame; a failed republish retains the prior frame. The atomic
  room transaction publishes a
  weak producer endpoint that expires safely on mission replacement. The
  endpoint remains deliberately inactive and the exchange contains only the
  explicit bootstrap until complete recovered aircraft inputs exist.
- The authenticated setup-to-room provenance chain now crosses the native iOS
  publication boundary together with an immutable `PlayerSpawnPose`. Ghidra
  Headless and Rizin independently confirm x/y/z radians, mode zero, exact
  Z-X-Y construction, and absolute-world runtime semantics. The loader uses
  the room's basis and unit policy for both geometry and pose; native
  publication recomputes the pose, consumes the exact ticket, commits Metal,
  then assigns pose and a fresh input-intention state before readiness.
  Runtime room switching now has retained pointer-free BSP and portal-room
  data; applying it to the stateful simulation/camera coordinator remains.
- Added optional build-generated initial mission configuration. Public defaults
  are empty/data-less. A future protected private workflow may supply Base64
  setup, Level, and optional player-object logical paths plus a decimal
  `uint32_t` start index; CMake validates them and generates a local header
  without committing private paths. Public pull-request workflows never read
  these inputs or signing secrets. AFPACK v1 contains no launch metadata; an
  authenticated bounded mission catalogue remains a target for AFPACK v2.
- Recovered AirCraft sound ID `0x20` as the `enginedive` sample. On the shared
  fifth-call audio pass, health `<= 0` starts or retains it and applies
  `clamp(-0.15 * velocity.y - 0.2, 0, 1)` volume; positive health stops it.
  A separate allocation-free C++20 transition preserves the exact command
  order and deliberately initializes the original constructor's unwritten
  `+0x566` state byte to false. A portable coordinator now runs it only on the
  shared fifth-call cadence, splices it between engine phase transitions and
  common modulation, and translates the combined stream to generic audio
  commands with validated caller-supplied clip/voice bindings. Live aircraft
  inputs and private decoded samples remain unwired.
- Recovered the primary weapon path from `EVENT_PRIMARY_ATTACK` through the
  AirCraft `WpMgun` pointer and persistent `AfWeapon::Fire` state into the
  separate `WpMGun` time-dependant refresh. The five technology levels use
  exact `0.12..0.08` second intervals and `40..100` projectile speeds; the
  refresh preserves a tenfold zero-ammunition cadence, selects/wraps the
  attachment-provided barrel, decrements only nonzero ammunition, creates a
  private `WpMGunAmmoTechN`, and processes projectile event `0xE2`.
  Allocation-free C++20 helpers preserve that timing/state transition and the
  complete semantic payload: rotated muzzle input, exact target-velocity lead,
  packed field meanings, zero target UID, five impact-damage profiles, exact
  acceleration bits, four-second lifetime, unobstructed ballistic step, actor
  damage command, material-15 impact interpolation, and bounded ricochet
  request. A transactional coordinator now selects the emitted rotated muzzle,
  caps the technology profile, produces the complete payload, and returns the
  already-advanced cadence/barrel/ammunition state so a later private
  allocation failure cannot incorrectly roll it back.
  A caller-owned fixed-capacity runtime now preserves that native branch
  order without importing private PE32 objects: it commits weapon state before
  capacity acquisition, delays muzzle/event-only reads until a reusable slot
  exists, activates semantic event `0xE2`, and returns a generation-tagged
  handle. First-free selection is an explicit deterministic port policy;
  saturated generations fail closed and reuse cannot revive stale handles.
  Root ID zero,
  monotonic ordinary-room IDs, and newest-first lookup now map bidirectionally
  to the retained mission-world catalogue without native pointers.
  `PhLine::GetBspCollision` now also confirms static-before-dynamic traversal
  through one strict-nearest fraction plus recursive visible type-zero portal
  continuation. An allocation-free single-hit decision seam implements the
  exact shared material-8, portal, client/server actor-gate, actor-contact,
  surface, normal, position-swap, and material-15 water behavior. Bounded
  per-mesh dynamic BSP construction now preserves the native splitter,
  prepend, clipping, material/provenance, and radius contracts. A non-portal
  allocation-free adapter lets retained static geometry and caller-ordered
  unit-scale dynamic instances compete through that same strict-nearest
  fraction. A second bounded allocation-free adapter now consumes flat
  per-room object ranges, follows automatic visible type-zero portals, replaces
  portal provenance with the later hit, composes whole-segment fractions, and
  fails typed on malformed ranges, out-of-segment hits, or cycles. A separate
  generic simulation coordinator now repeats the complete query after a
  projectile-level type-zero portal, preserves the full endpoint, returns all
  existing terminal decisions, and fails closed on query/decision errors or
  bounded cycles without allocation. An authenticated adapter now maps its
  current signed room through the retained catalogue, invokes the published
  runtime portal trace, preserves the exact `0x2152` material bits, and
  resolves the primary player's server actor gates from state committed in
  the same complete collision-frame generation. A typed callback remains for
  future non-player actors. A fail-closed terminal reducer now commits the
  resulting endpoint and room, preserves the material-15 water flag, applies
  actor/self damage and deactivation rules, and returns bounded surface or
  ricochet command data. The published runtime wrapper joins that reducer to
  both actor and portal/surface paths while rejecting flight/query mismatch.
  A higher allocation-free transaction now resolves the creator UID, invokes
  the recovered `DisableBsp`, brackets the complete query/portal/terminal
  path, and invokes `EnableBsp` only when BSP was previously enabled. Missing
  or already-disabled creators preserve the native no-enable path; rejected
  disable prevents tracing and rejected enable suppresses terminal
  commit/command data.
  Active-slot collision advancement and live event/effect dispatch, live
  muzzle transforms, non-player actor and dynamic-portal
  publication/resolution, concrete creator BSP callbacks, tracer/effect
  realization, secondary weapon families, dynamic camera-sphere contacts,
  runtime traces, and integration remain pending.
  The authenticated player CCF now also produces bounded immutable per-mesh
  colliders and ordered actor-local instances inside the atomic mission load.
  A two-pass `noexcept` frame adapter publishes a supplied live player
  world-pose, object ID, active flag, and current room into exact-size combined
  line object/range spans without allocation or partial mutation.
- The preserved placed-object `0x4101` property is now decoded as the same
  bounded flat `F0C0`/`F0C1` format used by room BSP while its raw wrapper and
  extension children remain available. Ghidra and Rizin confirm deferred
  `(placedObjectReference, polygonIndex)` binding, first-use mesh caching, and
  reverse runtime root-list order. All 1,160 selected wrappers parse into
  10,641 nodes and 16,212 polygons with zero owner/index mismatches.
  `MissionPlacedDynamicBspAssembly` now authenticates the room catalogue and
  placed scene, binds those polygons to physical triangles/material modes,
  reproduces source-local first-use mesh caching and per-room prepend order,
  converts unit-scale F050 world transforms into runtime coordinates, and
  emits exact ranges consumed directly by the allocation-free combined line
  query. The later cached-object no-relink branch is preserved as observed;
  the selected corpus has no repeated serialized cache key, so that branch
  remains a controlled-runtime-trace target. The authenticated mission
  snapshot now owns this assembly, charges its exact nested bytes to the global
  CPU budget, and validates its room/source provenance at publication.
  A two-owner mesh view plus a two-pass frame adapter compose the live player
  ahead of each room's unchanged placed list without copying geometry,
  allocation, or partial output. The mission-lifetime runtime now owns those
  immutable assets and exact flat buffers, exposes the last complete frame,
  stores the primary player's object ID, active flag, and both recovered
  projectile gates in the same successful transaction, and traces against its
  owned static arena without allocation. Failed republishes retain both the
  previous geometry and actor state. Other live actors, a real changing
  producer, controlled traces, and dynamic sphere collision remain separate.

## Confirmed

- Reference readme identifies v1.01.
- 33 files total 356,442,590 bytes.
- All 16 executable modules inspected are PE32/x86.
- `.mode` and `.type` files are DLLs despite custom extensions.
- `Resource.up` and language `.up` files begin with `UDSP 01 01` and are not
  standard archives recognized by 7-Zip.
- Original rendering uses Direct3D/DirectX 7-era interfaces and optional Glide.
- The locked Phase 1 toolchain is installed and its versions and source hashes
  are recorded in `docs/toolchain/LOCK.md`.
- The iOS control baseline requires full touch-only play plus controller-only
  play, hot-plug recovery, remapping, deterministic input frames, safe-area
  layouts, and optional haptics.
- Accepted v1.0 scope: a playable native Windows x64 product plus a private iOS
  ARM64 sideload over shared C++20 game, physics, resource, save, and semantic
  input code. Windows is the primary rapid-debug and controlled
  original-comparison environment; each product owns separate platform,
  renderer, physical-input, audio, and filesystem adapters.
- The iOS product retains its iOS 16.4 minimum deployment target, iPhone 17 Pro
  Max/iOS 26.6 and iPhone SE 3/iOS 26.3 runtime devices, available Apple
  Developer account, and no-App-Store policy. Version 1 remains single-player
  only, with no editors, multiplayer, or original CD music.
- Original and converted game data remains owner-private and outside Git,
  public Actions logs, caches, artifacts, and public packages for both products.
- Native-resolution 3D at 100% render scale and Hor+ widescreen are accepted
  product requirements on both platforms. Classic preserves the original
  model-kit art direction at high resolution; Enhanced adds modern rendering
  features without affecting deterministic gameplay or physics.
- GitHub Actions hosted macOS/Xcode currently provides unsigned iOS compile
  validation. Interactive device debugging and profiling later use local Apple
  tooling; private signing must keep original data local in an imported
  `.afpack`.

## Next

1. Build the trace-driven 12 ms AirCraft producer without treating the separate
   60 Hz input clock as physics. Confirm the Q15-to-legacy-event timing and
   payload policy, then publish one coherent changing pose/health/inactive/
   kinematic sample to Windows and iOS before connecting live camera and audio.
   The backend-neutral runtime planner and one-lease-per-frame Metal/D3D11
   consumers plus the isolated native-field target/apply/clamp/smoothing step
   and the separate already-formed signed native `THRUST_SET/APPLY` typed-write
   reducer are complete. The exact signed
   `TURN_SET`/`PITCH_SET`/`BANK_SET` branch structure, rest-clear behavior,
   and producer ranges where producer paths exist are now statically
   recovered; the separate pure structural reducers now implement the
   explicitly labelled full-domain PC53/RN compatibility policy while the
   model remains conditional on the live x87 control word. A
   simulation-thread-confined owner commits typed control writes and their
   shared rest clear transactionally, but owns no producer ordering or clock.
   A separate single-invocation step now composes the discrete command reducer,
   typed decoder, and that owner without adding a batch or scheduler.
   Keep all control reducers and that owner unwired from
   `PlayerAircraftState` until controlled traces prove
   numeric mode, Q15-to-event ordering, and sample-and-hold timing; do not
   manufacture a complete native flight-control scheduler from the nominal
   12 ms interval.
2. Obtain controlled runtime traces for free flight and the ground, inverted,
   water, collision, engine-transition, and too-high branches; establish
   x87-versus-portable numeric tolerances and deterministic replacement PRNG
   sequencing before composing the statically recovered 12 ms flight law.
3. Add persistent layout/visibility profiles, calibration and remapping,
   controller glyphs, haptics, and finished menu bindings; then run touch-only
   and controller-only acceptance on both target iPhones.
4. Connect the implemented weak camera-runtime endpoint to the recovered live
   AirCraft producer once it supplies distinct chase position, world anchor,
   vehicle rotation, live health, inactive state, and the confirmed scheduler
   delta in seconds without resampling the separate 60 Hz input pump. Replace the
   camera0-only publication only when that complete input exists. Preserve
   scalar clip and reciprocal depth while validating the new Hor+ port policy
   separately from recovered 4:3 evidence; dynamic-object and transparent
   collision-portal paths remain separate.
5. Validate the implemented runtime portal-to-room-state proposal against
   controlled multi-room executable traces when the isolated runtime is ready.
6. Keep BSP render culling disabled until its separate runtime semantics are
   proven against executable evidence.
7. Feed the runtime-owned player collider and concrete resolver from the
   recovered changing actor producer. Extend the same generation-matched
   publication/resolution to other live actors, then connect the implemented
   creator BSP guard callbacks and already reduced terminal damage/surface
   commands to private live actors/effects. Feed live muzzle transforms into
   the fixed-capacity activation runtime, advance its active slots through the
   collision transaction, and add tracer/effect adapters before wiring
   primary-fire intent into runtime.
8. In parallel with gameplay reconstruction, extend the implemented ADR-0013
   resolution/Hor+/UI/input/offscreen-target foundation with finished settings
   for render scale, Original 4:3 and safe FOV plus HUD/effect integration.
   Validate the implemented diagnostic overlay on both physical iPhones before
   lighting, materials, shadows, or post-processing.

## Open questions

- Which D3D feature-level floor, exact Windows 10 release floor, and private
  packaging procedure will be used?
- Which Windows-compatible method will install the signed Actions IPA on both
  registered phones?
These questions do not block static analysis or the archive work.

## Latest validation

- The staged native control-command step passes fresh complete Windows GCC
  15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds plus all 104 CTests.
  A fresh Clang 22.1.8 build passes the dedicated exhaustive test; one
  independent review additionally passes both GCC and Clang
  warnings-as-errors builds. All three reviewers report GO with no P0-P3
  findings. Public-boundary tests, 12 Rizin export tests, all three
  reverse-engineering wrapper suites, the 527-file public scan,
  changed-document links, local-path scan, clang-format, and
  `git diff --check` pass.
- The discrete native control-command slice passes complete clean Windows GCC
  15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds plus all 103 tests.
  Fresh Clang 22.1.8 and independent GCC warnings-as-errors builds pass the
  dedicated exhaustive reducer test. Ghidra/working-copy/Rizin wrapper suites,
  12 Rizin export tests, synthetic boundary tests, the 523-file public scan,
  268-row function catalogue, changed-document links, local-path scan, and
  `git diff --check` pass. Independent review's sole P2 catalogue overclaim is
  fixed; final review reports no open P0-P3. Pull-request Actions runs
  `30511495498` and `30511495515` pass all seven hosted Windows product,
  Windows/Ubuntu/macOS portable, clangd, iPhoneOS, and iPhoneSimulator gates.
- The backend render-scale slice passes independent complete MSVC 19.51 and
  MinGW GCC 15.2 Windows product rebuilds and all 94 tests in each build. Both
  public D3D11 smoke modes render a visible frame; the added scaled mode
  exercises a 50% offscreen color/depth target, Original 4:3 viewport, linear
  presentation pass, and unchanged output backbuffer without private content.
  Controlled owner-local 50% and 200% captures are distinct 960x540 outputs
  and remain outside Git. Hosted Visual Studio, iPhoneOS, and iPhoneSimulator
  results are recorded by the merge gate for this slice; physical-device Metal
  acceptance remains pending.
- The native-render-layout slice passes a clean 292-step GCC 15.2/Ninja build
  and all 90 portable tests. Full MSVC 19.51 and MinGW GCC 15.2 Windows product
  builds each pass 93/93 tests, including the data-less D3D11/XAudio2 smoke.
  A representative private D3D11 mission capture made after physical-size
  validation is exactly 3840x2160; the original installation and owner data
  remained unchanged and outside Git. The 459-file public-boundary scan,
  `actionlint`, changed-document link and local-path checks, and
  `git diff --check` pass. GitHub Actions runs `30470411401` and `30470413579`
  pass all seven hosted Visual Studio product, Windows/Ubuntu/macOS portable,
  clangd, iPhoneOS, and iPhoneSimulator gates.
- The authenticated aircraft-audio slice passes a fresh 289-step GCC 15.2
  build and all 89 portable tests. A fresh native MSVC 19.51/Ninja product
  build compiles 548 steps and passes 91/91 tests, including the real hidden
  D3D11/XAudio2 smoke; the existing MinGW product build independently passes
  the same 91/91 suite. Synthetic public-boundary tests and the 449-file scan,
  `actionlint`, changed-source clang-format, changed-scope local-path scanning,
  and `git diff --check` pass. A complete owner-local AFPACK also passes the
  hidden authenticated load/register path outside Git. GitHub Actions runs
  `30457131783` and `30457131656` pass the hosted Visual Studio product,
  Windows/Ubuntu/macOS portable, clangd, `iphoneos`, and `iphonesimulator`
  publication gates.
- The native iOS audio slice passes a fresh 281-step MinGW GCC 15.2 Release
  build and all 87 portable tests. Xcode 26.6 compiles both ARM64 `iphoneos`
  and `iphonesimulator` data-less bundles with deployment target 16.4 after
  compiling the AVAudioEngine Objective-C++ backend. The synthetic and actual
  441-file public-boundary scans, changed-source formatting, changed-scope
  local-path scan, and `git diff --check` pass. Audible output, interruption
  timing, and wired/Bluetooth route behavior remain explicitly unverified
  until physical-device acceptance.
- The AirCraft audio-composition slice passes a fresh 281-step MinGW GCC 15.2
  Release build and all 87 portable tests; the code-intelligence build also
  passes 87/87. The native product build passes 89/89 tests. Its hidden app
  validates D3D11 readback/resize, registers silent synthetic PCM, and drives
  XAudio2 2.9 through reconstructed engine start/running/shutdown plus
  destroyed-dive/recovery commands. Clangd reports zero errors in both new
  translation units. The three reverse-engineering wrapper suites, twelve
  Rizin normalization tests, synthetic and 438-file public-boundary scans,
  `actionlint`, new-source formatting, the 17-file local-path scan, and
  `git diff --check` pass.
- The first Windows product slice passes a local MinGW GCC 15.2 Release build
  and 85/85 CTest cases. The final test creates a hidden SDL3
  window, compiles HLSL, renders the shared scene through D3D11/DXGI, reads the
  back buffer, verifies visible pixels, recreates the swap-chain targets, and
  verifies a second frame. The same change
  passes a separate 271-step portable build with 84/84 tests, public-boundary
  unit tests and the 422-file scan, `actionlint`, formatting validation, and
  `git diff --check`. Pull-request Actions run `30439266445` passes the
  Visual Studio 2026 D3D11 product job and all portable Ubuntu, Windows, macOS,
  and clangd jobs; iOS run `30439256007` passes both `iphoneos` and
  `iphonesimulator`.
- Merged projectile runtime-pool main commit `bfde965` passes the exact-main
  Ubuntu, Windows, ARM64 macOS, clangd/code-intelligence, `iphoneos`, and
  `iphonesimulator` Actions jobs.
- The portable projectile runtime-pool slice passes a fresh 268-step
  Windows GCC 15.2 Release build and separate code-intelligence build plus
  83/83 tests in both. Its compilation database has 171 portable entries and
  no Apple-only source; clangd 22.1.8 reports zero errors in all three affected
  translation units with the trusted GCC query driver. The Ghidra,
  working-copy, and Rizin wrapper suites, twelve Rizin normalization tests,
  synthetic public-boundary tests and the 410-file public scan, the 265-row
  unique function catalogue with valid source paths and only its two known
  missing references, `actionlint`, the 19-file local-path scan, and
  `git diff --check` pass. Exact commit `6c5d386` compiles all 268 steps with
  GCC 13.3 and passes 83/83 tests in 26.23 seconds in a fresh isolated
  `Airfix-Dev` WSL clone. The exact-main GitHub Actions publication gate passed
  all six jobs.

## Blockers

- None for Phase 1 static analysis.
- No owner action is required to begin the Windows x64 product spike. The
  platform API/minimum-OS choices are engineering decisions, not blockers.
- The connected repository is public. Before the first signed device spike,
  signed IPA artifacts must be encrypted/protected (or repository visibility
  deliberately changed), because provisioning data must not be published.
- A signing certificate/profile for both device UDIDs, protected Actions
  secrets, and a Windows IPA installation path are required before that spike.
- No public distribution is planned; private signed/converted artifacts must not
  be shared.
