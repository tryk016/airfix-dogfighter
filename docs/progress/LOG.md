# Engineering log

This file is append-only. Corrections receive a new entry and link to the
superseded evidence.

## 2026-07-21 — initial non-executing audit

- `EV-20260721-001`: source folder contains 33 files totaling 356,442,590 bytes.
- `EV-20260721-002`: 16 candidate code modules parsed as PE32/x86; `.mode` and
  `.type` are DLLs.
- `EV-20260721-003`: `Resource.up` and `English.up` begin with `UDSP 01 01`;
  7-Zip does not recognize them as archives.
- `EV-20260721-004`: readme reports v1.01 and DirectX 7/Direct3D plus Glide
  rendering paths.
- `EV-20260721-005`: readme says music requires the game CD; no standalone music
  files appear in the installation inventory.
- Created ADR-0001, architecture, detailed roadmap, workflow, and initial parity
  matrix.
- Initialized the local Git repository and committed the planning baseline as
  `59828ed`.
- No original binaries were executed or modified.

## 2026-07-21 — iOS product and input requirements

- Designed the semantic action/input-context architecture and deterministic
  per-tick `InputFrame` contract.
- Specified touch flight stick, latching throttle, weapons, camera, multi-touch,
  layout profiles, handedness, calibration, controller mapping/hot-plug, and
  phone/controller haptics.
- Added lifecycle failure behavior for cancellation, backgrounding, overlays,
  audio interruption, and controller loss.
- Added complete port checklist covering screen/safe area, rendering,
  performance, thermal state, audio/video, atomic saves, packaging,
  accessibility, localization, security, diagnostics, testing, and release.
- Recorded ADR-0002; no production implementation or original-binary execution
  occurred.

## 2026-07-21 — version 1.0 scope decisions

- Owner confirmed private signed sideload only and explicitly excluded App Store
  distribution.
- Set minimum deployment target to iOS 16.4.
- Recorded available Apple Developer account and iPhone 17 Pro Max as the primary
  physical device; Mac/Xcode host remains unconfirmed.
- Excluded dogfight/multiplayer, House Editor, and Paint Room from v1.0.
- Confirmed that neither the original CD image nor audio tracks are available;
  v1.0 supports absent music and must not source unofficial copies.
- Added `V1-SCOPE.md` and ADR-0003 and updated plan, requirements, and parity
  priorities.

## 2026-07-21 — GitHub Actions and device matrix

- Owner selected GitHub Actions hosted macOS/Xcode for all iOS compilation;
  local Mac/Xcode is not required.
- Kept iOS 16.4 as deployment target while defining runtime coverage only on
  iPhone 17 Pro Max/iOS 26.6 and iPhone SE 3/iOS 26.3.
- Added the compact 4.7-inch Home-button/A15 device path alongside the large
  Dynamic Island/high-refresh device path.
- Designed unsigned pull-request builds and a protected manual signed-IPA job
  with ephemeral keychain, certificate/profile secrets, verification, and
  short-lived artifacts.
- Separated CI-built data-less IPA from a locally converted private `.afpack` so
  original/converted assets never enter GitHub.
- Added ADR-0004 and `docs/ci/GITHUB-ACTIONS-IOS.md`.

## 2026-07-21 — MacBook escalation policy

- Owner confirmed that work may move to a MacBook when genuinely needed.
- Accepted two escalation thresholds: hard blocker without local Xcode/device
  tooling, or a documented estimate of at least 20% acceleration for the
  affected remaining task/milestone.
- Added a required explicit `MACBOOK GATE` notification format, measurement
  formula, likely trigger list, and hybrid Windows/Actions/Mac responsibility
  boundary.
- Current phase does not require a MacBook.

## 2026-07-21 — repository bootstrap and Phase 1 toolchain

- Connected and bootstrapped `https://github.com/tryk016/airfix-dogfighter.git`;
  published the planning baseline and subsequent scope/CI documentation on
  `main`.
- Confirmed that the remote is public. Original or converted game data remains
  excluded, and raw signed IPA artifacts will not be published.
- Installed and verified the locked static-analysis/C++ toolchain documented in
  `docs/toolchain/LOCK.md`.
- `EV-20260721-006`: LLVM identified `UdsPack.dll` as a four-section COFF-i386
  DLL with 44 named MSVC C++ exports. Exported classes expose package open/read/
  seek, file read/seek/tell, directory enumeration, and hash-table lookup.
- No original binary was executed or modified.

## 2026-07-21 — FMT-UDSP recovery and portable parser

- `EV-20260721-007`: Ghidra decompilation of `UpPackage::UpPackage`,
  `UpPackage::Open`, `UpHashTable::Lookup`, and `UpFile::UpFile` established the
  32-byte header, 24-byte records, table relocation, lookup, and decompression
  contracts.
- `EV-20260721-008`: reconstructed the exact name hash, including the original
  signed-byte behavior required by `Maskinegevær.gti` and other localized names.
- `EV-20260721-009`: all five archives passed bounds, table, name-hash, and
  directory-range checks; all 1,911 compressed payload streams decode to their
  declared output sizes using only opcodes `0x65`, `0x66`, and `0x67`.
- `EV-20260721-010`: added a portable C++20 library, read-only `udsp-list`
  summary/verifier, decompressor output limits, malformed synthetic cases, and
  a passing CTest target.
- Added a minimal-permission CI matrix for Ubuntu 24.04, Windows Server 2025,
  and native ARM64 macOS 26; the checkout action is pinned to its immutable
  v6.0.2 commit.
- The analysis performed no original-binary execution, extraction, or mutation.

## 2026-07-21 — complete PE surface reports

- `EV-20260721-011`: generated LLVM headers, sections, imports, and exports for
  all 16 PE32/i386 modules using `tools/Inspect-Pe.ps1`.
- Confirmed the static import graph: executable → engine/CC/UDSP, engine →
  CC/UDSP, CC → UDSP, mode/type plugins → engine/CC, and renderers → CC.
- Confirmed plugin exports: `typeCreate`, `missionCreate`, and `dllCreate`
  factory seams plus version/name/multiplayer metadata.
- Raw reports remain under ignored `artifacts/pe/`; only reproducible tooling and
  durable conclusions are committed.

## 2026-07-21 — portable CI and bounded metadata I/O

- Draft PR #1 passed CMake/CTest on Ubuntu 24.04, Windows Server 2025, and native
  ARM64 macOS 26 GitHub-hosted runners.
- `EV-20260721-012`: refactored `Archive::open` to read only the 32-byte header
  and validated metadata tail. `Resource.up` now needs 130,961 bytes of metadata
  buffering rather than a full 170,642,453-byte copy.
- Kept full-file buffering isolated to the explicit tooling-only `--verify`
  path, which validates stored compression streams.
- Added a synthetic filesystem-backed test in addition to in-memory and
  malformed-input tests.

## 2026-07-21 — startup and plugin contracts

- `EV-20260721-013`: targeted Ghidra xrefs recovered type discovery
  (`game\\types\\*.type`, `nfVersion == 0x6D`, `typeCreate`), mode discovery
  (`game\\modes\\*.mode`, `missionName`, optional `bMultiplayer`,
  `missionCreate`), and renderer probing/creation (`dllVer == 0x3B`, `dllName`,
  `dllCreate`).
- `EV-20260721-014`: recovered the Windows application loop,
  `NfMain::LoadPackages`, package fallback/substitution behavior, and the
  high-level `NfMain::Open` initialization order.
- Section entropy and import/string evidence classify `Dogfighter.icd` as an
  older protected/compressed artifact; `Dogfighter.exe` is the usable v1.01
  reference bootstrap.
- Added reusable Ghidra scripts for string-xref and selected-name decompilation;
  strengthened the wrapper so a failed post-script cannot report false success.
- Designed the iOS replacement boundary as deterministic compile-time type,
  mode, and renderer registries rather than runtime Windows DLL loading.

## 2026-07-21 — private AFPACK conversion boundary

- `EV-20260721-015`: specified AFPACK v1 with fixed 64/80-byte-safe metadata,
  normalized UTF-8 paths, per-entry SHA-256, no redundant outer compression,
  a deterministic manifest, and atomic iOS import requirements.
