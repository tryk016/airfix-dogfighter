# ADR-0019: optional private HD texture replacements

- Status: accepted; staged implementation in progress
- Date: 2026-08-03
- Deciders: project owner and implementation lead

## Context

The reconstructed renderer currently loads every scene and HUD texture from an
authenticated owner-local GTI source. `MissionWorldRoomLoader` resolves a
logical GTI path to one archive file, reads that file through
`VerifiedContentSession`, builds a bounded RGBA8 upload plan, decodes the
selected authored mip chain, and publishes the complete room only after every
texture succeeds. D3D11 then creates one immutable texture per dense asset ID.
Metal prepares all textures in a retained heap, accounts them against the
shared 384 MiB snapshot GPU-admission ledger, and publishes them with the
complete room snapshot. CPU upload pixels are released after native ownership
is established.

This is a sound Classic-mode transaction, but it is not a replacement cache or
a streaming system:

- the current texture import request retains a dense asset ID and archive file
  index, but drops the resolved logical path before GTI bytes are read;
- the source GTI is not currently identified by SHA-256 during mission load;
- both products expect one `LoadedTextureAsset` representation containing a
  GTI upload plan and decoded RGBA8 levels;
- D3D11 has no aggregate texture-residency ledger equivalent to Metal's
  snapshot ledger;
- a mission snapshot owns all of its GPU textures, with no cross-mission cache
  or incremental residency; and
- the durable presentation settings contain `VisualProfile`, but intentionally
  have no independent HD texture selection.

An owner-local pipeline on the separate `codex/texture-hd-pipeline` worktree,
at commit `c92ba4ec5a6fe349e796bc71bd69ea4ab8a1a7f2`, defines a public JSON Schema
and public local-only tools. Its reviewed private corpus was checked without
copying it into this repository: it currently reports 1,833 logical textures,
1,689 content-deduplicated accepted results, exact 4x dimensions, and 16,304
PNG mip files. These counts are evidence, not runtime constants.

Each accepted result is PNG RGBA8 with straight alpha. The full mip chain is a
sequence of PNG RGBA8 files down to 1x1; mip zero is byte-identical to the base
result. Alpha-bearing mipmaps were filtered in premultiplied-alpha space and
transparent RGB was extended to reduce halos. The declared sample space is
`encoded-unclassified`, so neither the package nor its 4x scale proves that a
texture is color data, sRGB, linear data, a normal map, or another technical
channel layout.

The reviewed manifest and all images remain owner-private. A single accepted
result may live in a corrective layer different from the bulk output layer.
Runtime code therefore cannot derive a filename from a checksum, method,
category, or layer convention; every base and mip path must come from the
manifest record.

## Decision

The project supports an optional, owner-configured HD replacement package
through a portable `TextureReplacementResolver`, with implementation kept
deliberately staged. Public stages 1-8 are implemented in source: AFRS persists
the requested mode, both native settings panels expose the independent choice,
and both products resolve it against validated package availability before an
atomic full-mission visual reload. Windows accepts an explicit session-local
root. iOS imports an owner-selected folder copy into private Application
Support, persists only an opaque product-generated locator, reopens the shared
portable package session after restart, and uses the existing transactional
Metal RGBA8/mip upload. Physical-device budget and visual acceptance remain
pending, so this is not yet a completed device-support claim. This ADR does not
merge the pipeline branch or add the private package to a build, bundle, Git,
or CI.

### Independent setting and effective state

Add an independent setting:

```text
TextureMode = Classic | Enhanced
```

`TextureMode` is orthogonal to `VisualProfile`:

- `VisualProfile::classic` versus `VisualProfile::enhanced` selects renderer
  presentation policy such as filtering, lighting, materials, and later
  post-processing;
- `TextureMode::classic` versus `TextureMode::enhanced` selects the source of
  texture pixels; and
- every combination remains valid. In particular, high-resolution texture
  replacement must not silently enable lighting or change simulation.

Classic is always available and keeps the current GTI parse, preferred-variant
selection, authored/generated mip behavior, and failure semantics. It has no
dependency on an HD manifest or package root.

