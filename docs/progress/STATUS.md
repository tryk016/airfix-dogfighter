# Project status

**Updated:** 2026-07-23
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
- Added the native iOS private-content coordinator: document picker, bounded
  security-scoped copy into app-private storage, serialized install/inspection/
  rollback, lifecycle cancellation, progress and recovery UI, strict orphan
  temporary cleanup, and `AppSession` readiness gating.
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
  bindings. Native iOS adapters and the visual touch overlay remain pending.
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

1. Wire `GtiUploadPreparation` and private AFPACK-backed room data into the
   bounded Metal adapter. Replace the public synthetic payload only after
   authenticated content ownership and lifetime are defined; start with
   unlit/no-cull diagnostics, then add evidence-backed render states. Keep BSP
   culling and portal traversal disabled until their semantics are proven.
2. Implement native UIKit and Game Controller adapters over the deterministic
   input router, then add the configurable safe-area-aware touch overlay.
3. Recover the runtime rule that selects a physical CCF room during gameplay;
   do not infer it from the structurally different World `ROOM` catalog.

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