- Implemented a portable C++20 bounded metadata parser and verifier, streaming
  SHA-256 with published known-vector tests, deterministic writer, malformed
  pack cases, traversal rejection, corruption detection, and refusal to replace
  an existing output.
- Added an allowlisted `afpack-create` CLI. It accepts only `Resource.up` and one
  known localization selected from the installation root; original Windows
  executables and plugins cannot be supplied as arbitrary inputs.
- Extended the UDSP parser with bounded containing-file regions so nested
  archives are validated directly inside AFPACK without extracting them.
- `EV-20260721-016`: produced a temporary English pack from the owner's
  read-only installation, streamed and reverified all content digests, reopened
  both nested archives (2,628 resource and 218 localization records), and
  confirmed a 192,755,765-byte/3-entry result. The temporary private pack was
  removed after validation and no original data entered Git or CI.

## 2026-07-21 — GTI/CCF asset surface

- `EV-20260721-017`: added bounded full-entry and decoded-prefix reads, then
  inventoried `Resource.up` plus `English.up` by extension, size, compression,
  decoded magic, and format metadata without extracting an asset tree.
- The selected pair contains 1,985 GTI textures (155,924,867 decoded bytes),
  285 `.ccf` names plus one extension-less CCF scene (53,826,365 bytes total),
  215 object definitions, 104 WAV effects, 52 mission AFS files, and the
  world/level/UI support formats recorded in `ASSET-INVENTORY.md`.
- `EV-20260721-018`: Ghidra recovered `GtImage` v4 chunk selection,
  bits-per-pixel, mip-size and alpha/palette behavior plus `CcChunkReadStream`'s
  inclusive 6-byte framing and `CcRoom::LoadSceneCcf` section dispatch.
- Implemented portable GTI/CCF metadata parsers and malformed-input tests. All
  1,985 GTI files (3,990 variants) and all 286 CCF signatures pass; 12 GTIs use
  a documented terminal declared-size overrun while still containing complete
  computed mip data.
- All selected CCF roots contain ordered sections `0x1000`, `0x2000`, `0x3000`,
  `0x4000`, corresponding to rooms/spatial data, materials, meshes/blueprints,
  and placed scene nodes.
- A Windows CI-only AFPACK test crash was traced to 1 MiB stack buffers under
  MSVC's default stack reserve. Reduced both streaming buffers to 64 KiB; local
  tests pass and commit `c002182` subsequently passed Ubuntu, Windows, and
  ARM64 macOS CI.

## 2026-07-21 — first decoded texture

- `EV-20260721-019`: implemented base-level RGBA8 conversion for observed GTI
  formats 3 (P8), 4 (P8+A8), 6 (ARGB4444), 7 (RGB888), and 8 (ARGB8888), with
  exact synthetic channel/alpha fixtures and palette-index bounds.
- Inventory decoding exercised all 3,990 GTI variants in Resource/English with
  caller-bounded per-entry memory and no persistent extraction.
- Added a private `gti-preview` diagnostic. A 256x128 aircraft frontend image
  decoded with correct orientation/colors; the paired format-4 and format-8
  paths differ by mean absolute RGB 1.70/1.56/1.69, consistent with expected
  palette quantization.
- Generated PPM/PNG previews remain under ignored `artifacts/preview/` and are
  never committed or uploaded.

## 2026-07-21 — CCF direct-child census

- `EV-20260721-020`: extended the bounded CCF parser through each top-level
  section's complete direct-child sequence and revalidated all 286 scenes.
- Direct IDs are `0x1100` under rooms and `0x2100` under materials. Blueprint
  and placed-node sections use primary `0x3100`/`0x4100` IDs plus optional
  `0x4200` and `0x4300` records.
- Across the corpus there are 392 room, 10,385 material, and 9,328 records in
  each of the blueprint and placed-node tables. The latter counts match in
  every file; the parser records this evidence without yet imposing a semantic
  one-to-one invariant.

## 2026-07-21 — first green iOS application builds

- `EV-20260721-021`: added a data-less Objective-C++/UIKit/Metal application
  shell generated with CMake, plus a portable `AppSession` lifecycle/content
  state machine and five cross-platform test targets including its lifecycle
  fixtures.
- Added a public-source boundary check and an unsigned `macos-26` workflow
  pinned to Xcode 26.6. It builds separate ARM64 `iphoneos` and
  `iphonesimulator` bundles and verifies platform, architecture, minimum iOS
  16.4, and absence of original/private/signing file types without uploading
  the application.
- Run `29898694161` passed both Apple jobs (45 seconds device, 66 seconds
  simulator) alongside the existing Ubuntu, Windows, and ARM64 macOS tests.
- The first AppleClang passes exposed a delegate property placement error and a
  missing explicit CoreGraphics link. Both were isolated from logs and fixed in
  focused commits before the green run.
- The repository remains public and therefore supports unsigned CI only.
  Signing credentials, profiles, device identifiers, signed IPA, and AFPACK
  data remain outside GitHub until a private signing boundary is selected.

## 2026-07-21 — AfChunkContainer and object dependencies

- `EV-20260721-022`: recovered the separate FourCC/u32-size container used by
  world, level, object, briefing, and path files. Child sizes exclude their
  eight-byte headers; duplicate IDs, zero payloads, and odd sizes are valid.
- A bounded portable parser validated all 299 selected files and 7,376 chunks.
  Added a semantic parser for all 215 `MODL`/`OBJE` definitions, preserving raw
  localization references and unknown chunks while enforcing exact strings,
  12-byte `GRAV`, zero-byte `HIDE`, and singleton known fields.
- Ghidra established that object `MESH` is the blueprint name resolved inside
  the referenced `CCFF`; only `PuKeyBrown.object` omits it. The community object
  manual's example lengths and counts agree with the corpus, while its claim
  that `OBJE` lacks `CATG` does not (111 of 142 have it).

## 2026-07-21 — CCF material dependencies

- `EV-20260721-023`: recovered the complete observed `0x2100` material layout,
  implemented strict bounded metadata parsing, and revalidated all 286 CCF
  files/10,385 materials directly inside `Resource.up`.
- Materials expose 10,256 primary and 263 environment texture references. The
  original loader also accepts a secondary texture property, but none occurs in
  the selected corpus. All records contain the four mandatory fixed-shape
  property chunks in one of four exact optional-texture sequences.
- Corpus validation corrected the initial string model: 5,043 empty material
  prefixes use a zero-byte `CcString` representation, whereas non-empty strings
  include their terminal NUL. Added an explicit synthetic regression fixture.
- The public repository records only parser code and aggregate facts. No CCF,
  GTI, extracted name list, or derived proprietary asset was written or
  uploaded.
- Commit `e28bcf8` passed portable CI run `29900887675` and unsigned iOS run
  `29900887609` on the existing public, data-less build boundary.

## 2026-07-21 — external `cc-tools` audit