The target settings model distinguishes the durable requested mode from the
effective mode used by the active mission:

```text
requested texture mode: persisted user preference
package availability: not configured | validating | ready | unavailable
effective texture mode: Classic | Enhanced
mission reload required: yes | no
```

When the requested mode is Enhanced but the package is absent, stale, unsafe,
or invalid, effective mode is Classic. The UI exposes only a fixed, path-free
message such as `HD texture pack unavailable; Classic textures will be used.`
It never exposes a host path, logical GTI path, checksum, manifest record, or
pipeline method. A package becoming ready does not change a running mission.

Changing the requested mode requires a mission/resource-snapshot reload in the
first implementation. The new room and complete active texture namespace are
prepared before publication; failure preserves the old room and old effective
mode. A later cache may reduce reload work, but it must not turn mode changes
into partial in-place mutation.

The implemented Windows product remains deliberately bounded. Classic is the
default. AFRS persists only the requested mode; an explicit `--texture-mode`
is a session override and is never written back. The private root and relative
reviewed-manifest path remain explicit session-local configuration and are
never stored in AFRS. A locator may accompany a persisted Classic or Enhanced
preference without repeating the mode override. Windows validates a configured
package at startup, then its native pause panel enables Enhanced only for a
ready package. A mode change reloads the room and all authenticated HUD texture
owners without re-registering aircraft audio. CPU and GPU preparation precede
the renderer's single ownership swap; a preparation failure preserves the old
scene, active mode, simulation endpoints, and Classic fallback. iOS enables
Enhanced only while its installed package has passed the same accepted-only
manifest and root-capability validation. A package or mode change cancels any
stale publication ticket and prepares a complete replacement mission snapshot
before Metal's no-fail ownership swap.

`RenderPresentationSettings` now carries the requested `TextureMode` through
AFRS semantic schema 4. Existing schema-1/2/3 records migrate to Classic. A
future-schema record remains opaque and downgrade-protected. The configured
private package root is platform-local content configuration, not a field in
the portable presentation record. `TextureModeRuntimeState` independently
resolves requested mode, coarse package availability, effective mode, Classic
fallback, and whether an already active mission snapshot requires reload; it
rejects forged enums and impossible Classic/Enhanced active-state pairs.

### Package configuration and ownership

The package root is supplied only by an owner-local platform adapter:

- Windows may use an explicit `--texture-pack-root` launch option and later a
  private content-manager selection stored below the application preference
  root;
- iOS uses a separate owner-imported Application Support subtree managed by
  its native document-picker importer; and
- tests inject a temporary synthetic root.

The root is never compiled into CMake, embedded as an application resource,
placed in an iOS bundle, copied into AFPACK implicitly, inferred from the
current directory, or committed. The products store no build-machine path.
Original GTI and archive files remain read-only and unchanged.

The platform adapter opens and validates the root once and supplies a bounded
file-store capability to portable code. Portable code does not concatenate a
manifest string and then open an unrestricted host path. Windows opens each
component relative to the previously pinned directory handle with reparse
processing disabled. Apple platforms use the equivalent descriptor-relative
no-follow walk. The shared first implementation in ADR-0020 rejects symbolic
links, junctions, other reparse entries, devices, FIFOs, sockets, multiply
linked files, and directories where a regular file is required. It stores no
configured root spelling. Windows, iOS, and the portable synthetic suites call
the same platform factory; iOS uses the POSIX capability.

Package-session construction is also portable. `TexturePackSession` assigns a
non-zero process generation, pins the root capability, reads the bounded
manifest through that capability, builds the accepted-only immutable index,
and constructs the non-owning resolver while preserving owner lifetimes. The
Windows adapter is deliberately only a compatibility facade over this shared
implementation. The iOS importer reuses it after installing an owner-selected
folder copy; it has no independent parser or weaker path-opening policy.

The iOS installer asks the system document picker for a folder copy. It never
opens, renames, or modifies the owner's original folder. The disposable copy
is moved under a product-generated lowercase `pack-<UUID>.partial` identity,
scanned without following links, and must contain exactly one JSONL candidate
that opens as an accepted reviewed-corpus session. Neither a layer name nor a
specific manifest filename is hardcoded. Only after validation is the staging
directory renamed to its immutable `pack-<UUID>` identity.

