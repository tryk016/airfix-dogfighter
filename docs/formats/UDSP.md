# FMT-UDSP — `.up` archive format

**State:** discovery  
**Confidence:** 1/3 overall  
**Evidence:** `EV-20260721-003`

## Known facts

- Files `Resource.up`, `English.up`, `Dansk.up`, `Norsk.up`, and `Svenska.up`
  begin with ASCII `UDSP` (`55 44 53 50`).
- The next two observed bytes are `01 01`; treating them as a version is only a
  hypothesis.
- At offset `0x08`, the first four bytes interpreted little-endian are `0x2CD0`
  for `Resource.up` and `0x01F8` for `English.up`. These values could be entry
  counts, directory sizes, or offsets; no claim is accepted yet.
- Plain ASCII fragments occur near the beginning of `English.up`, including
  actor-like names. That suggests a directory/name table near the header, but it
  does not establish record boundaries.
- 7-Zip 26.00 does not recognize the container.
- `UdsPack.dll` is a 24,576-byte PE32/x86 DLL and is the highest-value static
  source for the layout.

## Header observations

```text
Resource.up @ 0x00
55 44 53 50 01 01 00 00 D0 2C 00 00 84 CC 29 0A ...

English.up @ 0x00
55 44 53 50 01 01 00 00 F8 01 00 00 02 43 51 01 ...
```

## Hypothesis table

| Field | Current hypothesis | Confidence | How to falsify |
|---|---|---:|---|
| `0x00..0x03` | magic `UDSP` | 3 | compare all packages/code |
| `0x04..0x05` | format version 1.1 | 1 | inspect version branches in `UdsPack.dll` |
| `0x06..0x07` | flags/reserved | 0 | trace header reads |
| `0x08..0x0B` | entry count | 1 | derive loop bound and compare archive size/names |
| following data | fixed/variable directory records | 1 | identify repeating reads and validated offsets |

## Research procedure

1. Extract imports, exports, strings, and call graph from `UdsPack.dll`.
2. Locate checks for `0x50534455` (`UDSP` little-endian).
3. Name each header read by offset, without assuming semantics.
4. Identify loops over records and all bounds/error paths.
5. Compare values across all five archives to distinguish counts from offsets.
6. Implement a listing-only parser with checked arithmetic and no extraction.
7. Validate every candidate offset/size against file boundaries.
8. Add extraction only after names, data spans, compression, and overlap checks
   are understood.

## Security requirements for the parser

- No trust in counts, offsets, sizes, names, terminators, compression ratio, or
  integer addition/multiplication.
- Refuse overlapping/out-of-file regions unless the format explicitly proves
  shared data is valid.
- Normalize output paths; reject absolute paths, drive prefixes, and `..`.
- Listing is read-only; extraction requires a separate output directory.
- Test empty, truncated, oversized, duplicate-name, traversal, decompression
  bomb, and checksum-error synthetic inputs.

## Unknowns

- Directory placement and record width.
- Name encoding/hash algorithm.
- Compression/encryption/checksum algorithms.
- Alignment and duplicate/shared-data rules.
- Whether language packages share the exact same entry schema as `Resource.up`.

