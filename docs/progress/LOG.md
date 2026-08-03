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

- Owner confirmed that local Apple hardware will be available for later
  physical-device debugging and profiling.
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
  rather than choosing an arbitrary child mesh. This is the next asset task.
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
  only aggregate metadata to UIKit. That initial single-World smoke request was
  later superseded by the authenticated aggregate-mission transaction recorded
  on 2026-07-27. If lifecycle or revision invalidation rejects the handoff
  before consumption, its potentially large CPU payload is detached in
  constant time and destroyed on a dedicated serial worker rather than on main.
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

## 2026-07-24 - complete static aircraft flight-law contract

- `EV-20260724-004`: exact scalar, instruction, constructor, vtable, and
  scheduler exports closed the static `AircraftFlightForceStep` contract.
  AirCraft selects a 12 ms dependant interval and `AfVehicle::ProcessEvent`
  converts its integer-millisecond payload with `0.001f`, yielding nominal
  `dt = 0.012f` seconds (83 1/3 Hz).
- Mapped all 15 `ApplyForce`, five `ApplyTorqueOnly`, and one
  `ApplyForceOnly` sites, including their branch gates, transformed offsets,
  collision probes, height/water accumulators, throttle smoothing, propeller
  animation, and final state damping. Instruction evidence confirms the
  ambiguous indirect force call uses the rigid-body subobject.
- Joined all ten registration-tuple positions to the type constructor. Nine
  are stored, one is unused, and the instance constructor links the recovered
  scale to its rigid-body mass contributions and force equations. Physical
  units and several gameplay field names remain deliberately unassigned.
- Resolved the two inherited flight-step vtable calls as
  `AfVehicle::DisableBsp` and `AfVehicle::EnableBsp`. The original ordering
  remains integrate previous accumulators, reset, rebuild auxiliary state,
  accumulate the next forces, then perform collision work.
- Added deterministic Ghidra scripts for exact four-byte scalar interpretation
  and instruction listings. Extended the headless wrapper with a validated
  `ProgramName` mode for existing ignored projects, so repeat reports need no
  original-file path. Added fake-launcher Windows tests for import, legacy
  reuse, path rejection, unresolved reports, and native-process failure.
- No aircraft motion was added to the portable simulation. Runtime traces,
  collision-scalar meaning, physical units, float tolerance, player spawn
  identity, and the 60 Hz input versus nominal 12 ms physics-clock bridge
  remain prerequisites for a parity implementation.
- A clean CMake/Ninja validation build completed all 122 targets and all 35
  CTest cases passed. The PowerShell wrapper tests and the 198-file public
  boundary check also passed.
- Both new Ghidra exporters compiled and ran under Ghidra 12.1.2. Repeating the
  scalar export produced the same SHA-256
  `604e745d828f5378eac151ee78bade04851b9d9b511f5f7cdf60ae3b95e3e5d6`.
- Two independent read-only reviews caught and corrected an inaccurate force
  arm, incomplete BSP segment descriptions, implicit state ordering, and
  overclaimed confidence. Both final re-reviews passed with no remaining
  blocking findings.

## 2026-07-24 - complete native gameplay-action transport

- Expanded the UIKit gameplay overlay from the first stick/fire/pause slice to
  independent flight stick, latching throttle and held thrust adjustment,
  camera look, both fire actions, next/direct weapon selection, rear view,
  camera cycle/recenter, mission status, and pause. Capture regions respect safe
  areas and 44-point targets, touches outside controls pass through, and explicit
  VoiceOver actions cover held and discrete controls.
- Expanded the one-player Apple extended-controller adapter to both sticks,
  triggers, D-pad thrust, shoulders, face buttons, optional right-stick click,
  and combined menu/options pause. A 64-edge native FIFO preserves short taps.
- Added the allocation-free portable `ControllerInputBatchBridge`. It validates
  generation, Q15 samples, edge capacity/order/control transitions, starting
  state, and final reconciliation atomically before emitting bounded portable
  events. Deterministic deadzone and change thresholds prevent idle drift while
  preserving all digital edges.
- Completed default touch/controller gameplay mappings plus controller
  confirm/cancel/tab menu actions. The native coordinator now exposes explicit
  gameplay/menu/modal/control-editor contexts and resets sources before
  transitions. A latched absolute throttle target does not block the two-tick
  lifecycle neutral gate and is restored deterministically afterward.
- Expanded frozen `PlayerAircraftState` intent capture to flight, throttle,
  camera, primary/secondary fire, rear view, weapon/camera/mission/pause
  actions, and direct weapon slots `1–8`. Canonical hash version 2 covers every
  field, while invalid selections and counter overflow remain atomic.
- The new controller-batch target brings the portable suite to 36/36. Native
  Objective-C++ compilation and target-device ergonomics remain delegated to
  unsigned iOS Actions and later physical-device acceptance respectively; no
  original game data or private path is part of these tests.

## 2026-07-24 - player spawn and campaign start normalization

- `EV-20260724-005`: targeted static reports for `NfMain::CreatePlayer`,
  `NfPlayer::Spawn`/spawn event cases, `NfMission::GetStartPosition`,
  `NfMission::Call` case 10, `NfPlayer::GetPrimaryActor`, and the vehicle skin
  path recover the player/type/primary-actor chain, fixed 16-entry start table,
  selector modulo/fallback rule, and complete hierarchy construction for every
  present skin blueprint slot. Raw reports and original inputs remain ignored.
- The authored `AddStartPos` room name is passed to
  `CcWorld::GetRoomByName`. A private aggregate census covers 20 campaign setup
  scripts and finds one start per script; all 20 normalize uniquely into their
  selected main CCF with no parse, missing, ambiguous, or capacity issue, and
  none selects the primary receiving room. Only aggregate counts are public.
- Added the bounded, non-executing `MissionSetup` scanner for the exact
  `AddStartPos` shape plus fail-closed selected-CCF normalization and start
  selection. Source, room-name, and fixed-table limits, embedded NULs,
  malformed syntax, non-finite numbers, missing/ambiguous rooms, and forged
  resolution state are rejected atomically.
- This checkpoint does not claim full `CcWorld::GetRoomByName` parity. The
  runtime world may contain rooms contributed by multiple CCF loads, and the
  exact legacy `CcName` comparison policy remains to be recovered.
- Two independent read-only reviews found and closed forged logical start
  indices, embedded-NUL bypasses, an overbroad room-lookup claim, a false
  function-catalog call edge, and smaller status precision issues.
- A fresh CMake/Ninja build completes all 128 targets and all 37 CTest cases
  pass. The Windows Ghidra-wrapper tests, public-boundary scan, and
  `git diff --check` also pass. Native Apple compilation remains delegated to
  GitHub Actions; no original game data or private path enters the build.

## 2026-07-24 - ordered mission world-room lookup

- `EV-20260724-006`: deterministic reports from the locked Cc/AfEngine
  programs recover `CcName` null/present state and comparisons,
  `CcWorld::GetRoomByName`, newest-first `CreateRoom`, the room-section paths
  in `CcRoom::LoadSceneCcf`, and the complete synchronous mission-root scene
  order in `NfMission::LoadLevel`. Raw reports and original binaries remain in
  ignored local storage; module identity is anchored by the public source
  manifest.
- Direct observations show main `CCFF` and optional first `BCKD` loads with
  flags `0`, followed by each `OBJE` object CCF with flags `0x2000`, all into
  the same initially void root. Only flag `0x20` suppresses room sections.
  `AddStartPos` is gated until that load and world initialization complete.
- Each `0x1000` section treats a leading direct `0x1100` as a receiver/root
  binding. A room after another direct child is ordinary. Root lookup requires
  both present name and prefix; ordinary lookup compares only the name with
  MSVCRT case-insensitive semantics. The portable boundary implements exact
  ASCII parity and rejects embedded NUL or non-ASCII input whose CRT
  locale/codepage behavior is not proven.
- A private aggregate census covers 20 setups/main scenes, seven backgrounds,
  and 2,101 object CCF scene calls. Main scenes contain 101 physical rooms:
  20 primary and 81 ordinary. Background and object scenes contain no named
  ordinary rooms in this mission flow. All 20 starts resolve uniquely to
  ordinary main-scene rooms, with no missing, ambiguous, empty, non-ASCII,
  root, background-only, object-only, exact-collision, or ASCII-fold-collision
  result. No original name, path, script, or payload is published.
- Added `CcfRoomSectionMetadata` so the portable parser preserves repeated
  room-section boundaries and leading-child semantics instead of flattening
  them into one assumed primary. Added `MissionWorldRooms`, which replays
  ordered enabled sections, retains a shared root plus newest-first ordinary
  rooms and every source/physical contributor, and resolves starts through
  distinct `worldRoomIndex` types.
- Source, section, contributor, runtime-room, per-component, aggregate-name,
  integer, primary-binding, and catalog-structure limits fail closed.
  Root/full-name and ordinary/name-only matching, repeated sections,
  unknown-leading children, disabled/empty sections, root renaming/collisions,
  null versus present-empty names, modulo/fallback selection, non-ASCII/NUL,
  missing/ambiguous starts, exact limits, and forged public state have
  synthetic coverage.
- Two independent read-only reviews found and closed flattened room-section
  semantics, pre-limit scan and forged-catalog memory growth, a valid ordinary
  room at physical index zero, accidental draw-plan assumptions, stale
  documentation, and catalog call-edge precision. Both final re-reviews pass.
  A fresh CMake/Ninja Release build completes all 131 targets and all 38 CTest
  cases pass. Windows Ghidra-wrapper tests, the 208-file public-boundary scan,
  and `git diff --check` also pass.

## 2026-07-24 - source-aware mission room drawing

- `EV-20260724-007`: targeted instruction/decompilation passes over the locked
  Cc program recover `CcLoadedScene`/`CcLoadedEntity`/`CcLoadedRoom` lifetime,
  per-load reference lookup, fresh mesh/material/object publication, exact
  placed masks, room ownership, and `FreezeAll` static-BSP rebuilding. Raw
  reports and original binaries remain in ignored local storage; no private
  name, path, payload, or hash is published.
- Each load owns a transient room-reference table. When a later physical room
  record resolves to the same runtime room, its wrapper replaces the earlier
  wrapper in that loaded scene; the runtime room and already attached content
  survive. Placed lookup sees only the active per-load wrappers. A missing room
  reference selects the receiving root. References never cross source/load
  boundaries.
- Placed object, null, and light publication use masks `0x2440`, `0x3000`, and
  `0x2800` respectively. The mission's object-definition loads use `0x2000`,
  so they contribute room records and resources but publish no placed scene.
  The source-aware portable contract records this independently from flag
  `0x20`, which suppresses room sections.
- Added `MissionWorldRoomDrawPlan`, which scans enabled sources once in load
  order and objects in physical order. It replays active room-reference
  rebinding, maps through the ordered runtime catalog, emits receiver fallback
  once per placed record, preserves source-local mesh/material/texture identity,
  and assigns global mesh slots by `{sourceIndex, physicalMeshIndex}` first use.
- Added atomic `MissionWorldRoomDrawAssembly` with per-source material binding,
  authored-world transforms, aggregate resource/byte limits, and parallel
  numeric mesh/instance provenance. Its output crosses the unchanged
  `DrawSubmissionPlan` boundary. Global multi-source texture-ID assignment and
  an authenticated mission loader remain the next integration.
- Synthetic coverage includes repeated room sections, two sources contributing
  one ordinary runtime room, same-source wrapper replacement, root fallback,
  physical-order preservation, source-local numeric collisions, same-source
  mesh reuse, `0x20` fallback, `0x2000` suppression, late-source atomic
  failure, forged catalog/source identity, top-level reordering, exact limits,
  and one-under aggregate bytes.
- `FreezeAll` calls each room's `Freeze`, discards accumulated static BSP chains,
  and rebuilds one derived static tree from the accumulated ordinary and portal
  object lists. It does not delete objects or meshes; BSP remains a rebuildable
  spatial index rather than another geometry source. Portal-BSP rebuilding is
  a separate path and is not claimed as active in this mission flow.
- Both final independent re-reviews pass after canonical catalog replay,
  internal plan re-resolution, exact CCF source-identity binding, pre-allocation
  aggregate limits, and expanded forged/one-under coverage. A fresh
  CMake/Ninja Release build completes all 135 targets and all 39 CTest cases
  pass. Windows Ghidra-wrapper tests, the 213-file public-boundary scan, and
  `git diff --check` also pass.

## 2026-07-27 - offline reverse-engineering workbench

- `EV-20260727-001`: verified official releases, current licenses, and
  published hashes or signatures for Rizin 0.9.1, Cutter 2.5.0, rzpipe 0.6.2,
  and the x32dbg executable from x64dbg 2026.05.27. Ghidra 12.1.2 remains the
  canonical static tool. The owner accepted the Binary Ninja Free 5.3 R2
  license and confirmed the official installer hash; installed build
  `5.3.9757` has a valid Vector 35 signature. Network-facing settings are
  disabled and a program-specific outbound firewall rule is enabled. The
  optional manual comparison remains pending an operator-scheduled GUI window
  and does not block the automated Ghidra/Rizin path.
- Added an explicit-manifest working-copy builder. It rechecks source and copy
  hashes, rejects traversal and reparse points, never overwrites a conflicting
  copy, and stores no source path in public output. Original files remain
  read-only and outside Git.
- Added a deterministic Rizin/rzpipe exporter and safety wrapper. Reports are
  path-free, schema-checked, double-hash-bound, atomically published, and
  read-only with user startup configuration disabled. Missing automatic
  function discovery fails closed; the analyst must explicitly request an
  in-memory function at a separately confirmed RVA, and the report records
  that provenance.
- `EV-20260727-002` / `EXP-20260727-001`: fresh Ghidra and Rizin analyses
  cross-check representative rendering, input, and flight-law functions.
  Rizin matched all confirmed boundaries and exposed useful raw call/data
  sites, but its inferred x86 ABIs were unreliable and its automatic analysis
  missed the large flight function. Ghidra remains authoritative for
  signatures and structured pseudocode.
- Created the isolated `Airfix-Dev` WSL2 distribution with automount and
  Windows interop disabled. A native ext4 CMake/Ninja build completed all 135
  targets and CTest passed 39/39 at commit `db3730d`; other distributions and
  global WSL settings were left unchanged.
- No game binary, original resource, tool archive, decompiler database, local
  path, pseudocode dump, debugger trace, or vendor-licensed file is included
  in the repository.

## 2026-07-27 - global mission texture binding

- Added `MissionWorldRoomTextureBindings`, the missing portable producer for
  the global texture namespace consumed by source-aware room assembly. It
  replays the canonical mission draw plan and requires each texture-source CCF
  pointer to match the exact ordered load source.
- Every supplied CCF logical path is resolved against its UDSP file index
  before binding. This is a metadata-only identity check; the caller must parse
  each CCF and bind its index in the same immutable authenticated load
  transaction. Each source retains its own `TEXU` and local material-reference
  namespace, while global dense `TextureAssetId` values are assigned by
  canonical first use and deduplicated only by unique archive file index across
  sources and texture roles. GTI payloads are not read.
- All renderer-facing output is atomic. Source-count/pointer/index mismatches,
  a forged catalog, missing or ambiguous late textures, dependency failures,
  overflow, and aggregate or per-source exact/one-under limit failures have
  synthetic coverage.
- The result feeds `MissionWorldRoomDrawAssembly` and `DrawSubmissionPlan`
  without an adapter. A complete Windows CMake/Ninja build and all 40 portable
  CTest targets pass. The authenticated mission manifest was the next content
  integration at this checkpoint; no private game data or local path was added.

## 2026-07-27 - authenticated mission load manifest

- Added move-only, private-constructor `MissionLoadManifest` publication bound
  to the exact `ContentRevision`. The construction transaction additionally
  pins the opaque identity of the authenticated session handle. Its request
  requires independent explicit setup and Level logical paths; setup lookup
  never derives a name from the Level and never tries a sibling or fallback.
  That one `VerifiedContentSession` reads the setup AFS, Level, World, and every
  unique physical Level `OBJE` definition; no diagnostic label or package path
  is reopened. Move transfers a valid proof and poisons the source, while
  `belongsTo()` and result `success()` reject moved-from state. The token is
  construction-only; `belongsTo()` accepts any separately authenticated
  session with the same `ContentRevision`.
- Exact unique lookup now covers the explicit setup, Level -> World -> main
  `CCFF`, optional first `BCKD`, and every physical `OBJE` definition ->
  `CCFF`. The bounded setup scanner never executes AFS and retains no more than
  16 finite `AddStartPos` records. The manifest owns their canonical setup
  path/index identity and source footprint, while raw AFS bytes are discarded.
  Descriptors retain main/backdrop/physical-placement order and repeated CCF
  loads. Repeated object definitions are read and parsed once by archive file
  index, while an `OBJE` dependency with a `MODL` root fails closed. Level
  `MODL` references remain metadata-only and do not become mission-root CCF
  sources.
- This phase reads no CCF or GTI payload. It preflights the compressed setup
  and definition read peaks, aggregate unique definition source bytes, each and
  all planned CCF source footprints (charging every ordered descriptor,
  including repeats), descriptor count, and owned published CPU state.
  Published accounting includes the setup identity, start records, and room
  names. Cancellation, callback failure, overflow/allocation failure, or any
  late dependency error returns no partial manifest. Setup/World/`OBJE`/`MODL`
  metadata resolution checks cancellation per item. Exact handle identity and
  revision are checked before and after callbacks and again before publication.
  Callback-driven replacement or move-out returns `sessionIdentityChanged`,
  including same-revision replacement.
- Synthetic coverage includes exact setup identity and present-empty setup,
  malformed and over-capacity `AddStartPos`, typed setup offsets, a non-openable
  diagnostic label, two `BCKD` chunks with first-only selection, repeated
  `OBJE` definition/CCF identities, deliberately malformed metadata-only
  `MODL` and CCF payloads, a `MODL` root reached through `OBJE`,
  missing/ambiguous and late dependencies, compressed exact/one-under budgets,
  cancellation, callback failure, exact handle/revision binding, same-revision
  replacement, and callback move-out.
- CCF parsing, ordered room construction, global texture binding, aggregate
  draw/submission assembly, and native publication are deliberately not wired
  yet. The manifest supplies their authenticated same-session input rather than
  claiming that work is complete.
- A fresh complete CMake/Ninja build passes and all 41 portable CTest targets
  pass, including the new manifest regression target. No private game data,
  original-derived payload, or local path was added.

## 2026-07-27 - authenticated aggregate mission room load

- Added `MissionWorldRoomLoader`, which consumes `MissionLoadManifest` and a
  `VerifiedContentSession` for the same cryptographic `ContentRevision`. It
  validates every ordered CCF descriptor, exact logical-path/file-index
  identity, role, flag, placement index, and recorded compressed allocation
  footprint before reading a payload. The loader pins the exact handle marker
  and revision before/after every callback and read and again before
  publication.
- Physical CCF entries are cached by archive file index in first-use order,
  while the main/backdrop/physical-OBJE descriptor list remains fully repeated
  for legacy room publication. The public synthetic transaction contains four
  semantic loads over three physical CCF reads and records cache mapping
  `{0,1,2,2}`. Repeated object loads retain room contributors but suppress their
  independent placed scene and its deliberately missing texture through the
  recovered `0x2000` behavior.
- The loader builds the source-aware room catalogue, resolves the fixed
  `AddStartPos` table with modulo or anonymous-root fallback, constructs the
  selected room's dense global texture namespace, prepares every GTI, preserves
  mesh/instance source provenance, assembles the combined model, and validates
  its `DrawSubmissionPlan`. The authenticated-setup follow-up below replaces
  the temporary external start-table seam with manifest-owned records and
  carries setup provenance into the result.
- Per-entry and unique-aggregate CCF/GTI footprints,
  decoded/upload/resident RGBA, and published CPU ownership are independently
  checked. Retained CCF metadata has post-parse logical accounting on top of
  the parser's fixed admission caps; it is not presented as an allocator/RSS
  ceiling. Exact/N-1 compressed budgets, independently recomputed ownership
  counters, malformed last CCF and second GTI, late draw/submission rejection,
  callback-time destruction of borrowed inputs, cancellation, complete-callback
  failure, moved proofs/sessions, separately authenticated equal revisions, and
  same-revision handle replacement all return no partial room.
- `VerifiedContentTransactionIdentity` now carries a private retained marker
  rather than an `ifstream` address. Exact session moves preserve identity,
  independently opened or replacement sessions receive new markers, and copied
  guards keep displaced markers alive so allocator-address reuse cannot create
  an ABA match.
- A fresh Windows GCC/Ninja build completes and all 42 portable CTest targets
  pass. No private game data, original-derived payload, generated analysis
  database, or local source path was added. Native mission publication,
  retained CCF/catalogue data for runtime room switching, and player-pose
  application remain the next integration boundaries.

## 2026-07-27 - authenticated mission setup provenance

- `MissionLoadManifestRequest` now requires an explicit `setupLogicalPath`
  independently of the Level path. The builder performs one exact unique lookup
  against the authenticated UDSP archive and never derives, searches, or falls
  back to a setup path based on the Level name.
- The same pinned `VerifiedContentSession` and `ContentRevision` cover setup,
  Level, World, and unique object-definition reads. All request paths and limits
  are bounded and snapshotted before the first callback. The setup parser is
  non-executing, recognizes only the exact `AddStartPos` form, accepts finite
  locale-independent numbers, and enforces the hard legacy capacity of 16.
- A valid manifest owns the canonical setup path/index identity, the checked
  compressed-source footprint, and every parsed start record. Raw AFS bytes and
  borrowed views are destroyed before publication. A successfully
  authenticated setup containing no starts is valid and requests the anonymous
  root-room fallback.
- `MissionWorldRoomLoader` no longer accepts a caller start span. It snapshots
  the manifest-owned records, resolves them against the complete ordered
  semantic CCF room catalogue, and publishes the canonical setup identity and
  source footprint alongside the selected owned start, model, textures,
  provenance, and submission plan. Late setup, CCF, GTI, callback,
  cancellation, or identity failure remains atomic.
- Native iOS mission publication, applying the selected start room and pose to
  the reconstructed player, and retaining CCF/catalogue state for later room
  transitions remain open. No private content name, path, script, or
  original-derived payload was added.

## 2026-07-27 - native authenticated aggregate-mission publication

- Replaced the native single-World request with an explicit private setup
  logical path, Level logical path, and `uint32_t` start index. The serialized
  content worker builds `MissionLoadManifest` and then
  `LoadedMissionWorldRoom` through the same pinned
  `VerifiedContentSession`/`ContentRevision`; neither logical path is inferred,
  reopened, logged, or exposed by the public snapshot, UI, or status surface.
  They exist only as transient native request inputs.
- Added a one-shot private Objective-C++ aggregate snapshot. Its public surface
  exposes only bounded counts and revision metadata. The move-only mission
  payload crosses into renderer preparation exactly once, while a stale
  unconsumed payload is detached on main and destroyed on the existing serial
  teardown queue.
- Metal preparation now consumes the authenticated aggregate room off-main and
  retains setup/start selection, CCF cache mapping, and mesh/instance
  provenance. After the complete Metal texture set exists, decoded CPU texture
  vectors are released instead of being held by the published renderer owner.
- Closed the main-thread transaction order: renderer validation is read-only,
  the coordinator revalidates and consumes the exact outstanding
  serial/revision ticket, and the renderer immediately performs a no-fail,
  constant-time atomic strong-pointer swap. Preparation or validation failure
  conditionally abandons only the matching current ticket; stale failures
  cannot cancel newer work.
- Added optional build-generated startup configuration. Public defaults remain
  empty and data-less. Private builds can provide Base64 setup and Level paths
  plus a decimal start index through
  `AIRFIX_IOS_INITIAL_SETUP_LOGICAL_PATH_BASE64`,
  `AIRFIX_IOS_INITIAL_LEVEL_LOGICAL_PATH_BASE64`, and
  `AIRFIX_IOS_INITIAL_START_INDEX`; CMake validates the inputs and generates a
  build-tree header. No private path is stored in source control.
- AFPACK v1 remains intentionally free of launch metadata. An authenticated,
  bounded mission catalogue is reserved for AFPACK v2. The retained selected
  start is not yet applied to simulation because no player pose model exists,
  and runtime room switching still requires a retained CCF/catalogue arena.
  This entry records implementation scope; it does not claim completion of the
  current GitHub Actions validation.

## 2026-07-27 - authenticated player start-pose publication

- Ghidra Headless and an independent Rizin pass closed the `AddStartPos`
  transform contract. The three fields are x/y/z radians in mode zero; the
  legacy matrix starts at identity and applies Z, X, then Y. Player spawn uses
  the resulting position and matrix as an absolute world pose and applies room
  membership separately. Physical source length units remain unknown.
- Added an explicit portable converter rather than relying on a library Euler
  convention. It preserves the recovered signs/order, transposes legacy
  row-vector storage, conjugates through the same room basis, and applies the
  configured runtime-unit scale only to position. Data-less tests cover
  identity, every positive quarter-turn, mixed order, basis conversion,
  maximum finite input, non-finite values, and a singular basis.
- Added a trivially copyable `PlayerSpawnPose` separate from the frozen
  input-intention `PlayerAircraftState`; the canonical state hash and advance
  rules remain unchanged. The mission loader retains authenticated raw values,
  converted runtime values, exact room/index selection, and the basis used.
  Publication recomputes the pose and rejects any mismatch.
- The private Objective-C++ snapshot keeps an independent pose copy after its
  large room payload moves to the renderer. A new mission request no longer
  clears the prior deterministic state. After read-only Metal validation, the
  coordinator consumes the exact ticket, performs the no-fail Metal commit,
  then assigns the new pose and fresh input state before setting content ready.
  Stale or failed candidates leave the earlier committed state intact.
- Both the incremental code-intelligence build and a fresh 148-step Windows
  Ninja Release build completed; all 43 portable tests passed in each build
  tree. The 244-file public-boundary scan and an independent post-fix review
  also passed.
  Binary Ninja Free was opened for a planned third manual view, but the
  Computer Use approval expired before any binary was opened; the empty
  session was closed and contributed no evidence. No private path, game
  binary, tool database, or original-derived payload was committed.

## 2026-07-27 - recovered player actor visual transforms

- A focused Ghidra Headless pass closed the player visual construction path.
  The actor model is separate from mission placed nodes. The three recovered
  skin roots are `thirdperson`, `damaged`, and `destroyed`; every present root
  clones its complete subtree, receives local translation zero plus legacy
  Y(pi), and starts hidden. Only slot zero is initially unhidden, receives a
  dynamic BSP, and receives the actor UID recursively. Binary Ninja and the
  game executable were not used.
- Added a bounded `ObjectVisualDrawAssembly`. It independently resolves the
  selected blueprint subtree, shares physical meshes in first-use order,
  retains mesh-bearing DFS instance order, preserves converted authored-world
  transforms, and publishes parallel source provenance only on complete
  success. The diagnostic model preview now consumes this production builder
  instead of duplicating its geometry assembly.
- Added a separate visible-slot `PlayerActorVisualDrawAssembly`. It derives
  every mesh node relative to a potentially nonidentity selected authored
  root, applies the recovered zero plus Y(pi) root-local pose, and deliberately
  stops before absolute spawn placement. Tests prove the later player-world
  pose is composed exactly once and cover null/mesh roots, a nontrivial basis,
  malformed dependencies/graphs, non-finite and singular transforms, material
  failures, first-use sharing, DFS provenance, and atomic limits.
- Independent review found that the first adapter revision inherited byte
  accounting based on the smaller generic provenance. The final revision
  recounts the complete model and larger actor provenance from zero with
  checked multiplication/addition. Exact-limit succeeds and limit-minus-one
  fails atomically; the re-review approved the correction with no remaining
  findings.
- Incremental and fresh Windows GCC Release builds complete; all 45 portable
  tests pass. The fresh tree builds 154 steps, the actual public-boundary scan
  passes 251 files, and the function catalogue retains a valid 14-column
  schema. A private read-only before/after diagnostic produced identical
  counts, RGBA hash, and byte-identical PPM output; those images and all
  original-derived artifacts remain ignored. Local Clang linking is unavailable
  because the machine lacks Windows SDK libraries, so Clang/macOS/iOS remain
  CI validation responsibilities.
- No private path, original asset, binary, decompiler output, project database,
  or generated diagnostic was added. The next boundary is an explicit
  authenticated player object-definition source in the mission manifest,
  followed by tagged actor provenance in the combined room model and Metal
  publication.

## 2026-07-27 - authenticated player visual admission and scene join

- `MissionLoadManifest` now accepts an optional exact player visual logical
  path. The same pinned content session authenticates and parses that
  definition, then derives the model CCF identity, visible blueprint selector,
  and texture root only from the parsed payload. The player CCF is planned and
  bounded but is not read during manifest construction or inserted into the
  room-catalogue load list.
- Added a bounded player texture-binding adapter. It preserves the room's dense
  texture-ID prefix, deduplicates actor textures by authenticated archive entry,
  appends new entries in actor first-use order, and remaps primary, secondary,
  and environment roles into one global namespace. All dependency, graph,
  entry, binding, count, and byte failures are atomic.
- Added a bounded player scene adapter. It appends actor meshes and instances
  without cross-source mesh merging, composes the authenticated spawn-world
  transform with each actor-local transform exactly once, and retains contiguous
  final bindings plus actor-only provenance. The publication proof preserves
  each exact pre-composition actor-local transform.
- The optional private iOS request now carries an exact player definition path
  as owned data through the serialized coordinator. Public unsigned pull-request
  builds remain completely data-less and do not read repository secrets; a
  future protected, manually approved workflow owns any non-empty private launch
  inputs.
- Independent reviews found and closed incomplete repeated-mesh provenance,
  a non-representable binding range, a missing composed-transform error, and
  public-PR secret exposure. Re-reviews approved the final scene, texture,
  manifest, and iOS contracts. A full Windows GCC/Ninja build and all 47
  portable tests pass; `actionlint`, `git diff --check`, and the source-boundary
  rules are clean after the intentional documentation deletion is staged.
- The repository landing page was rewritten as a concise project introduction.
  The internal MacBook escalation document and its references were removed;
  technical architecture decisions and tool requirements remain documented.
  No private path, original payload, local analysis database, signing material,
  or generated content was added.

## 2026-07-27 - authenticated player actor loader publication

- Completed the optional player branch inside `MissionWorldRoomLoader`. The
  player descriptor is pinned before callbacks, its CCF participates in the
  same authenticated physical cache as room sources, and a shared source is
  read only once. Room semantic-source and cache-prefix semantics remain
  unchanged.
- The room texture namespace is retained as a stable prefix. Actor textures
  reuse matching authenticated entries and append only actor-specific entries
  in deterministic first-use order. The selected actor visual is composed with
  the authenticated absolute spawn pose exactly once, then joined with the
  static room into one final model and one `DrawSubmissionPlan`.
- The published result carries the optional descriptor, physical-cache index,
  actor binding, and mesh/instance provenance. The allocation-free publication
  validator checks absent/present co-occurrence, ranges and final indices,
  room prefixes, cache order, mesh/source identity, slot-zero selection, and a
  bit-exact recomposition of every actor world transform.
- Synthetic tests cover separate and shared player CCFs, shared and actor-only
  textures, root and table-selected starts, one physical read, malformed
  sources, late failures, callback/session/cancellation races, exact and
  one-under budgets, and publication tampering. An independent review found
  two admission-policy inconsistencies in typed source-limit reporting and
  global CPU preflight; both were corrected before publication.
- A separate native-path audit confirmed that the generic snapshot and Metal
  preparation already retain and render the complete static combined model,
  including actor ranges, textures, provenance, and transforms. Dynamic actor
  pose transport, a recovered camera/projection, secondary/environment texture
  use, and device visual acceptance remain separate follow-up work.
- A fresh Windows GCC/Ninja Release tree built all 160 steps and the complete
  portable suite passed 47/47. The public-boundary scan checked 256 files,
  `actionlint` and `git diff --check` passed, and no original content or private
  path entered the repository.

## 2026-07-27 - player actor pose-frame and native exchange pilot

- Added a portable bounded `PlayerActorPoseFrame` builder. It validates the
  authenticated actor binding and exact instance provenance, checked
  `uint32_t`/count/byte ranges, and finite nonsingular operands before
  producing a strictly ordered actor-only override frame. Each result owns its
  payload; its borrowed exchange view is lvalue-only so a temporary cannot
  create a dangling span.
- Every override is composed exactly once as `actorWorld * actorLocal`.
  Failures publish no partial vector and retain typed index/geometry details.
  Tests cover noncommutative transforms, missing and malformed bindings,
  gaps/order/duplicates, exact and one-under limits, nonfinite/singular input,
  nonfinite composition, direct exchange publication, dynamic actor resolve,
  and byte-for-byte authored fallback for a static instance.
- Added a per-private-snapshot native `ScenePoseRuntime`. During serialized
  off-main Metal preparation it derives the exact step-zero actor frame,
  verifies every transform bit-for-bit against the already validated combined
  model, begins one scene, and requires the initial publication to succeed
  before the candidate can commit.
- The native plan enforces independent instance, override, and frame-byte
  ceilings before allocation. Checked retained storage for both exchange slots
  enters the existing private CPU admission budget; the temporary initial
  frame is released after publication. The render loop acquires at most one
  lease for a complete frame and otherwise uses the immutable authored
  transform.
- Independent reviews approved the portable and Objective-C++ contracts after
  one retained-memory finding was corrected. The portable suite passes 48/48,
  the public-boundary scan checks 259 files, and no proprietary content or
  private path is included. Real Objective-C++/Metal compilation remains the
  responsibility of the unsigned iOS GitHub Actions run for this commit.

## 2026-07-27 - reusable player-pose producer and camera boundary

- Replaced the native step-zero-only wrapper with portable
  `PlayerActorPoseRuntime`. Its factory copies authenticated actor-local
  sources, owns one scratch frame and two exchange slots, enforces exact
  instance/override/frame limits before allocation, and reports the complete
  retained logical byte charge.
- `tryPublish` is bounded, allocation-free, and `noexcept`. It recomposes every
  actor override as `actorWorld * actorLocal`; pre-publication failures preserve
  the last accepted slot, stale steps fail closed, and a busy exchange permits
  the deterministic simulation state to advance while dropping only the
  contested visual update.