Activation uses the canonical AFTL v1 locator in a durable current/backup
pair. The locator contains only the product-generated package identity and the
validated root-relative manifest path, has fixed framing plus SHA-256, and is
never shown in product UI or logs. Unknown future AFTL schemas are retained
opaque and block downgrade writes. A failed or unconfirmed commit is inspected
again before any success is reported; a rejected replacement closes all file
handles before its exact owned directory is removed. The previous current and
backup packages remain available for recovery. The app bundle, CMake resources,
Git, and public CI never contain the locator, manifest, PNG files, or package
directory.

### Manifest index

Introduce a bounded, streaming JSONL reader rather than loading the complete
manifest into one unconstrained JSON document. It accepts only the reviewed
schema version supported by the runtime and requires:

- a reviewed-corpus header with internally consistent record and logical-path
  counts;
- a configured maximum manifest size, line size, record count, logical paths
  per record, total logical-path bytes, and decoded metadata bytes;
- exact 64-lowercase-hex SHA-256 fields;
- exact positive source and result dimensions with checked 4x arithmetic;
- `status == accepted` and `review.status == accepted`;
- non-empty `method` and bounded `parameters`, while treating them as
  provenance rather than runtime dispatch names;
- `parameters.sample_space == encoded-unclassified` until a separate
  classification layer proves otherwise;
- `source.alpha_usage` in `opaque`, `binary`, or `translucent`;
- non-empty `result.path` and `result.mipmap_directory` resolved only through
  the root capability; and
- a complete, conflict-free pair of indexes by normalized logical path and
  source GTI SHA-256.

Runtime must not hardcode current layer names, file-stem rules, the corrective
layer, or the current corpus counts. `result.path` is the base image and
`result.mipmap_directory` is the mip root for every record.

Logical keys reuse the archive's existing validation and separator
normalization, followed by locale-independent ASCII case folding matching
legacy archive lookup. Absolute paths, drive-qualified paths, UNC/device
paths, alternate data streams, empty/dot/dot-dot components, control
characters, embedded NUL, and overlong values are rejected before indexing.
Two records may not claim the same normalized path with different source
checksums or results. The representative record category is validated
independently from the source-category list, as required by the public schema:
`mixed` may summarize multiple concrete categories without appearing in that
list.

The current private schema authenticates the base PNG with
`output_png_sha256`, but does not contain an expected SHA-256 for every
non-zero mip. The first runtime slice can still verify the complete exact file
set, RGBA8 PNG shape, dimensions, mip-zero identity, and the base checksum. A
future schema revision or private derivative manifest should add per-mip
digests (or one authenticated ordered-chain digest) before individual mip files
are trusted as independently streamable artifacts. Runtime must not claim
cryptographic verification of non-zero mip contents from the current schema.

### Proposed portable interfaces

Names are provisional but ownership boundaries are binding:

```text
TexturePackageConfiguration
  owner-local root capability
  reviewed manifest relative name
  configured validation and memory limits

TextureReplacementManifestIndex
  immutable manifest generation identity
  index by normalized logical path
  index by source GTI SHA-256
  accepted records only

TextureSourceIdentity
  dense TextureAssetId
  normalized logical path
  authenticated content revision/archive file identity
  source GTI SHA-256

TextureReplacementResolver
  resolve(TextureSourceIdentity, TextureLoadPolicy)
    -> ReplacementCandidate | FallbackToGti(reason)

PreparedTextureAsset
  source origin: original GTI | private HD PNG | private compressed derivative
  dimensions and mip metadata
  alpha usage and explicit color classification
  decoded/upload payload variant
  accounted CPU/GPU byte totals

TextureCacheKey
  texture mode
  normalized logical path and source GTI SHA-256
  manifest generation
  target GPU format and color-space view
  mip policy and resident mip range

TextureResidencyManager
  independent Classic and Enhanced cache partitions
  CPU staging and GPU-residency ledgers
  priority, cancellation, generation, and eviction
```