- `EV-20260721-024`: audited the user-supplied CC0-1.0 project
  [`RonnyReverse/cc-tools`](https://github.com/RonnyReverse/cc-tools/tree/e34efcd858ec4475fa03d3f8668fa4e26f9e780e)
  at commit `e34efcd858ec4475fa03d3f8668fa4e26f9e780e`.
- Its Kaitai schemas independently agree with the recovered UDSP, GTI,
  AfChunk, CCF material, and candidate mesh shapes. They are used as hypotheses,
  not vendored as a dependency: CCF semantics are incomplete, resource limits
  are absent, and its generated Python/Pillow stack is old.
- The repository contains no visible original or derived game assets. CC0
  covers its code and schemas but grants no rights to Airfix Dogfighter data.
- The audit exposed one naming discrepancy in `CcName`; a known corpus sample
  plus loader order confirms the local first-string `name`, second-string
  `prefix` interpretation. It also identifies unobserved material `0x2153` and
  mesh helper IDs as candidates that remain unknown until Airfix evidence
  confirms them.

## 2026-07-21 — CCF mesh geometry

- `EV-20260721-025`: implemented the loader-confirmed `0x3100` fixed prefix,
  nested transform shape, vertex/triangle tables, UVs, paint metadata, and mesh
  control leaves with checked bounds, exact known sizes, count limits, and
  forward-compatible unknown-chunk retention.
- Revalidated all 286 CCF files directly inside `Resource.up`: 6,995 meshes,
  136,363 vertices, 170,039 triangles, 168,419 UV records, 95,424 paint records,
  and 3,428 optional range/control records all pass. Every triangle index is in
  the final vertex-table range.
- Synthetic fixtures cover the fixed transform, optional vertex vector,
  material reference, three-color paint, UV coordinates, control leaves,
  opaque/extended paint, unknown mesh extensions, an out-of-range index
  failure, and a nested descriptor-allocation bomb stopped by the shared global
  budget.
- Diagnostic inventory output contains counts only. It neither extracts nor
  writes mesh names, positions, indices, textures, or other proprietary data.
- Commit `3c50eb8` passed portable CI run `29902026372` and unsigned iOS run
  `29902026361` on the public, data-less build boundary.

## 2026-07-21 — bounded UDSP logical lookup

- `EV-20260721-026`: implemented logical path normalization and the original
  directory-then-file-range hash lookup, followed by full bytewise legacy
  case-insensitive comparison for every equal-hash candidate.
- Lookup returns `notFound`, `unique`, or `ambiguous`; it never accepts hash
  equality alone or silently selects a duplicate. Mixed slash/case, missing,
  duplicate, length-limit, absolute/drive, empty-component, and traversal cases
  have synthetic fixtures.
- The API resolves archive metadata only. It performs no host-filesystem
  canonicalization, extension guessing, package-layer precedence, or writes.

## 2026-07-21 — object-to-CCF dependency resolution

- `EV-20260721-027`: added the common bounded `0x4200` null/`0x4300` light
  blueprint prefix and an ordered 9,328-entry CCF blueprint index, then joined
  object `CCFF`/`MESH` through CCF-local names and triangle material references.
- `udsp-list --resolve-objects` validates all 215 definitions directly against
  `Resource.up`: 90 select meshes, 124 nulls, one has no selector, and 14 require
  ASCII case folding. All 215 CCF paths resolve uniquely.
- The 90 selected meshes resolve 212 stable per-object material uses and 210
  typed primary/secondary/environment texture edges with no missing or
  ambiguous material references.
- The resolver returns typed missing/ambiguous/limit issues, never guesses a
  first blueprint for the no-selector case, and emits aggregate counts only.
- Commit `8192aa2` passed portable CI run `29903498078` and unsigned iOS run
  `29903498076` on the public, data-less build boundary.

## 2026-07-21 — deterministic portable input core

- `EV-20260721-028`: implemented stable semantic action and axis IDs, signed
  Q15 values, a fixed-capacity sequence-ordered event queue, and versioned
  per-tick `InputFrame` output independent of platform timestamps.
- Multi-source buttons use logical OR, analog axes use latest-meaningful-source
  ownership with drift rejection, and touch/controller/keyboard defaults cover
  the first gameplay and menu action set.
- Source loss and context changes synthesize only the required releases.
  Lifecycle reset clears transient state and admits gameplay only after two
  consecutive neutral ticks, preventing stuck fire or movement on resume.
- Unit fixtures cover press/hold/release, taps between ticks, event ordering,
  source handoff/fallback, drift, disconnect/reconnect neutral gating, context
  blocking, strict queue limits, invalid/unbound inputs, timestamp independence,
  and discrete weapon selection.
- The iOS shell remains data-less and platform adapters are not claimed here;
  UIKit touch capture and Apple Game Controller integration are the next layer.
- Commit `0a62a92` passed portable CI run `29904638685` and unsigned iOS run
  `29904638715`, including compilation of the portable input target for iOS.

## 2026-07-21 — explicit no-music package configuration

- `EV-20260721-029`: the deterministic AFPACK test now opens and verifies a
  generated package, reads its manifest payload, and requires
  `capabilities.music` to be `false` with no contradictory `true` declaration.
- This makes the accepted missing-CD configuration executable evidence at the
  converter/package boundary. Runtime audio fallback remains future audio work;
  no replacement soundtrack is sourced or bundled.
- Commit `4483bd8` passed portable CI run `29905345959` and unsigned iOS run
  `29905345976` on the public, data-less build boundary.

## 2026-07-21 — coordinate and winding conversion contract

- `EV-20260721-030`: recovered a right-handed `+X` right, `+Y` up, `+Z`
  forward source basis, legacy row-vector matrix application, the reverse-cross
  face-normal order, degenerate `+Y` fallback, and index-swap behavior from
  independent `Cc.dll` functions.
- Added the API-neutral `airfix::render` conversion layer. It applies
  `B * transpose(raw) * inverse(B)`, transforms positions with an explicit
  unit scale, preserves raw UVs by default, swaps winding exactly once under a
  reflected basis, and recomputes normalized legacy-compatible face normals.
- Synthetic tests include asymmetric/noncommuting matrices, the independent
  property `runtime(B*v) = B*legacy(v)`, non-unit and degenerate triangles,
  reflection with corner-attached UVs, inverse-transpose fallback under shear,
  explicit V flip, and malformed inputs.
- Read-only inventory converted all 6,995 meshes and retained only aggregate
  counts; no proprietary geometry was exported or committed.

## 2026-07-21 — complete GTI mip chains and Metal upload policy

- `EV-20260721-031`: added bounded per-level layout/decoding and strict owned
  RGBA8 chains for formats 3/4/6/7/8, retaining the original quarter-area byte
  offsets and validating palette, source, output, and aggregate limits.
- Across 3,990 variants, 3,974 exact authored chains provide 9,523 uploadable
  levels. Sixteen legacy dimension anomalies safely decode level zero and use
  runtime generation; inventory decoded 9,539 levels in total.
- The first Metal contract is straight-alpha `MTLPixelFormatRGBA8Unorm` with no
  implicit row/V flip. Exact levels are copied individually; anomalous chains
  allocate the natural level count and use `generateMipmaps(for:)` after the
  base upload. sRGB selection remains evidence-gated.
- Full GCC `-Werror` build and all nine test targets pass. The corpus validator
  also converted every CCF mesh and selected an explicit mip policy for every
  GTI variant without writing original or derived assets.
- Commit `d931271` passed portable CI run `29907055317` on Ubuntu, Windows, and
  ARM64 macOS plus unsigned iOS run `29907055262` for `iphoneos` and
  `iphonesimulator` on Xcode 26.6.

## 2026-07-21 — resolved textures and first private model

- `EV-20260721-032`: decompiled `GtTextureGroup::AddSearchPath` and
  `GtTextureGroup::AddTexture` at RVAs `0x00046BB0`/`0x00046C30`. Ordinary CCF
  material textures use exactly `TEXU\\source.gti`; `.tga` is a separate
  boolean path, not a fallback guessed by the portable resolver.
- Added typed bounded lookup from direct selected-mesh-root texture edges to
  UDSP entries. Read-only validation resolves 210/210 edges uniquely, with zero
  missing roots, unsafe paths, missing files, or ambiguities.
- Added deterministic seam splitting for per-corner UVs/flat normals, stable
  first-use materials, contiguous source-order draw ranges, 32-bit indices,
  local bounds, and aggregate limits in the backend-neutral draw payload.
- Added a bounded deterministic CPU rasterizer and private `model-preview`
  tool. The first mesh-backed object rendered a complete textured silhouette:
  111 triangles, 291 draw vertices, six ranges, three materials, and two
  primary textures. Raw-V and explicit flipped-V previews both remain under
  ignored `artifacts/private-model-preview/`; no private image or hash is in Git.
- The selected aircraft object resolves to a null group, confirming that a
  faithful complete aircraft requires its blueprint hierarchy and transforms
  rather than choosing an arbitrary child mesh. This is the next asset task,
  not a MacBook blocker.
- Commit `ee6229f` passed portable CI run `29911160578` on Ubuntu, Windows, and
  ARM64 macOS plus unsigned iOS run `29911160555` for `iphoneos` and
  `iphonesimulator` on Xcode 26.6.

## 2026-07-21 — blueprint hierarchy and recursive instancing

- `EV-20260721-033`: static analysis separates the `0x3000` blueprint/prototype
  graph from the independent `0x4000` already-placed scene graph. Equal table
  counts do not establish an index mapping between them.
- The 9,328 blueprint nodes resolve into 2,062 roots and 7,266 ordered parent
  edges with maximum depth 7. Full-corpus validation finds zero missing parents,
  duplicate references, or cycles.
- Blueprint transforms are authored in world space. `CcSrtNode::SetParent` at
  RVA `0x0000E0F0` preserves the child's world relation while deriving its
  parent-relative local transform; `GetWorldRelation` at `0x0000E440` composes
  parent first, using `CcSRT::InheritParentSRT` at `0x0002BFD0`.
- The object path loads its CCF with flag `0x2000`, suppressing `0x4000`, then
  selects a `0x3000` blueprint by case-folded `MESH` name and calls
  `CcBlueprint::MakeInstance(..., true)` at RVA `0x000244F0`. This instances the
  selected node and descendants only; a null root is a valid transform group.
- Complete read-only traversal of all 215 selectors covers 2,687 selected
  blueprint nodes, 1,858 mesh instances, 2,842 material uses, and 2,959 texture
  edges. All 2,959 texture paths resolve uniquely with zero missing, invalid, or
  ambiguous graph, material, or texture dependencies. Root selection remains
  90 mesh, 124 null, and one no-selector case. The prior 212/210 counts covered
  directly selected mesh roots only.
- A private grouped-aircraft diagnostic contains 46 nodes, 25 mesh instances,
  21 null groups, 663 triangles, and maximum subtree depth 3. Only anonymous
  aggregates are public; no name, path, hash, geometry, or image was retained.
- Implemented an iterative, depth-bounded graph resolver, complete-subtree
  material/texture resolution, a multi-instance draw-model payload, and one
  shared auto-fit/z-buffer diagnostic. The private 46-node aircraft renders as
  one coherent assembly; a repeat is byte-identical and exclusive output
  creation refuses overwrite.
- Visual comparison locks raw V as the current CCF/GTI mapping policy for this
  path: the explicit flipped-V variant corrupts aircraft markings. Both private
  variants remain ignored and outside Git.
- Commit `9776d1c` passed portable CI run `29913419442` on Ubuntu, Windows, and
  ARM64 macOS plus unsigned iOS run `29913419463` for `iphoneos` and
  `iphonesimulator` on Xcode 26.6.

## 2026-07-21 — parent-relative transform math

- Implemented conversion of authored CCF SRT values into the runtime
  column-vector convention plus exact parent-world/child-world to local
  derivation and parent/local recomposition.
- Synthetic non-commutative rotation, translation, shear, basis conversion,
  singular, non-finite, and round-trip tests pass. `rawScalar` remains preserved
  metadata and is never applied as an inferred scale.
- Commit `cc0e10f` passed portable CI run `29914087067` on Ubuntu, Windows, and
  ARM64 macOS plus unsigned iOS run `29914087120` for `iphoneos` and
  `iphonesimulator` on Xcode 26.6.

## 2026-07-21 — bounded AFPACK reads

- Added configurable limits for total archive size, metadata, entry count,
  logical-path bytes, string table, and individual payloads. File-backed open
  rejects oversized declarations before allocating or reading metadata.
- Added a caller-bounded `Pack::readEntry` path. It verifies both the physical
  archive size and the selected entry SHA-256 before returning bytes, including
  rejection of a same-size payload replacement discovered during review.
- Synthetic boundary tests cover every exact ceiling and one-byte/count-beyond
  rejection, pre-read metadata rejection, bounded reads, changed source size,
  and same-size payload mutation. Full portable GCC tests pass 13/13.

## 2026-07-21 — public DrawModel Metal smoke path

- `EV-20260721-034`: added an Objective-C++ `MTKView` renderer that consumes the
  exported `airfix::render::DrawModelPayload` rather than a backend-local model.
  The public fixture has one shared mesh, two instances, two ordered ranges, and
  a generated 2×2 RGBA8 texture; no original or derived game data is present.
- Vertices and transforms are explicitly repacked into a fixed Metal ABI. The
  smoke pipeline uses BGRA8 color, Depth32Float less/write, UInt32 indices,
  nearest/clamp sampling, no blending, and no culling.
- CMake compiles `AirfixShaders.metal` offline into `default.metallib`, and the
  iOS bundle verifier now requires a non-empty library. Independent source
  review found no P1/P2; Xcode 26.6 Actions remains the authoritative compiler.
- Initial iOS run `29915449239` compiled and linked Objective-C++ for both SDKs,
  then exposed a multi-configuration path expansion bug in the custom shader
  post-build command. Run `29918090023` then proved CMake did not place the
  otherwise unknown extension in Xcode's source phase. The build now invokes a
  bounded CMake helper after link and reads Xcode's real `TARGET_BUILD_DIR` and
  `WRAPPER_NAME`; bundle verification remains fail-closed.
- Commit `1b055fa` passed portable CI run `29918440818` on Ubuntu, Windows, and
  ARM64 macOS plus unsigned iOS run `29918440831` for `iphoneos` and
  `iphonesimulator`; both application bundles contain non-empty
  `default.metallib` output from Xcode 26.6.

## 2026-07-21 — strict AFPACK manifest semantics

- Added a bounded portable JSON parser and typed manifest v1 model. It validates
  UTF-8 and surrogate pairs, integer overflow, duplicate keys, exact known
  fields, and configurable input/string/item/depth/entry ceilings.
- Semantic validation now requires Airfix Dogfighter 1.01, one of the four
  supported locales, no music/multiplayer/editors, and exactly Resource plus the
  matching localization archive. Path, kind, size, and lowercase SHA-256 must
  agree one-to-one with the already validated AFPACK entry table.
- Synthetic tests cover the canonical writer shape, all four locales, schema,
  version, game, capabilities, allowlist/table disagreements, digest spelling,
  malformed Unicode/JSON, trailing bytes, numeric overflow, and parser limits.

## 2026-07-21 — bounded nested UDSP metadata

- Added one shared `ParseLimits` policy to `Archive::parse`, `open`, and
  `openRegion`. Archive/metadata sizes, directory/file counts, string/name
  bytes, per-entry stored/unpacked sizes, and checked aggregate unpacked size
  are validated before the related read, reserve, or string allocation.
- Exact/+1 synthetic cases cover all ten ceilings, including aliasing records
  that repeatedly reference the same decoded name, a 1 MiB file-backed metadata
  tail, a two-entry aggregate wider than 32 bits, and a bounded region inside a
  containing file. Existing UDSP behavior remains source-compatible.
- The read-only real `Resource.up` passes the default policy: 170,642,453 bytes,
  130,961 metadata bytes, 478 directories, 2,628 files, and 191,873,346 total
  declared unpacked bytes. No source payload entered the repository.
- Commit `b37f7a6` passed portable CI run `29919436812` on Ubuntu, Windows, and
  ARM64 macOS plus unsigned iOS run `29919436787` for `iphoneos` and
  `iphonesimulator` on Xcode 26.6.

## 2026-07-21 — AFPACK validation and activation transaction foundation

- Added a bounded validation gate that keeps one staged-file handle for AFPACK
  metadata, manifest, and nested UDSP parsing. A fresh final binding pass hashes
  the metadata and every payload again, so same-size pathname replacement or
  in-place mutation cannot produce a mixed accepted result.
- Added canonical binary AFAC v1 active records with one rollback reference,
  checked generation increments, bounded sizes, exact flags/layout, and
  digest-derived package filenames. No caller-controlled path is persisted.
- Added Windows and POSIX/iOS durability primitives for exclusive creation,
  replace, and no-replace publication. They validate regular-file identity,
  reject path aliases, fail closed on source substitution, and document their
  app-private, serialized transaction boundary.
- Synthetic tests cover malformed limits, progress/cancellation, payload
  mutation during validation, AFAC round trips/overflow, publication collision,
  source substitution, relative/absolute aliases, and replacement rollback.
  Independent reviews found and closed the initial handle/path TOCTOU and unsafe
  cleanup cases. The full local GCC suite passes 18/18 and the public-boundary
  scanner passes 121 files.
- Commit `a539389` passed portable CI run `29921712751` on Ubuntu, Windows, and
  ARM64 macOS plus unsigned iOS run `29921712749` for `iphoneos` and
  `iphonesimulator` on Xcode 26.6.

## 2026-07-21 — composed private AFPACK installer

- Composed exclusive staging, bounded source copy, copy-vs-disk SHA-256,
  stable-source AFPACK/manifest/nested-UDSP validation, post-validation digest
  confirmation, content-addressed no-replace publication, AFAC rotation, and
  idempotent reimport into one portable installer transaction.
- Existing digest-named packs are reused only after physical size, complete
  digest, full semantic validation, and a second digest bind all pass. A corrupt
  collision is preserved for recovery and never overwritten.
- Cancellation remains effective through copy/validation and immediately before
  the durable AFAC temp is committed. Commit/complete observers are best-effort
  and cannot invalidate a completed activation.
- Post-rename errors require exact AFAC readback plus successful retry of file
  and directory synchronization. Any unreadable/mismatched or non-durable
  outcome raises `InstallCommitUnknown` with the requested generation.
- Synthetic first install, rotation/previous, idempotence, invalid/cancelled
  preservation, malformed active, limits/UUID, corrupt final collision,
  progress, callback, post-rename, readback, and durability-retry faults pass.
  Independent review closed the post-rename durability gap; local suite passes
  19/19 and the public-boundary scanner passes 125 files.

## 2026-07-21 — world, level, briefing, and path semantics

- `EV-20260721-035`: implemented bounded semantic parsers for `HOUS`, stable `FHOU` `OBJE`
  placements, all confirmed `BRIF` strings, and signed-int16 `PATH` deltas.
  Variable level `MODL`/`IAOB` chunks remain preserved descriptors.
- Read-only corpus validation passes 29 worlds, 493 rooms, 522 ordinal radar
  lists/47,589 line records, 31 levels/2,444 static placements, 23 briefings,
  and one path/833 delta records with zero errors.
- One world contains two different valid `BCKD` chunks. Loader evidence proves
  first-match behavior, so the parser validates and retains the ignored second
  descriptor instead of rejecting shipped content under the earlier singleton
  assumption.
- Independent review found no production P1/P2 and strengthened tests for
  non-adjacent ordinal LLST/LDAT pairing plus exact preservation of unknown and
  ignored chunk descriptors/payloads.

## 2026-07-21 — public project overview and path hygiene

- Replaced the engineering-notebook README with a first-visit project page
  covering scope, legal/private-content assumptions, current maturity, device
  matrix, native reconstruction architecture, touch/controller design, build
  path, iOS CI boundary, repository map, and contribution rules.
- Removed the owner workstation's original-data path from every tracked
  document and tool default. Inspection scripts now require an explicit source
  root; the iOS bundle check rejects source-library path patterns without
  embedding one workstation-specific value.
- Commit `ccd450a` published the documentation/path-hygiene change. Synthetic
  drive-qualified strings remain only as deliberate traversal/path rejection
  fixtures.

## 2026-07-21 — portable content recovery and verified rollback

- Added read-only startup inspection that derives candidate names exclusively
  from AFAC digests and verifies regular-file type, exact size, full pack hash,
  AFPACK metadata/manifest semantics, and both nested UDSP regions.
- Typed states distinguish first-launch no-content, ready, rollback available,
  unusable, malformed active, and transient unavailable. A temporary I/O error
  never becomes a rollback recommendation.
- Rollback accepts only a verified prior inspection, re-authenticates the
  previous pack, performs two exact stale-AFAC checks, rotates through a new
  generation, and requires exact readback plus durable resynchronization after
  post-rename ambiguity.
- Cancellation/progress remain bounded; the commit tail is non-cancellable and
  phases are monotonic. Synthetic filesystem, corruption, stale-state, fault,
  and durability tests passed independent P1/P2 review with no remaining issue.

## 2026-07-21 — complete observed FHOU model/state records

- `EV-20260721-036`: targeted Ghidra address exports recovered the `MODL`
  writer/reader and five `IAOB` serializer variants in `Dogfighter.exe`.
  `MODL` is the ordinary six-float/room/object prefix followed by three ordered
  string/u32 pairs and one loader-optional compatibility word.
- `IAOB` first stores two identity strings used for runtime matching and then a
  class-specific u32 tail. Read-only corpus validation establishes exactly 85
  one-word, 23 two-word, and 64 eight-word records: 172 records and 643 words.
- The bounded parser now validates all 31 levels: 2,444 static placements,
  1,176 model placements, 172 instance-state records, and zero unknown chunks.
  State fields remain neutrally named until their gameplay meanings are proven.
- Added a metadata-only resolver for the level world plus all static and model
  definition paths. It preserves transforms/state/order, reports explicit
  lookup outcomes, enforces aggregate placement/path limits, and performs no
  payload read or extension fallback. All 31 world references, 2,444 static
  references, and 1,176 model references resolve uniquely with zero issues.

## 2026-07-21 — native iOS private-content gate

- Added a serial native coordinator over the portable installer and recovery
  service. It presents the iOS document picker, copies at most 512 MiB through
  a security-scoped URL into an exclusive app-private file, then releases the
  external scope before AFPACK validation and activation.
- Startup inspection, import, verified rollback, lifecycle cancellation, and
  bounded progress feed one safe-area/Dynamic-Type UI and the portable
  `AppSession` content gate. Errors contain no source paths, filenames, hashes,
  or package metadata.
- Independent review closed loss of rollback UI after picker cancellation,
  stale readiness after rejected imports, and force-kill orphan retention.
  Cleanup recognizes only lowercase canonical-UUID temporaries, removes only
  regular non-link files in three trusted direct directories, and never scans
  digest-named packs.

## 2026-07-21 — placed CCF scene records

- `EV-20260721-037`: targeted Cc.dll exports confirm the F010 two-string name,
  nested string, F030/F040 vector, orientation, and opaque BSP helper paths used
  by `CcRoom::LoadSceneCcf`.
- Implemented bounded `0x4000` object/null/light record decoding with physical
  order, exact common transforms and references, strict known singleton shapes,
  zero-copy `0x4210`, optional light strings, and opaque `0x4101` preservation.
- Read-only corpus validation parses all 286 CCF scenes and 9,328 placed nodes:
  6,995 objects, 2,130 nulls, and 203 lights. All shipped records use F050
  matrices; the loader-supported alternate F040 orientation remains synthetic-
  tested. Independent review found no P1/P2.

## 2026-07-21 — CCF rooms and placed-scene references

- `EV-20260721-038`: decoded the bounded `0x1100` name/reference prefix and
  marked the first physical record as the receiver/root-room binding. All 392
  rooms across 286 CCF files parse with no zero or duplicate references.
- Implemented bounded iterative placed parent, mesh, ordinary-room, and portal-
  room resolution, including skipped missing-mesh objects, explicit receiver
  fallback, mesh-prototype parents, stable child order, ambiguity rejection,
  cycle/depth checks, and allocation/edge limits.
- Read-only census yields 9,328 instantiated nodes, 2,062 roots, 7,266 edges,
  maximum depth 7, and zero missing, ambiguous, or cyclic dependencies. All
  6,995 object meshes and 154 nonzero portal rooms resolve uniquely.
- Independent review closed three P2 gaps: malformed graph relationships are
  now cleared atomically, every node-kind/variant pair is checked, and objects
  skipped for a missing or ambiguous mesh perform no room/portal resolution.
  Re-review found no remaining P1/P2.

## 2026-07-23 — CCF room fog, BSP, and spatial bindings

- `EV-20260721-039`: recovered the exact room fog, static/portal BSP node, and
  polygon-descriptor loaders plus the deferred polygon-to-object resolution
  path from `Cc.dll`.
- Implemented an iterative flat-arena parser for `0x1101`, `0x1200`, `0x1201`,
  direct `0xF0C0`, and exact `0xF0C1` descriptors. Depth, tree, node, polygon,
  and shared descriptor limits are enforced before dependent growth; legacy
  nonzero child flags and the shipped quiet-NaN face-normal sentinel are
  preserved.
- Added a fail-closed spatial resolver that joins room BSP descriptors through
  the canonical placed scene to a unique object, mesh, triangle, ordinary room,
  and portal target without copying geometry. Independent review caused the
  resolver to own its placed-scene resolution and to preflight raw polygon
  arenas, closing stale-resolution and allocation-limit findings.
- Read-only validation passes all 286 CCF scenes: 392 disabled fog records,
  98,095 nodes, and 198,210 resolved polygon bindings, including 332 portal
  bindings. No missing, ambiguous, cross-room, out-of-range, or portal-target
  issue was found.
- BSP is confirmed as a spatial index rather than a geometry source. The next
  renderer step is a conservative draw-all assembly for one room; culling and
  portal traversal remain disabled until their traversal semantics are proven.

## 2026-07-23 — conservative first-room draw assembly

- Added an assets-only first receiver/root-room plan that internally resolves
  the canonical placed scene, retains physical object order, includes the
  loader-compatible receiver fallback, deduplicates meshes/materials by first
  use, and publishes primary/secondary/environment texture dependencies.
- Added a backend-neutral bounded builder that shares mesh payloads and emits
  one instance per selected object. Placed F050 transforms use their own
  authored-world relation exactly once; cross-room parents and mesh-prototype
  transforms do not alter the instance result.
- The plan never consults room BSP for visibility. Synthetic partial/malformed
  spatial metadata cannot hide, duplicate, or reorder geometry. F040 placed
  orientation remains unsupported and fails closed rather than receiving an
  invented matrix meaning.
- Per-mesh and aggregate mesh, instance, material-binding, vertex, index,
  material, range, and byte limits precede dependent growth. Missing or
  duplicate materials, invalid placed graphs, non-finite/singular transforms,
  unsupported orientation, overflow, and draw conversion errors publish an
  empty model with typed diagnostics.
- Read-only validation builds every first room in all 286 scenes: 2,084
  instances, 2,084 per-scene unique meshes, 3,120 material uses, 3,272 texture
  edges, 123,121 draw vertices, 140,943 indices/46,981 triangles, and 6,435
  ranges with zero issue. No private payload or derived geometry is written.

## 2026-07-23 — runtime texture identity and upload planning

- Generalized the recovered `TEXU\\source.gti` lookup without changing the
  object wrappers. World resolution verifies `CCFF` against the supplied CCF
  source entry and internally derives the canonical room dependencies; it does
  not accept an unrelated dependency span.
- Added fail-closed runtime binding that validates the complete upstream
  resolution one-to-one, preserves material order and three texture roles, and
  assigns dense first-use IDs while deduplicating shared archive entries.
- Added backend-neutral GTI upload metadata with recovered variant preference,
  authored-chain versus base-plus-generated-mips policy, parser-hard
  dimension/mip ceilings, checked row/byte arithmetic, and independent
  decoded/upload/resident budgets.
- Two independent reviews identified and closed stale-dependency, provenance
  overclaim, upstream-error erasure, and relaxable-hard-bound findings. The
  portable suite passes 27/27 tests.
- Read-only object-corpus validation resolves 2,959 edges into 614 per-object
  unique imports: 612 authored and two generated, 2,355 uploaded and 2,368
  allocated levels, with no resolution or upload-plan issue.
- All 29 worlds bind uniquely to CCF metadata. Their 135 rooms and 6,318 placed
  nodes yield zero objects in the first receiver/root binding, proving that the
  next world pass must select an actual room explicitly rather than assume the
  receiver owns visible geometry.

## 2026-07-23 — explicit physical CCF-room selection

- Generalized the assets-only plan, backend-neutral assembly, and world texture
  resolver to accept an explicit physical `CcfMetadata::rooms` index. The
  first-room entry points remain thin room-zero compatibility wrappers.
- Receiver fallback remains exclusive to physical CCF room zero. Ordinary
  rooms select only placed objects whose resolved room index matches exactly;
  nulls, lights, cross-room parents, and incomplete BSP do not alter selection.
  Out-of-range indices, including `SIZE_MAX`, fail closed with the requested
  index retained in typed diagnostics.
- The inventory now validates every room and maintains a per-placed-node
  coverage bitmap. A selected object must occur exactly once across all plans,
  while nulls and lights must occur zero times. Aggregate counters use checked
  `uint64_t` addition.
- Read-only validation covers all 392 rooms in 286 CCFs: all 6,995 objects
  appear exactly once, yielding 6,995 instances, 10,461 material bindings,
  10,519 texture edges, 170,039 triangles, and 20,211 ranges with no issue.
- The 29 world CCFs contain 135 rooms. Exactly 105 ordinary rooms are non-empty;
  all 29 receiver rooms and one ordinary room are valid empty plans. Their
  4,911 objects appear exactly once and receiver/fallback instance count is
  zero.
- Per-room world texture plans validate 7,247 edges and 2,637 first-use import
  requests: 2,629 authored and eight generated, with 9,165 uploaded and 9,205
  allocated mip levels. These are per-room aggregates rather than a global
  runtime cache.
- Corpus structure disproves an implicit World `ROOM` to CCF-room join: the
  catalogs have different counts and no proven position, ID, reference, or
  name key. The public parameter is therefore named `ccfRoomIndex`; BSP
  visibility remains disabled.
- Independent review found no P1/P2 in the explicit-room APIs. The portable
  suite remains green at 27/27 tests, and no private payload was written.

## 2026-07-23 — atomic texture data and draw submission

- Added `prepareGtiUpload` as the bounded owned-data transaction above
  `describeGtiUpload`. It rejects oversized source bytes before parsing,
  applies variant and metadata-record budgets inside the parser before each
  retained record, composes GTI parse/plan/decode, and materializes every
  authored mip or only the valid base level for legacy generated-chain
  anomalies.
- `GtiUploadPreparation` publishes its plan and RGBA8 upload levels atomically.
  Request identity, selected variant/format/checksum, mip-policy shape,
  per-level dimensions/row/image bytes, and decoded/upload/resident totals must
  all agree. Typed parse, plan, decode, mismatch, limit, or overflow issues
  clear both outputs; a later corrupt authored mip cannot leak a partial chain.
- Added fail-closed `buildDrawSubmissionPlan`. Aggregate mesh, instance,
  vertex, index, material, range, command, and source-byte limits are
  preflighted before output allocation. Complete validation covers finite
  vertices/transforms, ordered and containing bounds, empty-mesh zero bounds,
  indices, contiguous triangle ranges, material/mesh slots, texture-coordinate
  modes, and all three optional texture roles.
- Successful submission preserves mesh order and emits commands in instance-
  then-range order, retaining primary/secondary/environment bindings and valid
  texture ID zero. Any issue suppresses the entire upload/command plan with
  typed contextual diagnostics; valid empty models and meshes yield empty work.
- Added focused synthetic test targets for authored and generated GTI data,
  later-mip atomic failure, every materialization budget, deterministic shared-
  mesh commands, exact submission limits, malformed slots/ranges/transforms,
  and bounds. Independent review found and closed the finite-but-invalid AABB
  gap. Texture review found that metadata budgets were initially applied only
  after parsing; parser-level pre-allocation limits and chunk-heavy/zero-variant
  regressions closed it. Both re-reviews found no remaining P1/P2.

## 2026-07-23 — bounded multi-mesh Metal submission adapter

- Replaced the synthetic renderer's private range traversal with the
  backend-neutral `DrawSubmissionPlan`. The public fixture now contains two
  reusable meshes and three instances in deliberate mesh-slot order
  `1 -> 0 -> 1`; every command selects its model transform through
  `instanceIndex` and rebinds the matching per-mesh vertex/index resources.
- Kept the established explicit render policy: BGRA8 presentation,
  Depth32Float less/write, UInt32 indices, nearest/clamp sampling, no blending,
  and no culling. No sRGB, premultiplication, V-origin, projection, front-face,
  or visibility behavior was inferred.
- Texture asset ID zero remains a real binding. A separate one-pixel RGBA8
  fallback is bound explicitly for an absent primary texture or
  `TexcoordMode::none`, including independent synthetic coverage of both
  conditions.
- Added two-pass resource preparation. The first pass checks every vertex and
  index byte count, command offset/range, instance-to-mesh relationship,
  `device.maxBufferLength`, a 64 MiB aggregate GPU budget, and a separate
  64 MiB packed-CPU budget. Vertex repacking and Metal-buffer creation begin
  only after the complete preflight succeeds.
- Valid empty/no-draw meshes use strongly retained wrappers with nullable
  buffers instead of zero-length Metal allocations; preflight guarantees that
  a draw command can reference only populated resources. C++ allocation
  failures are converted to `NSError`, and renderer state is published only
  after the complete immutable resource snapshot and all Metal objects exist.
- Independent review approved the adapter after the empty-mesh, exception
  boundary, command consistency, and pre-allocation findings were closed.
  This checkpoint still uses only public synthetic data: AFPACK, private GTI
  textures, asset-backed room rendering, and visual testing on a physical
  iPhone remain pending.

## 2026-07-23 — single-handle UDSP entry reads

- Reworked `Archive::open(path)` to use one binary handle and added
  `Archive::open(stream, sourceLabel)`. The label is diagnostic text only:
  parsing and reads never reopen or interpret it as a filesystem path.
- Added indexed stream overloads for `readFilePrefix` and `readFile`. They
  require the same seekable stream and its exact current containing-file size,
  then constrain the parsed archive and selected stored payload to their
  declared regions before any payload I/O.
- Added a common `streamoff`/`streamsize` representability preflight before
  metadata, complete stored-payload, or uncompressed-prefix allocation. An
  independent review found the original post-allocation check and approved the
  checkpoint after the preflight and sparse virtual-stream regression closed
  it.
- Kept `path + FileEntry` overloads as compatibility conveniences. They match
  the entry back to the parsed table, but a newly opened path does not prove
  backing-object provenance. Stable-source callers must retain the stream,
  accept unspecified post-call position, and serialize access.
- Synthetic coverage includes standalone and embedded
  prefix-plus-UDSP-plus-suffix layouts, compressed/uncompressed and zero-byte
  prefixes, a poisoned label, invalid index, exact and one-under output limits,
  current-size mismatch, suffix containment, and pre-allocation stream limits.
  The full portable suite passes 29/29.
- This checkpoint is a prerequisite for the private AFPACK content bridge. It
  does not claim that installed package payloads are already decoded and fed
  through the iOS renderer end to end.

## 2026-07-23 — authenticated runtime content session

- Added the portable `airfix_content` library and a move-only
  `VerifiedContentSession`. Opening a session authenticates the exact physical
  size and full SHA-256 of one owned seekable AFPACK stream against an active
  `{generation, size, digest}` revision.
- The same handle is retained through AFPACK metadata, strict manifest, and
  exact `source/Resource.up` UDSP parsing. `sourceLabel` is diagnostic text
  only and is never reopened; subsequent source-entry reads remain serialized
  on the retained stream.
- Added reusable public-only generators for synthetic UDSP and AFPACK fixtures.
  The AFPACK fixture uses the production writer and contains no original or
  converted game content.
- Regression coverage rejects wrong size, digest, generation, malformed nested
  UDSP, invalid hash-buffer limits, and cancellation before or during hashing.
  A poisoned-label test points at a real invalid file while supplying a
  different valid already-opened file handle, proving that no label-based
  pathname reopen contributes bytes.
- The full portable suite passes 30/30. The library is linked into unsigned iOS
  builds for Apple compilation coverage, but the native coordinator and Metal
  renderer do not consume a loaded room yet.

## 2026-07-23 — atomic AFPACK-to-room loader

- Added `WorldRoomLoader`, which consumes only an authenticated
  `VerifiedContentSession`, an exact World logical path, and an explicit
  physical CCF room index. It derives every CCF and GTI archive index
  internally and never interprets World `ROOM`, BSP, or a diagnostic label as a
  hidden join, visibility rule, or host path.
- The transaction resolves and parses World/CCF, verifies exact `CCFF`
  provenance, resolves material textures, assigns dense first-use texture IDs,
  preflights and prepares GTI uploads, assembles the room model, and validates
  its backend-neutral draw submission. Only the complete result is published,
  tagged with the session revision.
- Added checked per-entry and aggregate limits for texture count, source bytes,
  decoded/upload/resident RGBA, and published CPU data. Compressed source
  accounting includes both simultaneously live stored and unpacked buffers.
  GTI metadata and upload planning run before decode so exact aggregate
  overruns fail before the RGBA allocation.
- Added reusable synthetic World, two-room CCF, triangle/material/placed-node,
  and exact 2-by-2 RGBA8 GTI fixtures. End-to-end coverage includes an empty
  receiver room, ordinary room, exact CCF selection among two entries, valid
  texture ID zero, duplicate texture-role deduplication, cancellation, malformed
  later GTI atomicity, two-texture aggregate limits, compressed peak memory,
  and published CPU exact/one-under boundaries.
- Independent review found and closed ambiguous aggregate-GTI limit mapping and
  incomplete compressed-read peak accounting. The full portable suite passes
  31/31. Native active-lease/coordinator/Metal texture handoff remains pending.

## 2026-07-23 — authenticated active-content lease

- Changed startup recovery to retain the exact AFPACK handle used for the AFAC
  size and full SHA-256 authentication. The move-only `ActiveContentLease`
  binds that handle to the active generation/reference, parsed AFPACK,
  manifest, and exact `source/Resource.up` archive/index.
- Added consuming `VerifiedContentSession::adopt`. It transfers the complete
  authenticated state without reopening the digest-derived path, hashing the
  package a second time, or reparsing AFPACK/manifest/UDSP metadata.
- Made recovery inspections move-only and added an rvalue-only
  `takeReadyLease`, so only a verified current candidate can enter runtime and
  a lease cannot be consumed twice. Rollback keeps its independently retained
  previous-candidate handle and still re-verifies the path before publishing a
  new AFAC generation.

## 2026-07-23 — private full-room runtime smoke

- Built a temporary AFPACK from the owner's external original installation,
  installed it through the production transaction, inspected its AFAC, adopted
  the retained authenticated handle, and loaded one explicit ordinary room
  through the complete World/CCF/GTI/draw-submission pipeline.
- The resulting immutable room contained 62 meshes, 62 instances, 23 dense
  texture assets, and 167 validated draw commands. This is private runtime
  evidence rather than a committed fixture; the generated package, installed
  content root, and all original-derived bytes were removed after the run.

## 2026-07-23 — native authenticated room-to-Metal publication

- Extended the native coordinator to adopt the exact ready
  `ActiveContentLease` into a `VerifiedContentSession` on its serialized worker.
  The authenticated handle remains worker-confined, and content mutation closes
  all inspection/session readers before the writer transaction starts.
- Added a portable publication gate that binds each asynchronous request to the
  complete active `{generation, size, digest}` revision and a monotonic serial.
  Session state is checked before and after `WorldRoomLoader`; main accepts only
  the still-current ticket and matching result revision. Replacement, lifecycle
  invalidation, content revision changes, and serial exhaustion fail closed.
- Added a one-shot Objective-C room snapshot. It moves one complete
  `LoadedWorldRoom` from the worker to the renderer exactly once while exposing
  only aggregate metadata to UIKit. The initial native smoke request selects
  the exact logical World `Game/Worlds/axis_1.world` and explicit physical CCF
  room index 1. If lifecycle or revision invalidation rejects the handoff before
  consumption, its potentially large CPU payload is detached in constant time
  and destroyed on a dedicated serial worker rather than on main.
- Added two-phase Metal replacement. All private mesh buffers, RGBA8 textures,
  authored or generated mip chains, offsets, payload, and submission state are
  prepared off-main. Main revalidates the publication ticket and performs one
  atomic strong-pointer assignment, leaving the previous room untouched on any
  preparation or publication failure.
- Private preparation checks a 128 MiB packed-CPU ceiling, a 256 MiB
  per-snapshot GPU ceiling, and `device.maxBufferLength`. Every created buffer
  and texture is then charged by its actual `allocatedSize`; main also checks
  the actual current-plus-candidate allocations against a 384 MiB transition
  ceiling.
- Each render command buffer retains its immutable snapshot until GPU
  completion. When the last owner releases a snapshot, its large Objective-C
  and C++ resource graph is transferred to a serial off-main release queue.
  Exact peak accounting across multiple retired GPU-in-flight snapshots remains
  a bounded P2 follow-up, not a completed guarantee.
- The full portable suite passes 32/32. The private full-room smoke remains
  aggregate evidence only—62 meshes, 62 instances, 23 textures, and 167 draw
  commands—and no private content or generated artifact was committed.

## 2026-07-24 — first native iOS input slice

- Added a safe-area-aware UIKit control overlay with a landscape flight stick,
  primary fire, and pause/resume. Independent touch ownership permits stick plus
  fire, selective hit testing leaves the rest of the view usable, partial
  cancellation releases only affected controls, and lifecycle/content
  boundaries neutralize the whole overlay. Minimum target sizes, pressed
  visuals, and custom accessibility actions are included.
- Added a narrow Apple extended-controller adapter for one player. The left
  stick maps to bank/pitch, the right-trigger value crosses an explicit Q15
  threshold for primary fire, and Menu/Options map to pause. A fixed 32-edge,
  generation- and order-tagged FIFO preserves complete digital transitions
  between input ticks; overflow resets input and forces a recoverable pause.
- Added the main-thread iOS input coordinator and a fixed 60 Hz input pump that
  continues while the Metal view is paused. It delivers one immutable
  `InputFrame` per completed tick to an exception-bounded host sink and publishes
  rate-limited diagnostics. Empty, throwing, or exhausted pipelines fail closed.
- Controller disconnect, reconnect, first hot-connect, visibility, room/content
  changes, application lifecycle, and explicit pause use common source reset and
  neutral-gate rules. Foreground activation and successful room publication no
  longer auto-resume; the player must explicitly use pause/menu.
- Integrated the input slice into the native room smoke view, including a
  noninteractive diagnostic HUD and forced-pause/error states. The full V1
  action surface, remapping, profiles, calibration, haptics, and physical-device
  acceptance remain pending.

## 2026-07-24 — aggregate Metal heap-admission ledger

- Added a thread-safe 384 MiB aggregate admission ledger with move-only,
  single-consumption reservation tokens. Closed state and reserved bytes share
  one atomic state word; token move/reset, split, downward reconciliation,
  supplemental absorption, lifetime, exact boundaries, and concurrent
  close/reserve behavior have deterministic tests.
- Converted public bootstrap, persistent fallback, and private-room buffers and
  textures to retained shared/tracked `MTLHeap` ownership. Checked sequential
  size/alignment plans are admitted before `newHeap`, and buffers are populated
  through heap-created shared storage.
- After every resource and generated mip is complete, each candidate measures
  its heaps' `currentAllocatedSize`. A smaller debit reconciles downward; Metal
  page-rounding growth must obtain and absorb a supplemental reservation before
  the candidate can leave preparation. Failure destroys the unpublished
  resources and heaps before releasing the plan token, leaving the published
  room unchanged.
- Accepted prepared, current, retired, and GPU-in-flight snapshots remain
  charged until their resources and heaps are destroyed off-main. The contract
  explicitly does not claim a hard byte ceiling for the short pre-measurement
  page-rounding window because Metal exposes no documented pre-creation bound.
- A clean configure/build and the complete portable suite pass 33/33. The
  public-boundary scanner also passes; unsigned Objective-C++/Metal compilation
  remains delegated to GitHub Actions.

## 2026-07-24 — aircraft flight boundary and deterministic player state

- `EV-20260724-001`: targeted import, vtable, caller, and address passes over
  `Game/Types/AirCraft.type` recovered its 63-slot aircraft vtable. Sixteen
  executable entries missed by automatic function analysis were created only
  at vtable-proven addresses before decompilation.
- Vtable slot 45 at `0x10003F40` is a per-step aircraft force method accepting
  `float dt`. Its branch-dependent body contains 15 `ApplyForce`, five
  `ApplyTorqueOnly`, and one `ApplyForceOnly` call sites. It contains neither
  `ResetForceAndTorque` nor `CalcAuxiliary`; the only reset observed in this
  module is construction-time.
- Vtable slot 30 at `0x10007920` consumes collided polygons, builds static
  collision constraints, and calls `CcRigidBody::CalcAuxiliary` twice. Separate
  AI logic exposes numeric control indices/ranges and event mappings, but the
  player-control join, axis names/signs, scheduler order, units, and complete
  flight equations remain unresolved.
- Added `airfix::simulation`, a platform-independent frozen-pose
  `PlayerAircraftState`. One accepted `InputFrame` preserves uninterpreted Q15
  bank/pitch intentions, exact primary-fire held/edge state, a distinct
  completed-step count, monotonic source tick, and an explicit little-endian
  canonical hash. Invalid transitions are atomic.
- The native iOS bridge advances the state exactly once for each eligible frame
  while the session is running, never catches up from tick gaps, skips the
  pause-press frame, and fails closed on an invalid transition. The HUD exposes
  step, hash, intentions, and fire counters without moving the rendered model.
- A clean GCC configure/build passes the complete 34/34 portable suite. The
  Objective-C++ bridge remains delegated to unsigned GitHub Actions builds.

## 2026-07-24 — aircraft event/scheduler join and neutral pose transport

- `EV-20260724-002`: recovered the exact event-name table and the complete
  AirCraft control dispatch through `AfVehicle::ProcessEvent`. Player commands,
  analog input, and AI channels converge on `PITCH_SET`, `BANK_SET`,
  `THRUST_SET/APPLY`, and primary/secondary attack events. The similarly named
  `THROTTLE_SET/APPLY` values are confirmed no-ops for this inheritance path.
- Joined signed player command labels and analog dead-zone/nonlinear curves to
  vehicle fields `+0x440`, `+0x444`, `+0x448`, and `+0x44C`. The flight-force
  method reads those fields directly; positive pitch is `pitchup`, positive
  bank is `bankright`, and positive thrust apply is increase. Physical world
  axes and force units remain unassigned.
- `EV-20260724-003`: recovered the active dependant scheduler through
  `NfTimeHeap -> NfActor::Refresh -> AfVehicle::ProcessEvent`. The physics path
  runs `EulerODE -> ResetForceAndTorque -> CalcAuxiliary -> AirCraft slot45 ->
  collision/slot30 -> slot44`, followed by cannon and AI refresh.
- Confirmed that `EulerODE` derives and integrates from the previously
  accumulated force/torque before the reset. AirCraft slot45 therefore
  accumulates forces for the next ODE update rather than the current one.
  Slot30 is the static/BSP collision-list resolver; actor-vs-actor collision is
  a separate slot-29 hook.
- Added `ScenePoseExchange`, a neutral bounded dynamic-transform transport with
  two preallocated SPSC slots, generation-tagged publication, an exclusive
  writer claim, saturating reader counts, complete frame validation, stable
  RAII read leases, and exact authored-transform fallback. It contains no
  aircraft or flight semantics.
- An independent concurrency/lifetime review found no critical or high issue.
  Its medium lifetime finding was fixed by replacing raw owner pointers with a
  private shared control block. Exchange destruction now invalidates handles
  while outstanding leases remain safely readable/releasable, and allocator
  address reuse cannot revive a stale scene identity.
- A fresh GCC configure/build and the complete portable suite pass 35/35. The
  dynamic SPSC test also passes repeated stress runs, the public-source boundary
  remains clean, and native Apple compilation remains delegated to GitHub
  Actions.