- Native Metal preparation admits the source, scratch, and double-buffered
  storage before creating the runtime, then verifies the initial step-zero
  lease bit-for-bit against the authored combined model. Snapshot resources
  retain the only strong runtime owner so large destruction remains off-main.
  The controller carries only an optional weak endpoint across the main-thread
  publication boundary and swaps it after the exact content ticket commits.
- Every accepted later simulation step publishes before its state is assigned.
  Expiry or any non-busy publication error terminates the simulation pipeline
  without accepting the new state. Until recovered flight integration produces
  a changing world transform, later frames intentionally reuse the
  authenticated spawn world.
- Documented the first camera/projection boundary without inventing values.
  `CcPolyVertex::Project` confirms positive view-space Z and screen-centred
  X/add, Y/subtract projection; the visible-polygon endpoint and room/BSP build
  chain are located. Camera construction, FOV, clipping, aspect, render-list
  drain, BSP traversal, and portal semantics remain explicit unknowns.
- An independent native review found no P0-P3 issue. A clean 166-step
  Windows GCC/Ninja build and all 49 portable tests pass; the public-boundary
  scan checks 263 files, `actionlint`, and `git diff --check` are clean.

## 2026-07-27 - camera projection and portal render chain

- Ghidra Headless and Rizin independently matched the boundaries and raw
  callsites from `CcCamera::Render` through `RenderRoom`, ordered render-list
  draining, six-plane and distance clipping, `CcPolyVertex::Project`, and the
  concrete Direct3D `GtScreen::RenderTriangle` implementation. Recovered MSVC
  declarations, ECX use, and stack cleanup resolve Rizin's incorrect `cdecl`
  proposals.
- Confirmed positive-Z camera space, FOV clamping to `[1, 175]` degrees,
  `focal = 0.5 * windowWidth / tan(FOV * pi / 360)`, the exact near fallback
  branch, screen centre, and explicit screen-Y inversion. Constructor defaults
  are near `1`, far `100`, and FOV `90`; engine initialization installs near
  `0.25`, far `200`, and FOV `90`.
- `RenderRoom` gathers room object lists and recurses through portals whose
  clipped visible area is nonempty. The fourth parameter is depth and the
  constructor limit is two; no general visited-room set was found. Portal type
  one reflects the camera and reverses culling, which behaves like a mirror
  although the enum name remains unproven.
- Corrected the prior BSP hypothesis. The confirmed render path does not walk
  static-BSP children. Camera movement separately traces the portal-BSP,
  visiting the movement segment's start-side child first and retaining the
  nearest crossed portal polygon to resolve the next room.
- The evidence supports a portable camera-space-to-screen scalar contract but
  not a parity view or Metal projection matrix. World-to-view layout, gameplay
  camera modes, legacy depth mapping, and widescreen policy remain explicit
  blockers for the native camera join.
- Implemented that bounded scalar contract as immutable portable C++20. Its
  factory validates finite near/far/FOV/window inputs and derived scales,
  applies the recovered FOV clamp, and its allocation-free `noexcept` hot path
  preserves the exact near fallback, operation order, centre, and Y inversion.
  Far clipping, world-to-view, NDC, depth, and Metal matrices remain outside
  this component by design.
- The complete Windows GCC/Ninja build and all 50 portable tests pass. A
  separate GCC C++20 `-Wall -Wextra -Wpedantic -Werror` compile for the new
  implementation and tests is also clean. Independent review found no P0-P3
  issue; the public-boundary scan checks 267 files, `actionlint`, and
  `git diff --check` are clean.
- A follow-up Ghidra/Rizin audit recovered the full camera SRT relation that
  precedes that projection. `GetNodeRelation` subtracts camera translation,
  evaluates the object/world triplets against the transposed camera basis, and
  applies cached `q = 1 / scale²`; the renderer consumes this scratch relation
  only after `AddVisibleObjectsToRenderList` prepares it. Light and projector
  paths independently corroborate the same operation order and show no hidden
  X/Y/Z reflection.
- Camera render snapshots at `+0x978` and `+0x9B0` retain the prior and current
  world SRT for motion blur. Their roles are confirmed, but the detailed
  motion-blur endpoint contract, gameplay camera modes, Metal depth mapping,
  and widescreen policy remain open and are not part of the portable view
  transform.
- Implemented the confirmed world-point subset as immutable
  `LegacyCameraTransform`. The allocation-free `noexcept` builder validates
  finite values, normalized Gram orthogonality, uniform column length,
  `q * scale²`, tiny scales, and a fail-closed range that excludes subnormal
  inverse-square parity. The hot path preserves subtract, dot, then `q`
  operation order and reports non-finite input/output atomically.
- Independent review found and resolved a large-scale/subnormal tolerance
  gap and an rvalue getter lifetime hazard; the focused re-review reports both
  resolved with no remaining P0-P3 finding. A 172-step clean Windows
  GCC/Ninja build followed by the final incremental rebuild and all 51
  portable tests pass, including tiny/large scale, transpose/order, negative
  scale, overflow, immutability, and zero-allocation coverage. The
  public-boundary scan checks 271 files; `actionlint`, the function-catalog
  uniqueness check, and `git diff --check` are clean.

## 2026-07-27 - reverse depth and legacy viewport policy

- Closed the clipped-depth path from `CcPolyVertex::Project` through embedded
  `GtVertex`, the 44-byte D3D7 transformed vertex, and `DrawPrimitive` FVF
  `0x344`. The active driver uses ordinary `D3DZB_TRUE`, not hardware W-buffer
  mode, but manually assigns both transformed Z and RHW to `near / cameraZ`.
  Opaque mode one uses `GREATEREQUAL` with writes; three further modes combine
  `ALWAYS` or `GREATEREQUAL` with writes enabled or disabled.
- Confirmed inclusive near/far clipping, reciprocal depth range
  `[near/far, 1]`, and D3D viewport depth `[0,1]`. This gives Metal an exact
  reverse-depth scalar contract.
- Enumerated all named `SetWindow`/`SetCentre` callers. Startup requests
  exclusive fullscreen `640x480`; the engine has a full-screen caller, while
  menu/widget scenes use their own pixel rectangles. Horizontal FOV depends
  only on window width, and no adaptive widescreen, Hor+, letterbox, or
  pillarbox policy was found.
- Selected a parity-first iOS presentation policy: retain a logical 640x480
  canvas and aspect-fit it into the physical target. This is explicitly a port
  decision; optional widescreen remains a separate later feature with its own
  HUD, culling, touch-layout, and visual acceptance.
- Extended `LegacyScreenProjection` to publish camera-space Z and the exact
  recovered reciprocal-depth factor without introducing Metal state or
  clipping. Boundary, monotonicity, atomic-failure, and zero-allocation tests
  cover the new result.
- Added `LegacyCanvasLayout`, separating the recovered 640x480 extent from a
  validated, immutable, backend-neutral aspect-fit port policy.
  Independent review identified a float-endpoint overflow edge case; checked
  double-width endpoint headroom and regression tests close it.
- A clean Ninja build completed all 175 steps and all 52 CTest targets pass.
- A follow-up symbol/string audit corrected the fixed 520x310 window at
  `Dogfighter.exe` RVA `0x2B8D0`: it belongs to the deferred House Editor, not
  the main gameplay camera. Removed that incorrectly labeled rectangle from
  the portable layout contract; the recovered 640x480 startup canvas is
  unaffected.
- Recovered the two concrete Direct3D clear implementations. Whole-target and
  clipped-rectangle paths both call `IDirect3DDevice7::Clear` with
  `TARGET|ZBUFFER`, exact depth `0.0`, and stencil zero; their colors are
  respectively `0x00000000` and `0xFF000000`. This closes the original
  reverse-depth clear-value gap.
- Enumerated the depth-state callers and recovered the room-pass order
  `2 -> 2 -> 1 -> 3`, followed by mode `4` for lens overlays. Mode changes
  flush pending geometry before changing compare/write state; radar, fonts,
  2D primitives, and two mission-menu renderers consistently select mode `2`.
- Added `LegacyDepthState`, a `noexcept`, allocation-free and fail-closed
  portable mapping for the four confirmed active-path presets. It records the
  zero clear scalar but deliberately leaves the diagnostic Metal state
  untouched until gameplay camera integration.
- Correctly located the gameplay camera at `NfEngine + 0x30`. It uses the
  current full screen, giving `(0,0)-(640,480)` and centre `(320,240)` at
  startup, with projection defaults near `0.25`, far `200`, and horizontal FOV
  `90`.
- Recovered persistent chase tuples `camera0`, `camera1`, and `camera2`, their
  `0 -> 1 -> 2 -> 0` cycle, and the held `camera_rear` override. The vehicle
  rotates only offset X/Z while applying Y as world-up, advances through an
  exact nonlinear per-refresh recurrence, resolves portals plus sphere/line
  collisions, and finally looks at the vehicle.
- Added `LegacyGameplayCameraPreset`, a fail-closed and allocation-free
  portable implementation of the exact four tuples and persistent cycle.
  Smoothing, collisions, pose mutation, optional free look, and Metal
  integration remain deliberately separate.
- Independent review approved both portable contracts after one stale
  canvas/gameplay wording issue was corrected. A clean Ninja build completed
  all 181 steps and all 54 CTest targets pass; the public-boundary scan checks
  285 files, the function catalogue contains 206 unique entries, and
  `actionlint` plus `git diff --check` are clean.

## 2026-07-27 - stateless gameplay-camera chase step

- Rechecked the `AfVehicle::ProcessEvent` chase block with Ghidra instructions
  and Rizin. The recovered rotation maps directly to the portable
  column-vector `Mat3`; the CCF legacy-row adapter and world-to-camera
  transform do not belong in this path.
- Corrected the documented float operation order from the algebraic
  `target - camera` shorthand to the actual
  `(vehicle - camera) + worldOffset` sequence. Confirmed
  `error.x² + (error.y² + error.z²)`, strict threshold comparison, exact
  binary32 bias/damping constants, `(factor * speed) * error`, and no `dt`.
- Added `LegacyGameplayCameraChase`, an allocation-free `noexcept` contract for
  runtime-column offset rotation, world-up Y, nonlinear close-target damping,
  per-axis movement factors, and candidate camera position. Non-finite input
  and derived overflow fail atomically.
- Kept portal tracing, sphere/line collision, look-at rotation, and final pose
  publication outside this stateless stage. The portable implementation
  preserves recovered algebra/grouping but does not claim bit-identical x87
  extended precision across modern targets.
- Independent review found no P0-P3 issue and independently built/passed the
  focused test. A fresh release Ninja build completed all 184 steps and all 55
  CTest targets pass. The public-boundary scan checks 288 files; its synthetic
  suite now also protects local Ghidra application data. Ghidra/Rizin wrapper
  tests, Rizin normalization tests, `actionlint`, and `git diff --check` pass.

## 2026-07-27 - vehicle quaternion camera adapter

- Recovered `CcMatrixRot::FromQuat` at `Cc.dll` RVA
  `[0x2E50,0x2F2A)`: `(w,x,y,z)` fields, exact element order, pairwise
  product/sum/doubling grouping, and one final binary32 spill per matrix
  element. The function does not normalize or validate its input.
- Added `LegacyQuaternionRotation`, an allocation-free `noexcept` adapter that
  publishes the recovered mathematical column matrix for
  `applyRuntimeColumn`. It preserves zero and non-unit quaternion behavior and
  fails atomically on non-finite input or final matrix overflow. Portable
  `double` staging avoids premature binary32 rounding but deliberately does
  not claim universal bit identity with x87 extended precision.
- A deeper event-5 audit also corrected the previous negative finding about
  chase-axis factors. `AirCraft.type` RVA `0x2F60` regenerates each factor
  toward one using `refreshArgument * 0.25`, clamps to `[0,1]`, or clears it
  when the two recovered gates fail. The refresh argument's physical unit and
  behavior of other vehicle types remain open.
- Independent review found and resolved a pre-cast range-check gap and an
  overstrong x87 precision claim; focused re-review reports no remaining
  P0-P3 issue. A separate numerical audit supplied counterexamples proving why
  the portable `double` approximation must remain explicitly distinct from
  universal x87 bit identity. A fresh release Ninja build completed all 187
  steps and all 56 CTest targets pass. The public-boundary scan checks 291
  files; wrapper tests, report normalization, `actionlint`, and
  `git diff --check` are clean.
- Corrected the portable CI Windows runner from `windows-2022` to
  `windows-2025`, matching the repository's required status-check context.
  The prior mismatch left a fully green PR unmergeable even though no review
  approval was required.

## 2026-07-27 - gameplay-camera collision and look-at primitives

- Closed the backend-neutral event-5 camera algebra: exact near-plane sphere
  multiplier, collision-axis reduction, `AirCraft` factor recovery, normalized
  vehicle-to-camera line interpolation, raw portal-call arguments, and
  `CcAxisRot::FromDirection` followed by the mode-zero X/Y look-at matrix.
- Added `LegacyGameplayCameraCollision`, a pure allocation-free `noexcept`
  boundary. It rejects non-finite inputs, intermediate overflow, invalid near
  planes, out-of-range line fractions, and the still-unverified zero-direction
  look-at case without partial output.
- Verified the camera matrix convention through the existing recovered
  world-to-camera transform: right, up, and asymmetric `(1,1,1)` golden
  matrices map the target to positive camera-space Z. The widened
  trigonometric path is a portable approximation and does not claim universal
  x87/libm bit identity.
- Kept the real sphere contact solver, BSP line trace, room/portal mutation,
  dynamic-object collision, and stateful camera publication outside this
  module. Those remain the next retained-spatial-backend integration step.
- Independent review reports no P0-P3 finding and passes the focused GCC
  build with warnings as errors. The complete local build and all 57 CTest
  targets pass; the public-boundary scan checks 294 files, the function
  catalogue contains 209 unique entries, and Ghidra/Rizin wrapper tests,
  12 Rizin normalization tests, `actionlint`, and `git diff --check` pass.

## 2026-07-27 - retained mission BSP and portal line backend

- Cross-checked `CcBspNode`, `CcBspNodePoly`,
  `PhLine::TraceBspNode`, `GetPortalBspCollision`, and
  `CcRoom::TracePortals` with Ghidra instructions and Rizin. Child A is the
  negative plane side, child B is nonnegative, and both tree and polygon
  runtime lists reverse physical CCF order through prepend insertion.
- Recovered the exact near/node/far traversal, `FLT_MAX` nearest seed,
  binary32 `1e-6` endpoint gate, strict three-edge XOR inclusion test,
  one-sided default, explicit binary32 direction/distance/fraction stores,
  binary32 endpoint/polygon-relative and reconstructed-edge stores, `z+y+x`
  split-dot grouping, the asymmetric X/Y-versus-Z hit-point spill schedule,
  unordered-`t` far traversal, strict equal-fraction tie behavior, and
  unclamped hit fraction.
- Confirmed that portal continuation requires mesh `selectionFlagB`, placed
  object `portalType == 0`, and nonzero `rawFlag`; the third float argument to
  `CcRoom::TracePortals` is unused in this game build.
- Added `MissionWorldSpatialArena`, a bounded pointer-free source-world copy of
  authenticated BSP for every runtime room. It translates portal targets
  through semantic source/physical-room contributors, excludes
  placed-scene-disabled ghost collision, preserves the legal face-normal NaN
  sentinel, authenticates a supplied catalog by canonical rebuild-and-compare,
  and fails atomically before payload allocation on dependency, structure,
  float, aggregate physical-room/tree/node/polygon, or retained-byte errors.
- Added allocation-free source-world static/portal line tracing and bounded
  multi-room portal continuation. The explicit transition limit and
  non-disableable hard cap of 256 are a portable safety divergence from the
  original unbounded recursion.
- Integrated the arena into `LoadedMissionWorldRoom`, published CPU accounting,
  detailed loader progress/issues, and the allocation-free publication
  validator. Publication checks exact tree-reference permutation, node
  reachability/acyclicity, ranges, spatial and total published byte accounting,
  and source/target ownership. Synthetic tests cover ordering, target
  translation, gates, binary32 ties, unordered crossings, sentinels, forged
  catalogs, invalid topology, and exact/one-under budgets. Original assets and
  derived payloads remain outside Git.
- Three independent final reviews exposed and closed binary32 cancellation
  and 1-ULP hit-point cases, forged-catalog/pre-allocation hazards, missing
  topology/accounting proofs, and an allocating transform check inside the
  nominally allocation-free publication validator. The corrected validator
  uses `tryComposeNodeTransforms`; a global-allocation counter proves zero
  allocations for a complete valid player room.

## 2026-07-27 - runtime/source-world spatial adapter

- Added an allocation-free `noexcept` render-layer boundary that converts
  runtime segments through the mission `BasisTransform`, queries the retained
  source-world BSP, and transforms authored hit points and plane normals back
  to runtime coordinates. Points use the established basis and positive unit
  scale in both directions; normals use inverse transpose without a duplicate
  reflection flip and remain explicitly unnormalized plane covectors.
- Kept the source tracer's non-clamped legacy fraction as diagnostic evidence
  while adding the typed `outOfSegmentHit` result required by a future camera
  state publisher. No fraction is clamped and no hit point is reconstructed
  from a modified value.
- Added an opt-in strict portal mode that validates every local hit in
  `[0,1]` before its gate, segment advance, or room change. The runtime portal
  adapter always enables it, preventing an earlier invalid hop from being
  hidden by a later valid hit or partially publishing an intermediate room.
- Synthetic tests distinguish inverse transpose from direct normal mapping
  under shear, cover scale and reflection, both epsilon directions, exact
  endpoints, valid and invalid multi-room transitions, fail-closed basis/input
  handling, and 4,096 allocation-counted line/portal calls.
- Independent review found no blocking or significant issue. Its one
  nonblocking coverage suggestion was closed with a direct runtime-adapter
  test proving that a valid first hop followed by an invalid second hop keeps
  the intermediate room diagnostic-only.
- A fresh Release Ninja build completes all 199 steps and all 60 CTest targets
  pass. The code-intelligence build and its 60-test suite also pass, and the
  generated database contains the new translation units. Focused clangd checks
  with the selected trusted GCC query driver, the public-boundary suite,
  12 Rizin normalization tests, `actionlint`, and `git diff --check` are clean.

## 2026-07-28 - camera position and room-state proposal

- Added an allocation-free `noexcept` boundary that joins a candidate runtime
  camera position with the final room selected by strict portal traversal.
  Valid results expose one complete `{position, room}` proposal; every failure
  exposes no state, even after one or more diagnostic portal transitions.
- Preserved recovered semantics for blocked portals and net room cycles while
  keeping the portable hard transition ceiling and exact per-hop `[0,1]`
  fraction policy explicit. A zero limit fails before the first room change.
- Kept sphere/contact resolution, dynamic-object collision, synchronization,
  mutable simulation ownership, and Metal publication out of this narrow
  stage. The proposal is semantic all-or-nothing output, not a claim of
  lock-free or thread-safe mutation.
- Synthetic tests cover identity and transformed mission bases, valid,
  blocked, cyclic, invalid, out-of-segment, and transition-limit paths plus
  4,096 allocation-counted hot-path calls. No original assets are required.
- A fresh Release Ninja build completes all 202 steps and all 61 CTest targets
  pass. The code-intelligence build and its 61-test suite also pass; its
  generated database contains both new translation units. Focused clangd
  checks report zero errors, and the public-boundary suite checks 309 files.
  Ghidra/Rizin wrapper tests, 12 Rizin normalization tests, `actionlint`, and
  `git diff --check` are clean.

## 2026-07-28 - camera sphere-contact material boundary

- Generated fresh addressed Ghidra decompilation and instruction reports for
  the sphere/triangle test, closest-feature selector, constraint plane
  insertion and movement projection, then independently confirmed the
  sphere-test boundary with Rizin/rzpipe.
- Proved that the first u32 in CCF material child `0x2152` is native
  `CcMaterial + 0x5C`. The gameplay camera deletes collected sphere contacts
  whose non-null material carries raw value `0` or `8`; the values are not
  assigned guessed enum names.
- Promoted that optional raw u32 into material metadata, resolved each BSP
  triangle's material source-locally only when unique, and retained it in the
  pointer-free mission spatial polygon. Missing or ambiguous material
  references remain nullable and conservative.
- Recorded the exact static BSP radius pruning, polygon construction, native
  sphere-test requirement, closest-penetration fields, constraint loop, and
  separate dynamic-object boundary. The complex closest-feature mathematics
  remains unimplemented until its helper predicate, thresholds, and golden
  face/edge/vertex fixtures are recovered.
- A fresh Release/Ninja build completed all 202 steps and all 61 portable tests
  pass. The code-intelligence preset completed all 135 rebuilt steps and its 61
  tests pass. The public-boundary scan checks 310 files; Ghidra, working-copy,
  and Rizin wrapper tests, 12 Rizin normalization tests, the 221-entry function
  catalogue, `actionlint`, and `git diff --check` are clean.

## 2026-07-28 - camera constraint projection

- `EV-20260728-002` / `EXP-20260728-015`: fresh Ghidra instructions and
  decompilation recovered `CcConstraintPlane::Overrides` at RVA `0x26230`.
  Rizin independently matched its automatic 127-byte boundary and raw
  instruction sequence.
- Exported the exact native double constants: duplicate normals are suppressed
  only for dot products strictly above `0.999`, while a two-plane cross product
  stops movement only when its squared length is strictly below `0.0001`.
- Added allocation-free `noexcept` C++20 primitives for plane admission,
  directional override comparison, native oldest-active selection,
  single-plane projection, two-plane edge projection, and conflicting-plane
  stop behavior. Explicit binary32 spills preserve the relevant x87 data-flow
  boundary while non-finite inputs fail atomically.
- A fresh Release/Ninja build completed all 202 steps and all 61 portable tests
  pass; the code-intelligence build and its 61 tests also pass. The
  public-boundary scan checks 311 files, the function catalogue contains 222
  unique entries, and the Ghidra/Rizin/working-copy wrapper tests, 12 Rizin
  normalization tests, `actionlint`, and `git diff --check` are clean.

## 2026-07-28 - camera static sphere resolver

- `EV-20260728-003` / `EXP-20260728-016`: closed the finite nondegenerate
  `PhSphere::GetCollision` seven-axis candidate test and
  `PhCollidedPolyList::GetCollisionAndBestFree` face/edge/vertex selector.
  Rizin independently discovered the selector's exact 3,049-byte automatic
  boundary and its six squared-vector helper calls.
- Preserved native irregularities that a generic closest-point replacement
  would lose: strict versus inclusive tangent branches, stored
  `edge01`/`edge02` rounding, raw non-normalized edge/vertex directions, and
  reverse-discovery first-wins depth ties.
- Added an allocation-free `noexcept` static BSP resolver with B-then-A
  traversal, visible type-zero portal-room discovery, material modes `0`/`8`
  filtering, iterative candidate re-test, and caller-owned candidate/constraint
  workspaces. Non-orthonormal runtime bases fail closed because they would map
  the source sphere to an ellipsoid.
- Synthetic fixtures cover every face/edge/vertex branch, tangency, material,
  visible/invisible/nonzero portal behavior, adjacent-room discovery, limits,
  scale, basis, workspace, tie, and the 4,096-call allocation boundary.
  Dynamic-object BSP and synchronized camera-state publication remain separate.
- A fresh Release/Ninja build completed all 205 steps and all 62 portable tests
  pass. The code-intelligence build and its 62 tests also pass. The
  public-boundary scan checks 315 files; Ghidra, working-copy, and Rizin wrapper
  tests, 12 Rizin normalization tests, the 222-entry unique function catalogue,
  `actionlint`, and `git diff --check` are clean.

## 2026-07-28 - camera static collision state integration

- `EV-20260728-004` / `EXP-20260728-017`: joined the recovered event-5 static
  order without changing the existing portal-only primitive: exact near-plane
  sphere radius, static BSP correction, per-axis factor reduction, then portal
  tracing of the corrected endpoint.
- Added one allocation-free `noexcept` proposal containing corrected runtime
  position, final room, and all three axis factors. Candidate/constraint
  exhaustion, invalid inputs or basis, sphere-room limits, and later portal
  failures retain stage diagnostics but expose no partial proposed state.
- Synthetic integration fixtures prove that correction can prevent a raw
  candidate's portal transition or precede a valid transition, and protect
  workspace failures, factor failures, portal-limit failure, input
  immutability, and 4,096 allocation-counted complete calls.
- A fresh Release/Ninja build completed all 205 steps and all 62 portable tests
  pass. The code-intelligence build and its 62 tests also pass. The
  public-boundary scan checks 316 files; Ghidra, working-copy, and Rizin wrapper
  tests, 12 Rizin normalization tests, the 222-entry unique function catalogue,
  `actionlint`, and `git diff --check` are clean.

## 2026-07-28 - camera state owner and immutable snapshot

- `EXP-20260728-018`: placed the complete static camera proposal behind a
  non-copyable, non-moving single-writer owner. Initialization publishes
  generation one; later commits require strictly increasing simulation steps
  and never split position, room, or the three axis factors.
- Added a bounded two-slot SPSC publication protocol. Writer and reader claims
  prevent slot overwrite during a copy, generation revalidation rejects races,
  and the render consumer receives an owning immutable value rather than a
  pointer or span into mutable storage.
- Invalid initialization, failed/missing/malformed proposals, stale steps,
  generation exhaustion, and contention preserve the exact prior producer and
  published snapshots. The hot commit/acquire/current path is allocation-free
  and `noexcept`.
- Synthetic tests cover lifecycle, atomic rejection, transitions, monotonic
  steps/generations, 20,000 concurrent coherent frames repeated in 100 focused
  stress runs, and 4,096 allocation-counted operations. A fresh Release/Ninja
  build completed all 208 steps and all 63 tests pass; the code-intelligence
  build and its 63 tests also pass. The public-boundary scan checks 320 files;
  the Ghidra, working-copy, and Rizin wrapper tests pass; 12 Rizin normalization
  tests pass; and `actionlint` is clean. An isolated Linux GCC 13
  ThreadSanitizer build also passes the concurrent test with per-process ASLR
  disabled only to satisfy WSL2's TSan shadow-memory mapping constraint.
  Cross-platform and unsigned iOS validation remain required before merge.

## 2026-07-28 - retained-static line and gameplay-camera pose snapshot

- `EV-20260728-005` / `EXP-20260728-019`: completed the retained current-room
  static portion of the native vehicle-to-camera line stage. It consumes only
  a complete sphere/factor/portal result, preserves a miss, and on a hit
  applies the tracer-authored runtime point plus the conditional second portal
  update before exposing position, room, and unchanged factors.
- Tightened the single-writer camera owner so it accepts only the completed
  retained-static frame result. Invalid intermediate, line, room, limit, and
  out-of-segment outcomes retain diagnostics but cannot replace the prior
  producer or render snapshot.
- Canonical Ghidra evidence and independent read-only Rizin 0.9.1 exports
  agree that `CcMatrixRot::Reset` and
  `CcSrtNode::SetMatrixRotation` establish identity basis, unit scale, and unit
  cached inverse-square. The gameplay pose therefore needs no guessed scale.
- Added an immutable backend-neutral pose snapshot binding one acquired step
  and generation to the matching vehicle anchor, recovered look-at,
  unit-scale world-to-view relation, and explicit `0.25/200/90`, 640x480
  scalar projection. Ordered world projection preserves camera Z, legacy
  reciprocal depth, and near fallback without introducing clipping, NDC,
  Metal layout, or presentation policy.
- Synthetic tests cover line miss/hit/second-room transition, atomic
  failures, frame ownership, arbitrary look-at directions, positive-Z anchor
  coherence, ordered world projection, and 4,096 allocation-counted calls.
  Dynamic-object and transparent collision-portal paths remain separate.
- A fresh Ninja/GCC 15.2 Release build and the regenerated
  `code-intelligence` build each pass all 64 portable tests. Focused clangd 22
  checks pass with the trusted user-local MinGW query driver; no local path is
  committed. The public-boundary scan checks 324 files, the
  Ghidra/working-copy/Rizin wrapper tests and 12 Rizin normalization tests
  pass, and `actionlint` plus `git diff --check` are clean.

## 2026-07-28 - Metal gameplay-camera clip packet

- `EV-20260728-006` / `EXP-20260728-020`: derived a backend clip packet whose
  homogeneous divide exactly reproduces the recovered screen X/Y and
  `near / cameraZ` reverse depth. Standard homogeneous clipping supplies the
  near plane; an explicit `far - cameraZ` Metal clip distance supplies the
  separately recovered inclusive far plane.
- Added an immutable allocation-free C++20 packet and an explicit camera0
  bootstrap factory. The bootstrap binds generation one/step zero to the
  authenticated spawn transform and room, begins directly at the recovered
  target, and invents no predecessor or smoothing history.
- Private mission-room Metal preparation now owns that packet. Its separate
  gameplay shader preserves local-to-world, subtraction, three basis dot
  products, cached inverse-square, and scalar projection order. The pipeline
  uses recovered `greaterEqual` depth writes, zero depth clear, and a
  parity-first aspect-fitted 640x480 viewport. The public synthetic path keeps
  its diagnostic pipeline.
- Synthetic tests prove homogeneous/scalar agreement, reverse-depth and
  near/far boundaries, offset centres, invalid/overflow failures, bootstrap
  provenance, and 4,096 allocation-counted packet operations. A fresh
  Ninja/GCC 15.2 Release build and code-intelligence validation each pass all
  65 portable tests. Pull-request CI passes the clangd preset plus Ubuntu,
  Windows, ARM64 macOS 26, unsigned `iphoneos`, and unsigned
  `iphonesimulator`; Xcode 26.6 accepts the Objective-C++ and Metal contracts.

## 2026-07-28 - transactional gameplay-camera step coordinator

- `EV-20260728-007` / `EXP-20260728-021`: composed the recovered AirCraft
  producer order into one portable transaction: factor recovery, persistent
  mode and momentary rear selection, chase, static sphere/factor/portal
  correction, retained vehicle line and second portal update, look-at pose,
  homogeneous clip packet, then atomic state commit.
- Kept the unproven factor-recovery refresh argument and both recovered vehicle
  gates as explicit caller inputs. The coordinator does not substitute 60 Hz,
  the aircraft's 12 ms dependent interval, or another guessed `dt`, so native
  iOS integration remains intentionally separate.
- Mode, cumulative camera-cycle count, state, and returned packet change only
  after every stage and the single-writer commit succeed. Rear view never
  mutates the persistent mode; skipped accepted input counts cycle modulo
  three.
- Synthetic tests cover initialization provenance, complete stationary steps,
  separation of native chase-position and vehicle-anchor inputs, factor
  clear/recovery ordering, mode/rear input, early and late failure rollback,
  stale commits, and 4,096 allocation-counted successful steps. Fresh Release
  and code-intelligence builds both pass the current 66/66 portable suite;
  focused clangd 22 checks report zero errors.

## 2026-07-28 - mission-lifetime gameplay-camera packet exchange

- `EV-20260728-008` / `EXP-20260728-022`: added a fixed two-slot SPSC exchange
  for complete owning gameplay-camera clip packets. Camera step/generation
  monotonicity is independent of the exchange's contiguous ABA-resistant slot
  generation, so one busy render slot can drop a packet without wedging later
  complete camera publications.
- Move-only leases retain private shared storage, prevent slot overwrite, and
  remain readable after the facade lifetime. Publish/acquire are bounded,
  `noexcept`, and allocation-free; a failed race exposes no partial packet.
- Private Metal mission snapshots now own the exchange, account its storage
  under the retained CPU ceiling, validate the bootstrap through an initial
  lease, and hold one lease through each camera-dependent command encoding.
  No simulation producer endpoint or guessed refresh time was introduced.
- Tests cover rejection atomicity, two retained slots, a later generation
  after `busy`, move/lifetime behavior, 4,096 zero-allocation cycles, and
  20,000 concurrent coherent publications. Release and code-intelligence
  builds pass 67/67 tests; focused clangd 22 checks report zero errors.

## 2026-07-28 - gameplay-camera refresh time contract

- `EV-20260728-009` / `EXP-20260728-023`: closed the remaining AirCraft camera
  factor time join. `AfVehicle::ProcessEvent` converts the signed 64-bit event
  delta from milliseconds with exact `0.001f`, and both full and skipped
  physics paths pass that same binary32 seconds value to primary vtable slot
  `+0xB0`.
- Ghidra and an independently regenerated Rizin report agree on both caller
  sequences, the AirCraft target boundary `[0x10002F60,0x10003F1D)`, and the
  same stack argument at all three factor-recovery sites. Rizin's inferred
  calling convention and argument count remain rejected in favor of Ghidra's
  MSVC context and the native `ret 4`.
- AirCraft's recovered 12 ms dependant interval supplies nominal `0.012f`
  seconds (`0x3C449BA6`), so an unobstructed factor recovers by `0.003f` per
  nominal aircraft step. This is not the separate 60 Hz input-pump interval.
- Renamed the portable input to `refreshDeltaSeconds`, exposed the exact
  nominal AirCraft value, and added a nominal recovery test. The coordinator
  still requires the live aircraft gates and dynamic pose inputs before a
  native producer endpoint may replace the camera0 bootstrap.
- Fresh Release and code-intelligence builds compile all 220 steps and pass
  67/67 portable tests; focused clangd 22 checks are clean. The three RE
  wrapper suites, 12 Rizin normalization tests, public-boundary tests and
  337-file scan pass; the 224-entry function catalogue, `actionlint`, and
  `git diff --check` are clean.

## 2026-07-28 - gameplay-camera mission runtime and weak endpoint

- `EXP-20260728-024`: added one non-copyable, non-moving mission runtime that
  owns the authenticated arena by move, runtime basis, exact-size
  sphere-candidate and constraint workspaces, camera coordinator, and complete
  packet exchange. Creation performs all allocation and validates the camera0
  packet through its first lease.
- Workspace counts are bounded by the retained polygon count and explicit
  two-million-record ceilings. Checked additional storage covers both
  workspaces plus the exchange. iOS tightens that byte limit to the remainder
  of its existing 128 MiB private CPU-packed budget; the already admitted arena
  is transferred without copying or double charging.