`FallbackToGti` contains a fixed reason enum only. Examples are not configured,
manifest unavailable, record absent, source mismatch, unsafe path, invalid
type, checksum mismatch, invalid PNG, incomplete mip chain, unsupported color
classification, budget denied, cancelled, and stale generation. Formatting
that decision for logs or UI is path-free and checksum-free.

The existing `TextureImportRequest` must retain the already resolved logical
path when global dense texture IDs are formed. The loader reads the GTI bytes
exactly as it does now, computes SHA-256 over those immutable bytes, and asks
the resolver before GTI decode. The source checksum is therefore always an
exact confirmation, not a manifest assertion accepted without the original
source.

Resolution order is deterministic:

1. validate and normalize the logical path;
2. find its accepted manifest record;
3. confirm `source_gti_sha256` against the actual GTI bytes;
4. if the path record is absent or does not match, permit the SHA-256 index as
   the content-deduplication alternative only when the record is accepted and
   its texture-use classification is compatible;
5. open every result through the private-root capability;
6. validate the candidate against format, dimensions, chain, byte, and memory
   limits; and
7. return a complete replacement or one fallback decision.

No single replacement failure aborts mission loading. Once original GTI bytes
have been read, any Enhanced failure continues through the existing Classic
GTI preparation path for that texture. A Classic GTI failure retains its
current fail-closed mission behavior because there is no authenticated source
to fall back to.

### PNG and color contract

Stage one supports private PNG RGBA8 payloads only. Before decode it checks the
PNG signature and IHDR and requires 8-bit RGBA color type, positive bounded
dimensions, and a supported interlace policy. Decode is performed by a
separately reviewed PNG-only component or platform adapter behind identical
portable validation; broad image-format auto-detection is not accepted. The
decoded byte count, row pitch, level count, and every dimension are checked
against the manifest and budget before publication.

Straight alpha is retained in prepared CPU data. Any backend premultiplication
decision must be explicit and must match the material/blend path; it cannot be
inferred merely from the presence of transparency. The accepted offline mips
remain authoritative and are not regenerated by D3D11 or Metal.

`encoded-unclassified` is not a color-space selection. A separate classification
table, keyed by texture use rather than filename convention, must choose one of
at least `colorSrgb`, `colorLinear`, `normal`, `mask`, or
`technicalUnclassified`. Unclassified technical data remains RGBA8 UNORM and
is ineligible for automatic sRGB views, normal-map treatment, or lossy GPU
compression. The same source bytes may require separate GPU views when two
material uses have proven different color semantics.

### Cache, budgets, and streaming

Classic and Enhanced use separate cache partitions and never alias a resource
solely because their dense asset IDs match. The default mode switch retires the
inactive mission generation and releases its texture resources before the new
generation becomes active. Both variants may coexist only when an explicit
combined budget reservation succeeds; coexistence is an optimization, never a
requirement.

The current 512 MiB loader counters and Metal's 384 MiB aggregate snapshot
ledger remain hard upper boundaries, not recommended HD allocations. A 4x
RGBA8 image can require about 16 times the base-level storage before mip
overhead, so loading the full replacement corpus or every mission texture at
full resolution is not an acceptable first implementation.

`TextureResidencyBudget` supplies independent checked limits for:

- manifest/index metadata;
- source and encoded bytes in flight;
- decoded CPU staging bytes;
- queued upload bytes;
- resident GPU bytes; and
- number of assets and concurrent decode/upload jobs.

Windows later intersects a quality-profile cap with DXGI's current local
memory budget. Metal intersects its profile cap with the existing aggregate
snapshot ledger and device working-set guidance. Initial numbers are
conservative platform policy, not constants in the manifest; they must be
profiled on the iPhone SE 3, iPhone 17 Pro Max, and representative Windows
hardware before acceptance.

The first integration is mission-load-only and may choose a bounded top mip per
asset. Streaming follows only after the resolver and full fallback matrix are
proven. Later priority order is derived from authenticated runtime use, not
layer names:

