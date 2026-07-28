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

- `MissionLoadManifest` now accepts an optional exact player `OBJE` logical
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
