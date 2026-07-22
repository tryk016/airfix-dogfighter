# FMT-AFCHUNK — FourCC definition/mission container

**State:** generic framing and object-definition semantics implemented;
world/level/briefing/path payload semantics in progress

**Confidence:** 3/3 for framing and object fields listed below

**Evidence:** `EV-20260721-022`

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

## Confirmed next layers

- `HOUS`: `TEXU`, `CCFF`, optional `BCKD`, three-float `FLRY`, and paired room
  `LLST`/`LDAT` records.
- `FHOU`: checksum `GCCS`, world path `HOUS`, and repeated placements. The
  confirmed `OBJE` placement prefix is six float32 values followed by room and
  object-definition NUL strings.
- `BRIF`: string chunks including optional `OUT2` and loader-supported `TXT2`.
- `PATH`: `PPOS` is six float32 values; `PDAT` contains 12-byte records of six
  signed 16-bit deltas.

The portable resolver now joins object `CCFF`/`MESH` to CCF-scoped mesh/null/light
blueprints and material references. The next semantic parser can build on that
verified identity layer when interpreting the more variable `MODL` and `IAOB`
placements.

## Portable implementation

- framing/object parser: `src/airfix/assets/AfChunkContainer.*`;
- malformed, duplicate, odd-sized, string, gravity, and unknown-chunk fixtures:
  `tests/AfChunkContainerTests.cpp`;
- bounded corpus path: `udsp-list --inventory`.