1. product UI/HUD and player aircraft required for the next frame;
2. textures visible in the current room and nearby placed objects;
3. adjacent/portal-visible rooms and active actors;
4. distant/background content; and
5. speculative high-resolution mips.

Coarse accepted mips may become resident before finer levels. Each job carries
the content revision, manifest generation, texture mode, and mission
generation; stale completion is discarded. Eviction removes the least
recently used unpinned fine mips before whole resources. No streaming event,
cache hit, or fallback decision may change simulation order, physics, collision,
PRNG state, or input.

### Backend formats

The portable prepared-texture boundary does not expose D3D11 or Metal objects.
It can later carry either RGBA8 levels or authenticated precompressed
subresources:

- Windows prefers BC7 for classified color art and may use BC3 for a
  compatibility path that requires alpha;
- iOS uses ASTC profiles selected by classified role and quality budget; and
- technical/channel maps are not compressed until their channel semantics and
  acceptable error are proven.

RGBA8 is the only stage-one upload. BC7, BC3, and ASTC are later private
derivative layers with their own manifest identity, per-subresource checksums,
format metadata, and synthetic parser tests. They never replace or mutate the
accepted RGBA source layer.

The Windows pilot uploads the prepared RGBA8 mip chain as immutable D3D11
resources. Its conservative cache is partitioned by texture mode and manifest
generation, includes logical/source identity, GPU format, and mip policy in
the key, and is capped at 512 entries and 256 MiB. A generation or mode change
invalidates the inactive partition; Classic never needs to retain an Enhanced
copy. This is a bounded reload cache, not the later streaming residency
manager.

### Repository and telemetry boundary

The repository must reject, not merely ignore:

- `private-textures` and equivalent owner-content directories;
- PNG/JPEG/WebP/BMP/TGA/PPM and GPU texture containers such as DDS, KTX/KTX2,
  and ASTC;
- JSONL corpus/review manifests;
- original archives, executable binaries, model binaries, QA boards, cache
  databases, and platform texture derivatives; and
- local absolute paths or machine-specific configuration.

The pipeline commit contains proposed scanner coverage for these classes, but
that branch is not merged by this ADR. Scanner hardening is a separate
preparatory change and must land before any runtime integration.

Diagnostics may report aggregate counts only: requested/effective mode,
package availability category, replacement/fallback totals by fixed reason,
resident/queued bytes, cache hit rate, and resident mip range. Logical paths,
full or partial checksums, manifest names, local roots, model provenance, and
method parameters never appear in logs, crash text, overlays, or UI.

## Classic, Enhanced, and fallback flow

```text
mission texture dependency
        |
        v
read authenticated original GTI bytes
        |
        +---------------- TextureMode::Classic ----------------+
        |                                                       |
        |                                             existing GTI decode
        |                                                       |
        |                                                       v
        |                                              Classic cache/resource
        |
        +--------------- TextureMode::Enhanced ----------------+
                                                                |
                                             normalize path + SHA-256 GTI
                                                                |
                                           accepted manifest record/index?
                                               | yes                 | no
                                               v                     v
                                    validate private base/mips   fallback reason
                                               | valid               |
                                               v                     |
                                      budget/cache admission         |
                                          | yes       | no           |
                                          v           +-------------+
                                Enhanced cache/resource              |
                                                                     v
                                                          existing GTI decode
                                                                     |
                                                                     v
                                                          Classic fallback asset
```

The mission may therefore contain Enhanced and fallback Classic resources at
the same time. Its effective texture mode remains Enhanced, while diagnostics
report an aggregate degraded/fallback count. One bad replacement cannot
invalidate unrelated textures or abort the mission.

## Options considered

### Replace GTI during offline AFPACK conversion

| Dimension | Assessment |
|---|---|
| Runtime simplicity | High |
| Classic fidelity | Low |
| Owner recovery | Low |
| Package independence | Low |

Rejected. It would mutate or obscure the authenticated original texture source,
couple every install to one private enhancement corpus, and make per-texture
fallback difficult to prove.

### Derive HD filenames from logical paths or content hashes

| Dimension | Assessment |
|---|---|
| Initial implementation | Low |
| Corrective-layer support | None |
| Manifest authority | Low |
| Long-term compatibility | Low |

