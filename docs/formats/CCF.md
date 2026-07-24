# FMT-CCF — chunked scene/model container

**State:** file/root/top-level, room/fog/BSP, material, mesh geometry, blueprint
graph, placed-scene decoding, and bounded spatial reference resolution
implemented

**Confidence:** 3/3 for framing and blueprint hierarchy, 2/3 for remaining
section semantics

**Evidence:** `EV-20260721-017`, `EV-20260721-018`, `EV-20260721-023`,
`EV-20260721-024`, `EV-20260721-025`, `EV-20260721-033`,
`EV-20260721-037`, `EV-20260721-038`, `EV-20260721-039`

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

## Room records (`0x1100`)

`EV-20260721-038` confirms the bounded room prefix shared by the loader and the
independent `cc-tools` shape map:

```text
0x1100 room:
  0xF010 CcName
    0xF020 name
    0xF020 prefix
  u32 roomReference
  child chunks...
```

The first physical room record is special: the loader registers the receiving
world/root room under its stored reference. Later records find or create named
rooms. `CcfRoomMetadata::primaryBinding` retains that distinction without
constructing runtime rooms during parsing. Scene-load flag bit `0x20` skips the
complete room section. Bit `0x4000` does not control first-room ownership; it
only copies the first room's stored name/prefix onto the receiving room.

`EV-20260724-005` provides the gameplay lookup: mission setup
`AddStartPos("room", ...)` passes the authored name to
`CcWorld::GetRoomByName`. Rooms from the main and optional background CCF may
coexist in that world. The portable campaign normalizer maps an exact authored
name only within one selected `CcfMetadata::rooms` collection and retains its
physical index; this shipped-content normalization is not yet claimed as full
world-wide lookup parity. It never interprets the string as a World `ROOM`
record, CCF reference, or ordinal.

All 392 records across the 286 selected CCF files parse in physical order, with
one to eight rooms per file and no zero or duplicate references inside a file.
No private room names or geometry are recorded in the repository.

### Fog (`0x1101`)

Every room has one exact 36-byte fog chunk:

```text
0x1101 fog:
  u32 enabledRaw
  float32 first
  float32 second
  0xF030 color                       // 3 x float32
```

The portable model preserves the raw enable word and neutral scalar names. All
392 shipped records are disabled and contain `first = 1.0`, `second = 200.0`,
and a zero color, so fog must not be enabled merely because metadata exists.
Malformed sizes, duplicate fog chunks, and an invalid nested color shape are
rejected.

### Static and portal BSP (`0x1200`, `0x1201`)

`0x1200` owns static spatial trees and `0x1201` owns portal spatial trees. Each
contains zero or more `0xF0C0` roots. The loader also accepts a direct room
`0xF0C0` as a static tree; the selected corpus has no direct-root occurrence,
but the portable parser retains and tests that compatibility path. Multiple
roots are format-valid even though each shipped wrapper contains at most one.

An `0xF0C0` node has this exact loader-read prefix:

```text
0xF0C0 node:
  u32 childAPresenceRaw
  [0xF0C0 childA]                    // present when raw word is nonzero
  u32 childBPresenceRaw
  [0xF0C0 childB]                    // present when raw word is nonzero
  0xF040 splitNormal
  0xF040 pointOnPlane
  0..N x 0xF0C1 polygon descriptor
```

The A/B labels deliberately avoid an unproven front/back interpretation.
Minimum total node size is 50 bytes. Nodes are decoded iteratively into a flat
physical/preorder arena with optional child indices; this avoids call-stack and
recursive-ownership growth on hostile input.

Each `0xF0C1` descriptor is exactly 104 bytes:

```text
0xF0C1 polygon:
  0xF040 faceCross                   // unnormalized
  0xF040 faceNormal
  0xF040 point0
  0xF040 edge01
  0xF040 edge12
  u32 polygonIndex                  // zero-based in the placed object's mesh
  u32 placedObjectReference
```

