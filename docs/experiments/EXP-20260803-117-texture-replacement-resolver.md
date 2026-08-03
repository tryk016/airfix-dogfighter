# EXP-20260803-117: private texture resolver and exact Classic fallback

**Status:** implemented and locally validated; hosted cross-platform validation
pending

**Evidence boundary:** synthetic in-memory GTI, manifest, and encoded byte
strings only; no private manifest, image, original game asset, configured
package, local path, or derived resource was read or retained

**Decision:** GO for ADR-0019 stage 4; NO-GO for PNG/mip preparation, product
configuration, effective Enhanced textures, cache/streaming, or native upload

## Question

Can the runtime select one accepted HD base candidate from the exact GTI
identity while guaranteeing that every absence or failure continues through
the existing Classic decoder unchanged?

## Implemented boundary

```text
authenticated archive resolution
        |
        v
TextureImportRequest { dense ID, archive index, canonical logical path }
        |
        +---------------- resolver absent -------------------+
        |                                                     |
        v                                                     v
hash actual GTI bytes                              existing prepareGtiUpload
        |
accepted path record + exact source digest
        |
pinned-generation root capability reads manifest result.path
        |
verify output_png_sha256
        |
        +-- any failure --> existing prepareGtiUpload
        |
        +-- candidate --> owned encoded base bytes; stop before PNG decode
```

- `TextureImportRequest` retains the canonical archive logical path. Mission
  and player-actor global deduplication reject one physical archive index that
  arrives with inconsistent logical identities.
- `TextureReplacementResolver` is immutable and non-owning over one accepted
  manifest index and one root-confined file capability. Both share a non-zero
  package generation, and stale generations fail before source hashing or I/O.
- The primary key is normalized logical path. The actual GTI byte span is
  hashed and must equal `source_gti_sha256` before the selected base file is
  trusted.
- Source-digest-only lookup is an explicit alternative policy and is disabled
  by default because identical source bytes do not prove compatible color or
  technical-channel use.
- `result.path` is consumed exactly from the manifest and only through the
  pinned root capability. The resolver bounds encoded bytes and authenticates
  them against `output_png_sha256`.
- Results expose fixed enums only. No input path, host root, checksum, manifest
  method, or private record text is formatted for logs or UI.
- `prepareTextureSource` composes selection with the existing Classic path. If
  no resolver is configured or any resolver reason falls back, it calls
  `prepareGtiUpload` with the identical request, source span, and limits.
- Every current product caller supplies no resolver and consumes only the
  returned Classic preparation. Stage 4 therefore changes no product pixel or
  mission failure behavior.

## Synthetic verification matrix

The dedicated suite covers:

- separator/case-normalized path selection and exact source checksum;
- digest alternatives rejected by default and admitted only by explicit
  policy;
- invalid logical identity, source mismatch, and stale generation before file
  access;
- fixed mappings for unsafe path, missing file, unsafe indirection/type,
  multiply linked file, byte limit, changed-during-read, and I/O failure;
- corrupt base checksum and encoded-size limit returning no candidate bytes;
- byte- and plan-identical Classic preparation with no resolver and with a
  failed resolver; and
- successful selection stopping before either GTI fallback decoding or future
  PNG preparation.

## Validation record

- Fresh Windows GCC 15.2/Ninja compilation succeeds for the complete project.
- All 151/151 portable CTests pass, including the new synthetic resolver suite
  and the mission/player identity propagation regressions.
- A fresh MSVC 19.51/Ninja build compiles all 485 portable edges. All 149 native
  CTests pass; the two Python tests could not be launched by CTest inside the
  local sandbox, then pass directly as 9/9 and 8/8. The five resolver/identity
  suites pass again from the MSVC tree.
- `clang-tidy` with Clang analyzer and bugprone checks is clean for the new
  production resolver; new source/header/test files also pass clang-format in
  warnings-as-errors mode.
- Documentation integrity covers 117 experiment reports and 408 catalogue
  rows; 12/12 Rizin exporter tests, synthetic boundary regressions, and the
  actual 801-file public-boundary scan pass.
- Clean-clone Linux/macOS/iOS and hosted validation remain publication gates.

## Decision

**GO** for the isolated stage-4 resolver and exact Classic fallback boundary.

**NO-GO** for declaring any replacement renderable. The authenticated bytes
are still encoded PNG data. Stage 5 must independently validate PNG signature,
IHDR, RGBA8 shape, manifest dimensions, full mip-chain structure, mip-zero
identity, and checked decoded-memory budgets before any backend can consume a
replacement.