Rejected. At least one accepted result uses a separate correction layer, and
future immutable pipeline versions may rearrange every output. The manifest is
the only path authority.

### Put replacement decisions directly in D3D11 and Metal

| Dimension | Assessment |
|---|---|
| Backend duplication | High |
| Fallback parity | Low |
| Synthetic testing | Low |
| Future compressed formats | Medium |

Rejected. Package parsing, identity, fallback, and budgets belong in portable
C++20. Native backends only validate and upload an already prepared payload.

### Portable resolver with thin platform file/GPU adapters

| Dimension | Assessment |
|---|---|
| Cross-platform parity | High |
| Security testability | High |
| Initial refactor cost | Medium |
| Future format support | High |

Selected. It preserves Classic exactly, keeps private path authority outside
the renderer, and gives both backends one fallback and cache vocabulary.

## Consequences

- Classic remains the permanent recovery path and can run with no HD package.
- Enhanced texture selection becomes independent of the Enhanced visual
  profile and cannot affect deterministic gameplay.
- Mission texture identity must retain a normalized logical path and exact GTI
  SHA-256 through load and cache planning.
- Current all-at-once backend ownership must gain explicit residency and
  generation semantics before true streaming.
- Mode changes initially require an atomic mission reload.
- The current manifest is sufficient for accepted base-image authentication
  and structural mip validation, but not per-mip cryptographic authentication.
- A future compressed derivative does not require changing the resolver or
  gameplay code, only adding a new prepared payload and backend upload path.
- Full package validation can be expensive. Availability therefore needs a
  cancellable background validation transaction and a private opaque receipt
  keyed to the exact manifest generation; relevant files are still rechecked
  when opened.
- No original or derived image becomes a repository, CMake, CI, or application
  bundle dependency.

## Staged implementation plan

1. **Public boundary first.** Independently port and review only the scanner,
   ignore, and synthetic boundary tests needed to reject private texture
   directories, images, JSONL, archives, and GPU derivatives. Do not merge the
   pipeline branch wholesale.
2. **Portable manifest/index.** Add bounded JSONL parsing, schema validation,
   accepted-only indexing, logical-path normalization, redacted issue enums,
   and synthetic manifests. No image decode and no product setting yet.
3. **Root-capability adapters.** Add Windows no-reparse and Apple no-follow
   regular-file access behind one testable relative-file interface. Prove
   absolute, traversal, symlink, reparse, type, and stale-generation rejection.
   This stage is implemented by ADR-0020; it remains disconnected from every
   product and private package.
4. **Resolver and Classic fallback.** Retain logical path in texture imports,
   hash actual GTI bytes, resolve candidates, and prove that every individual
   failure continues through the byte-identical existing GTI path. Keep
   effective mode forced to Classic in products. Implemented: digest-only
   alternatives are disabled by default, base bytes are authenticated with the
   manifest checksum, and all failures are fixed path/checksum-free reasons.
   A successful candidate deliberately stops before PNG decoding.
5. **RGBA8 PNG preparation.** Select and license-review a PNG-only decoder,
   enforce RGBA8/IHDR and mip-chain contracts, add checked CPU accounting, and
   return `PreparedTextureAsset` without native GPU code. Implemented: the
   hash-pinned, zlib-licensed LodePNG memory decoder is compiled without its
   encoder, disk I/O, ancillary-chunk, or error-text surfaces. The portable
   boundary reauthenticates the base, requires non-interlaced 8-bit RGBA IHDR,
   validates CRC/decode, exact natural dimensions, straight-alpha metadata,
   declared `mip-00.png` through the 1x1 level, byte-identical base/mip zero,
   the absence of the next numbered mip, and independent per-level, chain,
   in-flight, and decoded-byte budgets. Non-zero mips remain explicitly
   structural-only because the current manifest has no per-mip digests.
6. **Windows opt-in pilot.** Add session-only `--texture-pack-root` and
   `--texture-mode enhanced`, D3D11 RGBA8 uploads, an initial bounded cache, and
   private capture comparison. A mission reload remains mandatory. No
   persistent UI claim yet.