- A private mission snapshot now strongly owns the runtime and Metal acquires
  packets through it. The main-thread room transaction also installs a weak
  producer endpoint. Mission replacement expires that endpoint, while a
  previously locked runtime remains self-contained and a retained render lease
  keeps its exact exchange slot readable.
- `tryAdvance` and `tryAcquire` are bounded, `noexcept`, and allocation-free.
  Coordinator failure never reaches transport; `busy` advances authoritative
  camera state but drops only that visual packet, and a later generation may
  publish. The iOS input consumer deliberately does not call the endpoint
  until all distinct live AirCraft fields and the scheduler delta exist.
- Synthetic tests cover ownership preflight, limits, initialization,
  publication, busy generation skips, rollback, weak/lease lifetime, and 4,096
  zero-allocation steps. The local Release suite passes 68/68 tests; the
  regenerated code-intelligence target builds and focused clangd 22 checks
  report zero errors. Apple Objective-C++/Metal compilation remains a required
  CI gate for this change.

## 2026-07-28 - named AirCraft camera input contract

- `EV-20260728-010` / `EXP-20260728-025`: Ghidra's exported
  `NfActor::GetHealth` symbol proves that instance float `+0x98` is current
  health. `AfVehicle::Activate` clears byte `+0x460`, while `Deactivate` and
  death paths set it, establishing the broader inactive-latch meaning.
- Independently generated Rizin reports confirm the exact getter and lifecycle
  boundaries, field widths and writes, plus the health/inactive tests in the
  AirCraft slot-44 target. Ghidra remains canonical for typed MSVC signatures;
  Rizin supplies the instruction-level cross-check.
- The complete camera input map now names distinct chase position
  `+0x278..+0x280`, world anchor `+0x1A4..+0x1AC`, quaternion
  `+0x284..+0x290`, live health, inactive state, and scheduler delta in
  seconds. The portable API now says `vehicleHealth` and `vehicleInactive`
  instead of exposing offset-derived placeholder names.
- This closes naming, not live publication. The weak iOS endpoint remains
  inactive until one reconstructed producer owns changing pose, health,
  lifecycle, and the native 12 ms scheduler event as a coherent step.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds compile all 223
  steps and pass 68/68 tests; clangd 22.1.8 reports zero errors in all five
  changed C++ translation units. The three RE wrapper suites, 12 Rizin tests,
  342-file public scan, 227-entry catalogue, `actionlint`, local-path scan, and
  `git diff --check` pass.

## 2026-07-28 - AfVehicle rest/sleep gate

- `EV-20260728-011` / `EXP-20260728-026`: proved that `AfVehicle +0x458` and
  `+0x45C` are the low and high DWORDs of one signed 64-bit rest-duration
  accumulator in scheduler milliseconds. The constructor writes 1999 ms and
  the refresh entry gate skips active physics at 2000 ms.
- Ghidra establishes the MSVC control flow and exported
  `AfVehicle::IsOnGround` symbol; independently regenerated Rizin reports
  confirm the constructor writes, byte `+0x464` getter, five exact-zero control
  tests, strict `0.03f` ground and `0.08f` water speed-squared thresholds,
  signed `ADD`/`ADC`, reset, and threshold comparison.
- The threshold-crossing step still integrates and then resets force/torque
  plus six rigid-body dynamic-state floats. A later sleeping refresh skips the
  active path but still invokes the post-collision refresh slot. AirCraft's
  water-unit predicate is false, so only its on-ground leg applies.
- Added a pure, allocation-free C++20 transition with exact recovered
  constants, signed delta behavior, one-shot clearing, strict predicates, and
  explicit checked-overflow deviation. It deliberately accepts precomputed
  velocity squared and remains separate from the frozen player simulation and
  control-event wake transition.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  226 steps and pass 69/69 portable tests; focused clangd 22.1.8 checks report
  zero errors. The three reverse-engineering wrapper suites, 12 Rizin tests,
  346-file public scan, 228-entry catalogue, `actionlint`, local-path scan, and
  `git diff --check` pass. GitHub Actions publication gates remain pending.

## 2026-07-28 - AirCraft engine-start and thrust-integrity state

- `EV-20260728-012` / `EXP-20260728-027`: proved that byte `+0x564` is active
  only during the engine-start sound transition. Slot 44 sets it above the
  strict `0.001f` smoothed-thrust threshold, accumulates the seconds timer at
  `+0x55C`, and clears it after the timer becomes strictly greater than
  `4.0f`. The byte selects the exact slow `0.0004f` smoothing branch in the
  force step; the normal branch uses the recovered `0.3f` and `0.02f`
  recurrence.
- Proved that float `+0x568` is a thrust-integrity factor initialized to one.
  Slot 30 qualifies collision responses with strict `0.9f` and `2.0f` gates,
  subtracts half the average normal-velocity-dot fourth power without
  clamping, and slot 44 then applies the exact `0x38000100` random recovery
  scale and ordered `[0,1]` clamp. The result first affects the next force
  step; recovery continues through the sleeping path.
- Ghidra supplies canonical MSVC/vtable/sound context. Independently generated
  Rizin reports match all four function boundaries, field widths, addresses,
  constants, and branch order. Original labels remain unavailable, so
  `engineStartTransitionActive` and `thrustIntegrityFactor` are explicit
  behavior-backed working names.
- Added three pure allocation-free C++20 helpers for smoothing, collision
  degradation, and recovery/clamp. Non-finite values and arithmetic overflow
  fail closed only when their native branch is reached; rejected branch inputs
  remain uninspected. The global native PRNG is replaced by a caller-supplied
  bounded sample and the helpers remain unwired from the frozen intent-only
  player simulation.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  229 steps and pass 70/70 portable tests. Clangd 22.1.8 reports zero errors in
  both new translation units. The three RE wrapper suites, 12 Rizin tests,
  public-boundary tests and 350-file scan, 228-row catalogue, `actionlint`,
  local-path scan, and `git diff --check` pass. GitHub Actions publication
  gates remain pending.

## 2026-07-28 - AirCraft engine-audio cadence and modulation

- `EV-20260728-013` / `EXP-20260728-028`: completed the engine-only subset of
  AirCraft primary vtable slot 44. The timer at `+0x55C` advances on every call
  while starting, while the counter at `+0x5E8` evaluates transitions and
  sound parameters exactly every fifth call.
- Confirmed strict start, four-second completion, and shutdown thresholds plus
  the ordered `0x1A..0x1E` engine-on, idle, turn, start, and stop roles. The
  native dependency strings were checked read-only; no sound file, binary,
  path, or generated report enters Git.
- Closed the producer/consumer join for `+0x558`: the flight step low-pass
  filters orientation matrix `m01`, and slot 44 uses its absolute value to
  modulate engine-turn volume. Recovered the exact speed/thrust load, upper-only
  running-volume clamp, running/turn pitch, complementary idle volume, mixer
  update, and model-parameter order.
- Added a pure allocation-free C++20 transition returning caller-owned state,
  adjusted smoothed thrust, and at most 14 ordered commands. It contains no
  playback backend or original asset dependency, validates only values reached
  by the native branch, and remains unwired from the intent-only player state.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  232 steps and pass 71/71 portable tests. Clangd 22.1.8 reports zero errors in
  both new translation units. The three RE wrapper suites, 12 Rizin tests,
  public-boundary tests and 354-file scan, 228-row catalogue, `actionlint`,
  local-path scan, and `git diff --check` pass. GitHub Actions publication
  gates remain pending.

## 2026-07-28 - AirCraft destroyed-dive audio

- `EV-20260728-014` / `EXP-20260728-029`: resolved sound ID `0x20` as
  `enginedive`. The AirCraft type-load entry registers that name and ID, and a
  read-only `Resource.up` metadata listing contains the matching logical WAV
  entry; no proprietary payload or generated report enters Git.
- Slot 44 reaches the state only on its shared fifth-call audio cadence.
  Health `<= 0` starts or retains the sample, fixes parameter 1 at one, and
  drives parameter 0 with the ordered
  `clamp(-0.15 * velocity.y - 0.2, 0, 1)` equation. Positive health stops an
  active sample without querying velocity.
- Ghidra supplies canonical MSVC/vtable and sound-method meaning; an
  independently regenerated Rizin function closes the missed type-load
  boundary and confirms exact slot-44 branches, fields, calls, scalar bits,
  and x87 order. The AirCraft constructor never initializes byte `+0x566`;
  portable state deliberately defaults false instead of reproducing undefined
  behavior.
- Added a pure allocation-free C++20 transition with caller-owned state and at
  most three existing audio-command records. It validates only consumed branch
  inputs, owns no sample/backend/path, and remains unwired until a coordinator
  composes it between phase transitions and common modulation.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  235 steps and pass 72/72 portable tests. Clangd 22.1.8 reports zero errors in
  both new translation units. The three RE wrapper suites, 12 Rizin tests,
  public-boundary tests and 358-file scan, 229-row catalogue, `actionlint`,
  local-path scan, and `git diff --check` pass. GitHub Actions publication
  gates remain pending.

## 2026-07-28 - primary WpMGun timing and spawn request

- `EV-20260728-015` / `EXP-20260728-030`: joined
  `0xB9 EVENT_PRIMARY_ATTACK` to the AirCraft primary pointer and persistent
  `AfWeapon::Fire(bool)` state, then through the separate `WpMGun`
  time-dependant refresh to private `WpMGunAmmoTechN` creation and projectile
  event `0xE2`.
- Recovered the `0x210`-byte weapon factory/constructor, adjusted
  time-dependant subobject, exact five-level `0.12..0.08` second cadence,
  `40..100` projectile speeds, initial/maximum ammunition `250/500`, barrel
  cycling, nonzero-ammunition decrement, tenfold zero-count cadence, and the
  one-projectile-per-refresh ceiling.
- Ghidra supplies the canonical MSVC/vtable/type/event context. Independent
  Rizin reports match both AirCraft handlers and the selected `Projectiles`
  functions, including exact x87 comparisons and field accesses; guarded
  function creation is recorded where automatic analysis missed a
  vtable-proven entry.
- Added a pure allocation-free C++20 state transition that emits at most one
  `{event 0xE2, barrel, speed}` request. It deliberately leaves private type
  creation, room/muzzle/velocity payload assembly, motion, collision, damage,
  effects, and secondary weapons outside this slice.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds pass all 73
  portable tests; the code-intelligence tree compiles 238 steps. Clangd 22.1.8
  reports zero errors in both new translation units with the trusted MinGW
  query driver. The three RE wrapper suites, 12 Rizin tests, public-boundary
  tests and 363-file scan, 237-row catalogue, `actionlint`, local-path scan,
  and `git diff --check` pass. GitHub Actions remains the publication gate.

## 2026-07-28 - WpMGun projectile payload and contact contract

- `EV-20260728-016` / `EXP-20260728-031`: completed the static join from
  `WpMGunAmmoTechN` type/instance creation through packed projectile event
  `0xE2`, shared activation and flight, actor damage, surface contact, and the
  private ricochet request.
- Recovered exact damage `3..7`, zero aircraft broad-hit radius, four-second
  lifetime, acceleration bits `0xBF96D5CF`, every semantic payload field, zero
  target UID, target-velocity lead, normalization fallback, strict expiry
  comparison, damage event `0x7D`, material-15 impact interpolation, and the
  ordered five-event ricochet setup.
- Ghidra 12.1.2 provides canonical MSVC types, exports, vtables, packed fields,
  and high-level pseudocode. Rizin 0.9.1 independently confirms boundaries,
  instructions, branches, immediates, and calls; functions created only after
  direct vtable evidence are explicitly marked as guarded rather than
  automatic.
- Added pure allocation-free C++20 contracts for the five ammo profiles,
  complete spawn payload and lead, active state, unobstructed ballistic/lifetime
  transition, actor damage, surface deactivation, and bounded ricochet request.
  Live mission objects, private allocation, room/BSP/dynamic-actor collision,
  tracer/effect realization, event dispatch, and secondary weapons remain
  explicit runtime boundaries.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  241 steps and pass 74/74 portable tests. Clangd 22.1.8 reports zero errors in
  both new translation units. The three RE wrapper suites, 12 Rizin tests,
  public-boundary tests and 367-file scan, 251-row catalogue, `actionlint`,
  local-path scan, and `git diff --check` pass. GitHub Actions remains the
  publication gate.

## 2026-07-28 - transactional WpMGun shot preparation

- `EXP-20260728-032` joins `EV-20260728-015` and `EV-20260728-016` at the
  runtime-facing boundary: cadence, barrel, and nonzero-ammunition state
  advance before private ammo allocation; event `0xE2` is assembled and
  dispatched only after allocation succeeds.
- Added an allocation-free C++20 coordinator that caps technology at four,
  selects one coherent already-rotated muzzle snapshot, joins the exact ammo
  profile and complete payload, and returns the advanced fire state beside the
  prepared request. Payload-only inputs are not consumed when no shot is due.
- Audited the retained mission BSP tracer as the next possible adapter. It
  carries static fractions, normals, and material collision values, but the
  complete native projectile loop also requires dynamic actor ownership,
  native-room mapping, and combined static/portal ordering. This slice
  therefore does not claim premature `DetectCollisions` parity.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  244 steps and pass 75/75 portable tests. Clangd 22.1.8 reports zero errors in
  both new translation units. The three RE wrapper suites, 12 Rizin tests,
  public-boundary tests and 371-file scan, 251-row catalogue, `actionlint`,
  local-path scan, and `git diff --check` pass. GitHub Actions remains the
  publication gate.

## 2026-07-28 - native world-room ID mapping

- `EV-20260728-017` / `EXP-20260728-033`: recovered the complete room identity
  law needed to join event `0xE2` to the retained mission BSP. `CcWorld` root
  has ID zero, its ordinary-room counter starts at one, and every created room
  is assigned the counter before it increments and is prepended to the
  newest-first list.
- Ghidra 12.1.2 provides the canonical `CcRoom`/`CcWorld` class and export
  meaning. Rizin 0.9.1 independently confirms the automatic boundaries,
  exact `+0x48/+0x8C/+0x78/+0x24` field accesses, root-first comparison, and
  newest-first loop.
- The existing `MissionWorldRoomCatalog` preserves the same successful-load
  order, including name-deduplicated rooms that consume no new identity.
  Added allocation-free bidirectional C++20 helpers using
  `roomCount - worldRoomIndex` for non-root rooms. Incomplete catalogues,
  negative/out-of-range values, and counts outside signed 32-bit IDs fail
  closed.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  244 steps and pass 75/75 portable tests. Clangd 22.1.8 reports zero errors in
  the changed production and test ranges. The three RE wrapper suites, 12
  Rizin tests, public-boundary tests and 372-file scan, 253-row catalogue,
  `actionlint`, nine-file local-path scan, and `git diff --check` pass.
  GitHub Actions remains the publication gate.

## 2026-07-28 - shared projectile collision decision

- `EV-20260728-018` / `EXP-20260728-034`: separated the deterministic
  `NfProjectile::DetectCollisions` decision layer from the still-missing live
  combined spatial iterator.
- Ghidra 12.1.2 confirms the canonical MSVC classes and high-level control
  flow. Rizin 0.9.1 independently confirms the automatic
  `PhLine::GetBspCollision` boundary, static-room list before dynamic objects,
  shared strict-nearest fraction, object-local transforms, recursive visible
  type-zero portal continuation, combined fraction, and exact projectile
  material/actor/position branches.
- Added an allocation-free C++20 decision for no hit, material-8 pass-through,
  client/server and actor-state pass-through, portal continuation, actor
  contact, and surface contact. It preserves callback position swaps,
  normalization, resolved identity, material, room, and material-15 water
  state and composes with the existing WpMGun surface callback.
- The retained static tracer is deliberately not presented as complete:
  moving actor geometry must compete through the same nearest fraction, live
  actor state must supply the native gates, and portal continuation needs a
  portable traversal ceiling.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds each compile all
  244 steps and pass 75/75 portable tests. The compilation database contains
  155 portable entries, including both changed translation units; clangd
  22.1.8 reports zero errors in each. The three RE wrapper suites, 12 Rizin
  tests, public-boundary tests and 373-file scan, 254-row catalogue validation,
  `actionlint`, nine-file local-path scan, and `git diff --check` pass. GitHub
  Actions remains the publication gate.

## 2026-07-28 - dynamic-object BSP and combined line adapter

- `EV-20260728-019` / `EXP-20260728-035`: recovered the complete
  `CcObject::CreateDynamicBsp` per-mesh construction path, mesh-cache
  ownership, `CcMesh::CalcSphere`, deterministic splitter score, native
  prepend order, negative-A/nonnegative-B side mapping, `+/-0.000001`
  classification, zero-plane clipping, and `0.0001` small-fragment rejection.
- Ghidra 12.1.2 supplies canonical MSVC types and readable construction/split
  pseudocode. Independently regenerated automatic Rizin 0.9.1 reports match
  `CalcSphere`, `CreateDynamicBsp`, `CreateBspTree`, and the 4,340-byte
  `CcBspPoly::Split` boundaries, instructions, calls, branches, and constants.
- Added a bounded mission-load C++20 builder over already converted public mesh
  geometry. It publishes an immutable pointer-free local BSP, source
  triangle/material provenance, material collision values, radius, typed
  failures, and retained-byte accounting. Vertex, triangle, material, node,
  retained/working polygon, working-vertex, depth, and byte ceilings fail
  closed.
- Added an allocation-free `noexcept` non-portal `PhLine` adapter. Retained
  room-static geometry and caller-ordered active object instances compete
  through one strict-nearest fraction; exact ties retain static or the earlier
  dynamic object. Object endpoints transform into local space, and a winning
  normal returns through the confirmed orthonormal unit-scale F050 relation.
- Tests cover native list/tie order, balanced and true crossing splits,
  small-fragment rejection, material/provenance retention, all nearest/tie
  combinations, object transforms/activity/broad phase, typed build/runtime
  failures, epsilon out-of-segment rejection, and 4,096 zero-allocation
  traces. Fresh Release and code-intelligence Ninja/GCC 15.2 builds each
  compile 247 steps and pass 76/76 tests. The generated database has 157
  portable entries; clangd 22.1.8 parses both new translation units with zero
  code diagnostics. The three RE wrapper suites, 12 Rizin normalization tests,
  public-boundary tests and 377-file scan, 259-row catalogue with only the two
  pre-existing missing references, `actionlint`, local-path scan, and
  `git diff --check` pass. GitHub Actions remains the publication gate.

## 2026-07-28 - combined line automatic portal continuation

- `EV-20260728-020` / `EXP-20260728-036`: completed the recursive tail of
  `PhLine::GetBspCollision` over the implemented room-static/dynamic strict-
  nearest query.
- Rechecked the typed Ghidra 12.1.2 body at RVA `0x00038BD0`: only a winning
  visible type-zero owner recurses from the exact contact into its target room;
  a later hit replaces polygon/normal provenance and uses
  `outer + (1 - outer) * inner`, while no later hit clears the complete
  collision. The existing independent Rizin 0.9.1 report confirms the exact
  1,175-byte function boundary and recursive call at `0x10038FFF`.
- Added an allocation-free iterative C++20 adapter over one flat object
  publication and per-room ranges. It preserves native list order, translates
  room-local object indices back to absolute provenance, carries portal
  type/target/visibility, and adds typed range, metadata, out-of-segment, and
  maximum-256 transition failures.
- Synthetic tests cover one/two portals, fraction composition, no later hit,
  recursion-unwind rounding, visibility/type gates, strict-nearest static
  blocking, malformed publication, an epsilon-admitted later hit outside its
  subsegment, zero/excessive budgets, self-cycle termination, and 4,096
  zero-allocation portal traces.
- A fresh Release Ninja/GCC 15.2 build compiles all 247 steps and passes 76/76
  tests; its compilation database contains 157 portable entries. Clangd 22.1.8
  reports zero diagnostics for the test translation unit and zero code
  diagnostics for the implementation (its seven reported failures are the
  same optional `ExtractFunction` feature probes seen before this slice).
  The three RE wrapper suites, 12 Rizin normalization tests, public-boundary
  tests and 378-file scan, 259-row catalogue validation with only the two
  pre-existing missing references, `actionlint`, changed-scope local-path
  scan, and `git diff --check` pass. The exact pushed commit also compiles all
  247 steps with GCC 13.3 and passes 76/76 tests in the isolated `Airfix-Dev`
  WSL environment. GitHub Actions remains the final publication gate.

## 2026-07-28 - authenticated player dynamic-collider publication

- `EXP-20260728-037` composes the already confirmed visible-slot
  `AfVehicle::SetSkin` hierarchy (`EV-20260727-004`) with the recovered
  `CcSrtNode` / `CcObject::CreateDynamicBsp` path (`EV-20260728-019`). It adds
  no new binary interpretation and reads no original file outside the existing
  verified-content transaction.
- Added a bounded `PlayerActorCollisionAssembly`. It resolves physical meshes
  and collision materials only through the same parsed player CCF and
  actor-local visual provenance used by rendering, builds immutable per-mesh
  dynamic BSPs in first-use order, and retains hierarchy instances in blueprint
  DFS order. All geometry, material, BSP, count, depth, and aggregate retained-
  byte limits fail atomically with typed issues.
- The authenticated asset is now owned by `LoadedMissionWorldRoom`, charged to
  the global published-CPU budget before scene composition, and covered by the
  allocation-free publication proof. Mesh/instance counts, actor provenance,
  local transforms, nested BSP byte accounting, and present/absent co-occurrence
  must agree with the final player scene.
- Added a two-pass `noexcept` frame publisher. It validates every actor-world
  composition before writing, then fills exact-size caller-owned dynamic object
  and per-room range spans with the supplied live object ID, active flag, pose,
  and room. Failure leaves both outputs untouched; 4,096 successive
  publications perform no observed heap allocation.
- Synthetic tests drive a published player object through the complete
  combined portal-line query and cover ordering, collision materials, exact
  retained bytes, authenticated loader integration, exact/one-under limits,
  provenance/transform/byte tampering, invalid output/room/pose inputs, and
  atomic failures. Fresh Release and code-intelligence Ninja/GCC 15.2 builds
  each pass 77/77 tests; the latter compiles 250 steps and produces 159
  portable compilation-database entries. Focused clangd 22.1.8 checks report
  zero code diagnostics in all four changed translation units (the reported
  nonzero implementation statuses are optional feature-probe failures).
  The three RE wrapper suites, 12 Rizin normalization tests, public-boundary
  tests and 382-file scan, 259-row catalogue with only its two pre-existing
  missing references, `actionlint`, 16-file changed-scope scan, and
  `git diff --check` pass. The exact pushed commit builds all 250 steps with
  GCC 13.3 and passes 77/77 tests in a new isolated `Airfix-Dev` WSL clone.
  GitHub Actions remains the final publication gate.

## 2026-07-28 - placed-object serialized dynamic BSP decoding

- `EV-20260728-021` / `EXP-20260728-038`: recovered the preserved object
  `0x4101` container as the same `F0C0`/`F0C1` node and polygon encoding used
  by room BSP. Ghidra 12.1.2 confirms the loader, first-use `CcMesh + 0x13c`
  cache, reverse prepended root-list order, and scene-complete polygon binding.
- Fresh automatic Rizin 0.9.1/rzpipe 0.6.2 reports independently match the
  161-byte polygon loader at RVA `0x0002A2F0` and the 39-byte deferred-binding
  helper at RVA `0x0002A3E0`. The latter stores the node-polygon pointer,
  polygon index, and placed-object reference in one global pending list.
- Added typed `dynamicObjectTree` metadata while preserving the raw `0x4101`
  wrapper and unknown direct children. Known roots reuse the iterative flat BSP
  decoder and shared depth/tree/node/polygon/descriptor budgets; mission
  retained-byte accounting now includes both the raw descriptors and every
  typed dynamic arena.
- A read-only corpus diagnostic parses all 286 CCF files and all 1,160 wrappers:
  1,160 roots, 10,641 nodes, 16,212 polygons, maximum depth 29, and zero
  non-Boolean child words, owner-reference mismatches, or out-of-range polygon
  indices. No private paths, names, hashes, geometry, or payloads are recorded.
- Synthetic tests cover multiple physical roots, nested children, exact
  polygon fields/order/source tags, preserved extension descriptors, duplicate
  and malformed wrappers, invalid child/polygon shapes, and the shared
  1,024-depth ceiling. The full mission loader exact/one-under retained
  metadata test now contains a typed `0x4101` tree. Runtime
  material/cache/list binding and placed dynamic collision publication
  deliberately remain a later atomic assembly.
- Fresh Release and code-intelligence Ninja/GCC 15.2 builds both pass 77/77
  tests, and the portable compilation database contains 159 entries. Clangd
  22.1.8 reports zero code diagnostics for the changed integration test (its
  three reported failures are optional extract-function probes). All three RE
  wrapper suites, 12 Rizin normalization tests, public-boundary tests and the
  383-file scan, `actionlint`, and `git diff --check` pass. Exact-commit WSL
  and GitHub Actions remain publication gates.

## 2026-07-28 - placed-object dynamic BSP assembly

- `EV-20260728-022` / `EXP-20260728-039`: recovered the native room-list
  publication around serialized `0x4101`. `CcRoom::LoadSceneCcf` builds and
  caches the first physical mesh use, then forces an unlink/relink which puts
  the object at the dynamic-list head. A later cache hit skips all nested data
  and performs no relink, so the portable room publication preserves that
  observed omission instead of inventing an instance.
- A fresh automatic Rizin 0.9.1/rzpipe 0.6.2 report independently matches the
  133-byte `CcObject::Link` boundary at RVA `0x000152B0` and its exact
  `object + 0x15c`, `object + 0x114`, `*[room]` list-head stores. Ghidra
  remains canonical for the `__thiscall` ABI, types, and encompassing loader
  pseudocode.
- Added `MissionPlacedDynamicBspAssembly`. It authenticates the ordered room
  catalogue and placed scene, material-binds the decoded trees, reproduces
  source-local first-use mesh caching plus native root/polygon/object prepend
  order, converts unit-scale orthonormal F050 objects and local BSP geometry
  into the runtime basis, and emits immutable meshes, exact room ranges, and
  source provenance for the existing allocation-free combined line query.
- Aggregate count/depth/geometry/retained-byte ceilings and typed failures
  clear all publishable state atomically. Synthetic tests cover the native
  cache quirk, material/provenance binding, per-room order, direct combined
  collision, root fallback, a dynamic portal, reflected coordinates, empty
  scenes, exact/one-under memory budgets, tampering, and malformed references.
  Mission-snapshot ownership and composition with live player/non-player
  frames remain later atomic integration boundaries.
- Final review aligned the assembly's orthonormality threshold with the direct
  dynamic-line consumer and added a regression for the previously admitted
  gap plus end-to-end type-zero portal-range continuation. Fresh clean Release
  and code-intelligence Ninja/GCC 15.2 builds each compile 253 steps and pass
  78/78 tests; the generated database contains 161 portable entries. Clangd
  22.1.8 reports zero code diagnostics in both new translation units (four
  production findings are optional `ExtractFunction` feature probes). The
  three RE wrapper suites, 12 Rizin normalization tests, public-boundary tests
  and 387-file scan, 263-row catalogue validation with only the two
  pre-existing missing references, `actionlint`, 11-file local-path scan, and
  `git diff --check` pass. The exact code commit
  `a531e663a5d4ffa87ccd459686b5d67fc39680c9` also compiles all 253 steps
  with GCC 13.3 and passes 78/78 tests in a new isolated `Airfix-Dev` WSL
  clone. GitHub Actions remains the final publication gate.

## 2026-07-29 - mission dynamic-collision ownership and composition

- `EXP-20260729-040` installs the previously authenticated placed-object
  dynamic BSP assembly in `LoadedMissionWorldRoom`. Empty missions retain one
  empty range per world room; populated missions retain the exact native
  object lists after the parsed CCF cache is destroyed.
- Added a distinct placed-collision load phase, bounded configuration, typed
  failure context, exact retained-byte charge, and final publication checks
  for completeness, world-room cardinality, semantic-source provenance, and
  the independently recalculated global CPU ledger.
- Added `LegacyDynamicBspMeshView`, which maps an immutable placed span followed
  by an immutable player span into one logical mesh index space. Both combined
  line-query variants consume it directly; existing single-span callers remain
  source-compatible and copy no mesh records or nested arenas.
- Added a two-pass `noexcept` mission-frame publisher. It validates all
  transforms, offsets, ranges, and exact output sizes before writing, then
  prepends the live player instances to the current room and copies each
  placed range unchanged. Inactive players retain stable slots with their
  active gate cleared. Failure leaves both caller buffers unchanged.
- Synthetic tests cover real loader ownership, exact and one-under placed
  budgets, typed publication tampering, two disjoint mesh owners, native-order
  exact-fraction ties, inactive fallback, no-player missions, atomic failures,
  and zero observed frame-publication allocations. Full cross-platform
  validation and Actions publication follow in this slice.
- Fresh clean Release and regenerated Debug/code-intelligence builds compile
  256 portable steps and pass 79/79 tests. The compilation database contains
  163 entries. Clangd 22.1.8 reports zero code diagnostics in the new frame,
  test, loader, and publication units; seven failures in the large existing
  tracer are optional `ExtractFunction` probes. The three RE wrapper suites,
  12 Rizin/public-boundary Python tests, public-boundary check and 391-file
  scan, 263-row catalogue with only its two pre-existing missing references,
  `actionlint`, 22-file local-path scan, and `git diff --check` pass.
- The exact committed tree `457ed6b` compiles all 256 steps with GCC 13.3 and
  passes 79/79 tests in a new isolated `Airfix-Dev` WSL clone. GitHub Actions
  remains the final publication gate.

## 2026-07-29 - mission-runtime dynamic-collision ownership

- PR #32 merged the authenticated mission placed/player composition as
  `c6f008c`; the exact `main` commit passed Ubuntu, Windows, ARM64 macOS,
  clangd/code-intelligence, `iphoneos`, and `iphonesimulator` Actions jobs.
- `EXP-20260729-041` extends the existing non-moving mission runtime to take
  ownership of the authenticated static arena, placed collision assembly, and
  optional player collision assembly during the native iOS handoff. Mesh
  records, nested BSP arenas, and provenance vectors are moved, not copied.
- The runtime preallocates exactly one reusable object record per placed
  object/player instance and one room range per retained world room. Checked
  object, room, multiplication, addition, and total-byte limits run before
  allocation; only these flat buffers are added to the runtime's existing
  workspace/exchange byte ledger because the loader already admitted the
  immutable assets.
- Producer-only `noexcept` methods publish into those stable buffers and
  portal-trace the last complete frame against the runtime-owned static arena.
  No bootstrap player state is invented. The first failure exposes no frame,
  while a failed republish preserves the previous complete publication.
- Tests cover legacy camera-only construction, dynamic ownership, exact and
  one-under limits, malformed assets/cardinality, native-order tie tracing,
  failed-republish retention, and 4,096 combined camera/publication/trace
  cycles with zero observed steady-state allocations.
- A fresh 256-step Release build and regenerated Debug/code-intelligence build
  pass 79/79 tests. Clangd 22.1.8 reports zero diagnostics in the changed
  runtime and test. Exact commit `40a6808` compiles all 256 steps with GCC 13.3
  and passes 79/79 tests in a new isolated `Airfix-Dev` WSL clone. GitHub
  Actions remains the publication gate for this slice.

## 2026-07-29 - projectile collision portal loop

- `EXP-20260729-042` refreshes canonical Ghidra 12.1.2 decompilation and
  instructions for `NfProjectile::DetectCollisions` at RVA `0x00035EF0`.
  A fresh automatic Rizin 0.9.1/rzpipe 0.6.2 report independently matches its
  exact `[0x10035EF0, 0x10036353)` boundary, repeated query call, back edge,
  and shared terminal path.
- Added a renderer-independent, allocation-free `noexcept` coordinator around
  the existing single-hit decision. It preserves the full endpoint while a
  type-zero projectile portal advances the start and signed room ID, then
  returns the first terminal no-hit, material, actor-gate, actor-contact, or
  surface decision.
- Callback rejection, inconsistent result states, unsafe hit metadata, and
  cycles fail with typed statuses and no terminal decision. The default
  transition budget is 64 and the portable hard maximum is 256; query and
  completed-transition counts remain available for diagnostics.
- Focused tests cover two-portal geometry, endpoint/room propagation,
  material and actor terminals, malformed callback states, zero/one-transition
  budgets, and 4,096 two-query cycles with zero observed allocations.
- A fresh Windows GCC 15.2 Release configuration compiles all 259 steps and
  passes 80/80 tests. Its compilation database has 165 portable entries;
  clangd 22.1.8 reports zero errors in the coordinator and test. Twelve Rizin
  normalization tests, the synthetic and 396-file public-boundary scans, the
  263-row unique function catalogue with only its two known missing
  references, `actionlint`, 13-file local-path scan, and `git diff --check`
  pass. Exact commit `ba5b2ec` compiles all 259 steps with GCC 13.3 and passes
  80/80 tests in a new isolated `Airfix-Dev` WSL clone. GitHub Actions remains
  the publication gate.

## 2026-07-29 - projectile runtime query adapter

- `EXP-20260729-043` composes previously confirmed room-ID, `0x2152`
  material, combined portal-line, and projectile decision contracts without a
  new binary assumption.
- Added a content/runtime integration adapter that maps ownerless static and
  owner-backed dynamic hits, preserves raw material bits as the native signed
  value, and converts authenticated portal world indices back to `CcRoom` IDs.
- Native actor order is explicit: material `8` and client gates do not invoke
  the live resolver; only the server path accepts resolved, lookup-miss, or
  rejected actor states. Missing or malformed live state fails closed.
- Focused tests cover all mapping states plus an actual two-room runtime frame:
  a hidden type-zero placed portal advances the projectile query into legacy
  room `1`, where a solid placed surface terminates it. 4,096 complete
  two-query runtime loops observe zero allocations.
- A fresh Windows GCC 15.2 Release build compiles 262 steps; it and the
  regenerated code-intelligence build pass 81/81 tests. The compilation
  database has 167 portable entries; clangd 22.1.8 reports zero errors in the
  adapter and test.
  Twelve Rizin normalization tests, the synthetic and 400-file public-boundary
  scans, the 263-row unique function catalogue with only its two known missing
  references, `actionlint`, 14-file local-path scan, and `git diff --check`
  pass. Exact commit `c1b4d15` compiles all 262 steps with GCC 13.3 and passes
  81/81 tests in a new isolated `Airfix-Dev` WSL clone. GitHub Actions remains
  the publication gate.

