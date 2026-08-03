# EXP-20260803-112: bounded reviewed HD manifest index

**Status:** implemented and locally validated

**Evidence boundary:** synthetic in-memory JSONL only; no private manifest,
image, original asset, or filesystem root was read

**Decision:** GO for ADR-0019 stage 2; NO-GO for filesystem resolution, PNG
loading, product settings, or enabling Enhanced textures

## Question

Can the portable C++20 core validate the final reviewed-manifest contract and
build an accepted-only replacement index without exposing private values or
granting it filesystem access?

## Contract

`parseTextureHdManifest` consumes caller-owned bytes and returns either one
immutable index or redacted issues. It requires one schema-1 reviewed-corpus
header followed by result records and enforces configurable ceilings for input,
line and record counts, JSON nesting and containers, strings, logical paths,
dimensions, and mip levels.

The parser rejects duplicate JSON keys, malformed UTF-8/JSON, inconsistent
corpus or review identity, non-lowercase SHA-256 values, unsafe paths, duplicate
logical identities, duplicate source digests, unexpected record/header fields,
and declared-count mismatches. Result dimensions must be exactly 4x their
source dimensions and `generated_mipmaps` must describe the complete natural
chain through 1x1.

Only a record whose top-level status and explicit review status are both
`accepted` enters the index. Rejected and manual-review records remain counted
for header validation but are not queryable. Logical paths use the same slash
and ASCII-case normalization for insertion and lookup. Source GTI SHA-256 is an
independent lookup key.

## Privacy and product boundary

Diagnostics contain only a stable issue enum and, where available, a one-based
JSONL line number. They contain no diagnostic text, field value, logical path,
checksum, or local path derived from input.

The component accepts no root path and performs no filesystem I/O. It cannot
open PNGs, inspect links/reparse points, allocate GPU resources, mutate caches,
or change settings. No runtime consumer is connected, so Classic GTI remains
the only effective product path.

## Synthetic validation

The in-memory suite covers:

- accepted-only retention, rejected/manual exclusion, normalized path lookup,
  digest lookup, and required metadata;
- malformed JSON/UTF-8, duplicate keys, schema/local-only/header/status/review/
  corpus/count inconsistencies, forged sample-space/category/model/QA values,
  invalid dimensions and incomplete mip chains;
- traversal, absolute and malformed relative paths, invalid digests, duplicate
  normalized logical paths, and duplicate source digests; and
- input, line, line-count, record, container, depth, per-string, total-string,
  logical-path, dimension, and mip ceilings.

No test reads a private manifest or image. The new target links only existing
portable repository libraries.

## Validation

- Dedicated GCC 15.2/Ninja parser build and synthetic CTest pass.
- A fresh complete GCC 15.2/Ninja build succeeds and all 144/144 portable
  CTests pass.
- Visual Studio 2026 MSVC 19.51/Ninja compiles and links the complete
  SDL3/D3D11/XAudio2 product and all test targets.
- All 158/158 Windows CTests pass, including both data-less product smokes.
- Public-boundary regression tests, full repository scanning, documentation
  link checks, and `git diff --check` remain publication gates.

## Decision

**GO** for the bounded parser and immutable accepted-only index as ADR-0019
stage 2.

**NO-GO** for accepting a private root, resolving or decoding files, enabling
Enhanced mode, publishing a replacement texture, or changing Classic caches.
Those capabilities require the separate stage-3 root-confined file capability
and the later resolver/fallback gates.
