# Project status

**Updated:** 2026-07-24
**Stage:** Phase 1 — static analysis and archive recovery in progress

## Now

- Planning and documentation system established.
- Original installation inventoried without executing binaries.
- Port strategy recorded in ADR-0001.
- Complete iOS port checklist and input/control/haptics system specified;
  semantic input architecture recorded in ADR-0002.
- Local Git repository initialized on branch `main`; planning baseline committed
  as `59828ed`.
- GitHub remote `tryk016/airfix-dogfighter` connected and the planning baseline
  published.
- Phase 1 toolchain installed and pinned: Ghidra 12.1.2, Temurin JDK 21.0.11,
  Python 3.14.6, CMake 4.4.0, Ninja 1.13.2, LLVM 22.1.8, and GCC 15.2.0.
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
  bindings. The first native iOS slice now adds a safe-area-aware multitouch
  stick/fire/pause overlay, a bounded edge-preserving Apple extended-controller
  adapter, a renderer-independent fixed 60 Hz input pump, explicit-resume
  lifecycle policy, diagnostics, and fail-closed gameplay-boundary resets.
  Complete V1 controls, remapping, profiles, and haptics remain pending.
- Added the first deterministic simulation consumer. The portable
  `PlayerAircraftState` preserves uninterpreted bank/pitch Q15 intentions,
  primary-fire held state and exact edge counts, its own completed-step count,
  monotonic source tick, and an explicit canonical hash. Invalid schema, Q15,
  tick, and counter transitions are rejected without partial mutation. The iOS
  bridge advances it exactly once per eligible running frame and fails closed;
  pose, movement, throttle, and weapon spawning remain intentionally absent.
- Recovered the 63-slot `AirCraft.type` vtable and 16 function entries missed
  by automatic analysis. The per-step method at `0x10003F40` consumes a float
  time delta and contains 15 force, five torque-only, and one force-only call
  sites. The static-collision method at `0x10007920` consumes collided polygons
  and resolves static/BSP contact lists.
- Recovered the complete player/AI control-event join. Player commands and
  analog input emit signed `PITCH_SET`, `BANK_SET`, and
  `THRUST_SET/APPLY` payloads into four `AfVehicle` fields read directly by
  the flight-force step. The similarly named `THROTTLE_SET/APPLY` events are
  confirmed no-ops for the AirCraft inheritance path.
- Recovered the scheduler-visible aircraft order:
  `EulerODE -> ResetForceAndTorque -> CalcAuxiliary -> slot45 force
  accumulation -> collision/slot30 -> slot44 refresh`. `EulerODE` consumes
  the prior accumulated forces; slot45 prepares the next integration step.
  AirCraft uses a 12 ms dependant interval converted to `0.012f` seconds.
  All 21 force/torque sites, direct state transitions, helper transforms, and
  constructor-field joins in slot45 are now statically mapped. Physical units,
  runtime contact traces, and actor spawn/render identity remain unknown.
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
  only an exact World logical path and an explicit physical CCF room index; CCF
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
- Added a portable `WorldRoomPublicationGate` and the native room handoff.
  Main-thread state binds each request to the exact active
  `{generation, size, digest}` revision and a monotonically increasing serial;
  replacement, lifecycle invalidation, content changes, and serial exhaustion
  fail closed. The worker rechecks the adopted session before and after loading,
  and a loaded result must carry the same revision before a callback is accepted.
- The first native smoke selection is the exact logical World
  `Game/Worlds/axis_1.world` and explicit physical CCF room index 1. Its
  Objective-C snapshot owns one immutable `LoadedWorldRoom`, exposes aggregate
  counts only, and permits the CPU payload to be consumed once. A stale
  unconsumed payload is detached and destroyed on a serial off-main queue.
- Implemented two-phase private-room Metal publication. Complete mesh buffers,
  authored RGBA8 mip levels, generated mip chains, texture arrays, draw offsets,
  payload, and submission state are prepared off-main; only a current snapshot
  is atomically published on main. Preparation checks a 128 MiB packed-CPU
  ceiling, 256 MiB logical and heap-plan ceilings, and
  `device.maxBufferLength`. Buffers and textures are created only from retained
  shared/tracked Metal heaps.
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
  address reuse from reviving stale handles. It is not yet connected to Metal
  or identified with a player actor.
- The dynamic-pose regression target brings the portable suite to 35/35.

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
- Accepted v1.0 scope: private sideload only, iOS 16.4 minimum deployment target,
  iPhone 17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3 runtime devices, Apple
  Developer account available, single-player only, no editors/multiplayer, and
  no original CD music.
- GitHub Actions hosted macOS/Xcode is the accepted iOS build/signing host; no
  local Mac is required. CI currently builds unsigned data-less bundles; future
  private signing produces an IPA while original data remains local in an
  imported `.afpack`.
- MacBook policy accepted: request a move only for a clearly reported hard
  blocker or documented >=20% acceleration of the affected remaining work.

## Next

1. Obtain controlled runtime traces for free flight and the ground, inverted,
   water, and too-high branches; assign field meanings and numeric tolerances
   before implementing the statically recovered 12 ms flight law.
2. Extend the native control surface to throttle delta, secondary fire, weapon,
   camera/rear-view, and mission actions before adding persistence, remapping,
   profiles, and haptics.
3. Recover dynamic player-actor creation/spawn and its grouped visual identity;
   never bind a player by first mesh, name resemblance, or source-node index.
4. Recover the runtime rule that selects a physical CCF room during gameplay;
   do not infer it from the structurally different World `ROOM` catalog.
5. Keep BSP culling and portal traversal disabled until their runtime semantics
   are proven against executable evidence.

## Open questions

- Which Windows-compatible method will install the signed Actions IPA on both
  registered phones?
- Should faithful 4:3 framing or expanded widescreen be the default?

These questions do not block static analysis or the archive work.

## Blockers

- None for Phase 1 static analysis.
- The connected repository is public. Before the first signed device spike,
  signed IPA artifacts must be encrypted/protected (or repository visibility
  deliberately changed), because provisioning data must not be published.
- A signing certificate/profile for both device UDIDs, protected Actions
  secrets, and a Windows IPA installation path are required before that spike.
- No public distribution is planned; private signed/converted artifacts must not
  be shared.
- No MacBook blocker exists in the current project phase.