7. **Persistent cross-platform setting and Windows reload.** The schema-4
   durable request, schema-1/2/3 Classic migration, requested/effective
   availability state, generic native UI, Windows startup resolution, and
   atomic Windows in-process mode-switch reload are implemented. Missing or
   invalid packages recover to Classic without mutating the active mission.
8. **Metal pilot and device budgets.** Implemented in source: owner-imported
   Application Support storage, durable AFTL locator recovery, the shared
   portable package session, availability-gated native UI, and the existing
   Metal RGBA8/mip upload transaction participate in one Enhanced mission
   reload. Hosted iPhoneOS and iPhoneSimulator compilation is green in PR
   #141; profiling on iPhone SE 3 and iPhone 17 Pro Max remains required before
   choosing final quality caps.
9. **Streaming.** Add generation-tagged priority jobs, resident mip ranges,
   cancellation, eviction, memory-pressure handling, and no-double-residency
   defaults. Validate deterministic simulation hashes throughout.
10. **Compressed derivatives.** Extend the private pipeline/schema with
    classified, checksummed BC7/BC3/ASTC payloads and choose them through the
    existing prepared-texture interface. Retain PNG RGBA8 and GTI fallbacks.

Lighting, PBR materials, normal maps, shadows, HDR, and post-processing remain
separate ADR-0013 stages. Texture replacement does not block the current
reconstruction and does not authorize those stages to infer unclassified
material semantics.

## Synthetic verification plan

Public tests use generated JSONL and generated tiny PNG byte arrays only. They
cover:

- empty/truncated/oversized manifest, line, string, array, record, and integer
  limits;
- wrong header/schema/scale/counts and trailing or duplicate JSON fields;
- malformed SHA-256, dimensions, alpha usage, sample space, status, and review
  status;
- rejected/manual-review records never entering either index;
- logical separator/case normalization, duplicate aliases, conflicting paths,
  path-first resolution, checksum confirmation, checksum alternative, and miss;
- absolute, drive, UNC/device, alternate-stream, dot-dot, control, and overlong
  paths;
- symbolic link, junction/reparse, type change, root escape, stale handle, and
  time-of-check/time-of-use replacement attempts;
- PNG signature/IHDR, exact RGBA8, dimensions, overflow, checksum, complete
  ordered mip names, exact chain to 1x1, extra/missing files, and mip-zero
  identity;
- opaque, binary, and translucent alpha metadata without automatic color-space
  selection;
- one corrupt or missing replacement falling back to GTI while other accepted
  replacements remain active;
- Classic behavior and bytes with no resolver configured;
- cache-key separation by mode, logical identity/checksum, manifest generation,
  GPU format/color view, mip policy, and resident mip range;
- CPU/GPU budget admission, integer overflow, cancellation, LRU eviction,
  pinned-resource protection, stale completion, and mode invalidation;
- schema-1/2/3 settings migration to Classic, Enhanced preference persistence,
  unavailable-package effective fallback, and reload-required state;
- path/checksum leakage guards for logs, exceptions, UI, and diagnostics;
- unchanged deterministic simulation hashes across Classic, Enhanced, mixed
  fallback, cache hit/miss, and streaming schedules; and
- repository-boundary rejection of images, JSONL, archives, private roots,
  cache databases, and GPU derivatives.

Private acceptance later compares the same scene in original, Classic, and
Enhanced texture modes on Windows D3D11 and both target iPhones. It measures
load latency, peak CPU staging, GPU residency, fallback counts, hitching, and
visual seams/halos without publishing owner content.

## Expected implementation files

The exact split may change during review, but the expected public surface is:

- `src/airfix/texture/TextureHdManifestIndex.hpp/.cpp` -- bounded JSONL records,
  reviewed-manifest validation, and immutable accepted-only indexes;
- `src/airfix/texture/TextureReplacementResolver.hpp/.cpp` -- normalized
  path/SHA lookup and fixed fallback decisions;
- `src/airfix/texture/PrivateTextureFileStore.hpp`, its platform-owner factory,
  and Windows/POSIX adapters -- root-confined regular-file capabilities;
