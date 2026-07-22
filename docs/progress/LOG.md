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
- Added a private `gti-preview` diagnostic. A 256x128 Spitfire frontend image
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
