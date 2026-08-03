# EXP-20260803-119: Windows session-only HD texture pilot

**Status:** implemented and locally validated; hosted cross-platform validation
pending publication

**Evidence boundary:** public code and tests use generated JSONL and in-memory
PNG RGBA8 only. One owner-local authenticated mission and reviewed package were
read-only acceptance inputs. Only aggregate counts are recorded here; no
private image, manifest record, logical path, checksum, configured root, or
capture is retained in Git.

**Decision:** GO for a Windows D3D11 session-only Enhanced pilot with exact
per-texture Classic fallback and a bounded reload cache; NO-GO for persistence,
iOS, streaming, automatic color classification, or compressed derivatives

## Question

Can the portable resolver and PNG preparer be connected to a complete Windows
mission transaction so accepted HD mip chains reach D3D11 while every package
or per-texture failure remains redacted, budgeted, and recoverable through the
unchanged GTI path?

## Implemented transaction

```text
explicit Windows session arguments
        |
        v
root-confined reviewed-manifest session
        |
        +-- package open fails --> fixed status + complete Classic mission
        |
        v
read authenticated GTI + normalize identity + SHA-256 confirmation
        |
        v
accepted manifest record + complete validated PNG RGBA8 mip chain
        |
        +-- any replacement/budget failure --> this texture's Classic GTI
        |
        v
complete room publication with mode/generation provenance
        |
        v
transactional bounded D3D11 cache and immutable texture views
```

- `TextureMode` is portable and independent from `VisualProfile`. Classic is
  always available and remains the default.
- Windows accepts Enhanced only when an explicit private root and a relative
  reviewed-manifest name accompany a complete mission launch. The selection is
  process-local, requires reload, and is forbidden in validation, smoke,
  import, and content-manager modes.
- Package construction owns the confined file store, accepted-only index, and
  resolver for one non-zero generation. It retains no root spelling and emits
  only fixed package categories.
- `MissionWorldRoomLoader` selects HD independently per GTI, accounts decoded,
  upload, resident, and published CPU bytes before committing it, and invokes
  the exact Classic preparation path after any replacement failure. A Classic
  GTI failure remains mission-fatal because no authenticated fallback exists.
- Publication validates aggregate Enhanced/Classic/fallback counts and every
  texture's mode, generation, and source-checksum provenance before a room can
  reach a backend.
- D3D11 prepares a complete candidate texture set before replacing the active
  room. The cache key includes mode, manifest generation, normalized logical
  identity, source digest, DXGI format, and mip policy. The cache is capped at
  512 entries and 256 MiB, reuses a same-generation view, evicts least-recently
  used unpinned entries, and clears the incompatible partition on mode or
  generation change.
- Stage 6 uploads unclassified data through RGBA8 UNORM and preserves authored
  offline mips. It does not infer sRGB, linear, normal-map, mask, or material
  semantics and does not introduce BC7, BC3, ASTC, or streaming.

## Schema compatibility correction

The first private read exercised a valid reviewed record whose representative
category was `mixed` while its `categories` array listed the concrete source
categories. The runtime parser had added an undocumented set-membership rule
not present in the public JSON Schema. It rejected the package and correctly
fell back to Classic.

The parser now validates the representative category and each concrete
category independently, retaining all schema enums and every identity, review,
path, dimension, status, and count check. A generated regression fixture proves
this exact public-schema case. The complete owner-local reviewed index then
parsed with no issue.

## Synthetic verification

- valid Enhanced plus missing-mip and decoded-budget failures in one mission;
- exact Classic fallback without mission abort or partial HD publication;
- mismatched/stale resolver generations and provenance/count publication
  rejection;
- CLI completeness, relative-manifest, mode-conflict, and restricted-command
  rules;
- package root/manifest failures mapped to fixed path-free categories;
- same-generation D3D11 view reuse, a 513-entry eviction witness, resident-byte
  accounting, and Classic partition invalidation; and
- schema-valid mixed representative category independent of the concrete
  category list.

All fixtures are generated at test time. They contain no original or derived
game asset.

## Private acceptance

The same authenticated mission was captured at a physical 1920x1080 backbuffer
and 100% render scale in Classic and Enhanced sessions. The Enhanced run
reported 34 Enhanced textures, zero Classic textures, and zero fallbacks. The
captured pixels differ and visibly preserve higher-frequency surface detail.
Both BMP files, runtime logs, and all package inputs remain owner-local and
outside Git.

## Current limits

- No persistent requested/effective setting or settings-panel control.
- No iOS/Metal package owner or device memory-profile acceptance.
- No cross-mission streaming, background decode, partial mip residency, or
  memory-pressure callbacks.
- No automatic sRGB selection or technical-channel interpretation.
- Current non-zero PNG mips are structurally validated but remain without
  individual authenticated digests under the existing private schema.
- The fixed cache caps are conservative pilot limits, not final hardware
  policy.

## Validation record

- Targeted MSVC parser, package, CLI, loader/publication, and D3D11 cache suites:
  PASS.
- Complete MSVC Windows product: PASS; 167/167 CTests, including D3D11/XAudio2,
  UI Automation, and native/scaled hidden product smokes.
- Complete portable C++20 build: PASS; 152/152 CTests.
- Synthetic boundary tests and the 811-file repository scan: PASS.
- Documentation integrity: PASS; 119 experiments and 408 catalogue rows.
- Rizin normalization: 12/12 PASS; CI-classifier tests and `git diff --check`:
  PASS.
- Hosted Linux/macOS/Windows, clangd, and unsigned iOS Actions remain the final
  publication gate.
