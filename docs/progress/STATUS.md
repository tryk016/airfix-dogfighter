# Project status

**Updated:** 2026-07-27
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
  tick, and counter transitions are rejected without partial mutation. The iOS
  bridge advances it exactly once per eligible running frame and fails closed;
  pose, movement, throttle integration, camera behavior, and weapon spawning
  remain intentionally absent.
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
  runtime contact traces, and dynamic actor-to-render publication remain
  unknown.
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
- The manifest optionally authenticates one explicit player `OBJE` definition.
  Its canonical definition and model-CCF identities, visible slot-zero
  blueprint selector, texture root, and checked source footprints are retained
  as a separate descriptor. The player CCF is planned but never read during
  manifest construction and is not inserted into the room-catalogue load list.
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
  allocation-free layout contract aspect-fits the recovered 640x480 canvas
  without presenting that port policy as original behavior. A second portable
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
  BSP/sphere/line queries, room transitions, stateful pose publication, and
  Metal join remain unimplemented. Metal remains explicitly diagnostic and no
  full parity matrix has been introduced.
- The authenticated setup-to-room provenance chain now crosses the native iOS
  publication boundary together with an immutable `PlayerSpawnPose`. Ghidra
  Headless and Rizin independently confirm x/y/z radians, mode zero, exact
  Z-X-Y construction, and absolute-world runtime semantics. The loader uses
  the room's basis and unit policy for both geometry and pose; native
  publication recomputes the pose, consumes the exact ticket, commits Metal,
  then assigns pose and a fresh input-intention state before readiness.
  Runtime room switching still requires retaining the CCF/catalogue arena
  rather than reconstructing it from the published static room.
- Added optional build-generated initial mission configuration. Public defaults
  are empty/data-less. A future protected private workflow may supply Base64
  setup, Level, and optional player-object logical paths plus a decimal
  `uint32_t` start index; CMake validates them and generates a local header
  without committing private paths. Public pull-request workflows never read
  these inputs or signing secrets. AFPACK v1 contains no launch metadata; an
  authenticated bounded mission catalogue remains a target for AFPACK v2.

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
- GitHub Actions hosted macOS/Xcode currently provides unsigned iOS compile
  validation. Interactive device debugging and profiling later use local Apple
  tooling; private signing must keep original data local in an imported
  `.afpack`.

## Next

1. Obtain controlled runtime traces for free flight and the ground, inverted,
   water, and too-high branches; assign field meanings and numeric tolerances
   before implementing the statically recovered 12 ms flight law.
2. Add persistent layout/visibility profiles, calibration and remapping,
   controller glyphs, haptics, and finished menu bindings; then run touch-only
   and controller-only acceptance on both target iPhones.
3. Implement the retained BSP/portal collision backend and stateful camera
   publication join before introducing a Metal camera matrix or performing
   physical-device visual acceptance. World-to-camera, scalar projection with
   reciprocal depth, exact zero clear, depth modes, camera presets, quaternion
   matrix, stateless chase target/smoothing, collision/factor/line primitives,
   look-at pose math, and parity-first 4:3 presentation are recovered and
   implemented. Auxiliary weapons and effects remain separate.
4. Introduce a retained CCF/catalogue arena when runtime room switching is
   implemented; recover portal tracing, camera room traversal, and later
   transitions afterward.
5. Keep BSP culling and portal traversal disabled until their runtime semantics
   are proven against executable evidence.

## Open questions

- Which Windows-compatible method will install the signed Actions IPA on both
  registered phones?
- Which optional widescreen/Hor+ design should follow the parity-first 4:3
  presentation after physical-device acceptance?

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