## 2026-07-29 - generation-matched primary-player projectile resolver

- `EXP-20260729-044` closes the concrete resolver gap for the already
  authenticated primary-player collider without inferring any gate value or
  publishing a synthetic non-player actor.
- Added a typed player actor state carrying the native object ID, active flag,
  projectile-collision enable, and actor-acceptance gate. Successful dynamic
  frame publication commits those values beside the exact geometry
  generation; a rejected republish retains both previous products.
- Added a concrete primary-player lookup and projectile-loop overload.
  Unpublished/zero-ID queries reject, mismatched IDs are native lookup misses,
  and an exact match supplies all three recovered actor gates. The callback
  overload remains the extension seam for other actors.
- A complete one-room runtime fixture resolves the published player to an
  actor contact, then reproduces server and client actor-gate pass-through
  after a disabled acceptance gate. No-player, mismatched, transactional
  retention, and 4,096 zero-allocation query cases are covered.
- A fresh Windows GCC 15.2 Release build compiles all 262 steps; it and the
  regenerated code-intelligence build pass 81/81 tests. The compilation
  database has 167 portable entries and no Apple-only source. Clangd 22.1.8
  reports zero errors in both changed production units and tests. Twelve Rizin
  normalization tests, synthetic public-boundary tests and the 401-file public
  scan, the 263-row unique function catalogue with only its two known missing
  references, `actionlint`, 16-file local-path scan, and `git diff --check`
  pass. Exact commit `0796d98` compiles all 262 steps with GCC 13.3 and passes
  81/81 tests in a new isolated `Airfix-Dev` WSL clone. GitHub Actions remains
  the publication gate.

## 2026-07-29 - terminal machine-gun projectile collision commit

- `EXP-20260729-045` composes the previously cross-checked shared collision,
  `WpMGunAmmoActorHit`, and `WpMGunAmmoSurfaceContact` contracts without a new
  binary assumption or private runtime dependency.
- Added a fail-closed allocation-free reducer for completed projectile loops.
  It commits endpoint/room state, preserves no-hit/material/actor-gate flight,
  applies actor damage and creator/self deactivation, sets the persistent
  material-15 water flag before surface handling, and returns bounded
  surface/ricochet command data.
- Ownerless retained-room contact still deactivates, but an optional ricochet
  request is suppressed when no owner-backed material provenance exists.
  Failed actor lookup does not promote a raw object ID to a resolved actor UID.
- Added explicit and generation-matched primary-player runtime wrappers that
  join the published query to the reducer. Flight endpoint/room mismatch
  rejects before tracing. No live actor event or private effect is dispatched.
- Focused tests cover no-hit, actor damage, self-hit, ordinary and water
  surfaces, creator surfaces, ownerless material policy, typed failures, the
  published player path, a two-room portal/surface path, and 4,096 repeated
  transactions on each hot path with zero observed allocations.
- A fresh Windows GCC 15.2 Release configuration compiles all 265 steps; it and
  the regenerated code-intelligence build pass 82/82 tests. The compilation
  database has 169 portable entries and no Apple-only source. Clangd 22.1.8
  reports zero errors in all five changed production/test translation units
  with the trusted GCC query driver. The Ghidra, working-copy, and Rizin
  wrapper suites, 12 Rizin normalization tests, synthetic public-boundary
  tests and the 405-file public scan, the 263-row unique function catalogue
  with no bad paths and only its two known missing references, `actionlint`,
  19-file local-path scan, and `git diff --check` pass. Exact commit `b355415`
  compiles all 265 steps with GCC 13.3 and passes 82/82 tests in 44.80 seconds
  in a new isolated `Airfix-Dev` WSL clone. GitHub Actions remains the
  publication gate.

## 2026-07-29 - projectile creator BSP guard

- `EXP-20260729-046` closes the statically recovered creator-collision lifetime
  around `NfProjectile::DetectCollisions`. Fresh Ghidra instructions and
  decompilation plus an independent Rizin report confirm one creator lookup,
  virtual `DisableBsp`, the complete query/portal/terminal path, and a
  conditional virtual `EnableBsp` using the retained actor.
- Added a typed, synchronous, allocation-free live-actor transaction. Missing
  creators and already-disabled BSP preserve the native no-enable path.
  Rejected or malformed disable results stop before tracing; rejected enable
  retains collision diagnostics but suppresses terminal commit/command data.
- Focused tests cover exact callback order and actor identity, a two-room
  portal path under one guard pair, missing/zero/already-disabled creators,
  malformed callback results, endpoint/room mismatch before live access, and
  4,096 complete guarded actor transactions with zero observed allocations.
- A fresh Windows GCC 15.2 Release build and regenerated code-intelligence
  build compile all 265 steps and pass 82/82 tests. The compilation database
  has 169 portable entries and no Apple-only source. Clangd 22.1.8 reports zero
  errors in the three affected production/test translation units. The Ghidra,
  working-copy, and Rizin wrapper suites, 12 Rizin normalization tests,
  synthetic public-boundary tests and the 406-file public scan, the 265-row
  unique function catalogue with no bad paths and only its two known missing
  references, `actionlint`, the 16-file local-path scan, and `git diff --check`
  pass. Exact commit `71eaca1` compiles all 265 steps with GCC 13.3 and passes
  82/82 tests in 27.15 seconds in a fresh isolated `Airfix-Dev` WSL clone.
  GitHub Actions remains the publication gate.

## 2026-07-29 - portable machine-gun projectile runtime pool

- `EXP-20260729-047` refreshes the complete weapon-to-projectile activation
  join using hash-verified, read-only `Projectiles.type` and `AfEngine.dll`
  working copies. Ghidra remains canonical for ABI and event pseudocode;
  independent Rizin reports confirm exact boundaries and instruction bytes for
  `WpMGunRefresh`, the ammunition factory/constructor, and
  `AfAmmo::ProcessEvent`.
- The recovered order is now explicit in one portable transaction: cadence,
  barrel, and nonzero ammunition advance before capacity acquisition; muzzle
  and event-only data are consumed only after acquisition succeeds.
- Added a caller-owned fixed-capacity projectile runtime. It selects the first
  reusable slot, writes the complete semantic event-`0xE2` state and
  technology profile, and returns a 64-bit generation-tagged handle without
  allocation. Generation wrap fails closed and reuse cannot revive a stale
  identity. Pool capacity and first-free selection are deliberate port policy,
  not claims about the native private allocator.
- Focused tests cover full and empty pools, exhausted and skipped generations,
  state-before-capacity behavior, payload rejection after capacity,
  activation mapping, deactivation/reuse, mutable and const handle lookup,
  stale and out-of-range handles, and 4,096 successful activations with zero
  observed heap allocations.
- A fresh Windows GCC 15.2 Release build and separate code-intelligence build
  each compile all 268 steps and pass 83/83 tests. The compilation database has
  171 portable entries and no Apple-only source; clangd 22.1.8 reports zero
  errors in all three affected translation units. Ghidra, working-copy, and
  Rizin wrappers, 12 Rizin normalization tests, synthetic public-boundary
  tests and the 410-file public scan, the 265-row unique catalogue with only
  its two known missing references, `actionlint`, the 19-file local-path scan,
  and `git diff --check` pass. Exact commit `6c5d386` compiles all 268 steps
  with GCC 13.3 and passes 83/83 tests in 26.23 seconds in a fresh isolated
  `Airfix-Dev` WSL clone. GitHub Actions remains the publication gate.

## 2026-07-29 - Windows x64 promoted to a parallel playable product

- The project owner accepted a native playable Windows x64 application as a
  parallel version 1 target beside the private iOS ARM64 sideload. Windows is
  now the primary rapid-debug and controlled original-comparison environment,
  not merely a reference harness.
- ADR-0007 fixes one shared portable C++20 game, physics, resource, save,
  semantic-input, render-command, and audio-command implementation. Windows and
  iOS own separate window/lifecycle, renderer, physical-input, audio, and
  filesystem/private-content adapters.
- ADR-0008 selects SDL3 for the Windows window, operating-system events,
  keyboard, mouse, and controllers, plus D3D11/DXGI with HLSL for the renderer.
  SDL3 does not own draw submission; iOS remains on native Metal. Official
  Microsoft and SDL documentation confirms the desktop D3D11/DXGI/HLSL path,
  SDL's zlib license, and the current lack of SDL GPU support for the iOS
  Simulator, so SDL GPU remains a revisit option rather than the base.
- Added explicit Windows product requirements and cross-product acceptance
  gates. The current portable Windows compiler/test configuration remains a
  core validation path and is not represented as a playable product shell.
- Original files, converted `.afpack` packages, content-bearing builds, private
  traces, analysis databases, and machine-local paths remain outside Git and
  public Actions logs, caches, artifacts, and releases for both products.
- Updated the public project overview, architecture, version 1 scope, roadmap,
  input contract, toolchain plan, parity matrix, iOS product note, and status
  to reflect the two-product architecture.
- Regenerated the code-intelligence preset from the current source, completed
  the incremental Windows build, and passed all 83 tests. Public-boundary unit
  tests, the 413-file repository scan, `actionlint`, links in all 16 final PR
  documentation files, the local-path review, and `git diff --check` pass.

## 2026-07-29 - first native Windows D3D11 product shell

- Added the `AIRFIX_BUILD_WINDOWS_APP` product boundary and reproducible
  `windows-product`/`windows-product-release` CMake presets. The official path
  is an x64 Visual Studio 2026 build; portable and iOS configurations leave the
  target disabled and do not fetch SDL.
- Pinned official SDL 3.4.12 source by release URL and SHA-256, built it
  statically, staged its zlib license, and explicitly disabled SDL render/GPU,
  audio, camera, dialog, haptic, power, sensor, and tray subsystems. SDL owns
  the Windows window, events, keyboard, mouse, and controller plumbing only.
- Implemented a native SDL3 window/lifecycle loop and a separate D3D11/DXGI
  backend with HLSL, reusable vertex/index buffers, explicit fallback textures,
  per-instance transforms, depth/raster/sampler state, resize handling, and
  hardware-to-WARP device fallback. No DirectX 7 interface is reproduced.
- Extracted the existing proprietary-data-free Metal bring-up payload into one
  portable `PublicRenderSmokeScene`. Both native renderers now consume the same
  two meshes, three non-monotonic instances, four validated draw commands,
  texture bytes, and explicit one-pixel fallback.
- Added a hidden GPU smoke mode that compiles the real HLSL, submits the shared
  plan, reads the BGRA8 D3D11 back buffer before presentation, and rejects a
  clear-only result. A dedicated Windows Actions job builds the MSVC preset,
  runs all tests, and verifies third-party license staging.
- The native Windows product locally compiles with MinGW GCC 15.2 in Release
  mode and passes 85/85 tests; after disabling unused SDL subsystems, the
  D3D11 GPU readback test passes before and after swap-chain target recreation.
  A separate 271-step portable build passes 84/84 tests.
  Public-boundary tests and the 422-file scan, `actionlint`, clang-format,
  `git diff --check`, and the official SDL API/hash review pass. Pull-request
  Actions run `30439266445` passes the Visual Studio 2026 D3D11 product job and
  the complete portable matrix; iOS run `30439256007` passes both Apple SDK
  jobs.

## 2026-07-29 - baseline native Windows SDL3 input adapter

- Added portable `DesktopInputBridge` ownership of keyboard, mouse, and one
  standardized controller above the existing semantic `InputRouter`. It
  preserves complete controller taps through `ControllerInputBatchBridge`,
  rejects generation rollback and invalid Q15, resets relative mouse look after
  one fixed input frame, and fails closed on bounded queue/edge exhaustion.
- Expanded the default fallback profile to full baseline keyboard/mouse flight,
  combat, camera, pause, and menu actions. SDL physical scancodes remain stable
  USB HID IDs; mouse and controller platform types stay outside the C++ core.
- Added `AirfixSdlInputAdapter` with first-controller assignment, startup
  discovery, hot-plug/remap, disconnect release and pause request, full-state
  resampling after focus regain, SDL Y-axis normalization, trigger thresholds,
  and filtering of touch/pointer-synthesized mouse events.
- Replaced the renderer-only event loop with a fixed 60 Hz semantic-input pump.
  The public app still renders only the synthetic scene and discards frames
  until reconstructed gameplay is connected; no playability claim is made.
- A fresh 274-step Debug/code-intelligence build passes all 85 portable tests.
  A fresh 269-step MinGW Release product build against the pinned SDL 3.4.12
  source passes all 87 tests, including lifecycle/hot-plug/dead-zone/tap
  coverage, SDL mapping, and the real D3D11 GPU readback/resize smoke test.
  The 428-file public-boundary scan, `actionlint`, clang-format dry run,
  changed-scope local-path scan, and `git diff --check` pass. The local MSVC
  preset is unavailable because Visual Studio 2026 is not installed.
  Pull-request Actions run `30443041305` passes the Visual Studio 2026 D3D11
  product job, Windows/Ubuntu/macOS portable jobs, and clangd; iOS run
  `30443041280` passes both `iphoneos` and `iphonesimulator`.

## 2026-07-29 - portable audio contract and Windows XAudio2 2.9 shell

- ADR-0009 selects the inbox XAudio2 2.9 runtime for Windows 10/11, with the
  null default-device identifier, `AudioCategory_GameMedia`, later X3DAudio
  spatialization, and a native Apple backend remaining on iOS. Direct WASAPI,
  SDL audio, DirectSound, XAudio2 2.7, and the legacy DirectX SDK are not part
  of the product architecture.
- Added a portable C++20 audio contract with explicit clip/voice IDs, monotonic
  command batches, fixed command capacity, bounded finite gain/pitch, validated
  mono/stereo PCM16 views, and a 64 MiB per-clip limit. Public tests cover valid
  commands, malformed state, capacity, and PCM format rejection.
- Added a native Windows adapter that securely loads `XAUDIO2_9.DLL` from the
  system directory instead of linking the local MinGW XAudio2 2.8 import
  library. It owns copied immutable clips under count/aggregate budgets,
  bounds active voices, consumes missing-voice updates as safe no-ops, and
  rejects invalid, stale, or unknown-clip command batches.
- The mastering voice uses the virtualized default endpoint. No output device
  is a supported silent state; critical errors are reduced to an atomic signal
  in the callback and engine recreation occurs on the game thread. Focus loss
  pauses the engine and focus regain resumes it without destroying healthy
  voices.
- Expanded the data-less Windows product smoke to register muted synthetic
  PCM, start and stop a source voice when output exists, and validate the same
  commands without failing on a headless/no-endpoint runner. No original audio
  or private package is used.
- A local MinGW GCC 15.2 Release product build compiles the complete native
  shell and passes 88/88 tests, including the real D3D11 back-buffer readback,
  SDL3 mapping, portable audio contract, and native XAudio2 smoke. The
  code-intelligence build passes its full 86/86 portable tests. The two
  portable audio translation units report zero clangd errors; changed-source
  formatting, public-boundary tests and the 434-file scan, `actionlint`, the
  22-file local-path scan, and `git diff --check` pass.

## 2026-07-29 - AirCraft audio composition into the shared backend

- Added an explicit phase-command boundary to the recovered engine-audio step
  and a portable `LegacyAircraftAudioCoordinator`. It executes destroyed-dive
  state only on the shared fifth-call cadence and preserves native slot-44
  ordering: engine phase transitions, destroyed-dive commands, then common
  engine modulation.
- Added validated caller-owned bindings for all six recovered AirCraft sound
  roles. Clip and voice IDs must be valid, roles and voices must be unique,
  clips may be shared, and loop policy remains explicit until private sample
  metadata is verified. No original sample, path, or decoded data enters the
  public implementation.
- Play, stop, gain, and pitch operations translate transactionally into the
  bounded platform-neutral command batch. Model-parameter and legacy mixer
  update operations stay outside the device API. Values outside the portable
  backend contract fail closed rather than being silently clamped.
- Expanded the Windows product smoke from a direct synthetic voice start/stop
  into the recovered engine start, running, shutdown, destroyed-dive, and
  recovery sequence. One synthetic PCM clip exercises distinct role voices
  through XAudio2 2.9 and retains the supported no-output-device path.
- A fresh 281-step MinGW GCC 15.2 Release build and the code-intelligence build
  each pass all 87 portable tests. The native product passes 89/89, including
  the original engine-state tests, new composition tests, and D3D11 plus
  XAudio2 product smoke. Clangd reports zero errors in both new translation
  units. The three reverse-engineering wrapper suites, twelve Rizin
  normalization tests, synthetic and 438-file public-boundary scans,
  `actionlint`, new-source formatting, the 17-file local-path scan, and
  `git diff --check` pass. GitHub Actions publication remains pending.

## 2026-07-29 - native iOS AVAudioEngine command backend

- ADR-0010 selects AVAudioEngine with one player/varispeed chain per portable
  voice and an Ambient, foreground-only audio session. SDL audio and
  third-party middleware remain outside the iOS product.
- Added a main-thread Objective-C++ backend for the same bounded monotonic
  `AudioCommandBatch` and PCM16 registration contract used by Windows. It
  converts source PCM once into standard deinterleaved Float32, accounts for
  converted memory, bounds clips/voices, preflights clip IDs, capacity, and
  generation exhaustion, and treats missing-voice updates as safe no-ops.
- Looping voices retain their desired clip/gain/pitch state through engine
  configuration and media-service recovery. One-shot effects are consumed
  whenever output is paused or unavailable and are never replayed late.
  Completion callbacks enqueue main-thread cleanup and cannot erase a newer
  same-ID voice because each start receives a nonwrapping generation token.
- Integrated session activation with deliberate gameplay resume. Pause,
  inactive, background, view loss, content replacement, and terminal
  simulation failure deactivate audio. Interruption start, current-route loss,
  and media-service reset force gameplay pause and input neutralization; no
  notification may resume gameplay.
- A fresh 281-step GCC 15.2 Release build passes all 87 portable tests. Public
  boundary tests and the 441-file scan, changed-source formatting,
  changed-scope local-path scanning, and `git diff --check` pass. Xcode 26.6
  compiles and verifies data-less ARM64 `iphoneos` and `iphonesimulator`
  bundles with the iOS 16.4 deployment target. Audible output and the route/
  interruption matrix remain physical-device acceptance work.

## 2026-07-29 - authenticated owner-local aircraft audio binding

- ADR-0011 binds the six recovered aircraft roles directly to exact logical
  entries in the nested `Resource.up` owned by one unchanged authenticated
  content session. Loose WAV files, original-installation paths, and converted
  caches are not accepted by either product.
- Added a bounded portable RIFF/WAVE parser for integer PCM16 mono/stereo
  sources at 8–192 kHz. It validates RIFF and chunk framing, format arithmetic,
  data alignment, sampler-loop structure, independent size/count ceilings, and
  preserves typed parse errors with exact offsets.
- Added an atomic clip-set loader with per-source, aggregate transient, and
  published-PCM budgets. It resolves all six exact roles before reading,
  requires the observed full-buffer infinite loops for the four continuous
  layers, keeps start/stop as one-shots, binds publication to both revision and
  opaque authenticated-stream identity, and publishes either all clips or
  none.
- Added `afpack-install` as a portable command-line entry to the existing
  durable installer. Windows can open an explicit private content root,
  authenticate its AFAC-selected AFPACK, register all six clips with XAudio2,
  and perform a hidden validation run. The public smoke path stays synthetic
  and data-less.
- The iOS content worker loads audio from the same verified mission session and
  moves it through the opaque mission snapshot. Main prepares and fills a
  replacement AVAudioEngine backend before consuming the ticket, then commits
  renderer, backend, bindings, and mission state by ownership swaps. Failure or
  staleness leaves the previous committed mission untouched.
- Synthetic tests cover valid loading, missing/ambiguous entries, malformed
  WAVE data, loop evidence, every memory budget, cancellation, callback
  failure, session replacement, moved-from sessions, and invalid voice IDs.
  A complete owner-local AFPACK was also created, atomically installed, and
  accepted by the Windows hidden validation path outside Git; the temporary
  validation tree was removed and the original installation remained
  unchanged.
- A fresh 289-step GCC 15.2/Ninja build passes all 89 portable tests. A fresh
  548-step MSVC 19.51/Ninja build and the independent MinGW product build each
  pass all 91 tests, including native D3D11/XAudio2 smoke. The synthetic and
  449-file public-boundary scans, `actionlint`, changed-source clang-format,
  changed-scope local-path scan, and `git diff --check` pass. GitHub Actions
  runs `30457131783` and `30457131656` pass all seven hosted Visual Studio,
  Windows/Ubuntu/macOS portable, clangd, `iphoneos`, and `iphonesimulator`
  jobs.

## 2026-07-29 - authenticated mission rendering in the Windows product

- Added a strict Windows launch parser for an explicit owner-local content
  root and optional setup/Level pair, player-object path, and `uint32_t` start
  index. Duplicate, incomplete, overlong, overflowing, unknown, and mixed
  smoke/private requests fail closed. AFPACK v1 paths remain out-of-band and
  are never inferred or stored in public configuration.
- Refactored private startup so aircraft audio, the mission manifest, and the
  complete room load through one unchanged `VerifiedContentSession` and
  revision. Any failed stage suppresses application publication.
- Extended the D3D11 backend with a separately compiled gameplay HLSL path,
  recovered homogeneous scalar projection, explicit far clip distance,
  reverse-depth clear/compare, and the shared parity-first 4:3 aspect-fit
  viewport. The public diagnostic shader and scene remain unchanged.
- D3D11 now prepares bounded vertex/index buffers plus every dense mission
  texture before committing the replacement scene. Authored mip chains are
  uploaded intact; base-only plans use checked D3D11 mip generation. The
  renderer independently rebuilds and compares the portable submission plan
  and validates the native publication boundary.
- Expanded hidden private validation to issue an actual mission draw, read the
  D3D11 back buffer, and require visible output. Public CTest still uses only
  synthetic geometry and PCM. A separate hidden one-shot capture mode writes
  the checked BGRA8 buffer to a new private BMP without overwriting existing
  data.
- MSVC 19.51 and MinGW GCC 15.2 product builds each pass 92/92 tests, including
  the new command-line contract and native D3D11/XAudio2 smoke. A separate
  owner-local trial created and atomically installed a complete AFPACK outside
  Git, loaded an explicit mission, prepared its GPU resources, rendered through
  the gameplay camera, and passed back-buffer visibility. The exact temporary
  tree was removed afterward; the original installation was not modified.
- The portable code-intelligence build passes 89/89 tests. The 453-file public
  boundary scan, `actionlint`, changed-source formatting, changed-scope
  local-path scanning, and `git diff --check` pass. Hosted Windows, portable,
  clangd, and iOS gates remain mandatory before merge.

## 2026-07-29 - native-resolution modern rendering contract

- Accepted ADR-0013 as the binding cross-product rendering contract. At 100%
  render scale, the 3D target must equal the physical output extent; 3840x2160
  therefore means actual 3840x2160 scene rasterization, never an upscale from
  640x480, 1120x480, or another logical canvas.
- Distinguished reference camera coordinates, UI design space, output pixels,
  3D render-target pixels, viewport/safe area, and render scale. Standard
  widescreen is Hor+ with reference vertical FOV; Original 4:3 remains an
  aspect-fitted comparison mode.
- Defined orthogonal Classic/Enhanced visual profiles and
  Low/Medium/High/Ultra quality tiers. Both native backends consume the same
  C++20 camera/geometry/material/light/command contract, while D3D11/HLSL and
  Metal implement platform-specific passes without feeding visual state into
  deterministic simulation.
- Ordered implementation so exact native targets, projection, safe-area/UI and
  input transforms, tests, and diagnostics land before linear/sRGB sampling,
  lights, shadows, materials, HDR, atmosphere, particles, and post-processing.
  This foundation advances alongside gameplay reconstruction; costly effects
  do not block the active playable slice.
- Added the required aspect-ratio matrix, platform FPS goals, render-scale
  controls, diagnostic counters, and matched 1080p/1440p/4K/ultrawide capture
  policy. Owner-content screenshots and all original or converted assets remain
  outside Git and public CI.
- The portable Ninja/code-intelligence build is current and passes 89/89 tests.
  The 454-file public-boundary scan, local-path scan, local Markdown-link check,
  `actionlint`, and `git diff --check` pass; hosted platform builds remain the
  merge gate.

## 2026-07-29 - first authenticated Windows room-plus-aircraft frame

- `EV-20260729-003` / `EXP-20260729-048` corrected the player visual definition
  contract from the synthetic `OBJE` assumption to the owner-data-confirmed
  `MODL` root. Level `OBJE` definitions retain their existing strict `OBJE`
  requirement; the player gate now rejects that root with the existing typed,
  atomic kind-mismatch result.
- Updated the room loader's transient definition kind and made the synthetic
  manifest and end-to-end room fixtures use `MODL`. Exact path, session,
  revision, source-budget, CCF, texture, blueprint, scene, collision, and
  publication checks are unchanged.
- Every available aircraft definition passed the corrected manifest gate.
  Thirteen completed the current full room/player loader pilot; one reached a
  separate aircraft-specific downstream room-loader failure retained for
  focused follow-up.
- One representative owner-local launch completed the authenticated manifest,
  room, model CCF, texture, slot-zero hierarchy, start pose, combined draw
  model, D3D11 preparation, reverse-depth draw, visible-back-buffer check, and
  private BMP capture. The 960x540 image visibly contains the aircraft inside
  the mission room's current centered 720x540 parity viewport. It remains a
  static Classic/parity frame, not live flight or ADR-0013 completion.
- The current MSVC 19.51 and MinGW GCC 15.2 Windows product builds each pass
  92/92 tests, including the real data-less D3D11/XAudio2 smoke. Targeted
  portable manifest and room-loader tests pass 2/2. The private AFPACK and
  capture remain ignored and outside public infrastructure.

## 2026-07-29 - native render layout and first Hor+ backends

- `EV-20260729-004` / `EXP-20260729-049` implements the first ADR-0013
  milestone. New allocation-free portable types separate output pixels, 3D
  render-target pixels, camera coordinates, UI coordinates, safe areas, and
  input points. A 100% scale is an explicit exact identity; the bounded
  50-200% extent policy fails closed on invalid or overflowing input.
- Standard presentation now fills the 3D target using Hor+ while preserving
  the recovered reference vertical FOV. Original 4:3 remains a separately
  tested aspect-fit comparison policy. UI fitting and reversible input
  transforms depend on output/safe-area coordinates rather than 3D scale.
- D3D11 and Metal gameplay-camera paths consume the same portable layout on
  their current direct-to-native 100% path. The recovered 640x480 canvas is
  reference-camera metadata only, not a raster target.
- Portable tests cover exact 3840x2160 at 100%, 50% and 200% scaling, 4:3,
  16:10, 16:9, 19.5:9, 21:9, 32:9, Original 4:3, safe-area/UI/input mapping,
  invalid values, overflow, and no-allocation behavior.
- A hidden Windows capture-size option rejects malformed requests and now
  rejects any requested extent that does not match the physical D3D11
  backbuffer. Direct private readbacks of one representative owner-local scene
  produced exact 1920x1080, 2560x1440, 3840x2160, and 3840x1080 BMPs. These
  ignored captures and all source content remain outside Git and public CI.
- Backend offscreen targets/resampling for non-100% scales, settings/UI,
  diagnostics, modern visual stages, and physical-device iOS validation remain
  pending; this evidence does not claim ADR-0013 completion.
- A clean 292-step GCC 15.2/Ninja build passes 90/90 portable tests. Full MSVC
  19.51 and MinGW GCC 15.2 Windows product builds independently pass 93/93
  tests, including native D3D11/XAudio2 smoke. The 459-file public-boundary
  scan, `actionlint`, changed-document link and local-path checks, and
  `git diff --check` pass. GitHub Actions runs `30470411401` and `30470413579`
  pass all seven hosted Visual Studio product, Windows/Ubuntu/macOS portable,
  clangd, iPhoneOS, and iPhoneSimulator gates.

## 2026-07-29 - native backend render-scale targets

- `EV-20260729-005` / `EXP-20260729-050` implements the next ADR-0013
  foundation slice. At 100%, D3D11 and Metal retain the exact direct native
  output path. At non-100% scales, each backend creates a private BGRA8 scene
  color target and depth target at the portable render extent, renders the
  Hor+ or Original 4:3 viewport there, and linearly presents it to the
  unchanged native output.
- D3D11 target replacement is transactional and bounded by the D3D11 texture
  dimension limit. Metal publishes a new color/depth pair only after both
  private allocations succeed and adds explicit dimension and aggregate target
  byte limits. Neither backend silently changes a rejected scale.
- Windows now accepts validated `--render-scale 50..200` and `--original-4x3`
  product settings. Metal exposes equivalent validated main-thread renderer
  settings for later UIKit binding.
- A new data-less CTest renders a visible D3D11 frame through a 50% offscreen
  target, Original 4:3 viewport, and presentation pass. Independent complete
  MSVC 19.51 and MinGW GCC 15.2 product rebuilds each pass 94/94 tests.
- A controlled owner-local comparison retained a 960x540 output while changing
  the internal target from 480x270 at 50% to 1920x1080 at 200%. The frames are
  visibly distinct and have different hashes. Both captures and all source
  content remain ignored private artifacts.
- Finished settings UI, HUD composition, safe FOV, diagnostics, color/HDR,
  lighting/material/effect stages, and physical-device iOS performance remain
  pending. This slice does not claim a playable build or ADR-0013 completion.

## 2026-07-29 - shared frame diagnostics and native overlays

- `EV-20260729-006` / `EXP-20260729-051` adds a portable, fail-closed frame
  diagnostics contract. It validates and smooths backend samples, formats one
  locale-independent four-line report, and rasterizes a bounded RGBA8 panel at
  an output-relative pixel scale.
- Both native renderers now report output and 3D target extents, render scale,
  FPS, CPU and asynchronous GPU frame time, scene and total draw/triangle
  counts, active lights, and GPU memory with measurement accuracy shown.
  Diagnostic composition is excluded from its own workload counts and cannot
  feed timing or visual data back into simulation.
- D3D11 uses a non-blocking four-slot timestamp-query ring, estimates tracked
  resource bytes, uploads the portable panel to a dynamic texture, and blends
  it after native or scaled presentation. Metal obtains GPU time from completed
  command buffers, reports its admitted/backend allocation sizes, uploads the
  same panel to a shared texture, and places it inside UIKit safe-area insets.
- Windows adds explicit `--render-diagnostics` and a public, data-less
  `--capture-diagnostic-frame` path. A 2560x1440, 100%-scale D3D11 capture
  visibly proves that the panel is composed into the native backbuffer. Its
  SHA-256 is
  `4EF8D286642D3326F80B4FF444F3632042A2BD925F55ABF37289503A345DDCA4`;
  the local BMP remains ignored.
- The complete portable build passes 91/91 tests. Independent MinGW GCC 15.2
  and MSVC 19.51 Windows product builds each pass 95/95 tests, including both
  D3D11 overlay smoke paths. GitHub Actions runs `30476632803` and
  `30476641501` pass all seven hosted clangd, Windows, Ubuntu, macOS,
  iPhoneOS, and iPhoneSimulator gates. Physical-device timing accuracy remains
  pending.

## 2026-07-29 - deterministic Windows input-consumer parity

- Added a portable `PlayerAircraftPresentationCoordinator`. It evaluates one
  immutable `InputFrame`, publishes the matching actor pose when an endpoint
  exists, and commits `PlayerAircraftState` only after a successful or
  temporarily busy publication. Simulation rejection, an expired weak
  endpoint, or any non-busy pose failure leaves the last state/hash unchanged
  and makes the coordinator terminal.
- The iOS controller now uses the coordinator instead of duplicating the
  simulation/pose transaction. Synthetic tests exercise headless Windows
  staging, successful publication, real two-slot busy/recovery, expired
  ownership, typed pose rejection, and invalid simulation input.
- An authenticated Windows mission now becomes `ready` but remains explicitly
  paused until pause/menu is pressed. Eligible SDL3 frames are consumed exactly
  once; pause, focus regain, and controller replacement never auto-resume.
  Gameplay-boundary resets clear keyboard, mouse, and controller state and
  require fresh neutral input. Catch-up is capped at eight 60 Hz frames, matching
  the iOS pump policy.
- This milestone records changing input intentions and canonical hashes only.
  It does not claim movement: Windows continues to render the authenticated
  spawn transform until a trace-driven 12 ms producer exists. The Metal-local
  pose-runtime planner must be extracted before D3D11 consumes dynamic pose
  leases.
- A clean GCC 15.2 portable build passes 92/92 tests. A clean native MSVC
  19.51/Ninja Windows build passes 96/96 tests, including both D3D11 product
  smoke paths. The Visual Studio generator itself was not used locally because
  the Codex host exposed duplicate `Path`/`PATH` entries to MSBuild; the
  official VS environment and identical MSVC toolchain compiled the complete
  product successfully.

## 2026-07-29 - portable player-pose runtime preparation and D3D11 lease

- Extracted the Metal-local pose-runtime planning and preparation rules into a
  portable C++20 boundary shared by both native renderers. It distinguishes
  no-player missions, invalid authenticated payloads, and resource-limit
  failures; derives exact actor-only limits and retained bytes; and verifies
  the initial published frame bit-for-bit against the authored spawn pose.
- Metal now consumes the shared planner instead of maintaining a duplicate
  implementation. D3D11 strongly owns the prepared runtime in its immutable
  mission snapshot, resolves gameplay instances through one coherent lease per
  frame, and releases that lease after the draw pass.
- D3D11 exposes a replacement-safe weak producer endpoint matching Metal.
  Windows supplies it to the shared player presentation coordinator whenever
  an authenticated player runtime exists. Mission replacement expires old
  producers safely; candidate preparation remains transactional.
- Synthetic tests cover no-player, provenance and binding mismatches, exact
  planning, step-zero identity, changing-pose publication, tamper rejection,
  authored-instance mismatch, and platform-limit failure. A complete native
  MSVC 19.51/Ninja Windows build passes 97/97 tests, including both D3D11
  product smoke paths.
