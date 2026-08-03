# EXP-20260803-118: bounded private HD PNG preparation

**Status:** implemented and locally validated; hosted cross-platform validation
pending

**Evidence boundary:** generated in-memory PNG byte arrays, synthetic manifest
records, and synthetic file-store responses only; no private manifest, image,
original GTI, logical path, checksum, configured root, or derived owner asset
was read or retained

**Decision:** GO for ADR-0019 stage 5 as a disconnected portable preparation
boundary; NO-GO for product Enhanced selection, persistent settings, cache,
streaming, D3D11/Metal upload, or compressed derivatives

## Question

Can an authenticated replacement candidate become a complete backend-neutral
RGBA8 mip asset while failing closed on malformed images, incomplete chains,
stale private storage, or any encoded/decoded CPU budget violation?

## Implemented boundary

```text
stage-4 authenticated candidate + pinned root generation
        |
        v
revalidate base SHA-256 + 4x/sample-space/alpha/mip metadata
        |
        v
read manifest mipmap_directory/mip-NN.png through root capability
        |
        +-- mip-00 bytes must equal authenticated result.path bytes
        |
        v
PNG signature + IHDR: 8-bit RGBA, compression/filter 0, non-interlaced
        |
        v
memory-only PNG decode + CRC + exact dimensions/row pitch/alpha contract
        |
        v
checked per-level + chain + peak encoded + decoded CPU accounting
        |
        +-- any failure --> fixed redacted issue; no partial asset
        |
        `-- success --> PreparedTextureAsset; no GPU or product publication
```

- `prepareTextureHdPng` accepts only a non-zero candidate/file-store generation
  match and independently checks source identity, base checksum, exact 4x
  dimensions, natural mip count, `encoded-unclassified`, and a valid
  opaque/binary/translucent alpha enum before reading the chain.
- Paths are derived from the manifest-provided mip directory plus the public
  `mip-NN.png` chain convention. They are still passed through the root-confined
  no-follow file capability. No path or file-store message reaches diagnostics.
- Signature and IHDR checks happen before decode. Stage 5 admits only PNG color
  type 6, bit depth 8, compression/filter method 0, and non-interlaced data.
  LodePNG then validates the encoded stream and CRC while producing straight
  RGBA8 bytes in memory.
- Expected width and height halve independently, clamp at one, and must end at
  1x1 with the manifest's exact declared level count. Every expected level is
  required and the first undeclared numbered level must be absent.
- Alpha bytes are checked against manifest metadata: opaque requires 255,
  binary admits only 0/255, and translucent retains all straight-alpha values.
  No premultiplication or color-space conversion occurs.
- The output remains `technicalUnclassified`; Stage 5 does not infer sRGB,
  linear, normal, mask, material, or compression semantics.
- The manifest authenticates only `result.path`. Because `mip-00.png` must be
  byte-identical, level zero is `authenticatedBase`; non-zero levels are
  `structurallyValidated` and are not represented as cryptographically proven.
- The runtime checks the complete declared numbered chain and rejects the next
  numbered file. It intentionally does not enumerate unrelated directory
  entries; the private offline corpus verifier retains the exact-directory
  inventory gate.

## Decoder and license review

- Official source: `lvandeve/lodepng`.
- Pinned commit: `ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a`, dated
  2026-05-28. The repository publishes no tag or GitHub Release for this point
  and the commit is unsigned.
- Verification policy: CMake downloads only the official commit archive and
  requires SHA-256
  `c2459a3f9145258f901d262576f7a56ca08087d3b3efeee3ae033c0952120803`.
- License: zlib. The exact upstream license is copied beside the Windows
  product and into the iOS bundle; CI requires both product staging paths.
- Compiled surface: decoder and zlib/PNG core with memory input. Encoder, disk
  I/O, ancillary chunks, and error text are disabled. Source/build outputs are
  fetched into ignored build trees and are not vendored.

## Synthetic verification matrix

The dedicated suite generates tiny PNG streams in memory and covers:

- complete 4x4 -> 2x2 -> 1x1 decoding, exact pixels, row pitches, totals, and
  integrity labels;
- invalid signature, IHDR length, color type, interlace, dimensions, and CRC;
- missing middle mip, base/mip-zero mismatch, and an undeclared next numbered
  mip;
- corrupt source identity, invalid alpha enum, invalid scale, and stale
  generation before unsafe publication;
- opaque, binary, and translucent straight-alpha contracts;
- dimension, per-level encoded, total encoded chain, peak encoded in-flight,
  and aggregate decoded RGBA limits; and
- fixed mappings for invalid path/argument, stale generation, unsafe
  indirection/type, multiple links, size, changed-during-read, and I/O failure.

## Validation record

- Fresh Windows GCC 15.2/Ninja complete build: PASS; 152/152 CTests PASS.
- Fresh MSVC 19.51/Ninja complete portable build: PASS; 152/152 CTests PASS.
- Full MSVC Windows product build: PASS; 166/166 CTests PASS, including D3D11,
  XAudio2, native/scaled product smokes, and UI Automation.
- Staged Windows LodePNG license SHA-256 equals the fetched upstream license
  SHA-256 exactly.
- `clang-tidy` Clang analyzer and bugprone checks: no finding in the new
  production source. Portability output is limited to the repository's
  established `#pragma once` policy.
- New production/header/test files pass project clang-format. Repository
  boundary, documentation integrity, Linux/macOS/iOS, and hosted product checks
  remain publication gates until the branch is pushed.

## Decision

**GO** for a complete, budgeted, backend-neutral Stage 5 asset produced only
after the declared PNG chain validates.

**NO-GO** for user-visible Enhanced textures. Stage 6 must add an explicit
session-only Windows opt-in, mandatory reload boundary, conservative cache,
D3D11 RGBA8 upload, private comparison captures, and exact per-texture Classic
fallback without exposing owner paths or checksums.
