# FMT-AFCHUNK — FourCC definition/mission container

**State:** generic framing plus object, world, static/model level placement,
runtime instance-state, briefing, and recorded-path semantics implemented;
gameplay labels for several model/state slots remain intentionally neutral

**Confidence:** 3/3 for framing and implemented fields listed below

**Evidence:** `EV-20260721-022`, `EV-20260721-035`, `EV-20260721-036`

## Evidence and source quality

`AfChunkContainer::Read` and `AfChunkContainer::GetBuffer` in `AfEngine.dll`
establish the generic layout. `GetChunkByID` and `CountChunksByID` prove that
order and duplicate IDs are meaningful. `NfMission::LoadLevel` confirms world,
level, and object dependencies; `AfPathRecord::Load/Save` confirms the path
record family.

The community [Airfix DogFighter Programmer's Manual — Object File](https://airfixdogfighter.x10host.com/Programming%20Manual/filetypes/objectfile.php)
describes the same data generation and provides useful examples. It is treated
as a secondary source: every implemented rule was checked against loader code
and the complete selected corpus. Its example sizes and `GRAV`/`HIDE` counts
match exactly, while some generalizations do not (see below).

This format is unrelated to CCF's 16-bit IDs and inclusive six-byte chunk
lengths.

## Generic layout

All IDs and sizes are little-endian. No padding or alignment exists between
chunks, including after odd payload lengths.

```text
container:
  u32 rootFourCC
  u32 payloadSize       // physical file size minus 8
  chunks[payloadSize]

chunk:
  u32 chunkFourCC
  u32 payloadSize       // excludes this 8-byte header
  byte payload[payloadSize]
```

Zero-length chunks and duplicate IDs are valid. The portable parser requires
an exact root extent, a caller-provided chunk-count limit, complete child
headers/payloads, and overflow-safe offset arithmetic.

## Selected-corpus roots

Bounded parsing validated all 299 matching files and all 7,376 chunks in
`Resource.up` plus `English.up`.

| Root | Primary extension/role | Files | Chunks | Per-file range |
|---|---|---:|---:|---:|
| `HOUS` | `.world` | 29 | 2,015 | 69-71 |
| `FHOU` | `.level` | 31 | 3,854 | 33-217 |
| `MODL` | model/unit `.object` | 73 | 510 | 6-9 |
| `OBJE` | static/dynamic `.object` | 142 | 849 | 5-7 |
| `BRIF` | `.brf` | 23 | 146 | 1-8 |
| `PATH` | `.dat` flight path | 1 | 2 | 2 |

## Object definitions

Both `MODL` and `OBJE` use singleton child chunks. Text payload sizes include
the final NUL; localization references such as `@1234` remain raw strings for a
later localization resolver.

| ID | Portable field | Payload |
|---|---|---|
| `TYPE` | type | NUL string |
| `CATG` | category | NUL string; optional |
| `NAME` | name/localization reference | NUL string |
| `NATY` | nationality/team | NUL string; optional |
| `TEXU` | texture root | NUL path |
| `CCFF` | model scene | NUL CCF path |
| `MESH` | blueprint name | NUL string; absent in one shipped file |
| `GRAV` | gravity direction | exactly three float32 values; optional |
| `HIDE` | hidden marker | zero bytes; optional |

All 215 definitions pass the semantic parser. `GRAV` occurs in 56 files and
`HIDE` in 10, exactly matching the community manual. Important corrections:

- `CATG` is present in every `MODL` and in 111 of 142 `OBJE` files, so it is not
  valid to assume that `OBJE` lacks a category;
- `NATY` occurs in only 35 of 73 `MODL` files;
- `MESH` names the blueprint loaded from `CCFF`, as proven by
  `NfMission::LoadLevel`; the manual left it unresolved;
- `Game\Objects\Units\PuKeyBrown.object` is the sole definition without
  `MESH`, so the field remains optional in the portable model.

`TEXU` is a search root rather than a texture file. For each CCF material
texture source, `GtTextureGroup::AddTexture` constructs exactly
`TEXU\source.gti`; the suffix is always appended and no alternate root or
extension fallback is implied. The portable resolver applies this bounded rule
and resolves all 210 selected-corpus texture edges uniquely.

Unknown chunks are preserved as bounded descriptors rather than rejected. A
known singleton duplicate, malformed exact string, non-empty `HIDE`, or
non-12-byte `GRAV` is rejected.

## World, level, briefing, and path semantics

`HOUS` exposes singleton `TEXU`, `CCFF`, optional `BCKD`, exactly three float32
floor-Y values in `FLRY`, repeated `ROOM` records, and ordinal `LLST`/`LDAT`
pairs. A room is one byte of ID, two exact NUL strings, and one floor byte.
Each `LDAT` payload is a sequence of four-float line records. Across 29 worlds,
the parser validates 493 rooms, 522 line lists, and 47,589 line records.

One shipped world contains two different valid `BCKD` chunks. The loader asks
`GetChunkByID` for only the first match, so the portable model uses the first,
validates the later string, and retains its descriptor as an ignored duplicate.
Rejecting every known duplicate as malformed would incorrectly reject the
original corpus.

`FHOU` exposes singleton `GCCS`, singleton world path `HOUS`, and repeated
static `OBJE` placements. Each static placement is exactly six float32 values
followed by room and object-definition NUL strings with no trailing bytes.

`MODL` begins with the same transform/room/object identity. The game serializer
then writes three ordered NUL-string/u32 pairs and one compatibility u32; the
loader accepts the final word as optional for older data. The portable model
retains neutral state-string/value names until their gameplay meanings are
independently proven. All 1,176 supplied `MODL` records include the compatibility
word and pass exact parsing.

`IAOB` is a runtime-state overlay rather than another transform. Its first two
NUL strings are matched against a runtime type identity and instance identity;
the selected class then consumes the remaining u32 words. Five serializer paths
and the complete corpus establish bounded tails of 1, 2, or 8 words. The 172
supplied records contain 643 words in total: 85 one-word, 23 two-word, and 64
eight-word records. The generic parser preserves the words without assigning
unproven class-specific meanings.

Across all 31 levels, bounded parsing validates 2,444 static placements, 1,176
model placements, and 172 instance-state records with no unknown chunks.

`BRIF` validates the nine known singleton string chunks `NAME`, `OUTL`, `OUT2`,
`TEXT`, `TXT2`, `PRIM`, `SCND`, `AIRS`, and `SELA`. All 23 briefings across the
Resource/English pair validate. `PATH` requires an exact six-float `PPOS` and a
single `PDAT` whose 12-byte records contain six signed little-endian int16
deltas; the supplied path has 833 records.

The semantic API has explicit limits for chunks, individual strings, rooms,
line lists, aggregate line records, aggregate static/model placements, IAOB
state words, and path records. It validates sizes and counts before allocation,
requires exact terminators/trailing extent, preserves unknown descriptors, and
deliberately retains non-finite float bit patterns because the legacy loader
does not reject them.

The portable resolver joins object `CCFF`/`MESH` to CCF-scoped mesh/null/light
blueprints and material references. A second metadata-only resolver now maps
the level's world, every static object, and every model definition path to an
explicit missing/invalid/not-found/unique/ambiguous UDSP result without reading
payloads or guessing extensions. The complete selected corpus resolves all 31
world references, 2,444 static-object references, and 1,176 model references
uniquely with zero dependency issues.

## Portable implementation

- framing/object parser: `src/airfix/assets/AfChunkContainer.*`;
- malformed, duplicate, odd-sized, string, gravity, world, level, briefing,
  path, limit, signed-delta, and unknown-descriptor fixtures:
  `tests/AfChunkContainerTests.cpp`;
- bounded corpus path: `udsp-list --inventory`, which now invokes the matching
  semantic parser and reports only local per-file counts/flags.