- `src/airfix/render/PreparedTextureAsset.hpp/.cpp` -- backend-neutral RGBA8 and
  future compressed upload payloads;
- `src/airfix/render/TextureResidencyManager.hpp/.cpp` -- cache keys,
  partitions, budgets, generations, priorities, and eviction;
- `src/airfix/render/TextureRuntimePlan.*`,
  `src/airfix/content/LoadedTextureAsset.hpp`,
  `src/airfix/render/MissionWorldRoomTextureBindings.*`, and
  `src/airfix/content/MissionWorldRoomLoader.*` -- retain source identity and
  perform per-texture resolve-or-GTI preparation;
- `src/airfix/render/RenderPresentationSettings.*` and
  `src/airfix/settings/RenderPresentationSettingsCodec.*` -- later schema
  migration and durable requested mode;
- Windows command-line/content/settings files and `AirfixD3D11Renderer.*` --
  private root configuration, availability UI, cache, DXGI budget, and native
  upload;
- iOS content/settings files and `AirfixMetalRenderer.*` -- owner-local import,
  availability UI, aggregate-ledger admission, and native upload;
- CMake only for public source/test compilation and any reviewed decoder
  license, never private resources; and
- synthetic tests for every layer plus `.gitignore`, the public-boundary
  scanner, and its regression tests.

## Local-only files

The following remain outside Git and public CI:

- reviewed or intermediate JSONL manifests and validation receipts;
- original GTI/UP/AFPACK data and source inventories;
- exported PNGs, base HD PNGs, mip chains, correction layers, QA boards, and
  contact sheets;
- model executables, weights, provenance files, Python environments, and tool
  caches;
- BC7, BC3, ASTC, DDS, KTX/KTX2, or other GPU derivatives;
- Binary Ninja/Ghidra/Rizin/x32dbg project databases and runtime captures; and
- resolved package roots, local configuration, logs containing private data,
  and build-machine paths.

## Action items

1. [x] Inspect the existing GTI loader, texture binding, presentation settings,
       D3D11 upload, Metal heap publication, and current cache/budget state.
2. [x] Review the public pipeline contract and locally validate the reviewed
       corpus without copying private records into this repository.
3. [x] Record the architecture, fallback flow, security boundary, cache model,
       staged implementation, synthetic tests, and expected file changes.
4. [x] Land repository-boundary hardening as a separate preparatory change.
5. [x] Establish the shared RGBA8 sample-space/encoding contract without
       classifying legacy GTI or private replacements. Unclassified input stays
       UNORM, explicit sRGB and linear-data labels have native D3D11/Metal
       mappings, and unknown labels fail closed.
6. [x] Implement stage 2 as a bounded reviewed-manifest parser and immutable
       accepted-only index behind data-less synthetic tests. It performs no
       filesystem I/O and the product effective mode remains Classic.
7. [x] Implement stage 3 as the root-confined, generation-bound, path-redacted
       file capability in ADR-0020, behind synthetic files only and with no
       product caller.
8. [x] Implement stage 4: retain authenticated logical identity, hash actual
       GTI bytes, resolve the manifest-selected base file through ADR-0020,
       authenticate it, and prove exact per-texture Classic fallback under
       synthetic tests. Products remain forced to Classic.
9. [x] Implement stage 5 bounded PNG RGBA8/IHDR and mip-chain preparation with
       checked CPU accounting and no native GPU code.
10. [x] Run the Windows session-only RGBA8 pilot and private visual comparison.
        A 1920x1080 authenticated mission loaded 34 Enhanced textures with no
        Classic fallback; the owner-local captures remain outside Git.
11. [x] Add the portable persistent requested/effective state and schema-4
        migration after the fallback matrix and Windows pilot review.
12. [x] Add generic native UI and atomic Windows in-process mission reload.
13. [x] Add the iOS owner-import, durable private locator/session recovery, and
        atomic Metal mission-reload path without bundling private resources.
14. [ ] Validate the HD pilot and memory budgets on both target phones.
15. [ ] Add streaming and compressed derivatives only after device budgets and
        per-mip authentication are defined.