- This is a transport and ownership milestone, not a movement claim. Windows
  and iOS still publish the authenticated frozen spawn until controlled traces
  justify the separate 12 ms AirCraft producer. No private content, logical
  names, checksums, paths, or derived assets enter the repository.

## 2026-07-29 - isolated slot-45 thrust-control prefix

- Extended the existing portable thrust-state helper with one caller-owned
  state for persistent apply, target thrust, and smoothed thrust. An executed
  step updates and clamps the target only for positive health, then always
  delegates smoothing to the already implemented engine-start-aware
  recurrence.
- The transition preserves native branch visibility: non-positive health does
  not inspect the apply field, while target and smoothing remain live.
  Persistent apply is never cleared by the force step. Invalid active inputs or
  non-finite arithmetic fail closed without returning a partial state.
- Synthetic tests cover same-step ordering, persistent apply, upper/lower
  clamp, both smoothing branches, zero/negative-health gating, skipped invalid
  apply, later active rejection, and overflow/non-finite failures.
- The helper owns no `dt`, sleep gate, input clock, event reducer, Q15 mapping,
  rigid-body state, or renderer publication. It remains deliberately unwired
  until controlled traces establish the 60 Hz input-to-native-event and 12 ms
  sample-and-hold policy. No original or private data is required.

## 2026-07-29 - native thrust-event typed-write reducer

- Added a separate allocation-free C++20 decoder for already-formed native
  `0x63 THRUST_SET` and `0x64 THRUST_APPLY` events. It returns exactly one
  target/apply field write plus the proven nonzero rest-duration clear
  directive, or an explicit inactive/unsupported/outside-evidence result.
- The valid-event inactive gate precedes payload inspection. Active payloads
  are conservatively bounded to `[-255, 255]`, as confirmed for command and AI
  producers. The analog formula has no local clamp and its upstream raw-axis
  domain remains unproven; this is a portable fail-closed evidence boundary,
  not an invented native or universal producer range check.
- Wider-intermediate conversion retains exact distinguishing vectors:
  `APPLY(249) = 0x3C9FFC25` and `APPLY(255) = 0x3CA3D70B`.
  Synthetic tests cover nine exact vectors, all 511 admitted values against an
  independent integer-product/RNE32 oracle, inactive/out-of-range precedence,
  both misleading `THROTTLE_*` values, typed single-field writes, explicit
  last-write-wins/rest semantics, and composition with the separate slot-45
  transition.
- The decoder owns no Q15 mapping, input or event clock, queue, scheduler,
  state commit, force step, or renderer publication. It remains deliberately
  unwired until controlled traces establish the 60 Hz-to-12 ms sample-and-hold
  policy. No original or private data is required.
- Clean GCC 15.2 and MSVC 19.51 portable builds each pass 94/94 tests. A clean
  native MSVC Windows product build passes 98/98 tests, including both D3D11
  product smoke paths.

## 2026-07-29 - portable render-presentation settings contract

- Accepted ADR-0014 and added one allocation-free C++20 settings snapshot for
  render scale, Hor+/Original 4:3, diagnostics, and the Classic/Enhanced
  policy selector. Enhanced is explicitly not evidence that its lighting,
  material, or post-processing stages are implemented.
- Sparse overrides build and validate a complete candidate; invalid scale,
  non-finite input, or forged presentation/profile enums leave the complete
  base snapshot unchanged. A deterministic delta separately identifies scaled
  target, layout, overlay, and profile work.
- Added a versioned storage-neutral semantic record mapping. Conversion in
  either direction rejects unsupported schemas, invalid raw Boolean/enum
  values, and invalid scale without exposing a partial snapshot or treating
  raw object layout as a file format.
- Synthetic tests cover exact defaults and boundaries, NaN/infinity, atomic
  rejection, partial overrides, record round-trips, future schemas, delta
  classification and layout orthogonality. The value operations are `noexcept`
  and use no dynamically owning members. A clean GCC 15.2 build compiles all
  307 steps and the
  complete portable suite passes 95/95; the public boundary checks 480 files.
- Platform persistence, transactional target preparation, and final settings
  UI remain later ADR-0014 slices. No original asset, private path, checksum,
  binary, or owner-derived capture is used.

## 2026-07-29 - transactional D3D11 presentation snapshot

- Replaced the three independent D3D11 presentation setters with one complete
  `RenderPresentationSettings` transaction. A scale change prepares color,
  RTV, SRV, depth, and DSV in a private candidate bundle before changing the
  active renderer; exactly 100% publishes the direct-backbuffer state without
  retaining an intermediate target.
- Added an allocation-free publication gate after GPU preparation and before
  any active mutation. It is the insertion point for the later durable Windows
  save. Gate rejection, invalid settings, unavailable surface, and controlled
  late target failure leave both the snapshot and all five old COM identities
  unchanged; an accepted gate is followed only by no-fail publication work.
- Scaled resize now retains the old bundle until the swapchain and candidate
  targets are complete. A target-preparation failure records a pending extent,
  renders with the previous complete surface when possible, and first retries
  on the next frame. Repeated failures back off for 1, 2, 4, and up to 120
  frames instead of entering a per-frame allocation loop; a new explicit
  resize signal resets the schedule. Zero-extent suspension releases only
  swapchain resources and retains the independent scaled bundle for restore.
- Startup override failure reports only a stable category and continues with
  the active safe snapshot. Smoke-test transition rejection remains fatal so
  CI cannot hide a broken requested scale.
- A native D3D11 runtime test covers 100 -> 50 -> 200 -> 100, 50% and 200%
  resize, automatic resize retry, zero-extent suspend/restore, surface
  unavailability, consecutive late allocation failures with a skipped backoff
  frame, publication-gate rejection and retry, exact five-object identity,
  real Original 4:3 viewport selection, diagnostics resource lifetime, and the
  Enhanced policy snapshot. A clean MSVC 19.51/Ninja build passes all 100
  Windows tests; the portable preset remains at 95/95. Hosted CI and the
  equivalent Metal transaction remain pending.

## 2026-07-29 - native pitch/bank SET static contract

- `EV-20260729-007` / `EXP-20260729-056` cross-checks
  `PITCH_SET (0x5F)` and `BANK_SET (0x65)` in Ghidra 12.1.2 and Rizin 0.9.1.
  Both tools agree on the owning `AfVehicle::ProcessEvent` range, the exact
  local branches, signed `int32` payload, target fields `+0x448`/`+0x44C`, and
  the inactive-gate/store/extended-compare/rest-clear instruction order.
- The report records the exact binary32 scale bits `0x3D3020C5`, the startup
  PC53 observation, and 11 discriminating stored-binary32/x87 vectors under
  the explicit condition `PC=53, RC=nearest-even`. Independent review
  recomputed all 11 conditional stored binary32 results successfully; the
  static session's binary32 and PC53 validators each pass 11/11. Neither the
  live precision nor rounding control is claimed without a branch-time trace.
- Producer evidence remains separate from the native consumer: keyboard emits
  exactly `{-32,0,+32}`, AI is bounded to `[-32,+32]`, and DirectInput is
  bounded to `[-32,+32]` after the required X/Y `DIPROP_RANGE [0,10000]`
  setup, conditional on API/driver conformance.
- Active zero overwrites the selected field with positive zero without
  clearing the rest duration; active nonzero clears it; inactive events do not
  mutate state; repeated writes are not suppressed; and the last processed SET
  wins per axis. APPLY is a no-op only for the inspected AirCraft inheritance
  chain.
- This is a static behavioral contract and synthetic test plan, not a timing
  or implementation claim. Cross-producer ordering, event/refresh phase,
  process-wide live x87 precision and rounding control, and real nominal-12-ms
  clock behavior remain unknown. No reducer was implemented or wired.
  Public-boundary checks pass for 483 files, Rizin export tests pass 12/12, and
  no binary, analysis database, private asset, dump, trace, or local path is
  committed.

## 2026-07-29 - transactional Metal presentation snapshot

- Added a portable C++20 presentation transaction above the semantic settings
  value. Prepared and active states carry the exact view/device identities,
  positive output extent, surface generation, revision, derived target extent,
  and an optional copyable opaque target owner. Final validation reports stale
  revision, view, device, extent, and generation separately before a no-fail
  move commit.
- Replaced Metal's three independently mutable presentation settings and lazy
  target allocation with one complete-snapshot apply boundary. Exactly 100%
  owns no intermediate scene target. Every non-100% candidate must prepare both
  private BGRA8 color and Depth32 textures before publication, even when rounded
  dimensions could equal the output.
- Each Metal target pair now obtains and reconciles a reservation from the
  existing 384 MiB snapshot GPU ledger. Failed or partial preparation releases
  the candidate reservation and preserves the active settings and exact old
  target owner. Diagnostics no longer double-count those ledger-owned targets.
- A draw copies one immutable active state and command-buffer completion retains
  that lease. Positive resize publishes only a complete matching surface;
  failure suppresses presentation to a mismatched drawable and retries
  immediately, then after 1, 2, 4, ... up to 120 frames. Zero extent retains
  settings and the last complete pair. Explicit resize, success, and zero reset
  retry state.
- Removed the iOS shell's forced diagnostics enable so the canonical default is
  honored. Enhanced remains a stored selector only and changes no Metal pixels
  in this slice.
- Synthetic transaction tests cover 100 -> 50 -> 200 -> 100, exact target
  identities, orthogonal 4:3/diagnostics/profile reuse, consecutive late
  failures, every stale-surface category, resize/zero recovery, backoff,
  accounting, and in-flight owner lifetime. The local portable build and all
  96 CTests pass. Both independent reviews report GO with no remaining P0-P2.
  All seven hosted checks pass, including Xcode 26.6 iPhoneOS and
  iPhoneSimulator builds plus the native Windows D3D11/XAudio2 smoke.

## 2026-07-29 - durable render-presentation settings and Windows binding

- Added the canonical AFRS v1 byte format above the storage-neutral semantic
  record. The exact 71-byte current-schema document uses a little-endian
  envelope, ordered single-occurrence TLVs, exact IEEE-754 render-scale bits,
  a 4 KiB input ceiling, and SHA-256 over the complete envelope outside the
  digest field. Missing, duplicate, unknown, out-of-order, truncated, trailing,
  wrong-size, invalid-value, and noncanonical documents are rejected without
  producing a partial snapshot.
- A structurally intact future schema remains opaque exact bytes. A downgrade
  uses safe defaults, does not fall through to an older backup, and refuses to
  replace either retained future current or backup state.
- Extended the portable durable-file boundary with one-handle bounded reads on
  Win32 and POSIX. The reader rejects directories, symlinks/reparse points,
  multiple hard links, oversized input, truncation/growth, and identity, size,
  or link-count changes while reading.
- Added a serialized current/backup settings store. Only a fully validated
  current record can rotate into backup. New bytes are written to a prepared
  sibling, synchronized, atomically replaced, and read back exactly. A
  post-rename error is accepted only after the target matches the requested
  bytes and file/directory durability can be retried; all other ambiguous
  outcomes report `commitUnknown` without exposing a host path.
- Windows now derives its private settings directory through
  `SDL_GetPrefPath`, applies defaults -> one recovered persistent snapshot ->
  sparse explicit launch overrides, and keeps launch values session-only.
  Both positive and negative presentation/diagnostics flags and the
  Classic/Enhanced selector are expressible. Smoke, capture, and validation
  invocations deliberately bypass the profile resolver, so public CI cannot
  read or modify user preferences.
- Independent review initially rejected a linked-directory asymmetry. The
  store now validates the `settings/` leaf itself before load as well as save,
  rejects directory symlinks/reparse points, documents its serialized
  app-private parent trust boundary, and has a synthetic linked-directory
  regression test. Re-review reports GO with no remaining P0-P2.
- Clean WSL Ubuntu GCC 13.3/Ninja builds all 317 steps and passes 98/98 tests.
  Native MSVC 19.51/Ninja builds the complete Windows product and passes
  104/104 tests, including D3D11 renderer and both data-less product smokes.
  All seven PR checks also pass: hosted portable/macOS/Windows/clangd and the
  native Windows product in run `30502869591`, plus unsigned iPhoneOS and
  iPhoneSimulator in run `30502869598`.
  No original asset, private setting, owner path, or generated AFRS record is
  committed.

## 2026-07-29 - asynchronous iOS render-settings persistence

- Split Metal presentation changes into main-thread request capture, serialized
  off-main resource preparation, and main-thread fresh validation followed
  immediately by the existing no-fail move commit. The worker owns an immutable
  transaction snapshot and never reads the live drawable or UIKit state.
- Added a portable owner-thread persistence gate. Exactly one candidate may
  prepare, save, and publish; loaded values skip saving, new values save once,
  stale revision/surface results increment an exact attempt ticket and reprepare
  the already durable candidate without another save, and stale callbacks
  cannot abandon newer work.
- Added an iOS serial store under the private Application Support settings leaf.
  It creates missing components individually with their final protection,
  enforces mode `0700`,
  `NSFileProtectionCompleteUntilFirstUserAuthentication`, path-free error
  categories, and fresh-load recovery for an ambiguous durable commit.
  Permission repair uses an opened no-follow directory descriptor and exact
  device/inode identity is rechecked. Darwin file synchronization requests
  `F_FULLFSYNC` after `fsync` where the volume supports it.
- The iOS controller keeps persistent base, session override, and effective
  renderer values separate; coalesces later UI requests; and gates first
  gameplay presentation until startup load/default resolution. An exact-ticket
  preparation retry backs off from 50 ms to 2 s, while layout/activation may
  wake it early and stale post-save publication never performs a second save.
- Independent review found three related P2 classes across two rounds:
  guaranteed retry wakeup/timer identity, Foundation exceptions crossing
  `noexcept`, and path-based directory mutation. Exact timer generations,
  throwable Objective-C++ helpers, and descriptor-relative permission repair
  resolved them; final re-review reports no remaining P0-P2.
- Windows GCC/Ninja and isolated WSL GCC/Ninja each pass 99/99 CTests. The
  public-boundary scanner passes 505 files. PR #62 uses hosted runs
  `30505632630` and `30505632588` for portable/native Windows and both unsigned
  Apple SDK builds. All validation uses synthetic settings; no owner asset,
  profile path, or generated settings record is committed.

## 2026-07-29 - isolated native pitch/bank SET reducer

- Added an allocation-free typed-write decoder for already-ordered native
  `PITCH_SET (0x5F)` and `BANK_SET (0x65)` events. It accepts the complete
  signed `int32` consumer domain, preserves the inactive no-op gate, selects
  only pitch or bank, writes active positive zero, repeats equal overwrites,
  and requests the shared rest-duration clear for every active nonzero event.
- The sole numeric policy is explicitly named startup-compatible PC53 and
  nearest-even. It returns exact binary32 bits and uses integer rational
  arithmetic for the 53-bit retained-product rounding followed by the
  binary32 store rounding, so the result does not depend on the host FP
  environment. This remains a conditional compatibility policy, not a claim
  that live native x87 control state has been observed.
- Dedicated tests cover both axes, all 11 confirmed PC53/RNE vectors,
  `INT32_MIN/MAX`, the decimal-double, premature-binary32, and PC64
  discriminators, inactive and unsupported inputs, zero/nonzero rest
  behavior, repeated writes, interleaving, and last-processed-write wins.
- The reducer remains deliberately unwired from input producers,
  `PlayerAircraftState`, event queues, and the nominal 12 ms refresh. Those
  integrations still require controlled evidence for numeric mode and
  ordering.
- Independent review found one P2 validation-precedence error: an unsupported
  reconstruction policy was rejected before the native inactive gate. The
  gate now accepts every recognized inactive SET before active-policy
  validation; its regression test and symmetric per-axis/interleaving tests
  close the finding. Final re-review reports GO with no remaining P0-P2.
- An independent exhaustive audit compared every one of the `2^32` signed
  payloads with a hardware binary64 PC53/nearest-even reference and found no
  mismatch. A second exact-product oracle confirmed the sole positive PC53
  double-rounding discriminator and its sign counterpart.
- Clean Windows GCC 15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds
  each pass 100/100 CTests. A fresh Clang 22.1.8 build passes the dedicated
  reducer test, the public-boundary scanner passes 509 files, and
  `git diff --check` passes. All seven hosted checks pass: portable, native
  Windows product, macOS, and clangd jobs in run `30507391649`, plus unsigned
  iPhoneOS and iPhoneSimulator jobs in run `30507391659`.

## 2026-07-30 - native TURN_SET and transactional control state

- Identified the previously unnamed `AfVehicle+0x450` wake control as the
  positive-zero-initialized binary32 destination of `EVENT_TURN_SET (0x5D)`.
  Its active branch consumes the complete signed `int32` payload, uses the
  same exact `0x3D3020C5` x87 multiply/store path as pitch and bank, and clears
  the shared signed rest duration only for nonzero results.
- Confirmed discrete `turnleft/turnright` payloads `-32/+32` and zero/opposite
  release behavior. No analog or AirCraft/GroundUnit/WaterUnit AI producer,
  and no control-law consumer outside the sleep gate, was found.
  `TURN_APPLY (0x5E)` is a no-op in the inspected chains; `TURN_SET` remains
  distinct from the separate YAW event pair.
- Added an isolated `TURN_SET` typed-write reducer and moved the exact
  turn/pitch/bank PC53/RNE calculation to one host-FP-independent shared
  helper. The existing pitch/bank vectors remain unchanged.
- Added a simulation-thread-confined transactional owner for the two thrust
  writes and three angular SET writes. It validates before mutation, commits
  the selected field and explicit shared-rest clear together, preserves
  unselected state, and exposes the five sleep controls in native order.
  It owns no producer ordering, Q15 conversion, scheduler, sleep-result
  persistence, rigid-body clear, or live player-state integration.
- Synthetic tests cover all confirmed TURN_SET exact vectors, inactive and
  invalid paths, every field transaction, exact wake ordering, rollback,
  repeated and interleaved writes, and composition with the separate sleep
  helper using an arbitrary synthetic refresh delta.
- Independent review reports GO with no P0-P2 findings. Its two P3 notes were
  resolved by adding a direct `<bit>` include and removing nominal 12 ms from
  the owner-composition test. Clean Windows GCC 15.2/Ninja and isolated WSL
  Ubuntu GCC 13.3/Ninja full builds pass all 102 CTests. A fresh Clang 22.1.8
  build passes the three affected tests. Synthetic public-boundary tests, all
  12 Rizin normalization tests, the Ghidra/working-copy/Rizin PowerShell
  wrapper suites, the 519-file public scan, and `git diff --check` pass.
  Hosted main runs `30510158979` and `30510158971` subsequently pass the
  Windows portable, Windows D3D11/XAudio2, Ubuntu, macOS, clangd, iPhoneOS,
  and iPhoneSimulator jobs.

## 2026-07-30 - native discrete control-command reducer

- `EV-20260730-001` / `EXP-20260730-062` independently maps all eight bool
  command flags in the `Dogfighter.exe` user-command callback with Ghidra
  12.1.2 and Rizin 0.9.1. The callback stores its selected flag before payload
  selection and emits exactly one event for every valid invocation, including
  repeated true, repeated false, equal nonzero, and zero payloads.
- The exact mappings are `TURN_SET` and `PITCH_SET`/`BANK_SET` at magnitude
  32 plus `THRUST_APPLY` at magnitude 255. A true invocation selects its own
  sign; false restores the opposite sign when that flag remains active and
  otherwise emits zero. Cross-axis flag state remains independent.
- Added an allocation-free pure reducer over one already-ordered bool command
  invocation and an eight-bit caller-owned snapshot. It returns the complete
  next snapshot and one variant of the existing typed native-event inputs.
  The downstream decoders retain the vehicle-inactive gate, angular numeric
  policy, exact field conversion, and rest-clear decision.
- The reducer deliberately has no `InputFrame`, Q15, device, queue, focus-loss,
  key-repeat cadence, cross-producer ordering, scheduler, or player-state
  dependency. Final per-tick Q15 axes cannot preserve both opposing command
  flags, an in-tick tap, or last-invocation order, so live input wiring remains
  a separate evidence and architecture gate.
- Synthetic tests enumerate both values of every command over all 256 flag
  snapshots, both ordering directions for all four pairs, repeats, zero
  releases, cross-pair isolation, tap order, forged enum rollback, inactive
  and invalid-policy precedence, and complete typed-event-to-decoder-to-owner
  composition with exact bits and rest behavior.
- Independent review found one P2 catalogue overclaim: the complete
  multi-command dispatcher had been marked implemented for an eight-branch
  subset. It now remains `in_progress` and identifies the subset explicitly.
  Final re-review reports GO with no open P0-P3; its separate GCC 15.2
  warnings-as-errors build and dedicated test pass.
- Clean Windows GCC 15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds
  compile the complete repository and pass 103/103 CTests. A fresh Clang
  22.1.8 build passes the dedicated test. The public-boundary tests, all three
  reverse-engineering wrapper suites, 12 Rizin export tests, 523-file public
  scan, 268-row/14-column function catalogue, changed-document links,
  changed-scope local-path scan, and `git diff --check` pass. Pull-request
  Actions runs `30511495498` and `30511495515` pass all seven hosted Windows
  product, Windows/Ubuntu/macOS portable, clangd, iPhoneOS, and
  iPhoneSimulator jobs.

## 2026-07-30 - staged native control-command step

- Added one allocation-free production composition point for exactly one
  caller-ordered bool command invocation: eight-flag transition, typed native
  event, matching turn/pitch-bank/thrust decoder, typed write, and
  transactional control-field owner.
- Preserved the recovered stage order instead of inventing a larger atomic
  transaction. A recognized inactive invocation updates its callback flag but
  changes no vehicle control field. An active unsupported angular policy also
  retains that earlier flag update while failing closed before a field write;
  an unsupported command changes neither state.
- The result retains typed event/write evidence and distinguishes committed,
  inactive-ignored, unsupported-command, decode-rejected, and defensive
  owner-rejected outcomes. The owner-rejected path is unreachable for every
  write currently produced by the closed decoder set and guards future
  contract drift.
- Synthetic tests cover both bool values for all eight commands over every
  8-bit flag snapshot, exact event and write variants, binary32 bits, rest
  behavior, inactive and numeric-policy precedence, invalid rollback, repeats,
  deterministic mixed-axis replay, and prefix/suffix composition.
- No `InputFrame`, Q15 conversion, producer identity, event collection, trace
  serialization, sorting, deduplication, sample-and-hold, 60 Hz/12 ms timing,
  or live player-state wiring was added.
- Fresh Windows GCC 15.2/Ninja and isolated WSL Ubuntu GCC 13.3/Ninja builds
  compile all 336 steps and pass 104/104 CTests. Fresh Clang 22.1.8 passes the
  dedicated test, and one independent review additionally passes both GCC and
  Clang warnings-as-errors builds. All three reviews report GO with no P0-P3
  findings. Public boundary tests, 12 Rizin export tests, all three
  reverse-engineering wrapper suites, the 527-file public scan, document
  links, local-path scan, clang-format, and `git diff --check` pass.

## 2026-07-30 - iOS render-settings panel and menu input boundary

- Added an allocation-free portable settings-menu model over the existing
  ADR-0014 snapshot. It separates applied and draft values, derives the exact
  settings delta, freezes edits during Apply, issues non-wrapping exact tickets,
  promotes only exact successes, preserves the draft after failure, and ignores
  stale callbacks.
- Extended the iOS settings coordinator with path-free completion results and
  an active-renderer snapshot. Success is reported only after durable save and
  final Metal publication. Pending requests remain latest-wins and explicitly
  supersede an older pending completion without confusing durable and active
  state.
- Added a safe-area UIKit child overlay for 50-200% render scale,
  Hor+/Original 4:3, Classic/Enhanced preview, and renderer diagnostics. It uses
  Dynamic Type, VoiceOver semantics, standard controls, path-free errors, and
  explicitly distinguishes the Enhanced profile from optional private HD
  textures.
- Added an immutable UI-only input projection after exact portable-frame
  delivery. Menu/modal bindings now cover left-stick navigation, D-pad Up/Down,
  shoulders, A, and B without emitting gameplay actions. The pause surface has
  explicit touch Resume and controller A-settings/B-resume behavior.
- Opening or closing the overlay pauses audio/session/Metal, resets every
  physical source, changes input context explicitly, hides flight touch
  controls, and never auto-resumes gameplay. Closing during an in-flight Apply
  detaches the UI but does not pretend to cancel the durable transaction.
  Reopening is refused until the transaction is idle, so a panel seeded from
  an older active snapshot cannot enqueue a full candidate that rolls back the
  in-flight change.
- Independent review found that close/reopen race and a missing executable
  seam around asynchronous completion arbitration. The race is closed, and a
  portable request queue now owns exact identities, latest-wins superseding,
  two-phase reentrant completion, pending promotion, stale/duplicate rejection,
  and non-wrapping exhaustion under deterministic tests.
- The full local build and 106/106 CTests pass, as do clang-format,
  `git diff --check`, and the 536-file public-boundary scan. Pull-request
  Actions runs `30516125990` and `30516125991` pass all seven hosted jobs,
  including iPhoneOS and iPhoneSimulator compilation/linking. Physical-device
  interaction/persistence acceptance remains to be recorded.

## 2026-07-30 - Windows render-settings panel and native D3D11 overlay

- Added a native Windows pause and display-settings surface over the shared
  C++20 settings model. Keyboard, mouse, standard controller buttons, and
  D-pad navigation share explicit menu-context routing without leaking menu
  input into gameplay.
- Added a Direct2D, DirectWrite, and WIC raster path which uploads a
  premultiplied BGRA overlay to D3D11 and composites it after the 3D scene and
  diagnostics. The adaptive layout fits the supported 640x360 minimum while
  preserving DPI-aware scaling at larger window and capture sizes.
- Added a Windows transaction coordinator which keeps durable settings
  separate from sparse command-line overrides. Renderer publication follows a
  successful save; indeterminate commit recovery accepts only a fresh,
  verified current settings snapshot and never treats defaults or backup
  recovery as proof of durability.
- Added a public, session-only `--capture-settings-panel` diagnostic path for
  synthetic screenshots. Captures at 960x540, 1920x1080, and 3440x1440 were
  reviewed locally and remain ignored build artifacts rather than repository
  content.
- Independent review found and then verified fixes for the indeterminate-save
  readback rule and small-window clipping. Final review reports GO with no open
  P0-P2 findings; physical per-monitor DPI, minimum-size readability, pointer,
  and controller behavior remain device acceptance work.
- A clean Windows MSVC/Ninja product build passes all 115 CTests. The isolated
  `Airfix-Dev` WSL clone builds all 302 Linux targets and passes 106/106 CTests.
  Clang-format, `git diff --check`, and the 546-file public-boundary scan pass.
## 2026-07-30 - bootstrap and module-loader cross-check

- `EV-20260730-002`: a complete 16-module PE scan confirmed that only
  `AfEngine.dll` and `Cc.dll` own recovered dynamic-loading paths;
  `Dogfighter.exe` is the v1.01 Win32 application shell and does not dynamically
  load the historical plugins.
- Recovered the executable wrapper vtable and exact message-pump,
  input/time-update, active-frame-event loop order.
- Separated setup metadata probing, setup-only device enumeration, and the
  retained runtime `Cc.dll::GtOpenDevice` path. Confirmed graphics and mission
  handle teardown and bounded the remaining shared type-handle question.
- Ghidra 12.1.2 and Rizin 0.9.1 agree on representative function boundaries,
  direct calls, loader gates, and data references. No original binary or plugin
  was executed.
- Promoted bootstrap/resource loading from `not-investigated` to `observed`;
  portable implementation remains a deterministic C++20 registry rather than
  a recreation of Windows wildcard order or DLL loading.

## 2026-07-30 - native pitch/bank producer ordering and 12 ms phase

- `EV-20260730-003` / `EXP-20260730-067` follows every local keyboard,
  DirectInput joystick, and AirCraft AI path that creates or immediately
  dispatches `PITCH_SET (0x5F)` and `BANK_SET (0x65)`.
- Ghidra 12.1.2 and Rizin 0.9.1 agree on 19 function boundaries and the
  relevant instructions. Rizin automatic discovery found twelve; seven
  entries missed by automatic discovery were created only at independently
  established vtable or switch addresses and then agreed with Ghidra.
- Device construction in keyboard, mouse, joystick order plus head insertion
  proves traversal in joystick, mouse, keyboard order. Every manager request
  restarts at joystick. One successful joystick snapshot supplies bank before
  pitch and processes each raw event before requesting the next.
- Keyboard obtains one buffered record per request and executes mapped binding
  text synchronously. It has no fixed cross-axis order beyond record and
  binding order. Every command callback updates its held byte and emits one
  SET, including repeats.
- Analog history updates before immediate event processing and suppresses
  mapped raw changes below ten. AI relative-value caches also update before
  processing and suppress exact mapped binary32 repeats. An inactive drop is
  not replayed by either cache after activation.
- The AI step calculates controls then fully saves/processes changed PITCH
  before BANK. Its exact-binary32 `0.1` interval accumulator resets instead of
  retaining overshoot; uninterrupted 12 ms vehicle deltas therefore trigger
  nominally on the ninth refresh at approximately 108 ms.
- Local input drains before QPC-derived `SetTime`. `SetTime` polls remote
  input, then the periodic heap can invoke zero, one, or multiple 12 ms
  AirCraft refreshes; `SetRealTime` and render follow. The recovered time-event
  branch skips its heap refresh at a forward difference of 699 ms or more.
- No reducer, runtime integration, input frame, fixed-point axis, renderer,
  scheduler, executable, game resource, or tool database changed. The report
  recommends NO-GO for full live-input integration and conditional GO only for
  a later one-event-at-a-time adapter that preserves immediate native order
  without batching, sorting, deduplication, interpolation, or replay.
- The new Ghidra reference exporter compiled and ran headlessly. It now
  validates that every requested address belongs to program memory, records
  deterministic unresolved markers, and fails closed; real valid and invalid
  headless probes plus a wrapper regression pass. Forty-three selected Ghidra
  reports contain no unresolved-address marker, and all 19 normalized Rizin
  boundaries match the public ledger. The three RE wrapper
  suites, 12 Rizin tests, synthetic public-boundary tests and 549-file scan,
  287-row/14-column unique function catalog, changed-document links,
  changed-scope local-path scan, reserved-ID scan, and `git diff --check`
  pass.

## 2026-07-30 - isolated CcRigidBody integration kernel

- `EV-20260730-004` / `EXP-20260730-068` recovers the complete static
  variable-state contract for `CcODE::EulerODE`, `CcRigidBody::Derive`,
  `CalcAuxiliary`, `PostODE`, `ResetForceAndTorque`, and
  `CcQuaternion::Normalize`.
- Confirmed `count=0x0D`, `stateOffset=0x40`, and the ordered state fields:
  position, `(w,x,y,z)` quaternion, linear momentum, and world angular
  momentum. Configuration and auxiliary offsets through the force/torque
  accumulators are documented separately.
- Proved column-major matrix storage, native row-vector coordinate
  application, `W=R*bodyInverseInertia*transpose(R)`, and left-Hamilton
  quaternion differentiation. The upstream AirCraft constructor's in-place
  matrix inversion proves that `+0x18` is body inverse inertia during
  integration.
- Recorded the exact x87 instruction and binary32-store schedule, including
  matrix `k=0,2,1` accumulation, retained/spilled quaternion products,
  normalization order, and the X/Y-stored versus Z-retained angular-damping
  asymmetry.
- Added six synthetic exact-bit vectors. One damping vector distinguishes the
  three lanes under PC53/PC64, and one Euler vector produces `0x3F800000`
  under PC24/RNE or PC53/RNE but `0x3F800001` under PC64/RNE.
- Ghidra 12.1.2 and Rizin 0.9.1 plus bundled `rz-ghidra` pseudocode agree on
  every main/helper boundary and broad data flow. Rizin pseudocode's
  hidden-return typing remains secondary to the agreeing instructions.
- Both RE wrapper suites, 12/12 Rizin exporter tests, public-boundary
  synthetic tests, the 551-file public scan, the 298-row/14-column unique
  catalog check, changed-document links, changed-scope local-path scan, and
  `git diff --check` pass.
- Independent instruction-level review closed signed-zero, PC24, empty-CFG,
  transpose-call, self-contained-vector, and constant-report findings; the
  final re-review found no further issue.
- No game, GUI, debugger, executable path, kernel, scheduler, collision,
  renderer, live-input integration, binary, tool database, or private
  resource changed. The decision is NO-GO for bit-parity implementation until
  the live x87 control/exception state is captured or an explicit portable
  numeric policy is accepted.

## 2026-07-30 - one-event pitch/bank state step

- `EXP-20260730-069` implements the conditional GO from the producer-order
  report as one narrow, allocation-free composition point for an
  already-formed `PITCH_SET` or `BANK_SET`.
- Each call uses the existing exact typed decoder and the existing
  simulation-thread-confined state owner. A committed event returns one typed
  write and the complete next snapshot; inactive and rejected inputs return
  the original snapshot unchanged.
- The API is deliberately pitch/bank-only. It has no event collection,
  producer/source tag, timestamp, sequence number, cache, retry, replay,
  sorting, deduplication, interpolation, `InputFrame`, Q15 conversion,
  scheduler, clock, physics, or renderer ownership.
- Synthetic tests preserve joystick BANK-before-PITCH, AI PITCH-before-BANK,
  arbitrary caller order, last processed SET per axis, equal repeated writes,
  active positive zero, inactive drop without replay, and fail-closed invalid
  event/policy behavior. Exact PC53/RNE arithmetic remains owned by the
  lower-level decoder and remains explicitly conditional on the unobserved
  live x87 control word.
- This is not live input integration. The adapter remains unwired pending
  controlled evidence for `_ftol`, DirectInput conformance, remote ordering,
  wall-clock cadence, and the input-to-12-ms sample-and-hold join.
- Fresh complete Windows GCC 15.2/Ninja and MSVC 19.51/Ninja builds each pass
  107/107 CTests. Independent review reports GO with no P0-P3 findings after a
  separate clean GCC build and Clang 22 warnings-enabled syntax check.
  Clang-format, all three reverse-engineering wrapper suites, 12 Rizin
  exporter tests, changed-document links, local-path scanning, the 553-file
  public-boundary scan, and `git diff --check` pass. Hosted platforms remain
  the publication gate.