These vectors are stored in world space. Exactly 234 descriptors use the
legacy quiet-NaN sentinel `0xFFC00000` in all three components of
`faceNormal`; the parser accepts and bit-preserves it. Consumers must treat
that triplet as a legacy sentinel and must not normalize or reject the complete
scene because of it. No other BSP float is non-finite in the selected corpus.

The bounded parser enforces a maximum tree depth of 1,024, 100,000 nodes,
250,000 polygon descriptors, and a shared 250,000 spatial-descriptor budget per
CCF before arena growth. Child-presence words accept any nonzero value, matching
the original Boolean test. Truncation, parent overruns, malformed vector or
polygon shapes, impossible child links, and descriptor/depth exhaustion fail
closed.

### Deferred BSP binding

BSP is an index over already placed geometry, not another geometry source.
After `0x4000` resolution, every polygon descriptor is joined strictly as:

```text
placedObjectReference
  -> unique instantiated placed object
  -> that object's uniquely resolved mesh
  -> mesh.triangles[polygonIndex]
```

A portal tree additionally requires the placed object's resolved
`portalRoomReference`; the `0x1201` wrapper itself stores no target room. The
ordinary resolved room of the object must equal the room owning the BSP tree.
The portable resolver returns only indices into existing metadata, never
geometry copies. Missing, ambiguous, inactive or non-object targets, missing
meshes, out-of-range polygons, room mismatches, absent portal targets, invalid
tree ownership, or limit failures clear all resolved bindings atomically.

The complete selected corpus contains 389 static wrappers, 392 portal wrappers,
98,095 nodes, and 198,210 polygon descriptors: 97,883/197,878 static and
212/332 portal. Maximum tree depth is 40, maximum nodes/descriptors in one tree
are 2,442/3,985, and all 198,210 deferred bindings resolve without an error.
Every polygon index is within its mesh and every ordinary room agrees with the
owning tree. All 332 portal descriptors resolve a nonzero portal-room target.

### Conservative explicit-room draw assembly

The portable room plan scans the canonical placed scene in physical CCF order.
It selects instantiated objects explicitly assigned to one caller-supplied
physical `CcfMetadata::rooms` index, while omitting null and light nodes. The
loader-compatible external-receiver fallback belongs only to room index zero.
Room BSP is deliberately not consulted: an empty, partial, or repeated spatial
descriptor cannot hide, duplicate, or reorder draw instances.

Meshes are shared by resolved mesh index in first-use order. Materials are
resolved uniquely by reference across the selected mesh triangles, preserving
primary, secondary, and environment texture dependencies. The render builder
creates one seam-safe `DrawMeshPayload` per unique mesh and one
`DrawMeshInstance` per selected placed object. Each instance uses its own
stored authored-world F050 transform exactly once; neither the prototype mesh
transform nor the placed parent graph is composed onto it again.

The loader-supported alternate placed F040 orientation remains semantically
unproven and fails closed with a typed issue. Non-finite or singular
transforms, missing/duplicate material bindings, invalid placed dependencies,
integer overflow, and per-mesh or aggregate mesh/instance/vertex/index/
material/range/byte limits likewise publish an empty model rather than a
partial room.

Read-only validation builds plans and backend-neutral draw payloads for every
physical room of all 286 scenes: 392 rooms, 6,995 instances and mesh slots,
10,461 material bindings, 10,519 texture dependency edges, 409,565 draw
vertices, 510,117 indices (170,039 triangles), and 20,211 ordered draw ranges.
A coverage bitmap proves that all 6,995 placed objects occur in exactly one
room plan. No plan, transform, geometry, material, limit, or coverage issue
occurs.

The 29 CCFs referenced directly by world definitions contain 135 rooms and
6,318 placed nodes. Explicit selection assigns all 4,911 objects exactly once
to 105 non-empty ordinary rooms; all 29 receiver bindings and one ordinary room
are valid empty plans. The receiver binding is not treated as a default
playable room, and BSP traversal is still not used to guess visibility.

