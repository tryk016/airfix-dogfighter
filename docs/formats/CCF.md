# FMT-CCF — chunked scene/model container

**State:** file/root/top-level and direct-child chunk indexes implemented;
material/mesh payload decoding in progress

**Confidence:** 3/3 for framing, 2/3 for section semantics

**Evidence:** `EV-20260721-017`, `EV-20260721-018`

## Evidence

`CcRoom::LoadScene` dispatches `.ccf` to `CcRoom::LoadSceneCcf` in `Cc.dll`.
The loader checks the magic/version below and uses `CcChunkReadStream`.
`OpenChunk` reads a 16-bit ID and 32-bit total length; `CloseChunk` seeks to
`chunkStart + totalLength`, proving that the length includes the six-byte chunk
header. The portable parser validated all 286 CCF signatures in the selected
content (285 `.ccf` names plus one extension-less scene), including every
top-level section's complete direct-child sequence.

## File header and root

All integers and floats are little-endian.

| Offset | Size | Field | Value/meaning |
|---:|---:|---|---|
| `0x00` | 4 | magic | ASCII `CcFf` |
| `0x04` | 4 | version | `0x98092901` |
| `0x08` | 2 | root ID | `0x0001` |
| `0x0A` | 4 | root total size | includes root's 6-byte header |
| `0x0E` | ... | root payload | child chunk sequence |

Every selected file satisfies `8 + rootTotalSize == physicalSize`.

## Generic nested chunk

| Offset | Size | Field |
|---:|---:|---|
| `+0x00` | 2 | chunk ID |
| `+0x02` | 4 | total size, including this header |
| `+0x06` | `size - 6` | payload and/or nested chunks |

Container/leaf meaning is schema-driven; it cannot safely be inferred from the
numeric ID alone. A parser tracks parent end offsets and checks every addition,
minimum length, child boundary, and close position.

## Top-level sections

All 286 selected files have exactly this ordered root shape:

```text
0x1000, 0x2000, 0x3000, 0x4000
```

Static loader behavior gives these provisional meanings:

| ID | Role | Confirming operations |
|---:|---|---|
| `0x1000` | rooms/spatial structures | creates/finds `CcRoom`, loads BSP trees |
| `0x2000` | materials | `0x2100` creates `CcMaterial`; texture/env-map children |
| `0x3000` | meshes/blueprints | `0x3100` reads vertices, polygons, UVs and limb links |
| `0x4000` | placed scene nodes | creates objects, lights, nulls and references |

Known nested IDs already recovered include material `0x2100`, texture children
`0x2110/0x2111/0x2120`, mesh `0x3100`, vertex `0x3110`, polygon `0x3120`, room
`0x1100`, and several optional extension IDs in the `0xF000` range. Exact field
contracts remain to be recorded before implementation.

### Direct-child corpus census

`EV-20260721-020` indexed the direct children without interpreting leaf
payloads. The counts below cover all 286 CCF files and provide hard bounds for
future allocation and fixture design.

| Section | Direct IDs observed | Total children | Per-file range |
|---:|---|---:|---:|
| `0x1000` | `0x1100` | 392 | 1-8 |
| `0x2000` | `0x2100` | 10,385 | 1-435 |
| `0x3000` | `0x3100`, optional `0x4200/0x4300` | 9,328 | 1-373 |
| `0x4000` | `0x4100`, optional `0x4200/0x4300` | 9,328 | 1-373 |

The `0x3000` and `0x4000` direct-child counts match in every file. Their ID
sets occur in the same three corpus-wide groups: 100 files contain only the
primary mesh/object IDs, 157 also contain `0x4200`, and 29 also contain
`0x4300`. This is strong structural evidence for paired blueprint/placed-node
tables, but the parser does not yet assume one-to-one semantic correspondence.

## Portable implementation and next step

- file/root/top-level/direct-child parser: `src/airfix/assets/LegacyFormats.*`;
- synthetic bounds/length tests: `tests/LegacyFormatsTests.cpp`;
- ignored decompilation evidence: `artifacts/ghidra/Cc.dll.*`.

The next parser layer will decode one `0x2100` material and one `0x3100` mesh
without constructing a runtime scene. A diagnostic converter can then emit one
model's positions, indices, UVs, material names, and GTI dependencies for a
first Metal or desktop renderable fixture.