## 2026-07-30 - isolated mission outcome state

- `EV-20260730-005` / `EXP-20260730-070` recovers the exact two-byte
  `NfMission` outcome boundary. Full-object offsets `+0x498` and `+0x499`
  independently store failed and accomplished; `NfMission::Call` sees the same
  fields at `+0x448/+0x449` through its `AfFunctionCall` subobject.
- `SetupAfServerFunctions` registers AFS `MissionFail` as `0x47` and
  `MissionSuccess` as `0x48`. Neither case reads a payload. The selected byte
  is set on every call, while only a transition from neither flag set requests
  the exact ordered console sequence `pause` then `menu`.
- Repeats do not re-present. Conflicts do not overwrite: fail followed by
  success and success followed by fail both end with both flags true.
  Direct `NfMission::Fail` sets only failed and requests no presentation;
  the outcome projection of `Reset` clears both bytes.
- Ghidra 12.1.2 is primary. Rizin 0.9.1 independently matches all sixteen
  selected `AfEngine.dll` boundaries. The selected `Singleplayer.mode`
  candidate at RVA `0x12D0` is excluded as an unrelated cheat callback.
- The new allocation-free `LegacyMissionOutcomeState` applies one
  already-ordered call at a time, returns a `requestPauseThenMenu` directive,
  supports direct fail and reset, and fails closed on unsupported identifiers.
  It owns no trigger evaluation, AFS process, batching, ordering, console,
  campaign/save state, multiplayer, renderer, audio, or platform menu.
- Synthetic tests cover all four native flag combinations crossed with both
  calls, both conflict orders, repeats, direct fail, reset, and unsupported
  identifiers. Fresh complete GCC 15.2/Ninja and MSVC 19.51/Ninja builds each
  pass 108/108 CTests. Clang 22.1.8 passes a warnings-as-errors syntax check.
  All three RE wrapper suites, 12 Rizin exporter tests, public-boundary tests
  and the 561-file scan, the 313-row/14-column unique catalogue, changed
  document links, changed-addition local-path and reserved-ID scans, and
  `git diff --check` pass. Review's sole P2 confidence overclaim was fixed by
  applying the project-wide static-only confidence cap; final re-review
  reports GO with no remaining P0-P3 finding. Hosted publication gates remain.

## 2026-07-30 - Controller Profile Core V1

- Added a fixed-capacity controller-only semantic profile with four calibrated
  stick axes and at most 48 bindings. Resolution is atomic and rejects invalid
  schemas, Q15 ranges, enums, controls, physical/target combinations, contexts,
  conflicts, hidden tail state, capacity overflow, unreachable or sub-threshold
  menu navigation, and missing/unreachable pause/menu recovery controls.
- The binding compiler preserves every default touch, keyboard, and mouse
  binding while replacing the complete controller set only after validation.
  No active `InputRouter`, platform adapter, input frame, physics state, or
  native UI is changed.
- Axis calibration is integer-only with round-to-nearest, ties-to-even and
  supports deadzone, outer saturation, 250-2000 permille sensitivity,
  inversion, and linear/squared/cubic curves. The default is exact identity for
  all 65,535 valid symmetric-Q15 values on each of four axes; `-32768` fails
  closed.
- Added canonical AFIP V1 persistence: a 4-KiB-bounded little-endian envelope,
  SHA-256 integrity, exact active-binding encoding, strict current-schema
  fields, envelope-integrity-checked opaque future schemas preserved
  byte-for-byte, and a private durable `current -> backup -> default` store.
- The store rejects linked, non-regular, oversized, malformed, and unsafe
  partial entries; malformed current state is never promoted. Atomic durable
  publication, exact readback, unchanged detection, and commit-unknown
  recovery are covered without exposing paths or document bytes.
- Fresh Windows GCC 15.2/Ninja and MSVC 19.51/Ninja builds each compile all
  357 steps and pass all 111 CTests. Standalone Clang 22 warnings-as-errors
  builds pass for the profile, codec, and store, and the pure profile tests
  independently pass under WSL/GCC. Live application remains a separate
  safe-boundary transaction because active router state is indexed by binding
  position.
- Independent review found and verified fixes for unreachable recovery
  thresholds/directions after calibration and scale, inconsistent backup
  persistence diagnostics, and stale documentation. Windows and iOS now share
  the same portable UI-navigation actuation/release constants. Final re-review
  reports GO with no remaining P0-P3 finding.

## 2026-07-30 - safe native controller-profile startup

- Added an immutable runtime configuration that keeps one resolved controller
  profile together with its exact compiled binding table and a mask of physical
  controls actually used by that profile. Active position-indexed router state
  is never mutated in place.
- Windows interactive startup now loads the private AFIP store through
  `current -> backup -> default` recovery before constructing SDL input.
  Session-only validation modes remain data-less. A changed durable profile is
  applied on restart; no live caller can assert a pause boundary.
- iOS startup applies the canonical default through the same fresh-pair seam.
  The iOS seam is one-time and pre-start, with a fully prepared pair and a
  statically no-throw publication point. D-pad left/right now reach the portable
  previous/next controls.
- Live replacement is deliberately deferred until each product has a
  host-owned pause transaction. A menu/input context alone cannot authorize a
  swap.
- V1 deadzone layering is explicit: the legacy transport floor is applied
  before profile calibration. Configured axes emit every changed calibrated
  Q15 value so a small delta cannot hide a binding threshold crossing. Triggers
  remain a fixed binary native transport at Q15 `16384`; incompatible
  continuous, scaled, or alternate-threshold bindings fail validation.
- Synthetic tests prove prepared profile/table consistency, calibration,
  remapping, transport-floor boundaries, a sub-`1024` threshold crossing,
  discarded pre-swap input, blocked held full snapshots, exactly two neutral
  ticks, fresh remapped input, and safe omission of unmapped controls from full
  snapshots and later events. A fresh GCC 15.2/Ninja build completes 360 steps and
  passes 112/112 CTests; a fresh MSVC 19.51/Ninja Windows product build
  completes 642 steps and passes 121/121 CTests. Clang 22 warnings-as-errors,
  synthetic public-boundary tests, the 572-file repository scan, and
  `git diff --check` pass. Independent re-review reproduces the sparse-profile
  failure before the fix, confirms the corrected full-state/edge behavior, and
  reports GO with no P0-P3 finding. Hosted Apple compilation remains the
  publication gate.

## 2026-07-30 - private iOS controller-profile startup

- Added a load-only Objective-C++ AFIP adapter that resolves the private
  current/backup/default store off-main and delivers exactly one value-only
  result on main before native input may start. Missing or unavailable storage
  installs the in-memory canonical default through the same one-time seam.
- AFIP and AFRS now share one process-wide serial persistence queue and one
  hardened Application Support settings-directory helper. The boundary
  requires owner-only permissions and Data Protection, rejects linked or
  wrong-type entries, verifies the opened inode/device, and never publishes a
  path, checksum, document byte, device identifier, or exception string.
- Startup is gated on visible view, completed profile load, ready frame
  consumer/profile installation, and active application state. A late profile
  completion re-evaluates paused mission readiness without resuming gameplay.
  It restores the menu and Resume action only when content, renderer,
  simulation, and input are all ready; an open settings panel retains modal
  context.
- A portable startup-policy test covers content-before-profile,
  completion-after-disappear, completion-in-background, complete startup,
  late Resume availability, and failure gates. A fresh GCC 15.2/Ninja build
  completes 362 steps and passes 113/113 CTests. Native Apple compilation and
  physical Data Protection/lifecycle acceptance remain publication and device
  gates.

## 2026-07-30 - Windows controller calibration for next launch

- Added a portable controller-profile menu model with distinct immutable
  active, persisted, and draft records. Complete-profile validation and exact
  binding preservation apply to every edit; immutable save tickets promote only
  durable/draft state, so changed calibration remains explicitly
  restart-required.
- Extracted the exact configured controller transport/profile transform into
  one shared runtime/UI helper. The Windows pause surface now provides four
  per-axis editors, resets, raw/adjusted Q15 preview, repair-only save after
  recovery, and `Save for next launch` without replacing or mutating the active
  SDL adapter.
- Added a storage-only Windows coordinator with typed unavailable, blocked,
  failed, committed, unchanged, and commit-unknown outcomes. Ambiguous
  publication is accepted only after exact readback of a valid current AFIP;
  backup/default recovery is not accepted as proof.
- Added a mutually exclusive data-less controller-panel capture mode. It uses
  only a synthetic profile/sample, never reads private settings or game data,
  and generated an inspected 1920x1080 D3D11 frame.
- Targeted GCC/MSVC model and bridge tests pass. MSVC builds and links the
  complete Windows application, and targeted command-line, coordinator, panel,
  rasterizer, SDL mapping, profile-model, and bridge tests pass 7/7. A fresh GCC
  15.2/Ninja build completes 365 steps and passes 114/114 portable CTests; the
  complete MSVC 19.51/Ninja Windows product passes 124/124 CTests including both
  D3D11 product smokes. Synthetic public-boundary tests and the 586-file scan
  pass. Independent review reports GO with no P0-P3 finding. Hosted Actions
  remain the publication gate.

## 2026-07-30 - private iOS controller calibration for next launch

- Extended the private iOS AFIP adapter with asynchronous durable save on the
  shared serial settings queue. Typed failure categories stay path-free, and a
  commit-unknown result is accepted only after exact readback of a valid
  current AFIP; backup/default recovery cannot confirm publication.
- Added a main-thread controller-profile coordinator that owns immutable active
  and mutable persistent records but has no reference to the active input
  coordinator. The UIKit editor reuses the portable active/persisted/draft menu
  model and saves only for the next process launch.
- Added a safe-area two-level calibration panel for all four standardized stick
  axes. Touch and controller navigation reach deadzone, saturation, sensitivity,
  response curve, inversion, per-axis/all-axis reset and durable save. The pause
  menu now selects Display settings or Controller calibration, and both panels
  use the validated menu context rather than requiring modal/control-editor
  bindings.
- Added a value-only 15 Hz preview boundary containing only connection state and
  four raw Q15 axes. Disconnect, input reset and lifecycle boundaries zero every
  sample. Adjusted preview uses the exact shared transport/profile transform and
  never publishes draft values to gameplay.
- Extracted the backup/default repair predicate into portable settings code and
  covered clean defaults, valid current, backup recovery, replaceable invalid
  current and blocked future-schema cases. A complete GCC/Ninja build passes
  114/114 CTests; a complete MSVC 19.51/Ninja Windows rebuild links the product
  and passes 124/124 CTests. Hosted Apple compilation, independent final review
  and physical iPhone persistence/lifecycle acceptance remain publication and
  device gates.

## 2026-07-30 - bounded controller binding remap core

- Accepted ADR-0015 and added bounded typed catalogs for seven digital gameplay
  actions plus standardized controller buttons/triggers. Custom, missing,
  ambiguous, combined-context, analog, menu, pause/back, and throttle layouts
  are classified without exposing raw AFIP fields to native interfaces.
- Extended the existing active/persisted/draft profile model with atomic moves,
  cancel-first conflicts, explicit swaps only between unique supported actions,
  and a full default-binding reset that preserves all four calibration records.
  Button/trigger transports are normalized to the fixed AFIP V1 contract, and
  every candidate is resolved as a complete profile before draft publication.
- Unknown conflict-resolution values, protected recovery controls, and custom
  layouts fail closed without mutation. Save remains frozen, full-record,
  retryable after failure, exactly round-trippable through the AFIP codec, and
  next-launch-only; the active router is never rebuilt in place.
- Synthetic tests cover catalog bounds, classification, disjoint contexts,
  move/swap normalization and atomicity, pause/global-back/throttle protection,
  forged values, reset, save freeze, failure retry, and codec round-trip.
- The portable GCC/Ninja build passes 115/115 CTests. The complete MSVC
  19.51/Ninja Windows product links `AirfixDogfighter.exe` and passes 125/125
  CTests including both D3D11 product smokes. Clang-format
  warnings-as-errors, synthetic public-boundary tests, the 596-file scan, and
  `git diff --check` pass.
- Independent review found four issues in the first draft, verified all four
  fixes, and returned GO with no remaining P0-P3 finding. Native Windows/iOS
  text pickers and hosted Actions remain publication/follow-up gates.

## 2026-07-30 - mission outcome consumer boundary

- `EV-20260730-006` recovers the native result predicate as
  `missionExists && accomplished && !failed`; both outcome flags therefore
  select failure and Retry.
- Success always replaces the typed `THRD` side, then adds an absent signed
  `AXMI`/`ALMI` maximum or replaces it only when the existing value is smaller
  than `mission_number + 1`. An unchanged maximum does not suppress the
  preceding thread update.
- Added an allocation-free value-only `LegacyMissionOutcomeConsumer` with
  failure/Retry, success/Continue, typed Axis/Allied and maximum
  none/add/replace directives. Unknown sides and `INT32_MAX` fail closed
  without UI or persistence directives rather than copying x86 wraparound.
- The ordinary mission refresh boundary runs active AFS processes before
  polling trigger refresh; selected `Spawned` events use a separate immediate
  update-then-call path. Remaining compiler, bytecode and global-order gaps
  keep live AFS wiring at NO-GO.
- Synthetic tests cover the full mission/flag table, conflict precedence,
  ignored failure metadata, both sides, signed maximum comparisons, negative
  and upper boundaries, always-set-thread behavior, forged values, and
  deterministic trivially-copyable results.
- The complete portable GCC/Ninja build passes 116/116 CTests. The complete
  MSVC 19.51/Ninja Windows product links `AirfixDogfighter.exe` and passes
  126/126 CTests, including both D3D11 product smokes. Focused Clang 22
  warnings-as-errors, clang-format, synthetic public-boundary tests, the
  600-file repository scan, the 322-row/14-column unique catalogue, and
  `git diff --check` pass. Independent review reproduces the GCC Release build
  and all 116 CTests, checks the implementation against the raw Ghidra
  evidence, and returns GO with no P0-P3 finding. Hosted platform builds remain
  the publication gate.

## 2026-07-30 - native controller binding pickers

- Added one allocation-free typed picker model above the existing complete
  AFIP draft. It preselects the exact current control, bounds every action and
  assignment index, applies moves cancel-first, and revalidates a conflict
  before an explicit atomic swap.
- Extended the Windows controller-settings panel with a compact five-row
  keyboard/mouse/controller picker and a separate conflict screen whose
  initial selection is `Cancel`. The bounded raster snapshot carries only
  typed action, status, control-index, phase, and conflicting-action metadata;
  it exposes no controller identity or raw AFIP record.
- Extended the iOS safe-area panel into one shared calibration/remap editor.
  Touch and controller navigation reach all seven gameplay actions and fourteen
  assignable controls, keep long selections visible, use Dynamic Type and
  accessibility labels, and require a separate swap or reset confirmation.
- Both platforms retain one complete draft, one outer Cancel, and one
  next-launch-only save. Missing, ambiguous, custom, protected, stale, invalid,
  and save-in-progress states fail closed without mutating the active router.
- The complete portable GCC/Ninja build passes 117/117 CTests. The complete
  MSVC 19.51/Ninja Windows product links `AirfixDogfighter.exe` and passes
  127/127 CTests, including both D3D11 product smokes. Synthetic
  public-boundary tests and the 604-file scan pass. Independent review found a
  controller-input save-freeze gap and one non-localized selected label,
  verified both fixes, and returned GO with no remaining P0-P3 finding. Hosted
  Apple compilation and physical-device acceptance remain publication or
  device gates.

## 2026-07-30 - aircraft type catalogue and refresh prerequisite

- `EV-20260730-007` / `EXP-20260730-077` cross-checks `typeCreate`
  `[0x10001000,0x100017E8)` in Ghidra 12.1.2 and Rizin 0.9.1. Both identify 17
  calls to the common constructor, the same registration order, string
  references, and immediate tuple words.
- Added `LegacyAircraftTypeCatalog`, preserving every ten-field tuple as exact
  source words. Compile-time checks and independent full-table tests fix count,
  order, names, bit views, lookup identity, and fail-closed path behavior.
- Mission player visuals now carry an optional typed aircraft ID resolved only
  from the authenticated canonical object path. All 14 `Ac*` AirCrafts paths
  and three `Au*` Units paths are covered; cross-directory names fail closed.
  Generic MODL-root visuals remain valid without one; manifest and publication
  tests reject missing or forged associations.
- Added `LegacyAircraftVehicleRefreshGate`, which passes the committed five
  controls and shared rest duration through the existing sleep helper. Success
  commits only rest duration; non-finite and overflow rejection is atomic.
- The complete GCC/Ninja build passes 119/119 CTests. Focused Clang 22
  warnings-as-errors, synthetic public-boundary tests, the 611-file repository
  scan, changed-document links, the 322-row/14-column unique catalogue, and
  `git diff --check` pass. Independent review found the omitted `Units`
  directory for the three `Au*` records; the all-17 resolver/test fix closes
  it, and final re-review reports GO with no remaining P0-P3 finding. Hosted
  platform Actions remain the publication gate.

## 2026-07-30 - supplied-module x87 policy audit

- `EV-20260730-008` / `EXP-20260730-078` identifies the executable CRT entry
  `[0x00435BBA,0x00435D0C)` and its startup helper
  `[0x00435D5C,0x00435D6E)`. The call is exactly
  `_controlfp(_PC_53, _MCW_PC)`, before both empty executable initializer
  ranges and the WinMain-style game shell; it changes precision control but
  does not select a rounding mode.
- Rizin 0.9.1 plus `rzpipe` 0.6.2 performed a function-aware pass over all 15
  ordinary supplied runtime images. Their 4,277 independently discovered
  functions contain no decoded x87/SSE environment-write instruction, and
  import enumeration finds a known environment mutator only as the executable
  `_controlfp`. The two discoverable protected-ICD functions add no completeness
  claim and remain outside the recovered startup path.
- The raw `FLDCW` byte hit at executable VA `0x0041BD00` is rejected as
  misaligned: the real instruction begins at `0x0041BCFF` and is
  `mov ebx, ecx`. A deterministic Ghidra exporter now expresses the same
  canonical query, but its fresh normal-profile headless pass remains a
  publication gate because the current managed sandbox cannot load Ghidra's
  per-user OSGi bundle.
- Static evidence closes the obvious game-owned later-writer question but does
  not observe RC, external-library effects, thread inheritance, exception
  masks/status, or the consumer-site CW/SW. Turn/pitch/bank therefore retain
  the explicit startup-compatible PC53/nearest-even label, live producer wiring
  remains conditional, and a bit-parity `CcRigidBody` implementation stays
  NO-GO pending a small ordinary-user hardware-breakpoint capture.
- Both RE wrapper suites, all 12 Rizin exporter tests, synthetic
  public-boundary tests, the 613-file public scan, changed-document links,
  changed-scope local-path scanning, the 324-row/14-column unique catalogue
  with only its two known historical missing references, and
  `git diff --check` pass. A fresh normal-profile Ghidra exporter run remains
  a follow-up independent-validation gate; the published whole-module result
  is explicitly and narrowly attributed to Rizin.

## 2026-07-31 - AFS mission-outcome bytecode boundary

- `EV-20260731-002` / `EXP-20260731-080` recovers the descriptor and bytecode
  boundary between owner-private AFS scripts and the existing portable
  mission-outcome transition.
- `SetupAfServerFunctions` gives both `MissionFail` and `MissionSuccess` one
  formal type-`2` argument word. The native handler ignores its value, but the
  interpreter still removes that word together with the execution-object word.
- Rizin confirms that source literal `true` is compiler tag `0x137`, which
  emits `0x1B, 1`; Ghidra and Rizin agree that interpreter opcode `0x1B`
  pushes that immediate word. Rizin 0.9.1 confirms that a registered native
  call then emits exactly
  `0x24, 0x26, descriptor-token`; the distinct script-function sequence is
  `0x23, 0x25, descriptor-token`.
- An aggregate-only scan of the verified resource working copy found 67
  structurally consistent outcome calls across 52 AFS members: 47 fail and 20
  success. All use literal `true`; no script text, logical path, or content is
  published.
- Added an allocation-free, fail-closed C++20 recognizer for the exact
  five-word call site. It accepts only `[0x1B, 1, 0x24, 0x26, token]`, a
  matching nonzero descriptor token, an explicit handler, one argument word,
  and identifier `0x47` or `0x48`, then returns the existing typed outcome
  call. It owns no VM stack, scheduler, trigger, process, or platform behavior.
- Complete clean Windows GCC 15.2, WSL2 GCC 13.3, and Windows MSVC
  19.51/Ninja builds each pass `120/120` CTests. Focused WSL2 Clang 18.1
  warnings-as-errors and WSL2 ASan/UBSan, both Rizin wrapper suites, the 12
  exporter tests, synthetic public-boundary tests, the 617-file public scan,
  the 330-row/14-column unique catalogue with only two known historical
  missing references, changed-document links, local-path and evidence-ID
  checks, and `git diff --check` pass. The read-only formatting check and
  hosted platform builds remain publication gates.

## 2026-07-31 - safe FOV presentation setting

- `EXP-20260731-081` implements ADR-0016 as a shared presentation-only
  `0..25` degree vertical-FOV increase with exact zero default. A bounded
  tangent-space multiplier expands the centered logical camera canvas without
  mutating recovered camera state, simulation, physical targets, viewport,
  safe area, or UI coordinates.
- D3D11 and Metal consume the same portable layout value. Windows exposes the
  setting through its DPI-aware panel and a validated session-only command-line
  override; iOS exposes it through the safe-area UIKit panel. Both native menu
  paths support one-degree controller edits.
- AFRS advances to canonical schema 2 with an exact binary32 field. Schema 1
  migrates to exact zero, future schemas remain opaque, and malformed or
  non-finite values fail closed. FOV-only application reuses compatible GPU
  targets through the existing prepare-save-publish transaction.
- The complete portable build passes 120/120 CTests. Focused MSVC syntax checks
  for the changed Windows surfaces and `git diff --check` pass. Hosted Windows,
  iPhoneOS, and iPhoneSimulator compilation plus physical-device visual
  acceptance remain the publication and device gates.

## 2026-07-31 - independent native UI scale

- `EXP-20260731-082` and ADR-0017 add one durable 75-150% UI-content scale with
  exact 100% default and 5% product-menu steps. It is classified separately
  from scene layout and target replacement and cannot change camera, 3D render
  extent, mission state, or simulation.
- Windows composes the user value with per-monitor DPI and still rasterizes
  directly at the output extent through D2D/DWrite. Enlarged vertical layouts
  use a bounded selection-centred row window instead of shrinking the chosen
  size back to fit. iOS recomputes native Dynamic Type fonts and spacing inside
  Auto Layout, its safe area, and the existing scroll view; controller focus
  automatically reveals its selected row.
- Renderer diagnostics are the first shared renderer-owned UI consumer. Their
  output-pixel raster scale now composes with UI scale independently of scene
  render scale. Complete HUD/reticle adoption remains a later, explicit slice.
- AFRS advances to canonical 87-byte schema 3 with exact binary32 field 6.
  Schema 1 defaults safe FOV and UI scale, schema 2 defaults only UI scale, and
  schema 4+ stays opaque and downgrade-blocking.
- The complete portable GCC suite passes 120/120 tests. A fresh Windows x64
  MSVC 19.51/Ninja product build completes 669 steps and its full suite passes
  130/130 tests, including both native D3D11/XAudio2 product smokes. Isolated
  WSL2 Clang 18 compiles the changed portable modules and passes all five
  affected tests; strict warnings-as-errors retains only the documented
  pre-existing diagnostic-format warning. Synthetic public-boundary tests, the
  621-file scan, changed-line formatting, local-path review, and
  `git diff --check` pass. PR #87's first hosted validation passes all seven
  jobs: Ubuntu 24.04, macOS 26, Windows 2025, the Windows x64 D3D11/XAudio2
  product smoke, clangd, iPhoneOS, and iPhoneSimulator. Physical-device iOS
  acceptance remains separate.

## 2026-07-31 - Windows gameplay-camera runtime parity

- `EXP-20260731-083` replaces the D3D11 mission's frozen bootstrap camera
  packet with the same bounded `LegacyGameplayCameraMissionRuntime` already
  owned by Metal. Publication still validates the complete mission and prepares
  GPU resources before moving collision ownership or replacing the active
  snapshot.
- The bootstrap must expose simulation step `0` and camera generation `1`.
  Windows publishes only a replacement-safe weak producer endpoint and retains
  one packet lease from layout construction through the complete draw pass.
  Missing acquisition drops the gameplay frame instead of falling back to a
  stale static camera.
- The product shell includes endpoint lifetime in ready/resume decisions but
  deliberately does not advance the camera from the 60 Hz input pump. Live
  motion remains gated on the distinct trace-driven 12 ms AirCraft producer
  and accepted x87 numeric policy.
- A synthetic D3D11 mission test covers bootstrap, a direct generation-2
  publication and render, failed replacement retention, successful replacement
  expiry, and fresh replacement bootstrap without private data.
- A fresh portable GCC 15.2/Ninja build completed 383 steps and passed 120/120
  tests. The current MSVC 19.51/Ninja Windows product build completed all
  pending targets and passed 130/130 tests, including the D3D11 renderer and
  both native product smokes. Independent review reported no findings.
  Synthetic public-boundary tests, the 622-file scan, changed-range formatting,
  local-path review, and `git diff --check` pass. GitHub Actions for commit
  `66ba042` passed all seven jobs: Ubuntu 24.04, macOS 26, Windows 2025, the
  dedicated Windows x64 D3D11/XAudio2 product smoke, clangd, iPhoneOS, and
  iPhoneSimulator.

## 2026-07-31 - native material render contract

- `EV-20260731-003` / `EXP-20260731-084` cross-checks the complete CCF
  `0x2140/0x2150/0x2151/0x2152` mapping through native load and save paths and
  recovers the Direct3D flat/Gouraud selector, three texture stages, four blend
  modes, cull state, filtering, and render-phase depth modes.
- Independent Ghidra/Rizin analysis also closes the per-room sorted-list
  formula and instruction order, unsigned ascending radix order, prepend tie
  behavior, cross-kind triangle/sprite/custom queue, and no-depth-write blend
  phase. Only `_ftol` edge parity remains conditioned on the live x87 policy.
- The parser now retains every numeric material property. Exact native reset
  defaults and render-relevant raw fields flow through CCF-aware world,
  aggregate mission, and player bindings into backend-neutral materials and
  draw commands. A mismatched reference/state span fails atomically.
- D3D11 and Metal output is intentionally unchanged. A later backend slice
  needs triangle-level runtime keys and the shared cross-kind queue rather than
  enabling partial transparency on range-level commands.
- Fresh GCC 15.2/Ninja and independent WSL2 GCC builds each pass all 120
  portable CTests. An isolated MSVC 19.51/Ninja build completes all 669 steps,
  links the Windows product, and passes all 130 CTests. Synthetic
  public-boundary tests, the 623-file repository scan, catalogue validation,
  local-path review, and `git diff --check` pass. Independent final review
  reports no remaining finding. Hosted platform builds remain the publication
  gate.

## 2026-07-31 - portable legacy sorted-render queue

- Added a per-room backend-neutral queue for already-keyed caller-owned
  triangle, sprite, and custom references. It performs the recovered four
  stable unsigned LSD byte passes without kind/material regrouping and retains
  the supplied native input-chain order for equal keys.
- The queue applies explicit item and portable two-array working-memory limits
  before allocation and publishes no partial prefix on failure. Its integer
  helper reproduces only the post-conversion key subtraction; `_ftol`, live
  x87 policy, camera-space key production, triangle payload generation, and
  backend consumption remain deliberately outside this slice.
- Corrected the static trace: `PostSort` is the function that drains the sorted
  queue between the two layer drains, not an additional hook after a prior
  sorted-item drain.
- Fresh Windows GCC 15.2/Ninja builds the complete portable tree and passes
  121/121 CTests. MSVC 19.51/Ninja links the complete Windows product and
  passes 131/131 CTests including both native D3D11 product smokes. Synthetic
  commit `88235e8` also builds all 376 steps from a clean source export in the
  dedicated WSL2 GCC 13.3/Ninja environment and passes 121/121 CTests. The
  public-boundary tests, 626-file scan, 340-row/14-column unique function
  catalogue, and `git diff --check` pass. Two independent reviews report no
  code finding; their sole P2 stale-validation-provenance finding is corrected.
  PR #90 hosted runs `30622519149` and `30622518942` pass all seven Windows
  product, Windows/Ubuntu/macOS portable, clangd, iPhoneOS, and
  iPhoneSimulator jobs.

## 2026-07-31 - full-room diagnostic capture

- Added a backend-neutral diagnostic camera that computes a finite world AABB
  from every transformed non-empty mesh instance and fits all eight corners
  inside a configurable viewport margin. Projection and depth ranges are
  derived from the requested output aspect; malformed models and policies fail
  closed.
- Windows exposes the owner-private `--capture-overview-frame` mode. It uses an
  owning one-frame camera packet without acquiring or publishing gameplay
  camera state, forces widescreen presentation plus the diagnostics overlay,
  and applies front-face culling only to physical room-shell contributors for
  a cutaway view. Placed objects and the player aircraft keep the normal
  two-sided rasterizer.
- A local Classic capture was generated and visually inspected at exact
  1920x1080 with a 1920x1080 render target at 100% scale. It submitted all 190
  scene draw calls and 1,782 scene triangles and showed the textured room,
  placed contents, player aircraft, and diagnostic panel. The source content
  and derived BMP remain ignored and outside Git.
- A fresh GCC 15.2/Ninja build completes 389 steps and passes 122/122 portable
  CTests. The native MSVC 19.51/Ninja Windows product passes 132/132 CTests,
  including both D3D11 product smokes. Public-boundary tests, the 630-file
  repository scan, 12 Rizin normalization tests, and `git diff --check` pass.

## 2026-07-31 - Enhanced scene-texture sampling

- `EXP-20260731-086` turns the previously policy-only visual profile into its
  first bounded pixel-affecting stage. Classic preserves the established
  nearest/mip-point scene sampler; Enhanced selects trilinear 8x anisotropic
  filtering through one backend-neutral C++20 contract.
- D3D11 and Metal create independent immutable Classic, Enhanced, overlay, and
  final-presentation samplers. Per-frame scene selection follows the same
  immutable presentation snapshot recorded by diagnostics; UI pixels and
  render-scale presentation cannot inherit the scene sampler.
- Portable tests reject forged profiles, forged sampling modes, invalid
  anisotropy, and mismatched diagnostics atomically. D3D11 renderer coverage
  proves a successful profile transaction changes the selected scene policy
  while existing resource-retention and layout guarantees remain intact.
- Private 1920x1080 Classic and Enhanced captures of one authenticated mission
  retain identical 278-draw/1,311-triangle geometry and 100% scene scale. The
  Enhanced frame reports `TEX ANISO 8X`; 14.05% of uncompressed color bytes
  differ and oblique room surfaces are visibly smoother. Captures and owner
  content remain ignored and outside Git.
- The complete portable suite passes 123/123 tests. The native MSVC
  19.51/Ninja product build succeeds and passes 133/133 tests. Hosted Apple
  compilation and the complete public-boundary gate remain publication
  requirements; physical-device Metal inspection remains separate.

## 2026-07-31 - native gameplay screen projection

- Added an allocation-free bridge from the recovered gameplay camera's world
  projection through the modern Hor+/Original 4:3/safe-FOV layout to physical
  output pixels. The layout now publishes its scene viewport in both render-
  target and output domains.
- The bridge rejects camera/layout canvas or reference-FOV mismatch, preserves
  nested legacy failures, and returns unclamped off-screen coordinates with
  viewport inclusion and recovered near/far visibility as independent labels.
  No HUD art, visibility rule, simulation state, or live camera producer is
  inferred.
- Tests cover all required aspect ratios at 50%, 100%, 125%, and 200% render
  scale, Original 4:3 bars, safe FOV, round trips, depth states, malformed
  inputs, overflow, and allocation freedom. The complete portable suite passes
  124/124 CTests; the native MSVC Windows product builds and passes 134/134,
  including both D3D11 product smokes.

## 2026-07-31 - legacy weapon crosshair projection

- `EV-20260731-004` / `EXP-20260731-088` recovers the shared
  `AfWeapon::RenderCrosshair` gates, state fields, exact `2.0`/`0.5` constants,
  first-visible-size latch, collision-dependent logical size, and centred
  textured draw. Ghidra supplies the canonical MSVC class interpretation;
  Rizin independently matches the three selected function ranges.
- The original type module loads three separate 32x32 format-8 GTI roles:
  `MGsight`, `ROsight`, and `BOsight`. The source and decoded images remain
  private and outside Git.
- A new allocation-free shared C++20 planner composes recovered logical sizing
  with the native output projection. Position follows the gameplay camera;
  size follows output-fit UI scale and the independent user UI scale; 3D render
  scale has no effect. Off-screen and depth states remain labelled results for
  the later selected-weapon consumer.
- Synthetic tests cover the recovered canvas, first-visible latch, every
  required aspect family, 50-200% render scale, 75-150% UI scale, Original
  4:3, invalid input, nested projection failure, and 4,096 projections without
  allocation. Authenticated sight loading, live aim/collision, and native
  sprite submission remain separate work.
- A fresh GCC 15.2/Ninja Release build passes 125/125 portable CTests. A fresh
  MSVC 19.51/Ninja Release build of the Windows SDL3/D3D11/XAudio2 product
  passes 135/135 CTests including both native product smokes. Public-boundary,
  12/12 Rizin export-normalization, clang-format, and diff checks pass.

## 2026-07-31 - authenticated weapon-crosshair texture set

- `EXP-20260731-089` turns the three recovered `MGsight`/`ROsight`/`BOsight`
  roles into one owner-content boundary. Every entry is resolved only inside
  one unchanged `VerifiedContentSession`; no logical key is opened as a host
  path and no loose or partial texture is accepted.
- Plan-only GTI validation precedes pixel decoding. It requires unique physical
  entries, selected format 8, exact `32x32` base dimensions, valid mip shape,
  distinct source identities, checked source/decoded/upload/resident budgets,
  and exact equality between preflight and materialized upload plans.
