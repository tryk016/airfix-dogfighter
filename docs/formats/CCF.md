# FMT-CCF — chunked scene/model container

**State:** file/root/top-level, material, mesh geometry, blueprint graph, and
placed-scene record decoding implemented; placed-room graph resolution pending

**Confidence:** 3/3 for framing and blueprint hierarchy, 2/3 for remaining
section semantics

**Evidence:** `EV-20260721-017`, `EV-20260721-018`, `EV-20260721-023`,
`EV-20260721-024`, `EV-20260721-025`, `EV-20260721-033`,
`EV-20260721-037`

## Evidence

`CcRoom::LoadScene` dispatches `.ccf` to `CcRoom::LoadSceneCcf` in `Cc.dll`.
The loader checks the magic/version below and uses `CcChunkReadStream`.
`OpenChunk` reads a 16-bit ID and 32-bit total length; `CloseChunk` seeks to
`chunkStart + totalLength`, proving that the length includes the six-byte chunk
header. The portable parser validated all 286 CCF signatures in the selected
content (285 `.ccf` names plus one extension-less scene), including every
top-level section's complete direct-child sequence.

The CC0-licensed community project
[`RonnyReverse/cc-tools`](https://github.com/RonnyReverse/cc-tools/tree/e34efcd858ec4475fa03d3f8668fa4e26f9e780e)
provides an independent Kaitai/Python shape map for CCF and related formats.
`EV-20260721-024` audited pinned commit `e34efcd858ec4475fa03d3f8668fa4e26f9e780e`.
It is useful hypothesis input, not a reference parser: its CCF support is
explicitly incomplete, it has no resource/depth limits, and several field names
remain unknown. Every adopted structure is still checked against loader code
and the complete selected Airfix corpus.

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
`0x4300`. Static loader analysis now proves that they have different roles:
`0x3000` contains prototypes and `0x4000` contains already placed scene nodes.
Equal counts do not establish, and the portable code does not assume, an index
mapping between the two tables.

## Material records (`0x2100`)

`EV-20260721-023` recovered and implemented every observed material record.
The corpus has the following strictly ordered property sequence:

```text
0x2100 material:
  0xF010 CcName
    0xF020 name
    0xF020 prefix
  u32 materialReference
  [0x2110 primaryTexture]
  [0x2111 secondaryTexture]
  [0x2120 environmentTexture]
  0x2140 vector properties
  0x2150 flag properties
  0x2151 mode property
  0x2152 scalar properties
```

Each texture property wraps one `0xF020` string. Its `u32` byte count includes
the terminal NUL for non-empty text. The shipped writer encodes an empty
`CcString` specially as count zero with no NUL; 5,043 material prefixes use
that representation, while no material name is empty. The portable parser
accepts both forms, rejects embedded/missing NULs for non-empty strings, and
limits text to 4,096 content bytes.

The meanings of the remaining numeric fields are intentionally not guessed:

| ID | Exact observed payload contract | Presence |
|---:|---|---:|
| `0x2110` | one `0xF020` primary texture string | 10,256 |
| `0x2111` | one `0xF020` secondary texture string | 0 (loader-supported) |
| `0x2120` | one `0xF020` environment texture string | 263 |
| `0x2140` | two 18-byte `0xF030` vec3 chunks, then one float32 | 10,385 |
| `0x2150` | two bytes, then one u32 | 10,385 |
| `0x2151` | one byte | 10,385 |
| `0x2152` | one u32, then two float32 values | 10,385 |

All 10,385 records match one of four exact sequences: 10,069 have primary
texture only, 187 have primary plus environment, 76 have environment only,
and 53 have neither optional texture. No selected asset contains a secondary
texture, but the original loader recognizes the field, so the portable model
retains it.

Malformed bounds, duplicate known properties, invalid fixed sizes, and an
invalid nested vec3 shape are rejected. Loader-compatible unknown, reordered,
or absent properties are retained in the chunk index without invalidating the
material; this separates format compatibility from the stricter corpus profile.
Names, prefixes, references, and the three optional texture dependencies are
exposed as metadata; uninterpreted numeric material values are not yet promoted
to runtime semantics. `cc-tools` also maps an unobserved `0x2153` shape, which
remains preserved as unknown until an Airfix sample or loader path confirms it.

## Mesh records (`0x3100`)

`EV-20260721-025` implements the complete geometry-bearing shape observed in
all 6,995 mesh records. The fixed prefix is:

```text
0x3100 mesh:
  0xF010 CcName(name, prefix)
  u32 meshReference
  u8 selectionFlagA
  u8 selectionFlagB
  u32 linkReference
  0xF040 position                 // 3 × float32
  float32 scalar
  0xF070 orientation
    0xF050 matrix
      3 × 0xF040                 // three stored matrix columns
  child chunks
```

The fixed part after `CcName` is exactly 98 bytes. The corpus has 6,991/4
values `0/1` for flag A and 6,841/154 for flag B. `linkReference` is zero in
1,150 records. Coordinate and matrix application are now established below;
flag and `scalar` semantics remain deliberately raw.

### Vertices (`0x3110`)

Every vertex begins with one 18-byte `0xF040` position. Its schema-driven
children are an optional second `0xF040`, loader-supported but unobserved
`0xF030`, and `0x4500` containing one u32.

| Observed vertex shape | Count |
|---|---:|
| position, `0x4500` | 65,395 |
| position, optional `0xF040`, `0x4500` | 70,968 |
| **Total** | **136,363** |

Every observed `0x4500` value is zero. Meshes contain 3-264 vertices. The
portable representation keeps both optional vectors and the raw `0x4500`
value without assigning normal/tangent semantics.

### Triangles (`0x3120`)

Each triangle begins with three u32 vertex indices and one u32 material
reference. Optional children are paint metadata followed by texture coordinates:

| Observed triangle shape | Count |
|---|---:|
| indices/material only | 96 |
| `0xF090` paint only | 1,524 |
| `0xF060` six-float UV only | 74,519 |
| paint plus UV | 93,900 |
| **Total** | **170,039** |

`0xF090` type 1 contains one `0xF030` vec3 (308 records); type 3 contains
three (95,116 records). The loader also supports type 4 with three vec3 values,
but the corpus does not use it. Every triangle index is validated after the
final vertex table is known; all shipped indices are in range and the maximum
is 263. Indices remain u32 in the portable model.

### Coordinate, matrix, winding, and UV contract

`EV-20260721-030` establishes a right-handed `+X` right, `+Y` up, `+Z` forward
source basis. The three stored orientation vectors are matrix columns used by
legacy row-vector algebra; a conventional runtime column-vector matrix uses
their mathematical transpose. The exact recovered face normal is normalized
`cross(p2-p0, p1-p0)`, with `+Y` for a degenerate face. Under a general basis,
normal directions use its inverse transpose. Identity conversion preserves
positions, index order, UVs, translation, and the raw `scalar`; a reflected
basis swaps indices 1 and 2 once to preserve facing.

No UV V flip or physical unit scale is inferred. Projection, Metal clip depth,
viewport parity, and final `MTLWinding` are render/camera decisions rather than
asset conversion. Exact RVAs, formulas, unresolved fields, and synthetic
fixtures are recorded in `docs/re/systems/COORDINATE-CONTRACT.md`.

### Mesh control leaves

- `0xF0A0`: one u32, present in all 6,995 records and always zero in this
  corpus; the loader passes it to `SetVampireMode`, so other values remain
  format-valid;
- `0xF0B2`: one byte, present in all records (6,985 ones and 10 zeroes);
- `0xF0D0`: optional u32 plus two float32 values in 3,428 records; finite large
  sentinel values are preserved bit-faithfully;
- `0x4501`: loader-supported u32 with zero corpus occurrences.

The two observed mesh child sequences differ only by optional `0xF0D0`.
Compatibility parsing does not require corpus order or completeness: bounded
unknown children are retained, known singleton duplicates and invalid exact
sizes are rejected, and per-mesh geometry plus a shared 250,000-descriptor
budget prevent nested allocation amplification. Unknown paint types remain
opaque; known types require their loader-read color prefix and tolerate a
`CloseChunk`-skipped extension tail.

## Blueprint index and object selection

`EV-20260721-027` extends the ordered `0x3000` table to all 9,328 blueprints:

| Kind | ID | Records | Portable geometry |
|---|---:|---:|---|
| mesh | `0x3100` | 6,995 | `CcfMeshMetadata` |
| null node | `0x4200` | 2,130 | index/common prefix only |
| light | `0x4300` | 203 | index/common prefix only |

Null and light records share `CcName`, three u32 references/link fields,
`0xF040` position, one float32 scalar, and the same `0xF070/F050/3×F040`
orientation shape. The fixed tail after `CcName` is exactly 100 bytes. Their
bounded child sequences remain available in the generic chunk index; light/null
effect payload semantics are deferred.

Blueprint names are not globally unique: 29 CCF files contain exact duplicate
names, including 18 groups spanning different kinds. The portable index
therefore preserves physical order and every candidate rather than using a
single-value map. The complete graph census finds no duplicate current
references within any CCF.

Object resolution is CCF-scoped: resolve `CCFF`, then match `MESH` against
`CcName.name` with bytewise ASCII case folding. Prefix is not a fallback.
Across all 215 object definitions, 90 select a mesh, 124 select a null, and one
has no `MESH` and deliberately receives no guessed fallback. Fourteen matches
require case folding. All selected names are unambiguous.

For selected meshes, each triangle's u32 material reference is joined to
`CcfMaterialMetadata.reference`, never treated as a vector index. The initial
direct-mesh-root diagnostic resolved 212 distinct-per-object material uses and
210 texture edges with no missing or ambiguous reference. Primary, secondary,
and environment roles remain separate. Complete subtree totals follow below.

### Blueprint hierarchy and object instancing

`EV-20260721-033` establishes that the u32 previously retained as a link is a
parent reference in every `0x3000` blueprint kind. A zero value denotes a root;
otherwise it names the parent's `currentReference`. Across all 286 CCF files,
the 9,328 blueprints form 2,062 roots and 7,266 parent edges with maximum depth
7. The read-only census found zero missing parents, duplicate references, or
cycles. Children retain physical file order.

The position and `0xF050` orientation stored in each blueprint are authored
world transforms. The legacy loader first creates all prototypes unparented,
then calls `CcSrtNode::SetParent(child, parent)` at RVA `0x0000E0F0`.
`SetParent` preserves both effective world transforms while deriving the
child's parent-relative local transform. `GetWorldRelation` at RVA
`0x0000E440` recursively composes parent first, and the exact SRT composition
path is `CcSRT::InheritParentSRT` at RVA `0x0002BFD0`. The formulas and the
F050 scalar limitation are recorded in
`docs/re/systems/COORDINATE-CONTRACT.md`.

Object definitions do not instantiate the parallel `0x4000` table. The
mission path loads an object's CCF with flag `0x2000`, which skips placed scene
records, selects a `0x3000` blueprint by the case-folded `MESH` name, and calls
`CcBlueprint::MakeInstance(..., true)` at RVA `0x000244F0`. That call creates
the selected node and its descendants only; it does not include ancestors.
Consequently a null blueprint is a valid transform-group root and cannot be
replaced by an arbitrary mesh child.

When a scene load does include `0x4000`, its object/null/light records form a
separate placed-node graph. The loader likewise defers nonzero parent links
until the table is complete and applies the same `SetParent(child, parent)`
contract. The records are decoded below; graph/reference resolution remains a
separate step, and no correspondence to the same physical index in `0x3000` is
inferred.

A bounded read-only traversal of all 215 object selections covers 2,687
selected blueprint nodes and 1,858 mesh instances, resolving 2,842 material
uses and 2,959 texture edges. All 2,959 texture paths resolve uniquely, with
zero missing, invalid, or ambiguous graph, material, or texture dependencies.
The selected roots remain 90 mesh, 124 null, and one object without a selector.
The earlier 212-material/210-texture result covered only directly selected mesh
roots and is retained as historical evidence rather than the complete object
subtree result.

A private grouped-aircraft diagnostic selects and renders all 46 nodes: 25
meshes and 21 null groups, 663 triangles total, and maximum subtree depth 3.
The full assembly is visually coherent, and the raw-V policy preserves its
markings while the explicit flipped-V diagnostic does not. These anonymous
aggregate/parity facts are the only private-model result recorded publicly; no
asset name, path, hash, geometry, or image is included.

Material texture strings are base names. `GtTextureGroup::AddTexture` at RVA
`0x00046C30` combines an object's `TEXU` search root with each source as
`root\\source.gti` before opening it. The bounded portable resolver implements
that exact rule for the 2,959 complete-subtree texture edges; it neither guesses
alternate roots nor treats an existing extension specially.

## Placed scene records (`0x4000`)

`EV-20260721-037` recovers the complete loader-read prefix and observed direct
children for the independent placed-node table. Every record begins with
`0xF010 CcName`, a current reference, room reference, parent reference, an
`0xF040` position, one raw float32 scalar, and `0xF070` orientation. Object
records (`0x4100`) additionally store a mesh reference, one raw byte, a portal
type word, and a portal-room reference before the transform.

The orientation loader accepts either an `0xF050` matrix containing three
`0xF040` vectors or one alternate `0xF040` vector. All 9,328 shipped records use
the matrix form; the alternate form remains supported and neutrally represented
because it is present in the original loader but not in the selected corpus.

| Record | Corpus count | Decoded direct children |
|---:|---:|---|
| `0x4100` object | 6,995 | `0xF0B0`, `0xF0B1`, `0x4501` u32; opaque `0x4101` BSP container |
| `0x4200` null | 2,130 | `0x4210` length plus zero-copy opaque bytes; `0x4500` u32 |
| `0x4300` light | 203 | bounded `0x4310`, `0x4320`, `0x4330`, and `0xF0B0` shapes |

The light values and optional texture strings retain neutral field names until
their rendering semantics are proven. `0x4101` is deliberately preserved
without recursively materializing its BSP tree. Known singleton duplicates,
invalid fixed sizes, string/count mismatches, parent-container overruns, and
the shared descriptor budget fail closed. A separate 100,000-record ceiling
precedes placed-node growth.

Read-only validation parses all 286 CCF files, including the extension-less
scene: 9,328 records in physical order, with zero parse errors. The portable
model exposes current/mesh/room/parent/portal references but does not yet apply
legacy room fallback, resolve the placed parent graph, or infer semantic names
for raw flags and light values.

## Portable implementation and next step

- file/root/top-level/direct-child/material/mesh parser:
  `src/airfix/assets/LegacyFormats.*`;
- synthetic bounds, strings, geometry, index, UV, opaque/extended paint,
  unknown-extension, and global descriptor-budget tests:
  `tests/LegacyFormatsTests.cpp`;
- bounded blueprint graph and subtree dependency resolution:
  `src/airfix/assets/CcfBlueprintGraph.*` and
  `src/airfix/assets/AssetResolver.*`;
- ignored decompilation evidence: `artifacts/ghidra/Cc.dll.*`.

The backend-neutral seam-safe draw-model payload, bounded blueprint graph, and
multi-instance diagnostic render a complete grouped aircraft from authored
world transforms. Portable parent-relative local derivation now round-trips the
recovered parent-first composition, and placed records are decoded. The next
scene step is bounded placed-graph and room-reference resolution. No proprietary
geometry or preview is committed.