The World `ROOM` table is a distinct domain and has no proven join to the CCF
room catalog. The selector's `ccfRoomIndex` always means a physical index into
`CcfMetadata::rooms`, never a World room position/ID or CCF reference. Mission
start setup is a separately proven name lookup on the runtime `CcWorld`; it
does not create a World-record-to-CCF join. Portable single-CCF normalization
remains narrower than that runtime lookup.

BSP culling, collision, and portal traversal remain later runtime features; no
geometry is hidden until those traversal semantics are separately proven.

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

Loader consumers narrow several otherwise raw child values:

- object `0xF0B1` is a Boolean dynamic-BSP enable/eligibility gate. Every one
  of the 1,160 shipped objects carrying a `0x4101` BSP block has it enabled;
- object and light `0xF0B0` form a shared Boolean lighting-classification gate,
  although a stable user-facing name remains unproven;
- object `0x4501` is a serialized limb-count/ordinal bound and null `0x4500`
  is a limb index/ID. Neither field is a parent reference. Both are absent from
  the selected placed records.

Read-only validation parses all 286 CCF files, including the extension-less
scene: 9,328 records in physical order, with zero parse errors. Every ordinary
room and mesh reference resolves uniquely. The 154 nonzero portal-room
references also resolve uniquely; 6,841 objects store zero and therefore have
no portal room. The portable model exposes current/mesh/room/parent/portal
references and resolves their scene dependencies without inferring semantic
names for the remaining raw flags and light values.

### Placed-scene reference graph

The bounded resolver reproduces the loader's deferred lookup after the complete
section is read. Instantiated placed nodes and loaded `0x3100` mesh prototypes
share the SRT-reference domain; `0x3000` null/light blueprints do not. A placed
object whose mesh reference is missing or ambiguous is not instantiated.
Parent, room, mesh, and portal-room references are resolved by value, never by
physical index. Missing ordinary rooms retain an explicit receiver/root-room
fallback, while a missing portal room remains unset. Duplicate candidates fail
closed instead of inheriting the legacy loader's list-order-dependent match.
Any structural parent error, cycle, or depth/edge-limit breach clears all
published roots and links atomically, so callers cannot traverse a partial or
out-of-policy graph. Objects skipped for unresolved meshes do not perform later
room or portal lookup, matching the loader's early-exit order.

The selected corpus resolves 9,328 nodes into 2,062 roots and 7,266 placed-
parent edges, with maximum depth 7 and no cycles, missing dependencies, or
ambiguities. All 6,995 object mesh references resolve uniquely. Seven parent
edges cross room boundaries, so room binding is resolved per node rather than
inherited from its parent.

## Portable implementation and next step

- file/root/top-level/direct-child/material/mesh parser:
  `src/airfix/assets/LegacyFormats.*`;
- synthetic bounds, strings, geometry, index, UV, opaque/extended paint,
  unknown-extension, and global descriptor-budget tests:
  `tests/LegacyFormatsTests.cpp`;
- bounded blueprint, placed-scene, and room-BSP graph/reference resolution:
  `src/airfix/assets/CcfBlueprintGraph.*`,
  `src/airfix/assets/CcfPlacedScene.*`,
  `src/airfix/assets/CcfRoomDrawPlan.*`,
  `src/airfix/assets/CcfRoomScene.*`, and
  `src/airfix/assets/AssetResolver.*`;
- bounded backend-neutral explicit-room assembly:
  `src/airfix/render/CcfRoomDrawAssembly.*`;
- ignored decompilation evidence: `artifacts/ghidra/Cc.dll.*`.

The backend-neutral seam-safe draw-model payload, bounded blueprint and placed
graphs, and multi-instance diagnostic render a complete grouped aircraft from
authored world transforms. Portable parent-relative local derivation
round-trips the recovered parent-first composition. Room fog/BSP decoding, all
deferred spatial bindings, and the conservative explicit-room draw assembly are
complete. The next scene step feeds the resolved texture IDs and
multi-mesh/multi-instance payload to Metal with BSP culling disabled. No
proprietary geometry or preview is committed.
