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
`FMT-CCF` for the ID sets and per-file bounds.

## Port priority derived from the corpus

1. GTI metadata and pixel conversion — required for every visible scene/UI.
2. CCF materials, meshes, blueprints, and room/object sections — required for a
   renderable model and room.
3. FourCC `AfChunkContainer` used by world/level/object/briefing/path data.
4. WAV effects and a no-CD-music audio configuration.
5. Mission AFS/object definitions and remaining UI/localization formats.

Multiplayer/editor-only data is retained in inventory evidence but is not a v1
implementation priority.
