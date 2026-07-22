# Resource and English payload inventory

**Date:** 2026-07-21

**Evidence:** `EV-20260721-017`, `EV-20260721-018`

## Method and boundary

`udsp-list --inventory` walks validated metadata, reads at most 16 decoded bytes
for general signature classification, and performs a bounded per-file read only
for recognized GTI/CCF parser validation. It neither extracts a directory tree
nor writes original payloads. Generated TSV reports remain under ignored
`artifacts/inventory/`; this document records only aggregate facts.

The current v1 content pair was inventoried: `Resource.up` and `English.up`.
Both archives had already passed complete UDSP decompression-size verification.

## Directory distribution

| Archive/root | Records |
|---|---:|
| Resource / `Graphics` | 2,130 |
| Resource / `Game` | 329 |
| Resource / `Sound` | 104 |
| Resource / `User` | 65 |
| English / `Graphics` | 176 |
| English / `Game` | 42 |
| **Total** | **2,846** |

## Extension inventory

| Extension | Records | Stored bytes | Decoded bytes | Initial role |
|---|---:|---:|---:|---|
| `.gti` | 1,985 | 126,616,555 | 155,924,867 | textures/images |
| `.ccf` | 285 | 53,737,331 | 53,737,331 | scenes/models/world geometry |
| `.object` | 215 | 36,915 | 36,915 | actor/object chunk definitions |
| `.wav` | 104 | 8,437,902 | 8,437,902 | PCM/RIFF sound effects |
| `.txt` | 65 | 34,792 | 34,792 | UI/layout scripts or definitions |
| `.afs` | 52 | 542,145 | 542,145 | mission setup/script text |
| `.tga` | 42 | 1,279,800 | 1,279,800 | user/default photos and stickers |
| `.level` | 31 | 344,117 | 344,117 | mission level chunks |
| `.world` | 29 | 804,963 | 804,963 | world/room placement chunks |
| `.brf` | 23 | 23,915 | 23,915 | briefing chunks |
| `.fnt` | 8 | 21,821 | 21,821 | font descriptor text |
| `.msk` | 4 | 327,680 | 327,680 | editor masks |
| `.raw` | 1 | 307,200 | 307,200 | localized frontend raw image |
| `.dat` | 1 | 10,044 | 10,044 | mission path chunks |
| no extension | 1 | 89,034 | 89,034 | valid `CcFf` scene (`table_attic`) |

Counts deliberately cover only the selected English content pair, not all four
localization archives. Resource contributes 1,833 GTI files and all 286 CCF
signatures; English contributes another 152 GTI files.

## Signature families

- GTI starts `GtIm`, version 4, then `CRC0`/`Imag` chunks.
- CCF starts `CcFf`, version `0x98092901`, then a root chunk.
- `.world`, `.level`, `.object`, `.brf`, and `.dat` use a separate four-byte
  `AfChunkContainer` family (`HOUS`, `FHOU`, `OBJE`/`MODL`, `BRIF`, `PATH`).
- WAV is ordinary RIFF/WAVE.
- TGA records have uncompressed true-color headers (64x64 or 128x128 in the
  supplied default user content).
- AFS/FNT/TXT/MSK are text-like and must be parsed with their own grammars, not
  inferred from filename alone.

All 286 CCF signatures have ordered root sections `0x1000`, `0x2000`,
`0x3000`, and `0x4000`. Bounded direct-child indexing found 392 room records,
10,385 materials, and two equal 9,328-record blueprint/placed-node tables; see
`FMT-CCF` for the ID sets and per-file bounds. Static analysis proves that the
tables have independent prototype/placed-node roles; their equal physical
counts do not imply an index mapping.

The semantic material pass validates all 10,385 `0x2100` records. It exposes
10,256 primary and 263 environment texture references; the loader-supported
secondary texture field is absent from the selected corpus. The special
zero-byte empty-string form occurs in 5,043 material prefixes and in no names.

The semantic mesh pass validates all 6,995 `0x3100` records: 136,363 vertices,
170,039 indexed triangles, 168,419 UV records, 95,424 paint records, and 3,428
optional range/control records. Every triangle index is in range. Aggregate
facts only are retained; no geometry or asset-name list is written.

The semantic placed-scene pass validates all 9,328 `0x4000` records in physical
order: 6,995 objects, 2,130 null nodes, and 203 lights. Exact transforms and
references plus bounded known child shapes are decoded; all shipped records use
the F050 matrix orientation, while the loader-supported alternate F040 form is
covered synthetically. Opaque BSP and null blocks are not copied or exported.

The same read-only inventory validates the runtime conversion boundary for all
6,995 meshes. GTI inspection covers 3,990 variants and 9,617 declared mip
levels: 3,974 variants/9,523 levels form exact authored chains; 16 variants use
their base level plus generated mips because legacy quarter-area byte counts do
not match modern clamped dimensions. In total, 9,539 RGBA8 levels are decoded
without writing derived textures.

The complete `0x3000` blueprint index contains those 6,995 meshes plus 2,130
null and 203 light records. A read-only aggregate resolver validates all 215
object `CCFF` paths and selectors: 90 mesh, 124 null, one no-selector, and 14
case-folded matches. The 9,328 prototypes form 2,062 roots and 7,266 edges with
maximum depth 7 and zero missing parents, duplicate references, or cycles.

Traversing every selected blueprint subtree covers 2,687 nodes and 1,858 mesh
instances, resolving 2,842 material uses and 2,959 texture edges. Applying the
recovered `TEXU\\source.gti` rule resolves all 2,959 edges to unique archive
entries, with zero missing, invalid, or ambiguous graph, material, or texture
dependencies. The earlier 212-material/210-texture census measured only
directly selected mesh roots and remains a historical narrower-scope result.

One private grouped-aircraft selection contains 46 nodes, 25 mesh instances,
21 null groups, 663 triangles, and maximum subtree depth 3. Only these anonymous
aggregates and the successful complete-assembly result are retained publicly;
no asset name, path, hash, geometry, or image is recorded.

All 299 selected `AfChunkContainer` files also pass exact root/child framing:
7,376 chunks total. The semantic layer validates all 215 object definitions,
including the observed optional category/nationality/mesh fields and exact
56 `GRAV` plus 10 `HIDE` records; see `FMT-AFCHUNK`.

The extended semantic pass also validates all 29 worlds, 493 rooms, 522 ordinal
radar lists containing 47,589 four-float line records, 31 levels with 2,444
stable `OBJE` placements, 1,176 `MODL` placements, and 172 `IAOB` runtime-state
records/643 words; 23 briefings across Resource/English; and the one path with
833 signed-delta records. One world has a loader-permitted second `BCKD`; the
first is authoritative and the validated second descriptor is retained. All
observed FHOU chunks now have bounded physical schemas; neutral state slot names
avoid claiming unproven gameplay meanings.

## Port priority derived from the corpus

1. GTI metadata and pixel conversion — required for every visible scene/UI.
2. CCF materials, meshes, blueprints, and room/object sections — required for a
   renderable model and room.
3. FourCC `AfChunkContainer` used by world/level/object/briefing/path data.
4. WAV effects and a no-CD-music audio configuration.
5. Mission AFS/object definitions and remaining UI/localization formats.

Multiplayer/editor-only data is retained in inventory evidence but is not a v1
implementation priority.