- The published dense IDs are local to the dedicated HUD set rather than the
  scene texture namespace. Role lookup revalidates the full set and exact
  authenticated-stream provenance. Live weapon/type mapping, aim/collision,
  GPU resource creation and D3D11/Metal sprite submission remain explicit
  later gates.
- A read-only private inventory smoke confirmed all three original entries are
  unique and contain the expected one-mip 32x32 format-4/format-8 variants; no
  owner bytes or output entered Git. Public coverage uses only synthetic
  AFPACK, UDSP and GTI data.
- A second private smoke used an existing owner-local AFPACK and the production
  `VerifiedContentSession` loader. It published three valid selected-format-8
  32x32 one-mip textures with 12,288 decoded/resident RGBA bytes; the throwaway
  driver and all owner data remain outside Git.
- Fresh Release validation passes 126/126 portable GCC/Ninja CTests and 136/136
  native MSVC/Ninja CTests, including the SDL3/D3D11/XAudio2 product and both
  Windows product smokes.

## 2026-07-31 - weapon-type crosshair binding

- `EV-20260731-005` / `EXP-20260731-090` recovers the exact mapping of all nine
  `Projectiles.type` weapon registrations: MG sight for `WpMGun`, bomb sight
  for `WpBomb`/`WpABomb`, rocket sight for five projectile families, and an
  affirmative no-sight result for `WpParaMine`.
- Ghidra confirms the sight-bearing loaders store their texture reference in
  type field `+0x4C`; `WpParaMine::Load` only delegates and sets its loaded
  byte. Rizin independently matches seven relevant function ranges and exact
  instruction bytes on the same hash-verified working copies.
- The AirCraft transition at RVA `0x00007600` stops and deactivates an old
  selected secondary at `+0x494`, stores a distinct non-null candidate, and
  activates it. Primary `+0x490` remains separate; final simultaneous
  composition or suppression is not inferred from static evidence.
- `LegacyWeaponTypeCatalog` and the allocation-free authenticated binder now
  publish type, role, HUD-local texture ID, content revision, and exact stream
  identity. Invalid types, forged sets, and another equal-revision stream fail
  closed; synthetic tests cover all nine mappings and no-sight behavior.
- A fresh GCC 15.2/Ninja Release build completes 403/403 steps and passes
  126/126 portable CTests. A separate fresh MSVC 19.51/Ninja Release build
  completes 689/689 steps and passes 136/136 CTests, including the complete
  SDL3/D3D11/XAudio2 application and both native Windows product smokes.

## 2026-07-31 - crosshair GPU resource staging

- `EXP-20260731-091` connects the complete authenticated three-texture sight
  set to platform resource ownership without adding a visible HUD policy.
- Windows loads the room and crosshairs through one unchanged verified
  session, prepares all D3D11 shader-resource views before the mission swap,
  retains HUD-local identity separately from scene textures, and includes the
  resources in diagnostic GPU-memory accounting.
- iOS carries room, aircraft audio, and crosshairs in one private snapshot.
  Metal validates the set, includes it in logical and aligned heap admission,
  uploads every texture, shares checked mip generation, and retains the set and
  native texture array in the immutable prepared-room owner.
- A complete MSVC/Ninja product build and all 136 CTests pass. A production
  owner-content D3D11 run also completes atomic publication and a private
  1920x1080 full-room capture; no owner data or derived image enters Git.
- Hosted `iphoneos` and `iphonesimulator` builds both compile the Metal
  transaction successfully in pull-request run `30645339243`.
- No crosshair draw is issued. Live weapon ownership, aim/collision state,
  primary/secondary composition, blend/depth policy, and sprite submission
  remain explicit later gates.

## 2026-07-31 - authenticated crosshair sprite submission

- `EV-20260731-006` / `EXP-20260731-092` resolves the `GtScreen` vtable call
  used by `AfWeapon::RenderCrosshair`. Ghidra and Rizin agree on the simple
  `[0x10044E20,0x10044E5A)` and full `[0x10044EA0,0x100451D0)` `BlitStretch`
  functions: full-image UVs, ARGB `0x7FFFFFFF`, texture/vertex alpha
  modulation, source-alpha/inverse-source-alpha blending, and always-write
  depth.
- A new value-only packet composes one projected rectangle with one exact
  authenticated per-type binding. It preserves both visibility labels and
  requires an explicit caller draw/suppress decision. Forged HUD-local IDs,
  invalid rectangles, stale revisions, and another authenticated stream handle
  fail closed before a native texture lookup.
- D3D11 now owns the recovered sprite pipeline and a private validation-only
  capture option; Metal prepares the equivalent blended Depth32 pipeline,
  sampler, depth state, and provenance-gated encoder. Normal frames still do
  not invent a live weapon or aim producer.
- A real first D3D11 capture exposed and then fixed a missing pixel-shader
  constant-buffer binding that had made tinted overlays transparent. The
  corrected owner-content 1920x1080 frame visibly contains the half-alpha
  machine-gun sight and diagnostics. The private BMP remains ignored and
  outside Git.
- The native MSVC/Ninja product builds and its full local suite passes
  `136/136`; the portable preset passes `126/126`. Public-boundary, Ghidra,
  Rizin, and compilation-database checks are green. Hosted Apple compilation
  remains the publication gate.

## 2026-07-31 - AFS function activation and initial order

- `EV-20260731-001` / `EXP-20260731-079` follows source through the forward
  lexer, tail-appended tokens, contiguous AST children, ascending object-child
  compilation, prepended compiled records, filtered process construction and
  FIFO execution nodes. Ghidra 12.1.2 is canonical; Rizin 0.9.1 and rzpipe
  0.6.2 independently confirm the selected PE32/i386 boundaries and
  instructions.
- The compiler maps `event` to Autoexec off and `action`/`timer` to Autoexec
  on. One new process therefore queues initial actions/timers in reverse source
  declaration order, while events remain available through explicit named
  calls. The evidence does not establish bytecode semantics, live
  `Load`/`Start` replacement, dynamic process mutation, or global dispatcher
  and presentation order.
- A bounded read-only census of all 52 authenticated local AFS objects found
  114 events, 328 actions, no timers, 47 mission-fail calls, 20
  mission-success calls, and no dynamic `Call`, `KillProcess`, or `LoadScript`
  use. Only aggregates are public; no script text, logical path, per-file
  result, original binary, or private resource is tracked.
- Added the portable `LegacyAfsFunctionSchedule` oracle for already-classified
  declarations. It preserves source indices and duplicates, emits only
  Autoexec declarations in the proven reverse order, and rejects forged kinds
  atomically. It does not parse, compile, execute, resolve names, own process
  state, or wire a live mission.
- After rebasing onto current `main`, a fresh Windows GCC/Ninja build completes
  408/408 steps and passes 127/127 CTests. A clean MSVC 19.51/Ninja build of
  the complete SDL3/D3D11/XAudio2 product passes 137/137 CTests. The Visual
  Studio generator again stalled in its compiler probe, so it was replaced by
  the equivalent MSVC/Ninja validation rather than treated as a source
  failure. Focused clang-format 22.1.3 warnings-as-errors also passes.
- Both analysis-wrapper suites, all 12 Rizin exporter tests, synthetic
  public-boundary tests, the 660-file repository boundary scan, the
  362-row/14-column unique function catalogue, and `git diff --check` pass.
  The generated compilation database contains 263 entries and no Apple-only
  sources. The catalogue retains only its two known historical missing
  references. Hosted Actions remain the publication gate.

## 2026-07-31 - crosshair render-event composition

- `EV-20260731-007` / `EXP-20260731-093` maps packed event `0x06` to the exact
  `AfVehicle::ProcessEvent` branch and proves its order: type gate, independent
  virtual AirCraft HUD stage, inactive gate, attached-camera gate, selected-
  secondary `+0x494` `RenderCrosshair`, then primary `+0x490`
  `RenderCrosshair`.
- Ghidra 12.1.2 supplies the canonical function/vtable interpretation. Rizin
  0.9.1 independently matches the branch instructions and the
  `[0x10006B00,0x10007321)` AirCraft HUD-stage range on hash-verified read-only
  copies. The complete HUD body remains only partially understood and is not
  implemented.
- `composeLegacyWeaponCrosshairRenderEvent` accepts optional, already-built
  authenticated packets and publishes them in exact selected-secondary-before-
  primary order. Missing slots remain independent; type, inactive, and camera
  gates publish nothing; one forged packet rejects the complete plan. It does
  not infer visibility, aim/collision, changing weapon ownership, or live frame
  lifecycle.
- Synthetic tests cover two-slot order, primary-only and empty plans, every
  recovered crosshair gate, bounds-safe access, and atomic provenance failure.
  Portable GCC and code-intelligence builds each pass 127/127 tests; the latter
  emits 263 compilation-database entries with no Apple-only sources. Native
  MSVC 19.51 builds the D3D11 product and passes 137/137 tests. Repeatable
  Ghidra/Rizin wrappers, 12/12 Rizin-export tests, the 661-file public boundary,
  formatting, catalogue checks, and changed-range clangd diagnostics pass.
  Hosted platform builds remain the publication gate for this branch.

## 2026-07-31 - active machine-gun projectile slot advance

- `EXP-20260731-094` composes the existing `NfProjectile` flight/lifetime,
  published room/material/portal collision, primary-player actor resolution,
  terminal machine-gun reducer, and creator-BSP guard for every already-active
  generation-tagged runtime slot.
- The bounded step validates batch shape, delta, and catalogue/runtime identity
  before mutation. It visits stable supplied indices, skips inactive profiles,
  commits lifetime without collision, keeps rejected slots unchanged, and
  continues later independent slots. A failed creator-BSP restore aborts the
  remainder and exposes no terminal command.
- Successful records carry the pre-step generation identity, terminal outcome,
  query/portal counts, and optional damage or surface/ricochet request. The
  function dispatches no actor event/effect and owns no scheduler, producer,
  tracer, audio, or render state. Fixed-pool index order remains explicit port
  policy rather than a claim about the native time heap.
- Synthetic coverage includes no-hit flight, strict lifetime, primary-player
  damage/deactivation, rejected-slot isolation, fatal restore failure,
  validation atomicity, zero-generation rejection, and 4,096 complete steps
  without heap allocation. Portable and code-intelligence builds each pass
  127/127 CTests; native MSVC builds the SDL3/D3D11 product and passes 137/137.
  The 263-entry compilation database contains no Apple-only source. Synthetic
  public-boundary tests, the 662-file repository scan, all 12 Rizin exporter
  tests, the 363-row/14-column unique function catalogue, and `git diff
  --check` pass. Hosted platform builds remain the publication gate.

## 2026-07-31 - Windows owner-private AFPACK product import

- The Windows product now exposes an exclusive `--import-afpack` operation
  over the existing portable streaming installer. SDL resolves the undisclosed
  per-user `content/` sibling, the adapter generates a canonical lowercase
  UUID, and a named mutex rejects a competing product importer in the same
  login session.
- `--installed-content` and `--validate-installed-content` consume the private
  root without repeating it on the command line. The existing explicit-root
  interfaces remain available for controlled development roots. Ordinary
  installer failures are reduced to fixed path-free categories; the ambiguous
  post-commit case instead asks for restart and installed-content validation.
- Synthetic tests cover canonical/distinct UUIDs, exact adapter handoff,
  successful result mapping, rejection redaction, ambiguous-commit guidance,
  missing-operation failure, installed/explicit-root exclusivity, and import
  option combinations. Controlled product invocations confirm both contention
  rejection and absence of a private path marker from stderr.
- A fresh MSVC 19.51 HostX64/Ninja product build passes all 138 CTests,
  including the installer/recovery suites, D3D11 renderer, and two product
  smokes. The portable code-intelligence build passes 127/127 CTests; its 263
  compilation-database entries contain no Apple-only source. Public-boundary
  synthetic tests, the 665-file repository scan, all 12 Rizin exporter tests,
  formatting, and `git diff --check` pass. The local Visual Studio 18.8
  generator continues to select a crashing HostX86 compiler probe despite an
  x64 preference, while direct HostX64 compilation succeeds; hosted Actions
  remain the authoritative preset gate.

## 2026-07-31 - Windows native private-content bootstrap

- Added an explicit pre-game `--manage-installed-content` workflow over the
  existing SDL-private root, AFPACK installer, active-record inspection, and
  rollback transaction. Native Windows dialogs provide the `.afpack` picker,
  cancellable normalized progress, fixed status/actions, retry, replacement,
  and verified-generation rollback without creating another parser or store.
- Import and rollback share one named writer mutex and fresh transaction UUIDs.
  Every ordinary attempted mutation is followed by a new authenticated
  inspection. An ambiguous commit instead terminates the manager and requires
  restart before another mutation.
  When recovery has verified only the previous generation, the manager requires
  restoring it before any replacement import; an indeterminate store permits
  only retry or close. This preserves the sole known-good AFAC rollback
  reference instead of rotating an invalid current generation over it.
  Picker cancellation stays in the current state; ordinary failures are
  reduced to path-free categories; an ambiguous commit remains distinct.
  Outside the owner-controlled OS picker, the manager echoes no local/logical
  path, checksum, generation identifier, or backend diagnostic and performs no
  live mission reload.
- Embedded Common Controls v6 manifests are used by the product and native
  prerequisite test. TaskDialog is resolved only inside the explicit manager
  path, which prevents missing UI activation from blocking the normal D3D11
  product startup. Synthetic tests verify the coordinator state table,
  cancellation/failure mapping, bounded retry, action gates, command-line
  exclusivity, and native COM prerequisites without opening a window or
  reading private content.
- Independent read-only review caught and verified corrections for the
  `rollbackAvailable` readiness/action gates, import from an indeterminate
  store, terminal ambiguous-commit policy, TaskDialog failure mapping, and
  recovery text. Final re-review reports no remaining P0-P3 findings.
- A clean MSVC 19.51 HostX64/Ninja product build passes 139/139 CTests,
  including the installer/recovery suites, native bootstrap tests, D3D11
  renderer, and both product smokes. A separate clean Clang 22.1.8/Ninja
  portable build passes 127/127 CTests; its 263-entry compilation database
  contains no Windows-product or Apple-only source. Synthetic public-boundary
  tests, the 670-file repository scan, changed-source clang-format, and `git
  diff --check` pass. Hosted platform builds remain the publication gate.

## 2026-07-31 - runtime-owned projectile room identity

- `EXP-20260731-096` removes the last projectile-query dependency on the
  temporary `MissionWorldRoomCatalog`. The recovered root/creation-order ID
  mapping is exposed through bounded room-count overloads, while the original
  catalogue overloads remain build-time validators over the same code.
- Projectile portal queries, terminal reduction, creator-BSP bracketing, and
  active-slot advancement now derive room identity only from
  `LegacyGameplayCameraMissionRuntime::worldRoomCount()`. The count belongs to
  the same retained arena and collision frame used for tracing, so a caller
  cannot join geometry from one mission to a catalogue from another.
- Synthetic tests prove catalogue/count equivalence, zero/overflow and
  index/ID rejection, one-room actor mapping, two-room portal continuation,
  unchanged creator/slot transactions, and steady-state allocation freedom.
  The fresh GCC 15.2/Ninja build completed 408 steps and passes 127/127 CTests.
  The fresh MSVC 19.51 HostX64/Ninja Windows-product build completed 702 steps;
  its final affected-graph rebuild passes 139/139 CTests, including both
  product smokes. Public-boundary synthetic tests and the 671-file scan, the
  263-entry portable compilation-database check, all 12 Rizin exporter tests,
  changed-addition local-path scanning, and `git diff --check` pass. Hosted
  platform builds remain the publication gate.

## 2026-07-31 - AirCraft controlled runtime-capture validator

- `EXP-20260731-097` and the capture runbook define a bounded, path-free JSONL
  contract for the exact reference modules and 14 recovered consumer,
  scheduler, event, and rigid-body RVAs. The fail-closed validator binds every
  sample to a stable thread and x87/MXCSR control policy while retaining sticky
  exception status as evidence.
- Exact rational arithmetic validates the 11 signed event-store vectors without
  host floating-point assumptions. The V1-V6 oracles cover recovered state,
  derivative, spill, precision-control, and rounding-control discriminators.
  Acceptance also requires chronological startup records and contiguous
  zero/one/many 12 ms refresh observations. Observation-only captures always
  remain NO-GO.
- Inputs are limited to 4 MiB, 10,000 samples, and 64 KiB per line. Symlinks,
  unknown or duplicate fields, wrong hashes/RVAs, mixed threads or policies,
  out-of-range times, malformed values, and oracle mismatches are rejected.
  Output never echoes filenames, paths, hashes, payloads, or state. Capture
  JSONL and debugger artefacts are ignored and blocked at the public boundary.
- Nine synthetic validator tests pass. The clean GCC 15.2/Ninja build completed
  408 steps and passes 128/128 CTests; the clean MSVC 19.51 HostX64/Ninja
  Windows-product build completed 712 steps and passes 140/140 CTests. The
  263-entry portable compilation database contains no Windows or Apple source.
  Public-boundary tests and the 675-file scan, all 12 Rizin exporter tests,
  actionlint, Python bytecode compilation, and `git diff --check` pass. No real
  capture or original/private data entered the repository; hosted platform
  builds remain the publication gate.

## 2026-08-01 - AirCraft armour/health-gauge plan

- `EV-20260801-001` / `EXP-20260801-098` use Ghidra 12.1.2 and Rizin 0.9.1
  to confirm the exact 2,081-byte AirCraft HUD stage at RVA `0x00006B00` and
  close one complete internal subsystem without claiming the remaining HUD.
- The recovered gauge draws optional `armour_meter`, an unconditional
  opaque-black annular damage mask, then optional `armour`. It uses the exact
  `screenWidth < 640 ? 6 : screenHeight - 136` anchor, centre `(72,top+64)`,
  radii 62/44, `-pi/4` start, binary32 `0.1` stepping, and smoothed-health
  sweep. A final quad is always emitted; valid input is bounded to 32 mask
  quads and 34 total commands.
- `LegacyAircraftHealthGaugePlan` is allocation-free, fail-closed, and keeps
  logical UI coordinates separate from native 3D resolution. It preserves the
  native active-window/type and two camera observations while leaving private
  texture authentication, UI mapping, D3D11/Metal submission, and the rest of
  the HUD disconnected.
- The complete portable Ninja build passes 129/129 CTests; its 265-entry
  compilation database includes the new source and excludes Apple-only code.
  A fresh MSVC 19.51 HostX64/Ninja Windows-product build compiles 705 steps and
  passes 141/141 CTests, including D3D11 and both product smokes. Public-boundary
  synthetic tests and the 680-file scan, function-catalog validation,
  formatting, and `git diff --check` pass. Hosted platform builds remain the
  publication gate.

## 2026-08-01 - authenticated health-gauge texture staging

- `EXP-20260801-099` closes the resource-ownership prerequisite for the
  recovered gauge without claiming a visible HUD. `armour_meter.gti` resolves
  only from authenticated `Resource.up`; `armour.gti` resolves only from the
  manifest-selected localization archive.
- Recovery and direct verified-session opening retain both nested UDSP views
  over one unchanged AFPACK stream. The new bounded loader requires the exact
  format-8 `128x128` pair, preserves archive kind and HUD-local identity,
  preflights aggregate source/RGBA budgets, and publishes nothing on a single
  role failure.
- Windows loads and uploads both resources before its no-fail D3D11 mission
  swap. iOS carries them in the private snapshot, accounts them in the shared
  Metal heap/admission plan, uploads/generates all mip levels, and retains them
  under the published room owner. At this checkpoint neither backend issued a
  health-gauge draw; EXP-20260801-100 supersedes the Windows private-validation
  state.
- The full portable build passes 130/130 CTests. A real MSVC 19.51 HostX64/
  Ninja build compiles the complete Windows product and passes 142/142 CTests,
  including both product smokes. Hosted iOS compilation remains the
  Objective-C++ publication gate.

## 2026-08-01 - authenticated health-gauge native submission

- `EXP-20260801-100` closes the screen-state and native-output prerequisites.
  Ghidra 12.1.2 plus Rizin 0.9.1 map the two format-8 texture calls through Cc
  `GtScreen::Blit`/`GtImage::IsAlpha` to source-alpha blending, and the black
  mask calls through `GtScreen::Fill` to opaque blending-disabled triangles.
  Both paths use recovered depth mode 2: ALWAYS compare with writes enabled.
- `LegacyAircraftHealthGaugeSubmission` is a fixed-capacity value packet bound
  to the exact authenticated transaction and gauge-local texture IDs. It maps
  the proven 640x480 UI domain into physical output pixels, scales the widget
  around its bottom-left anchor, preserves command order and state, and rejects
  forged, mixed-session, non-finite, incompatible-domain or out-of-policy input
  atomically. The 640x480 extent remains UI metadata and never becomes a 3D
  raster limit.
- D3D11 now owns dedicated four-point gauge shaders and switches alpha/opaque
  pipeline state per recovered command. The private capture-only CLI supplies
  50% health solely to the presentation planner. A real owner-private
  1920x1080 Enhanced capture contains the installed aircraft, authenticated
  HUD textures, 16 mask commands and diagnostics overlay. Ordinary gameplay
  still issues no gauge packet; live producer wiring and Metal consumption are
  separate gates.
- The clean GCC 15.2/Ninja portable build passes 130/130 CTests. The clean
  MSVC 19.51 HostX64/Ninja build compiles the complete Windows product and
  HLSL and passes 142/142 CTests. The synthetic public-boundary suite, 688-file
  repository scan, 12 Rizin exporter tests, nine capture-validator tests, all
  three reverse-engineering wrapper suites, changed-range formatting, and
  `git diff --check` pass. Hosted platform Actions remain the publication gate.

## 2026-08-01 - authenticated health-gauge Metal encoder

- `EXP-20260801-101` closes the dormant iOS backend slice without connecting
  fabricated health to the ordinary frame callback. The already published
  Metal room snapshot remains the sole owner of both authenticated gauge
  textures and their transaction identity.
- A checked 96-byte CPU/Metal ABI carries four physical-output points, native
  output extent, and ARGB tint. Dedicated texture and solid pipelines preserve
  the recovered source-alpha/opaque split; both share linear clamp and
  ALWAYS/write depth. The vertex shader emits the original arbitrary quads as
  two triangles rather than reinterpreting the annular mask as a rectangle.
- The encoder validates the complete packet, exact texture count and IDs,
  non-null GPU resources, finite in-output geometry, target/depth dimensions,
  and pixel formats before opening a render encoder. Rejection therefore draws
  no partial prefix; success preserves background/masks/foreground order.
- Existing portable submission tests remain the backend-neutral oracle. The
  clean GCC/Ninja suite passes 130/130 tests and the complete MSVC product
  suite passes 142/142. The synthetic boundary test, 689-file repository scan,
  12 Rizin exporter tests, nine capture-validator tests, all three RE wrapper
  suites, `actionlint`, and both staged and working-tree `diff --check` pass.
  Hosted `iphoneos`/`iphonesimulator` shader compilation remains the
  publication gate.

## 2026-08-01 - AirCraft HUD rolling-number helper

- `EV-20260801-002` / `EXP-20260801-102` use Ghidra 12.1.2 as the primary
  source and Rizin 0.9.1 as an independent check for exact AirCraft ranges
  `[0x10009210,0x10009268)`, `[0x10009280,0x1000939F)`, and
  `[0x100093A0,0x1000943E)`. Cc RVA `0x00044D90` independently closes the
  source-rectangle blit wrapper. All inspected copies were hash verified and
  read only.
- The helper preserves native `%04d` first-four-character formatting, four
  retained binary32 positions, cached `float(pow(0.0005,elapsed))`, strict
  distance-greater-than-five cyclic routing, one-step wrap, and invalid-value
  reset to positive zero. The portable boundary accepts an already-quantized
  signed integer and does not manufacture a live `_ftol` rounding policy.
- The exact draw plan uses destination pitch 8 and four independently gated
  7x9 source windows with vertical pitch 11. Read-only owner-local inventory
  confirms the `digits` GTI is 16x128 with one mip; no archive payload or image
  enters Git.
- `LegacyAircraftHudRollingDigitsState` and its fixed four-command plan are
  allocation-free and independent of assets and native APIs. A fresh GCC
  15.2/Ninja build compiles the complete portable graph and all 131/131 CTests
  pass. A fresh MSVC 19.51 HostX64/Ninja build compiles the complete Windows
  product and all 143/143 tests pass, including D3D11 and both product smokes;
  the Python validator is rerun outside the default command sandbox after its
  sole `operation not permitted` launch rejection. Public-boundary synthetic
  tests and the 693-file scan, 12 Rizin exporter tests, both RE wrapper suites,
  formatting, and `git diff --check` pass. Authenticated digit ownership,
  D3D11/Metal submission, live value meanings, elapsed-clock ownership, and
  runtime x87 acceptance remain later gates.

## 2026-08-01 - authenticated rolling-digit atlas and native submission

- `EXP-20260801-103` closes authenticated ownership of the exact `digits` GTI.
  A read-only owner-local parser check selects format 8 at `16x128`; the
  preview remains ignored and no original or derived payload enters Git.
- A bounded session loader publishes one dedicated HUD-local texture only
  after exact lookup, source/decoded/upload/resident budgets, GTI decode,
  format/dimension checks and unchanged opaque transaction identity pass.
  Synthetic failure paths remain atomic.
- The fixed four-command native-output packet maps recovered legacy UI
  rectangles and fractional source windows into physical output pixels,
  applies independent UI scale around the widget origin and retains tint,
  source-alpha blending, linear clamp and ALWAYS/write depth. Provenance,
  order, UV and geometry are revalidated before backend access.
- D3D11 and Metal now include the atlas in their all-or-nothing mission
  resource transactions and contain dormant fail-closed consumers. The shared
  overlay shader ABI accepts an explicit UV rectangle while every prior caller
  supplies full UVs. No ordinary frame fabricates a rolling-number value.
- A clean GCC/Ninja build passes all 132 portable tests. A clean MSVC 19.51
  HostX64/Ninja build compiles the complete Windows product and passes all 144
  tests, including the digit contract and D3D11 renderer/runtime-HLSL paths.
  The public-boundary scanner accepts all 699 tracked files. Hosted iOS
  compilation remains the Objective-C++/Metal publication gate.

## 2026-08-01 - AirCraft HUD analog instruments

- `EV-20260801-003` / `EXP-20260801-104` use Ghidra 12.1.2 as the primary
  source and Rizin 0.9.1 as the independent byte/control-flow check for exact
  AirCraft RVA `0x00007330`, range `[0x10007330,0x1000742F)`. The recovered
  helper draws an optional 64x64 face and a 7x30 rotated indicator with exact
  binary32 constants, pivot, tint and native right-before-left call order.
- An allocation-free four-command plan accepts the already-smoothed right
  value and the left remaining/initial ratio without assigning an unproved
  gameplay label or manufacturing live state. It retains independent missing
  textures and fails closed on invalid gates, extents and non-finite values.
- A bounded authenticated owner loads both format-8 64x64 faces from the
  selected localization archive and the format-8 16x32 indicator from the
  source archive. Exact dimensions, source/decode/upload/resident budgets,
  cancellation and immutable transaction identity are enforced atomically.
- The identity-bound native-output packet preserves logical 640x480 UI
  geometry independently of the 3D target, user UI scale around the lower
  centre, the indicator's 7/16 by 30/32 UV window, ARGB tint, source-alpha
  blending, linear clamp and ALWAYS/write depth. D3D11 and Metal stage the
  three resources in the mission transaction and own dormant fail-closed
  arbitrary-quad consumers. Ordinary frames still issue no fabricated draw.
- A fresh GCC 15.2/Ninja build passes 134/134 portable CTests. A fresh MSVC
  19.51 HostX64/Ninja build compiles the complete Windows product and runtime
  HLSL and passes 146/146 CTests, including the renderer and both product
  smokes. The synthetic public-boundary suite and 708-file repository scan
  pass. Hosted iPhoneOS/iPhoneSimulator compilation remains the publication
  gate for the Objective-C++ and Metal changes.

## 2026-08-01 - AirCraft HUD weapon panels

- `EV-20260801-004` / `EXP-20260801-105` close the primary and selected-
  secondary weapon-panel subsection inside the hash-verified AirCraft HUD
  range `[0x10006B00,0x10007321)`. Ghidra 12.1.2 remains primary and Rizin
  0.9.1 independently confirms the relevant blocks, calls, constants, and
  instruction order.
- An allocation-free fixed 14-command plan preserves both backgrounds before
  primary then secondary content, exact 640x480 anchors, rolling ammunition
  digits, icon/status geometry, the five-colour clamped palette, and atomic
  digit-state publication. It accepts already-quantized ammo/status values and
  does not invent a live `_ftol` policy.
- A bounded authenticated owner loads the fixed format-8 64x64 backgrounds
  from the selected localization archive and dynamically enumerates format-8
  64x32 `weapon_*.gti` icons in the source archive. No individual weapon name
  is hard-coded. Exact provenance, dimensions, mip/source/decode/upload/
  resident budgets, cancellation, callbacks, and immutable transaction
  identity are enforced atomically.
- The cross-owner native-output packet binds that catalog to the authenticated
  rolling-digit atlas, maps logical UI independently of the native 3D target,
  and preserves source-alpha textures, ALWAYS/write depth, and the recovered
  destination-multiply status fill. D3D11 and Metal stage all resources in the
  complete mission transaction and own dormant fail-closed consumers; ordinary
  frames publish no fabricated weapon state.
- A clean portable CMake/Ninja build passes all 136 CTests. The complete
  Windows x64 clang-cl/Ninja product and runtime HLSL build succeeds and all
  148 native CTests pass, including the renderer and both product smokes.
  Hosted iPhoneOS/iPhoneSimulator compilation remains the Objective-C++/Metal
  publication gate.

## 2026-08-01 - AirCraft HUD identity and status cluster

- `EV-20260801-005` / `EXP-20260801-106` close the final contiguous cluster in
  the hash-verified AirCraft HUD range `[0x10006B00,0x10007321)`. Ghidra 12.1.2
  remains primary and Rizin 0.9.1 independently confirms exact block order,
  calls, fields, and constants. The final controlled x32dbg attempt correctly
  armed four relevant hardware execution breakpoints in one mapped session but
  recorded zero accepted hits before pause/resume input loss ended the trial;
  it contributes no dynamic claim.
- An allocation-free ten-command plan preserves optional full-name `Ac*.gti`
  icon lookup, four rolling health-percentage digits, exact team-one star/
  other-team cross selection, four rolling technology-level digits, native
  anchors, gates, and atomic state advancement. Both `_ftol` results enter as
  already-quantized signed values.
- A bounded authenticated owner loads the two fixed format-8 `64x64` badges
  from the localization archive and dynamically enumerates all format-8
  `64x64` `Ac*.gti` icons in the source archive. No aircraft name is
  hard-coded; provenance, dimensions, mip/source/decode/upload/resident
  budgets, cancellation, callbacks, and immutable transaction identity are
  enforced atomically.
- The identity-bound native-output packet composes that owner with the shared
  rolling-digit atlas and retains full/fractional UVs, independent UI scale,
  source-alpha blending, linear clamp, ALWAYS/write depth, and separation from
  the physical 3D target. D3D11 and Metal stage the set in the all-or-nothing
  mission transaction and own dormant fail-closed consumers. Ordinary frames
  publish no fabricated identity, health, team, technology, or clock state.
- A clean MSVC 19.51 HostX64/Ninja build compiles all 737 Windows product
  steps and all 150 native CTests pass, including D3D11 and both product
  smokes. A clean portable build passes all 138 CTests. Hosted
  iPhoneOS/iPhoneSimulator compilation remains the publication gate.

## 2026-08-01 - AirCraft HUD elapsed clock

- `EV-20260801-006` / `EXP-20260801-107` use a fresh Ghidra 12.1.2 headless
  instruction export as the primary source and Rizin 0.9.1 as an independent
  byte/boundary check for the constructor, slot-44 refresh, and complete HUD
  stage. The recovered lifecycle initializes `AirCraft+0x54C` to `+0`, adds
  every supplied refresh delta, shares one value across six rolling-number
  consumers, and resets once only after all four enclosing HUD gates pass.
- `LegacyAircraftHudElapsedClockState` is a dormant, allocation-free,
  single-thread-confined transaction. It rejects invalid deltas and mixed/stale
  stages atomically; abort and each native gate retain elapsed time. It owns no
  scheduler, render-event publication, application calls, or fabricated live
  counter values.
- The startup-compatible PC53/RNE policy is represented with binary64 staging
  before the binary32 store. Live process-wide x87 state remains unaccepted,
  so this is not a bit-parity or runtime-integration claim.
- Independent instruction boundaries correct the rolling-digit initializer
  note from 88 to 90 bytes and the draw helper note from 158 to 160 bytes.
- Clean validation passes on all three supported host toolchains: Clang
  22.1.8/Ninja with 139/139 CTests, Visual Studio 2026 MSVC/Ninja with a
  740-step build and 151/151 CTests (including D3D11 and both product smokes),
  and the dedicated `Airfix-Dev` WSL distro with a 446-step GCC 13.3/Ninja
  build and 139/139 CTests. The dedicated distro was terminated afterward.
- The public-boundary scan passes for all 730 tracked files. The Ghidra and
  Rizin wrapper suites, 12/12 Rizin export tests, clang-format enforcement,
  `git diff --check`, and an independent post-fix review also pass.

## 2026-08-01 - policy-conditioned CcRigidBody oracle

- Implemented the GO portion of `EV-20260730-004` / `EXP-20260730-068` as an
  offline exact-rational Python oracle. Explicit PC24/PC53/PC64 precision and
  all four rounding directions are inputs; no host floating-point mode is
  assumed.
- The model preserves signed zero, every recovered arithmetic boundary and
  binary32 spill, matrix `k=0,2,1` accumulation, left-Hamilton quaternion
  derivative, X/Y-stored versus Z-retained damping, the 13-lane Euler step,
  post-update normalization, accumulator reset, and post-step auxiliary
  refresh.
- Non-finite values, invalid mass/time step, zero quaternion, unsupported
  policies, malformed shapes, divide-by-zero, and unrepresentable stores fail
  closed. This deliberately does not claim native NaN, trap, status-word, or
  branch-time policy behavior.
- The controlled-runtime capture validator now computes V1-V6 through the
  oracle instead of maintaining a second hard-coded result table. Fixed
  published fixtures independently check V1-V5 under RNE and V6 under all 12
  PC/RC combinations; further tests cover signed-zero/boundary encodings,
  rejection, immutability, and deterministic finite steps.
- The oracle remains research tooling and is not linked into Windows or iOS.
  No scheduler, live input, collision, renderer, game binary, debugger, or
  private/original resource is used. The bit-parity runtime NO-GO remains
  unchanged pending accepted numeric policy or a valid consumer-site capture.
- Three clean toolchains pass. Clang 22.1.8/Ninja and the isolated
  Airfix-Dev GCC 13.3/Ninja environment each build 446 steps and pass 140/140
  CTests. Visual Studio 2026 MSVC 19.51 HostX64/Ninja builds the complete
  740-step Windows product, runtime HLSL, and D3D11 path and passes 152/152
  CTests including both product smokes. Airfix-Dev was terminated afterward;
  all WSL distributions are stopped. Both RE wrapper suites, 12/12 Rizin
  exporter tests, Python bytecode compilation, the synthetic and 732-file
  public-boundary scans, the 375-row/14-column unique function catalogue, and
  `git diff --check` pass.

## 2026-08-01 - AirCraft HUD instrument readouts

- `EV-20260801-007` / `EXP-20260801-108` close the two four-digit readout
  blocks at AirCraft VA ranges `[0x10006C7E,0x10006CF9)` and
  `[0x10006CF9,0x10006D52)`. Ghidra 12.1.2 remains primary; the existing
  Rizin 0.9.1 report independently confirms the bytes, fields, constants,
  branches, and update/draw helper calls.
- Right consumes the already-quantized `100*length(slot-0x54 vector)` route;
  left consumes the already-quantized `100*(+0x400/+0x3F8)+0.5` route. Both
  retain one shared HUD elapsed snapshot, exact right-before-left execution,
  instrument-relative anchors, white right tint, and `AirCraft+0x550` left
  tint. No stronger vector meaning or live `_ftol` policy is asserted.
- `LegacyAircraftHudInstrumentReadoutsPlan` advances both optional retained
  states atomically and preserves advancement when the atlas cannot draw. Its
  paired authenticated submission reuses the existing digit-atlas packet and
  native-output/UI-scale mapping, so no texture, shader, or backend resource
  is duplicated. Ordinary frames remain disconnected from fabricated values.
- Three clean host toolchains pass: Clang 22.1.8/Ninja builds the complete
  portable graph and passes 141/141 CTests; Visual Studio 2026 MSVC 19.51
  builds the full 744-step Windows product, including SDL3, D3D11/DXGI,
  XAudio2, runtime HLSL, and both product smokes, and passes 153/153 CTests;
  the isolated `Airfix-Dev` WSL distro builds with GCC 13.3/Ninja and passes
  141/141 CTests. The dedicated distro was terminated afterward.
- The Ghidra and Rizin wrapper suites, 12/12 Rizin exporter tests, synthetic
  public-boundary tests, the 738-file repository scan, the 375-row/14-column
  unique function catalogue, changed-source formatting, changed-scope local
  path review, and `git diff --check` pass.

## 2026-08-01 - complete AirCraft HUD render event

- `EV-20260801-008` / `EXP-20260801-109` compose every recovered subsection of
  AirCraft RVA `0x00006B00` into one authenticated render event in native
  order: instruments, right/left readouts, weapon panels, health gauge, then
  identity/status.
- One elapsed-clock transaction feeds six bit-identical consumer snapshots.
  The event and all six replacement states publish only after every plan,
  native-output mapping, content revision, stream transaction identity, owner,
  and nested command validates. Any failure aborts and retains elapsed time;
  complete success performs the one exact positive-zero reset.
- D3D11 and Metal now own matching dormant full-event consumers and verify the
  complete GPU-resource set before fixed-order traversal. Ordinary gameplay
  frames remain unchanged and manufacture no live HUD values. The x32dbg Phase
  A attempt remains `NO-GO` with `0/32` accepted hits and supplies no runtime
  conclusion.
- A clean portable GCC/Ninja build passes 142/142 CTests. Visual Studio 2026
  MSVC 19.51 HostX64/Ninja builds the complete SDL3/D3D11/XAudio2 Windows
  product and passes 154/154 CTests including both product smokes. Hosted iOS
  build validation remains assigned to GitHub Actions. The isolated
  `Airfix-Dev` WSL distro independently builds 443 steps with GCC 13.3 and
  passes 142/142 CTests, then stops without touching either shared WSL distro.
- Both RE wrapper suites, all 12 Rizin normalization tests, all nine runtime-
  capture validator tests, synthetic public-boundary tests, the 742-file public
  scan, 375-row/14-column unique function catalogue, changed-document links,
  changed-scope path scan, clang-format warnings-as-errors, and
  `git diff --check` pass.

## 2026-08-01 - shared durable document-pair transaction

- ADR-0018 extracts the duplicated AFRS/AFIP byte transaction into
  codec-neutral `airfix::io::DurableDocumentPair` code over the existing
  Win32/POSIX `DurableFile` primitives. Domain callbacks classify exact bounded
  bytes as valid, future-preserve, or replaceable-invalid; codecs, semantic
  defaults, schema diagnostics, and public errors remain owned by each store.
- Current, backup, current-partial, and backup-partial publication now share
  one exact-readback path. Valid current bytes rotate exactly, invalid regular
  bytes are never promoted, future bytes block downgrades without mutation,
  unsafe/linked entries fail closed, and post-replacement failures produce
  `committedAfterReadback` or `commitUnknown` according to the proven durable
  outcome. Fixed diagnostics remain path-free.
- Direct synthetic tests cover both backup and current replacement faults
  before and after publication, failed durability retry, stale/unsafe partials,
  file-state classification, first/no-op/rotation/replacement commits, invalid
  configuration, and future preservation. Existing AFRS and AFIP store suites
  retain their observable recovery and requested-record contracts.
- Complete Windows GCC 15.2/Ninja and MSVC 19.51/Ninja portable builds each
  pass 143/143 CTests. A clean Linux GCC 13.3/Ninja build in the isolated
  `Airfix-Dev` WSL distro also passes 143/143; all three WSL distributions are
  stopped afterward. This is a reusable atomic byte boundary, not an
  implementation of campaign/save-game schemas, migration, iOS save
  lifecycle, or `THRD`/`AXMI`/`ALMI`/`SCOR`.
- Synthetic public-boundary tests and the 746-file repository scan pass, as do
  changed-source clang-format warnings-as-errors and `git diff --check`.

## 2026-08-01 - bounded Windows accessibility semantics seam

- The Windows settings panel now publishes its visible hit-test rows separately
  from the complete bounded logical row set. A fixed-capacity semantic tree
  covers every settings screen, including logically available rows clipped by
  a small viewport, without allocating or exposing private content details.
- Typed focus, invoke, decrement, and increment actions pass through the same
  panel state machine as pointer and controller input. Screen and monotonic
  generation guards reject stale or same-screen ABA actions; preview-only
  controller samples deliberately do not invalidate otherwise valid actions.
- The rasterizer and semantic tree share bounded labels and values. Snapshot
  validation fails closed for invalid enums, settings, dimensions, geometry,
  logical ordering, visible-row divergence, and malformed controller state.
  Stable runtime identifiers and parent relationships are covered by synthetic
  tests. A native COM UI Automation provider, `WM_GETOBJECT` bridge, and
  physical Narrator acceptance remain explicitly pending.
- An independent review found action-enum, visible/logical consistency,
  disabled-state, malformed-snapshot, and stale-generation edge cases; each was
  fixed and covered by regression tests before approval. A clean Visual Studio
  2026 MSVC/Ninja build completes the 751-step Windows graph and passes 155/155
  CTests, including both product smokes. Clang-format warnings-as-errors, the
  745-file public-boundary scan, and `git diff --check` pass.

## 2026-08-02 - bounded Windows UI Automation provider

- The SDL-owned Windows HWND now answers `WM_GETOBJECT` with a server-side UI
  Automation fragment provider over the existing immutable semantic tree. It
  exposes stable automation/runtime IDs, hierarchy and navigation, physical
  screen bounds, focus/offscreen/enabled state, polite live status, read-only
  values, and role-appropriate invoke actions without touching simulation.
- COM callers enqueue only fixed typed focus/invoke/decrement/increment
  requests into a 32-entry queue. The SDL owner thread drains that queue
  through the panel's screen and monotonic-generation guards; overflow,
  malformed trees, an older active generation, withdrawn fragments, and
  fragments cached on a different screen fail closed.
- The subclass reference owns a shared state independently of the host, so an
  unexpected failed detach cannot leave a dangling callback. `WM_NCDESTROY`
  withdraws the tree, clears queued actions, removes the subclass, and releases
  that reference. UI Automation remains an optional product capability if
  attachment is unavailable.
- A structured correctness/security review found and fixed cross-screen
  runtime-ID reuse, stale fragment-root/runtime/value access, an invalid
  `ChildrenInvalidated` runtime-ID argument, missing live-region metadata,
  insufficient fixed-root validation, and callback lifetime risk. Synthetic
  integration tests use a real hidden Win32 HWND and real `IUIAutomation`
  client round trips, including window destruction and stale-element cases.
- A fresh Visual Studio 2026 MSVC 19.51/Ninja build completes 758 build edges
  and passes 157/157 CTests, including the UI Automation integration test and
  both Windows product smokes. Clang-format warnings-as-errors, the 752-file
  public-boundary scan, changed-scope local-path review, and `git diff --check`
  pass. Physical Narrator acceptance remains explicitly pending.

## 2026-08-03 - proposed private HD texture integration

- ADR-0019 records a plan only; no runtime integration, private package,
  pipeline-branch merge, CMake resource, or product setting is added. Classic
  remains the only effective texture-source mode and continues through the
  existing authenticated GTI decode/upload transaction.
- The current loader, logical texture binding, RGBA8/mip preparation,
  presentation schema, D3D11 resource publication, Metal heap transaction, and
  cache/budget boundaries were inspected. The plan identifies that logical
  paths are currently dropped before GTI read, source GTI SHA-256 is absent,
  D3D11 has no aggregate texture-residency ledger, and both backends publish a
  complete mission-owned texture set rather than a streaming cache.
- The public pipeline contract at commit
  `c92ba4ec5a6fe349e796bc71bd69ea4ab8a1a7f2` was reviewed without switching or
  merging its branch. Its local verifier rechecked the reviewed private corpus
  successfully: 1,833 logical textures, 1,689 accepted unique results, and
  16,304 mip files. No logical path, checksum, image, manifest, private root,
  or other owner data entered the repository or network.
- The proposed architecture keeps `TextureMode` independent of visual profile,
  resolves every result path from an accepted reviewed-manifest record,
  confirms actual GTI bytes by SHA-256, rejects root escape and linked/special
  files, falls back per texture, and separates Classic/Enhanced cache keys and
  budgets. It explicitly records that the current schema authenticates the
  base PNG but has no expected digest for each non-zero mip, so later
  independently streamable mips need an authenticated chain extension.
- Implementation is staged behind repository-boundary hardening, synthetic
  JSONL/PNG tests, a root-confined file capability, a portable resolver, and
  byte-identical GTI fallback. Windows RGBA8 pilot, durable settings, Metal,
  streaming, and BC7/BC3/ASTC derivatives remain later gates.

## 2026-08-03 - private texture repository-boundary hardening

- Completed ADR-0019's first preparatory gate without implementing HD loading.
  The public scanner now rejects a `private-textures` directory at any level,
  common raster and GPU texture formats, JSONL/NDJSON manifests and backup
  copies, and common archive/disc-image containers. The owner-local root is
  ignored explicitly; no global image ignore can hide an accidentally pending
  public file from the scanner.
- Synthetic regression cases cover the private directory, case-insensitive PNG,
  ASTC, JSONL, `.jsonl.bak`, ZIP, and compound `tar.gz` paths. Each fixture is
  removed after its assertion so later checks remain independent. Existing RE
  database, trace, dump, and aircraft-capture guards remain active.
- A fresh local GCC 15.2/Ninja build completes 213 build edges and all 143/143
  portable CTests pass. The dedicated synthetic boundary suite and the
  753-file repository scan pass, as does `git diff --check`. No original game
  file, private texture, manifest record, local path, or pipeline implementation
  entered the change.

## 2026-08-03 - full-HUD Windows validation capture

- Added an explicit owner-private `--capture-hud-validation-frame` mode. It
  requires a complete authenticated mission/HUD transaction, selects icon
  indices only from installed catalogues, builds one fixed representative
  atomic HUD event plus a centred sight, and draws them through the existing
  D3D11 consumers. The values and rolling-state updates remain local to the
  capture; the ordinary frame API and interactive loop stay disconnected.
- The command-line boundary requires BMP output, a complete mission, enabled
  path-free diagnostics, and mutual exclusion from every other capture. The
  new mode and the pre-existing health-gauge capture both use a hidden
  session-only SDL window. Synthetic parser tests cover accepted and rejected
  requests.
- Real owner-local captures complete at exact 1920x1080, 2560x1440,
  3840x2160, and 3440x1440 physical output/render-target extents with render
  scale 100%, Enhanced presentation, and Hor+. Visual inspection of 16:9 and
  ultrawide output confirms horizontal scene expansion and a sharp, stable HUD
  root. All BMPs and source content remain outside Git.
- A fresh-worktree Visual Studio 2026 MSVC 19.51/Ninja build compiles the full
  SDL3/D3D11/XAudio2 product and all test targets. All 157/157 CTests pass,
  including the command-line parser, complete HUD transaction, D3D11 renderer,
  UI Automation, and both data-less product smokes. The first Visual Studio
  project-generator attempt stalled in compiler identification; its exact
  child processes were stopped, and the successful build used the established
  MSVC/Ninja path with an existing read-only SDL source checkout.

## 2026-08-03 - explicit texture sample-space contract

- Added a backend-neutral RGBA8 sample-space label with three intentional
  states: encoded/unclassified input, classified sRGB colour, and linear data.
  The format selector maps these to UNORM or UNORM-sRGB and returns an invalid
  result for every forged enum value.
- GTI description and decode remain conservative and publish only
  `encoded-unclassified`; no original or private texture is reclassified and
  the current rendered image is unchanged. Authenticated HUD texture owners,
  room plan comparisons, and Metal preflight now retain or validate the label
  instead of silently discarding it.
- D3D11 maps classified RGBA8 uploads to
  `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` and all other valid current uploads to
  `DXGI_FORMAT_R8G8B8A8_UNORM`. Metal carries the corresponding
  `MTLPixelFormatRGBA8Unorm_sRGB`/`MTLPixelFormatRGBA8Unorm` choice through
  the same heap descriptor plan. Existing GTI exercises only the unchanged
  UNORM branch until a later evidence-backed classifier exists.
- A fresh GCC/Ninja portable build and all 143/143 CTests pass. Visual Studio
  2026 MSVC 19.51/Ninja also compiles and links the complete SDL3/D3D11/XAudio2
  product with the native format mapping, and all 157/157 Windows CTests pass.
  Public-boundary tests and the 755-file scanner also pass. Hosted iOS
  compilation remains the publication gate for the Objective-C++/Metal
  spelling and integration.

## 2026-08-03 - bounded reviewed HD manifest index

- Implemented ADR-0019 stage 2 as a portable C++20 parser over caller-owned
  bytes. It accepts one bounded reviewed-corpus JSONL header plus bounded result
  records, rejects malformed or inconsistent data, and retains only records
  whose public and explicit review statuses are both `accepted`.
- The immutable index supports normalized logical-GTI-path and source-GTI-SHA
  lookup. It validates schema and corpus identity, lowercase digests, declared
  counts, 4x dimensions, natural mip-chain length, alpha/method parameters, and
  root-relative path syntax. Duplicate logical identities or source digests
  fail the whole load.
- Diagnostics expose only an issue category and optional one-based line number;
  they never retain a manifest value, logical path, checksum, or local root.
  The parser performs no filesystem I/O. Root confinement, file authentication,
  PNG decoding, replacement resolution, cache publication, settings, and
  Enhanced product mode remain later gates; Classic behaviour is unchanged.
- Tests construct all JSONL in memory and cover valid accepted/rejected mixes,
  lookups, malformed JSON, duplicate keys/identities, traversal, schema and
  count inconsistencies, and configurable input/line/container/string/path/
  dimension/mip limits. No original or private asset is read or copied.

## 2026-08-03 - root-confined private texture file capability

- Accepted ADR-0020 and implemented ADR-0019 stage 3 without connecting a
  private package or product. The portable interface contains only an opaque
  capability, non-zero generation, bounded relative read, byte result, and
  fixed path-free status; only the owner-side platform factory accepts an
  absolute configured root, whose spelling is discarded after open.
- Windows now walks individual UTF-8 components relative to pinned directory
  handles with `NtCreateFile` and reparse processing disabled. Apple/Linux use
  descriptor-relative `openat` with `O_NOFOLLOW`. Both require ordinary
  single-link files, bound allocation before read, and revalidate the opened
  identity and metadata after the exact read. A stale generation performs no
  filesystem access.
- Synthetic tests cover valid nested reads, path grammar and UTF-8 limits,
  absolute/traversal/alternate-stream names, missing/oversized/wrong-type and
  hard-link files, symbolic-link/junction roots and intermediates, generation
  mismatch, and replacement of the configured root spelling. Failures retain
  no partial bytes or input-derived diagnostics.
- MinGW GCC 15.2 and Visual Studio 2026 MSVC 19.51 compile the Windows adapter,
  and the focused suite passes with both. A fresh portable build passes all
  145/145 CTests; a fresh SDL3/D3D11/XAudio2 MSVC product build completes 766
  build edges and passes all 159/159 CTests. Clang analyzer/bugprone review is
  clean after fixing native-handle ownership across allocation failure and
  explicitly rejecting embedded NUL in the configured root.
  Formatting, public-boundary tests and the 768-file scan, changed-document
  links, changed-scope local-path review, and diff checks pass. Hosted Linux,
  macOS, and iOS builds remain publication gates.
- Classic GTI behavior is unchanged. Manifest filesystem loading, resolver,
  actual-GTI hashing, per-texture fallback, PNG preparation, settings, caches,
  native uploads, and effective Enhanced mode remain explicitly disconnected.

## 2026-08-03 - conservative CI fast path and compiler cache

- Added a fail-closed, repository-local pull-request classifier. Only a narrow
  documentation allowlist can skip native builds; empty ranges, comparison
  errors, control characters, workflow/CMake/source/test/tool changes, and all
  unknown paths request the complete matrix. No third-party path-filter action
  or workflow-level `paths-ignore` decision is used.
- The always-on Ubuntu quality job now owns public-boundary tests and scanning,
  repository-local Markdown links, unique experiment IDs, the 14-column
  function catalogue and its two explicit historical unresolved references,
  deterministic Rizin/runtime-capture validators, and revision-scoped
  whitespace checks. Two stale scheduler-report links were redirected to the
  published confirmed scheduler boundary.
- Pinned `sccache-action` and `sccache` versions accelerate supported public
  C/C++ Ninja/Make builds and publish cache statistics. Visual Studio/Xcode
  product generators remain intentionally uncached. A weekly full portable
  matrix and cold-by-default manual dispatch install no compiler launcher.
- The recorded pre-change baseline identifies macOS compilation as the dominant
  19m59s step. A clean local GCC 15.2/Ninja build completes 464 edges and all
  145/145 CTests pass. Classifier and documentation-integrity tests, actionlint,
  12 Rizin normalization tests, 9 runtime-capture tests, synthetic boundary
  tests, the 772-file public scan, and `git diff --check` pass. Hosted warm/cold
  comparison remains the publication and performance gate.
- The first hosted full-matrix validation passes on Ubuntu, Windows portable,
  Windows D3D11/XAudio2, macOS, iPhoneOS, iPhoneSimulator, and the clangd
  preset. The cache-population run reports 0/303 hits for Ubuntu and macOS and
  1/303 for clangd; its timings are retained as the controlled empty-cache
  observation. The 6-second quality job also confirms that source/workflow
  changes fail closed to the full matrix.
- The warm follow-up retains the full matrix because the pull request still
  contains workflow/tool changes. It reports 50%, 61%, and 74% hit rates with
  zero cache errors for clangd, Ubuntu, and macOS respectively. Their build
  steps fall by 39%, 54%, and 74% relative to the empty-cache run. Both Windows
  products and both unsigned iOS destinations pass again without compiler
  caching.
- The first documentation-only trial exposed a GitHub naming boundary: a
  skipped strategy matrix reports `matrix.os`, while the main ruleset requires
  the three concrete platform names. A docs-only Ubuntu compatibility fan-out
  now publishes those exact statuses only after `full_build=false`; it performs
  no checkout, build, or test and allocates no Windows/macOS runner. Full runs
  continue to satisfy the same names through their real platform jobs, with no
  ruleset bypass.

## 2026-08-03 - bounded AirCraft AI-to-physics cycle

- `EV-20260803-001` / `EXP-20260803-113` closes one conditional mode-1
  target-loss cycle without implementing runtime C++20. A failed target
  resolution calls `AfAI::DiscardTask`, changes numeric mode `1 -> 0`, clears
  the bounded target fields, and under explicit finite predicates leaves raw
  channel values `{50,0,0,-100,-100}`.
- Ghidra Headless 12.1.2 and independent Rizin 0.9.1 agree on the exact
  AirCraft/AfEngine function boundaries, task payload widths, vtable entries,
  control sources, and synchronous event order: thrust, pitch, bank, primary
  attack, then secondary attack. Under a named PC53/nearest-even oracle and
  the reachable prior cache vector `{255,32,32,1,1}`, the five signed payloads
  are `{128,0,0,0,0}`; branch-time PC/RC remains unobserved.
- The evidence corrects the earlier unconditional unchanged-value suppression
  model. `AIControls::HasChanged` uses binary32 `0x3C23D70A` without a
  candidate f32 spill, while `GetRelative` uses binary64
  `0x3F847AE147AE147B` and stores a binary32 cache. At PC53/nearest-even,
  unchanged raw zero still reports changed in the configured thrust, axis,
  and attack ranges.
- The phase join is now explicit: AI writes occur after the current force
  work; the next active force step can read those controls only after Euler
  consumes the older accumulators, and the following Euler step consumes the
  newly prepared force/torque. Weapon events stop at persistent firing state;
  projectile scheduling remains separate. Numeric states/layout, ordered
  dispatch, field reducers, and structural phase order are GO. Full behavior,
  derived task wrappers, native bit parity, wall-clock cadence,
  cross-producer ordering, and a runtime rigid-body port remain NO-GO.
- Targeted Ghidra and Rizin reports, both wrapper suites, all 12 Rizin exporter
  tests, synthetic public-boundary tests, and the actual 760-file scan pass.
  The 380-row/14-column function catalogue retains only its two known
  historical missing CC references; all new Rizin ranges and required IDs are
  present. Changed links, added-line local-path scanning, and
  `git diff --check` pass. A fresh fetch followed by a scan of all 216
  available refs and reachable object paths finds no collision for the
  reserved EXP/EV IDs; all six working-tree paths containing them are the
  intended report and ledgers. Independent review's one P1 cache-precondition
  finding is fixed with the reachable vector `{255,32,32,1,1}`; final
  re-review reports `GO` with no open P0-P3.

## 2026-08-03 - original campaign, frontend, and roster flow

- Completed a documentation-only static study in an isolated worktree. Ghidra
  Headless 12.1.2 was the primary source; Rizin 0.9.1/rz-ghidra independently
  checked central boundaries, calls, xrefs, fields, constants, and FourCCs. No
  game, debugger, or real save was run and no runtime C++20 source changed.
- Reused the established bootstrap, mission-loading, AFS scheduling/bytecode,
  outcome-state, result predicate/progression, and generic AFCHUNK evidence
  before extending it. Modern render-settings and ADR-0018 durable-document
  work were reviewed but explicitly not treated as original menu or atomic-save
  evidence.
- Joined startup user loading to main menu and profile selection; mapped all
  eight main-menu routes plus central command cases 6, 7, and `0x2B`; bounded
  campaign selection/briefing/start, pause, result, retry/continue/exit, and the
  logic-to-render ownership seam.
- Recovered two Axis/Allied catalogues with ten zero-based rows each. Missing
  side maxima default to zero; UI selection clamps stored signed values to
  `[0,9]`; success replaces `THRD` and raises only the selected `AXMI`/`ALMI`
  with signed `mission_number + 1`; failure leaves progression unchanged.
- Extended result evidence through the local-player gate, literal score terms,
  `NfPlayer::RegisterStats`, cumulative stat chunks, `SCOR`, and ignored
  remembered-roster write. Confirmed the original `FRIK` anomaly: its gate
  tests player `+0x4C` while its addition reads `+0x48`.
- Specialized AFCHUNK framing as FMT-ROSTER and documented profile
  create/select/delete/shutdown lifecycle, defaults, repeated `MEDA`, structure
  ownership, and malformed-file gaps. The original reader destroys prior state
  before full validation; the writer truncates directly with `wb`, has no
  atomic replace/backup, and can report success after losing up to eight final
  bytes.
- Published `EXP-20260803-114`, `EV-20260803-002`, FMT-ROSTER, the frontend
  state/call graphs, structure maps, platform requirements, and separate
  GO/NO-GO decisions. Complete frontend enum/modal parity, difficulty, ordinary
  medals/equipment, `threadend`, safe corrupt-file behavior, and bit-identical
  legacy export remain explicit unknowns.

## 2026-08-03 - AFS mission scripting VM static reconstruction

- Completed a documentation-only static study in an isolated worktree from
  the `origin/main` snapshot current at experiment start
  (`1ec27ace154607a99359f39ffa82390e8bc5ab35`). A later campaign-only remote
  advance was not imported into this isolated branch. No game, script,
  executable, debugger, or GUI was run; no runtime C++20, renderer, campaign,
  AI/physics, or CI source changed.
- Reviewed and reused the existing mission-load, outcome, campaign,
  Autoexec/source-order, outcome-bytecode, scheduler, trigger, and aircraft
  evidence before exporting new functions. Private AFS corpus scans were not
  repeated and no source, logical member, asset, binary, or per-file result is
  published.
- Fresh Ghidra Headless 12.1.2 analysis maps the NUL-text source ingress,
  lexer/parser/compiler, database/object/function ownership, mission
  Load/Start/Reset/destruction, process and execution structures, local
  scheduling, native gameplay dispatch, and all 70 VM opcode IDs
  `0x01–0x46`. Selected exports have zero unresolved addresses.
- Independent Rizin 0.9.1/rz-ghidra produced 35 normalized path-free reports
  and matches the central end-exclusive boundaries, instructions, calls,
  xrefs, offsets, and jump-table targets. Raw SP replay resolved opcode `0x37`
  as six-to-three component division after an initial reading missed the SP
  stores at `0x1006A903` and `0x1006A914`; no discrepancy remains.
- The original parser/runtime trusts source sizes, reads, allocation,
  compiler mutation, IP/SP, instruction widths, branches, slots, descriptors,
  arithmetic, text buffers, and time values. Unknown opcodes consume one word
  and quietly return. FMT-AFS therefore specifies a new bounded,
  transactional, pointer-free, fail-closed policy rather than unsafe behavior
  compatibility.
- `EV-20260803-003` publishes the complete call graph, opcode table, structure
  maps, exact one-step scheduler, event/action/timer-to-world/outcome diagram,
  Windows/iOS behavior list, explicit unknowns, and separate GO/NO-GO gates.
  Safe parser policy, static process model, and local scheduler order are GO;
  the full interpreter, typed calls/references, global AI/physics order, and
  numerical/time parity remain NO-GO.
- Independent raw and closed-diff reviews corrected missing-reference paths,
  wordcode ownership, call-edge typing, native ID `0x01`, allocation/null
  branches, `0x46` lifetime, decoder exits, and portable catalogue scope. Final
  Ghidra, Rizin, and documentation reviews report `P0=P1=P2=P3=0`. Wrapper
  tests, 12/12 Rizin exporter tests, documentation/link integrity (116
  experiments, 408 catalogue rows), 35/35 boundaries, 17/17 direct edges,
  70/70 opcodes, the 779-file public-boundary scan, 232-ref ID uniqueness,
  UTF-8/path/scope checks, and `git diff --check` all pass.

## 2026-08-03 - private HD texture resolver and exact Classic fallback

- Implemented ADR-0019 stage 4 without enabling private content in either
  product. `TextureImportRequest` now retains the authenticated archive logical
  path through local/global deduplication, while direct HUD loaders provide the
  same identity explicitly.
- Added a non-owning `TextureReplacementResolver` over the accepted-only
  manifest index and ADR-0020 root capability. It validates generation and
  logical identity, hashes the actual GTI bytes, requires the manifest source
  checksum, resolves the exact manifest-selected base path, and verifies the
  base PNG checksum before returning owned encoded bytes.
- Digest-only lookup is disabled by default and requires explicit caller
  policy. Every miss, mismatch, unsafe file, stale generation, limit, I/O, or
  checksum failure returns a fixed path/checksum-free reason and no private
  bytes.
- The shared `prepareTextureSource` boundary proves that no configured resolver
  and every replacement failure invoke the existing `prepareGtiUpload` with the
  identical request, GTI span, and limits. All current content loaders pass no
  resolver, so effective product behavior remains exactly Classic.
- The dedicated synthetic suite covers normalized path matching, guarded
  digest alternatives, stale and malformed identities, all file-store failure
  mappings, base checksum and byte-limit rejection, exact Classic equivalence,
  and the rule that a replacement candidate is not decoded as GTI. No private
  image, manifest, path, checksum, original asset, or package root is used.
- A complete GCC 15.2/Ninja build and all 151/151 CTests pass. A fresh MSVC
  19.51/Ninja build compiles all 485 portable edges and passes all 149 native
  CTests; the two sandbox-blocked Python launchers pass directly as 9/9 and
  8/8. Documentation integrity, 12/12 Rizin exporter tests, synthetic boundary
  regressions, the 801-file public-boundary scan, and `git diff --check` pass.
  Clang analyzer/bugprone checks are clean for the new production resolver,
  and all new C++ files pass the formatting gate.
  PNG signature, IHDR, RGBA8 dimensions, mip-chain completeness,
  decoded-memory accounting, cache/settings integration, and native
  D3D11/Metal upload remain explicit later stages.

## 2026-08-03 - bounded private HD PNG and mip-chain preparation

- Implemented ADR-0019 stage 5 as a disconnected portable boundary. Both
  products remain forced to Classic, no private path or package is configured,
  and no D3D11/Metal upload, selector, cache, or streaming path was added.
- Added `PreparedTextureAsset` and `prepareTextureHdPng`. The transaction
  revalidates generation, source/base identity, accepted 4x metadata, sample
  space and alpha enum before private I/O. Every failure returns one fixed
  path/checksum-free issue and destroys partial decoded levels.
- Each declared `mip-00.png` onward is read only through the pinned
  root-capability, checked as non-interlaced 8-bit RGBA PNG with exact natural
  dimensions, decoded in memory, and accounted against per-level, chain,
  peak-in-flight, and decoded CPU budgets. Mip zero must be byte-identical to
  the authenticated base; the first undeclared numbered mip must be absent.
- Selected LodePNG from its official repository at commit
  `ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a`. The upstream commit is unsigned,
  so CMake pins archive SHA-256
  `c2459a3f9145258f901d262576f7a56ca08087d3b3efeee3ae033c0952120803`.
  Only memory decode is compiled; encoder, disk I/O, ancillary chunks, and
  error text are disabled. Its zlib license is staged in Windows and iOS
  outputs and verified by product CI.
- The current manifest authenticates only the base PNG. Level zero is labelled
  authenticated after byte equality; non-zero levels are deliberately labelled
  structural-only and are not claimed cryptographically verified. Unrelated
  directory entries are ignored; the private offline corpus verifier remains
  responsible for its stronger exact-directory inventory check.
- Synthetic tests cover valid decoding, signature/IHDR/color/interlace/CRC
  failures, wrong dimensions, missing and extra numbered levels, stale or
  malformed metadata, all file-store failures, three alpha policies, and all
  CPU byte budgets. No PNG file, JSONL, original GTI, logical path, checksum,
  or private root from the owner corpus entered the repository.
- Complete GCC 15.2/Ninja and MSVC 19.51/Ninja portable builds pass 152/152
  CTests each. The complete Windows product passes 166/166 CTests including
  D3D11/XAudio2 and both product smokes. Clang analyzer/bugprone review has no
  open finding; exact upstream/staged license hashes match. Hosted
  Linux/macOS/Windows and unsigned iOS remain the publication gate.

## 2026-08-03 - Windows session-only HD texture pilot

- Connected ADR-0019's accepted-only resolver and bounded PNG mip preparer to
  the portable mission room transaction. Each Enhanced failure falls back to
  the unchanged authenticated GTI path; complete room publication validates
  mode, generation, source identity, and aggregate counts.
- Added an explicit Windows-only session contract for Enhanced mode. The root
  and relative reviewed manifest are never persisted or logged, invalid
  packages safely start the mission in Classic, and a mode change requires a
  mission reload.
- Added immutable D3D11 RGBA8 mip uploads behind a transactional,
  generation-partitioned cache capped at 512 entries and 256 MiB. Its key also
  includes source/logical identity, GPU format, and mip policy; Classic clears
  the Enhanced partition.
- Corrected one parser rule to match the public schema: representative `mixed`
  category is valid independently from the concrete source-category array. A
  synthetic regression covers it without retaining private data.
- A private read-only 1920x1080 comparison loaded 34 Enhanced mission textures,
  zero Classic textures, and zero fallbacks. Captures, logs, manifest, PNGs,
  and configured roots remain owner-local and outside Git.
- The complete MSVC product build passes 167/167 tests and the complete
  portable C++20 build passes 152/152. Public-boundary regressions and the
  811-file scan, documentation integrity (119 experiments/408 catalogue rows),
  12/12 Rizin normalization, CI-classifier tests, and `git diff --check` pass.
  Hosted Linux/macOS/Windows, clangd, and unsigned iOS remain the publication
  gate.
